// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/StringUtils.h"
#include "Common/Data/Text/Demangle.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/ELF/ElfReader.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/LineInfo.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"

const char *ElfReader::GetSectionName(int section) const {
	if (sections[section].sh_type == SHT_NULL)
		return nullptr;

	int stringsOffset = GetSectionDataOffset(header->e_shstrndx);
	int nameOffset = sections[section].sh_name;
	if (nameOffset < 0 || (size_t)nameOffset + stringsOffset >= size_) {
		ERROR_LOG(Log::Loader, "ELF: Bad name offset %d + %d in section %d (max = %d)", nameOffset, stringsOffset, section, (int)size_);
		return nullptr;
	}
	const char *ptr = (const char *)GetSectionDataPtr(header->e_shstrndx);

	if (ptr)
		return ptr + nameOffset;
	else
		return nullptr;
}

void addrToHiLo(u32 addr, u16 &hi, s16 &lo)
{
	lo = (addr & 0xFFFF);
	u32 naddr = addr - lo;
	hi = naddr>>16;
	// Note the casts: hi is a u16, so it promotes to int, and kernel modules load at 0x88000000 -
	// shifting a value of 0x8800 left by 16 would overflow a signed int.
	u32 test = ((u32)hi << 16) + (u32)lo;
	if (test != addr)
	{
		WARN_LOG_REPORT(Log::Loader, "HI16/LO16 relocation failure?");
	}
}

bool ElfReader::LoadRelocations(const Elf32_Rel *rels, int numRelocs) {
	std::vector<u32> relocOps;
	relocOps.resize(numRelocs);

	DEBUG_LOG(Log::Loader, "Loading %i relocations...", numRelocs);
	int numErrors = 0;

	{
		for (int r = 0; r < numRelocs; r++) {
			u32 info = rels[r].r_info;
			u32 addr = rels[r].r_offset;

			int type = info & 0xf;

			// Often: 0 = code, 1 = data.
			int readwrite = (info >> 8) & 0xff;
			if (readwrite >= (int)segmentVAddr.size()) {
				if (numErrors < 10) {
					ERROR_LOG_REPORT(Log::Loader, "Bad segment number %i", readwrite);
				}
				numErrors++;
				continue;
			}
			if (!SegmentIsLoaded(readwrite)) {
				// The segment exists but isn't PT_LOAD, so there's nothing at that address to patch.
				if (numErrors < 10) {
					ERROR_LOG_REPORT(Log::Loader, "Relocation against segment %i, which we didn't load", readwrite);
				}
				numErrors++;
				continue;
			}

			addr += segmentVAddr[readwrite];

			// It appears that misaligned relocations are allowed.
			if (((addr & 3) && type != R_MIPS_32) || !Memory::IsValidAddress(addr)) {
				if (numErrors < 10) {
					WARN_LOG_REPORT(Log::Loader, "Suspicious address %08x, skipping reloc, type = %d", addr, type);
				} else if (numErrors == 10) {
					WARN_LOG(Log::Loader, "Too many bad relocations, skipping logging");
				}
				numErrors++;
				continue;
			}

			// NOTE: During loading, we use plain reads instead of Memory::ReadUnchecked_Insruction.
			// No blocks are created yet, so that's fine.
			relocOps[r] = Memory::ReadUnchecked_U32(addr);
		}

		for (int r = 0; r < numRelocs; r++) {
			VERBOSE_LOG(Log::Loader, "Loading reloc %i  (%p)...", r, rels + r);
			u32 info = rels[r].r_info;
			u32 addr = rels[r].r_offset;

			int type = info & 0xf;
			int readwrite = (info >> 8) & 0xff;
			int relative = (info >> 16) & 0xff;

			// Already logged by the pass above.
			if (!SegmentIsLoaded(readwrite)) {
				continue;
			}

			addr += segmentVAddr[readwrite];
			if (((addr & 3) && type != R_MIPS_32) || !Memory::IsValidAddress(addr)) {
				continue;
			}

			u32 op = relocOps[r];

			const bool log = false;
			//log=true;
			if (log) {
				DEBUG_LOG(Log::Loader, "rel at: %08x  info: %08x   type: %i", addr, info, type);
			}
			u32 relocateTo = SegmentIsLoaded(relative) ? segmentVAddr[relative] : 0;

			switch (type) {
			case R_MIPS_32:
				if (log)
					DEBUG_LOG(Log::Loader, "Full address reloc %08x", addr);
				//full address, no problemo
				op += relocateTo;
				break;

			case R_MIPS_26: //j, jal
				//add on to put in correct address space
				if (log)
					DEBUG_LOG(Log::Loader, "j/jal reloc %08x", addr);
				op = (op & 0xFC000000) | (((op & 0x03FFFFFF) + (relocateTo >> 2)) & 0x03FFFFFF);
				break;

			case R_MIPS_HI16: //lui part of lui-addiu pairs
			{
				if (log)
					DEBUG_LOG(Log::Loader, "HI reloc %08x", addr);

				u32 cur = (op & 0xFFFF) << 16;
				u16 hi = 0;
				bool found = false;
				for (int t = r + 1; t < numRelocs; t++) {
					int t_type = rels[t].r_info & 0xF;
					if (t_type == R_MIPS_HI16)
						continue;

					// The candidate LO16 declares its own segment - use that rather than the HI16's,
					// which is what the mismatch warning further down is there to detect.
					int t_readwrite = (rels[t].r_info >> 8) & 0xff;
					if (!SegmentIsLoaded(t_readwrite))
						continue;
					u32 corrLoAddr = rels[t].r_offset + segmentVAddr[t_readwrite];

					// In MotorStorm: Arctic Edge (US), these are sometimes R_MIPS_16 (instead of LO16.)
					// It appears the PSP takes any relocation that is not a HI16.
					if (t_type != R_MIPS_LO16) {
						if (t_type != R_MIPS_16) {
							// Let's play it safe for now and skip. We've only seen this type.
							// These exists in some popular games like Assassin's Creed: Bloodlines and GTA: VCS: (https://report.ppsspp.org/logs/kind/1187)
							ERROR_LOG_REPORT(Log::Loader, "ELF relocation HI16/%d pair (instead of LO16) at %08x / %08x", t_type, addr, corrLoAddr);
							continue;
						} else {
							WARN_LOG_REPORT(Log::Loader, "ELF relocation HI16/%d(16) pair (instead of LO16) at %08x / %08x", t_type, addr, corrLoAddr);
						}
					}

					// Should have matching index and segment info, according to llvm, which makes sense.
					if ((rels[t].r_info >> 8) != (rels[r].r_info >> 8)) {
						WARN_LOG_REPORT(Log::Loader, "ELF relocation HI16/LO16 with mismatching r_info lo=%08x, hi=%08x", rels[t].r_info, rels[r].r_info);
					}
					if (log) {
						DEBUG_LOG(Log::Loader, "Corresponding lo found at %08x", corrLoAddr);
					}
					if (Memory::IsValidAddress(corrLoAddr)) {
						s16 lo = (s16)relocOps[t];
						cur += lo;
						cur += relocateTo;
						addrToHiLo(cur, hi, lo);
						found = true;
						break;
					} else {
						ERROR_LOG(Log::Loader, "Bad corrLoAddr %08x", corrLoAddr);
					}
				}
				if (found) {
					op = (op & 0xFFFF0000) | hi;
				} else {
					// Leave the instruction alone rather than writing hi's initial 0 into it. We
					// have no idea what the right immediate is, and zeroing the lui of a lui/addiu
					// pair is a guess that's wrong in a way that's hard to trace back to here.
					ERROR_LOG_REPORT(Log::Loader, "R_MIPS_HI16: could not find R_MIPS_LO16 (r=%d of %d, addr=%08x)", r, numRelocs, addr);
				}
			}
			break;

			case R_MIPS_LO16: //addiu part of lui-addiu pairs
			{
				if (log)
					DEBUG_LOG(Log::Loader, "LO reloc %08x", addr);
				u32 cur = op & 0xFFFF;
				cur += relocateTo;
				cur &= 0xFFFF;
				op = (op & 0xFFFF0000) | cur;
			}
			break;

			case R_MIPS_GPREL16: //gp
				// It seems safe to ignore this, almost a notification of a gp-relative operation?
				break;

			case R_MIPS_16:
				op = (op & 0xFFFF0000) | (((int)(op & 0xFFFF) + (int)relocateTo) & 0xFFFF);
				break;

			case R_MIPS_NONE:
				// This shouldn't matter, not sure the purpose of it.
				break;

			default:
			{
				char temp[256];
				MIPSDisAsm(MIPSOpcode(op), 0, temp, sizeof(temp));
				ERROR_LOG_REPORT(Log::Loader, "ARGH IT'S AN UNKNOWN RELOCATION!!!!!!!! %08x, type=%d : %s", addr, type, temp);
			}
			break;
			}

			Memory::WriteUnchecked_U32(op, addr);
			NotifyMemInfo(MemBlockFlags::WRITE, addr, 4, "Relocation");
		}
	}

	if (numErrors) {
		WARN_LOG(Log::Loader, "%i bad relocations found!!!", numErrors);
	}
	return numErrors == 0;
}


void ElfReader::LoadRelocations2(int rel_seg)
{
	u8 *buf, *end, *flag_table, *type_table;
	int flag_table_size, type_table_size;
	int flag_bits, seg_bits, type_bits;
	int cmd, flag, seg, type;
	int off_seg = 0, addr_seg, rel_base, rel_offset;
	int relocate_to, last_type, lo16 = 0;
	u32 op, addr;
	int rcount = 0;
	// This runs per relocation, so only report the first bad segment rather than one per entry.
	bool loggedBadSegment = false;

	const Elf32_Phdr *ph = segments + rel_seg;

	buf = (u8*)GetSegmentPtr(rel_seg);
	if (!buf) {
		ERROR_LOG_REPORT(Log::Loader, "Rel2 segment invalid");
		return;
	}
	// GetSegmentPtr only vouches for where the segment starts - p_filesz comes from the file too.
	if ((size_t)ph->p_offset + ph->p_filesz > size_) {
		ERROR_LOG_REPORT(Log::Loader, "Rel2 segment extends past the end of the file");
		return;
	}
	end = buf+ph->p_filesz;

	// Everything below reads forward from buf, so check there's something there each time. All of
	// these sizes and indexes come out of the file.
	auto haveBytes = [&buf, &end](int n) -> bool {
		if (end - buf < n) {
			ERROR_LOG_REPORT(Log::Loader, "Rel2: truncated relocation data");
			return false;
		}
		return true;
	};

	if (!haveBytes(4))
		return;

	flag_bits = buf[2];
	type_bits = buf[3];

	seg_bits = 1;
	while((1<<seg_bits)<rel_seg)
		seg_bits += 1;

	buf += 4;

	// Both tables are prefixed by their own size, and are indexed by bitfields out of the command
	// words below - so the tables and the indexes into them both need checking.
	if (!haveBytes(1))
		return;
	flag_table = buf;
	flag_table_size = flag_table[0];
	if (!haveBytes(flag_table_size))
		return;
	buf += flag_table_size;

	if (!haveBytes(1))
		return;
	type_table = buf;
	type_table_size = type_table[0];
	if (!haveBytes(type_table_size))
		return;
	buf += type_table_size;

	rel_base = 0;
	last_type = -1;
	while(buf<end){
		if (!haveBytes(2))
			return;
		// Byte-wise rather than *(u16 *)buf: how far buf has advanced depends on the table sizes
		// above, so it isn't necessarily even.
		cmd = buf[0] | (buf[1] << 8);
		buf += 2;

		flag = ( cmd<<(16-flag_bits))&0xffff;
		flag = (flag>>(16-flag_bits))&0xffff;
		if (flag >= flag_table_size) {
			ERROR_LOG_REPORT(Log::Loader, "Rel2: flag %d out of range (table has %d)", flag, flag_table_size);
			return;
		}
		flag = flag_table[flag];

		seg = (cmd<<(16-seg_bits-flag_bits))&0xffff;
		seg = (seg>>(16-seg_bits))&0xffff;

		type = ( cmd<<(16-type_bits-seg_bits-flag_bits))&0xffff;
		type = (type>>(16-type_bits))&0xffff;
		if (type >= type_table_size) {
			ERROR_LOG_REPORT(Log::Loader, "Rel2: type %d out of range (table has %d)", type, type_table_size);
			return;
		}
		type = type_table[type];

		if((flag&0x01)==0){
			off_seg = seg;
			if((flag&0x06)==0){
				rel_base = cmd>>(seg_bits+flag_bits);
			}else if((flag&0x06)==4){
				if (!haveBytes(4))
					return;
				rel_base = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
				buf += 4;
			}else{
				ERROR_LOG_REPORT(Log::Loader, "Rel2: invalid size flag! %x", flag);
				rel_base = 0;
			}
		}else{
			addr_seg = seg;
			if (!SegmentIsLoaded(addr_seg)) {
				if (!loggedBadSegment) {
					ERROR_LOG_REPORT(Log::Loader, "Rel2: relocating against segment %d, which we didn't load", addr_seg);
					loggedBadSegment = true;
				}
				continue;
			}
			relocate_to = segmentVAddr[addr_seg];
			if (!Memory::IsValidAddress(relocate_to)) {
				ERROR_LOG_REPORT(Log::Loader, "ELF: Bad address to relocate to: %08x (segment %d)", relocate_to, addr_seg);
				continue;
			}

			if((flag&0x06)==0x00){
				rel_offset = cmd;
				if(cmd&0x8000){
					rel_offset |= 0xffff0000;
					rel_offset >>= type_bits+seg_bits+flag_bits;
					rel_offset |= 0xffff0000;
				}else{
					rel_offset >>= type_bits+seg_bits+flag_bits;
				}
				rel_base += rel_offset;
			}else if((flag&0x06)==0x02){
				rel_offset = cmd;
				if(cmd&0x8000)
					rel_offset |= 0xffff0000;
				rel_offset >>= type_bits+seg_bits+flag_bits;
				if (!haveBytes(2))
					return;
				rel_offset = (rel_offset<<16) | (buf[0]) | (buf[1]<<8);
				buf += 2;
				rel_base += rel_offset;
			}else if((flag&0x06)==0x04){
				if (!haveBytes(4))
					return;
				rel_base = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
				buf += 4;
			}else{
				ERROR_LOG_REPORT(Log::Loader, "Rel2: invalid relocat size flag! %x", flag);
			}

			// seg is seg_bits wide, so it can name more segments than the file actually has - and
			// naming one we didn't load leaves nothing to write to.
			if (!SegmentIsLoaded(off_seg)) {
				if (!loggedBadSegment) {
					ERROR_LOG_REPORT(Log::Loader, "Rel2: bad or unloaded offset segment %d", off_seg);
					loggedBadSegment = true;
				}
				continue;
			}
			rel_offset = rel_base+segmentVAddr[off_seg];
			if (!Memory::IsValidAddress(rel_offset)) {
				ERROR_LOG_REPORT(Log::Loader, "ELF: Bad rel_offset: %08x", rel_offset);
				continue;
			}

			if((flag&0x38)==0x00){
				lo16 = 0;
			}else if((flag&0x38)==0x08){
				if(last_type!=0x04)
					lo16 = 0;
			}else if((flag&0x38)==0x10){
				if (!haveBytes(2))
					return;
				lo16 = (buf[0]) | (buf[1]<<8);
				if(lo16&0x8000)
					lo16 |= 0xffff0000;
				buf += 2;
			}else{
				ERROR_LOG_REPORT(Log::Loader, "Rel2: invalid lo16 type! %x", flag);
			}

			op = Memory::Read_Instruction(rel_offset, true).encoding;
			VERBOSE_LOG(Log::Loader, "Rel2: %5d: CMD=0x%04X flag=%x type=%d off_seg=%d offset=%08x addr_seg=%d op=%08x", rcount, cmd, flag, type, off_seg, rel_base, addr_seg, op);

			switch(type){
			case 0:
				continue;
			case 2: // R_MIPS_32
				op += relocate_to;
				break;
			case 3: // R_MIPS_26
			case 6: // R_MIPS_J26
			case 7: // R_MIPS_JAL26
				op = (op&0xFC000000) | (((op&0x03FFFFFF)+(relocate_to>>2))&0x03FFFFFF);
				// To be safe, let's force it to the specified jump.
				if (type == 6)
					op = (op & ~0xFC000000) | 0x08000000;
				else if (type == 7)
					op = (op & ~0xFC000000) | 0x0C000000;
				break;
			case 4: // R_MIPS_HI16
				addr = ((op<<16)+lo16)+relocate_to;
				if(addr&0x8000)
					addr += 0x00010000;
				op = (op&0xffff0000) | (addr>>16 );
				break;
			case 1:
			case 5: // R_MIPS_LO16
				op = (op&0xffff0000) | (((op&0xffff)+relocate_to)&0xffff);
				break;
			default:
				ERROR_LOG_REPORT(Log::Loader, "Rel2: unexpected relocation type! %x", type);
				break;
			}

			Memory::WriteUnchecked_U32(op, rel_offset);
			NotifyMemInfo(MemBlockFlags::WRITE, rel_offset, 4, "Relocation2");
			rcount += 1;
		}
	}

}


int ElfReader::LoadInto(u32 loadAddress, bool fromTop) {
	DEBUG_LOG(Log::Loader,"String section: %i", header->e_shstrndx);

	if (size_ < sizeof(Elf32_Ehdr)) {
		ERROR_LOG(Log::Loader, "Truncated ELF header, %d bytes", (int)size_);
		// Probably not the right error code.
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}

	if (header->e_ident[0] != ELFMAG0 || header->e_ident[1] != ELFMAG1
		|| header->e_ident[2] != ELFMAG2 || header->e_ident[3] != ELFMAG3)
		return SCE_KERNEL_ERROR_UNSUPPORTED_PRX_TYPE;

	// technically ELFCLASSNONE would freeze the system, but that's not really desireable
	if (header->e_ident[EI_CLASS] != ELFCLASS32) {
		if (header->e_ident[EI_CLASS] != 0) {
			return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
		}

		ERROR_LOG(Log::Loader, "Bad ELF, EI_CLASS (fifth byte) is 0x00, should be 0x01 - would lock up a PSP.");
	}

	if (header->e_ident[EI_DATA] != ELFDATA2LSB)
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;

	if (size_ < header->e_phoff + sizeof(Elf32_Phdr) * GetNumSegments() || size_ < header->e_shoff + sizeof(Elf32_Shdr) * GetNumSections()) {
		ERROR_LOG(Log::Loader, "Truncated ELF, %d bytes with %d sections and %d segments", (int)size_, GetNumSections(), GetNumSegments());
		// Probably not the right error code.
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}

	// One load address per program header. e_phnum is a u16, and the check above has established
	// that many headers really are in the file, so this is bounded by the file size. Entries stay
	// at SEGMENT_NOT_LOADED unless the first pass below actually loads that segment.
	segmentVAddr.assign(GetNumSegments(), SEGMENT_NOT_LOADED);

	// e_ident[EI_VERSION] is ignored

	// Should we relocate?
	bRelocate = (header->e_type != ET_EXEC);

	// Look for the module info - we need to know whether this is kernel or user.
	const PspModuleInfo *modInfo = 0;
	for (int i = 0; i < GetNumSections(); i++) {
		const Elf32_Shdr *s = &sections[i];
		const char *name = GetSectionName(i);
		if (name && !strcmp(name, ".rodata.sceModuleInfo") && s->sh_offset + sizeof(PspModuleInfo) <= size_) {
			modInfo = (const PspModuleInfo *)GetPtr(s->sh_offset);
		}
	}
	if (!modInfo && GetNumSegments() >= 1 && (segments[0].p_paddr & 0x7FFFFFFF) + sizeof(PspModuleInfo) <= size_) {
		modInfo = (const PspModuleInfo *)GetPtr(segments[0].p_paddr & 0x7FFFFFFF);
	}

	bool kernelModule = modInfo ? (modInfo->moduleAttrs & 0x1000) != 0 : false;

	std::string modName = "ELF";
	if (modInfo) {
		size_t n = strnlen(modInfo->name, 28);
		modName = "ELF/" + std::string(modInfo->name, n);
	}

	entryPoint = header->e_entry;
	u32 totalStart = 0xFFFFFFFF;
	u32 totalEnd = 0;
	int numLoadSegments = 0;
	for (int i = 0; i < header->e_phnum; i++) {
		const Elf32_Phdr *p = &segments[i];
		if (p->p_type == PT_LOAD) {
			numLoadSegments++;
			if (p->p_vaddr < totalStart) {
				totalStart = p->p_vaddr;
				firstSegAlign = p->p_align;
			}
			if (p->p_vaddr + p->p_memsz > totalEnd)
				totalEnd = p->p_vaddr + p->p_memsz;
		}
	}
	// Without this, totalStart stays 0xFFFFFFFF and totalEnd 0, so totalSize would come out as 1
	// and we'd go on to allocate at 0xFFFFFFFF.
	if (numLoadSegments == 0) {
		ERROR_LOG(Log::Loader, "ELF has no loadable segments");
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}
	totalSize = totalEnd - totalStart;

	// If a load address is specified that's in regular RAM, override kernel module status
	bool inUser = totalStart >= PSP_GetUserMemoryBase();
	BlockAllocator &memblock = (kernelModule && !inUser) ? kernelMemory : userMemory;

	if (!bRelocate)
	{
		// Binary is prerelocated, load it where the first segment starts
		vaddr = memblock.AllocAt(totalStart, totalSize, modName.c_str());
	}
	else if (loadAddress)
	{
		// Binary needs to be relocated: add loadAddress to the binary start address.
		// The caller picked the address, so we can't move it - but say so if it doesn't meet
		// what the module asked for, since that's how relocations end up off by 64KB.
		if (firstSegAlign > 1 && ((loadAddress + totalStart) & (firstSegAlign - 1)) != 0) {
			WARN_LOG_REPORT(Log::Loader, "Module %s loaded at %08x, which doesn't meet its segment alignment %08x",
				modName.c_str(), loadAddress + totalStart, firstSegAlign);
		}
		vaddr = memblock.AllocAt(loadAddress + totalStart, totalSize, modName.c_str());
	}
	else
	{
		// Just put it where there is room, but honor the alignment the first loadable segment
		// asks for. Most PRXs want no more than the allocator's default grain, so this usually
		// changes nothing - but a module's relocations are only guaranteed to resolve correctly
		// at a base meeting its declared alignment, and ignoring that produces addresses that
		// are wrong by a multiple of 64KB rather than an outright failure.
		//
		// CrossCraft Classic (Zig) is the case in point: it declares p_align 0x10000 precisely
		// because a stage of Zig's PSP pipeline emits mispaired HI16/LO16 relocations, and a
		// 64KB-aligned base makes that harmless (no carry is ever needed, so which of a symbol's
		// LO16 entries a HI16 got paired with stops mattering). Loaded at 0x08804000 instead, 46
		// of its addresses came out 64KB low and it jumped through a bogus vtable almost at once.
		u32 align = firstSegAlign;
		if (align > 1 && (align & (align - 1)) == 0) {
			if (align > 0x1000) {
				INFO_LOG(Log::Loader, "Module %s requests an unusually large segment alignment (%08x)", modName.c_str(), align);
			}
			vaddr = memblock.AllocAligned(totalSize, 1, align, fromTop, modName.c_str());
		} else {
			vaddr = memblock.Alloc(totalSize, fromTop, modName.c_str());
		}
	}

	if (vaddr == (u32)-1) {
		ERROR_LOG(Log::Loader, "Failed to allocate memory for ELF!");
		return SCE_KERNEL_ERROR_MEMBLOCK_ALLOC_FAILED;
	}

	if (bRelocate) {
		DEBUG_LOG(Log::Loader,"Relocatable module");
		if (entryPoint != (u32)-1)
			entryPoint += vaddr;
	} else {
		DEBUG_LOG(Log::Loader,"Prerelocated executable");
	}

	DEBUG_LOG(Log::Loader,"%i segments:", header->e_phnum);

	// First pass: Get the bits into RAM
	u32 baseAddress = bRelocate ? vaddr : 0;

	for (int i = 0; i < header->e_phnum; i++)
	{
		const Elf32_Phdr *p = segments + i;
		DEBUG_LOG(Log::Loader, "Type: %08x Vaddr: %08x Filesz: %08x Memsz: %08x ", (int)p->p_type, (u32)p->p_vaddr, (int)p->p_filesz, (int)p->p_memsz);

		if (p->p_type == PT_LOAD)
		{
			segmentVAddr[i] = baseAddress + p->p_vaddr;
			const u32 writeAddr = segmentVAddr[i];

			const u8 *src = GetSegmentPtr(i);
			if (!src) {
				ERROR_LOG(Log::Loader, "Segment %d pointer invalid?", i);
				continue;
			}
			if (p->p_filesz > size_) {
				ERROR_LOG(Log::Loader, "Segment %d size invalid", i);
				continue;
			}
			if ((s64)p->p_filesz + (s64)p->p_offset > (s64)size_) {
				ERROR_LOG(Log::Loader, "Segment %d size+offset invalid, reading outside the input", i);
				continue;
			}
			if (p->p_filesz > p->p_memsz) {
				ERROR_LOG(Log::Loader, "Segment %d filesz invalid - bigger than memsz", i);
				continue;
			}
			const u32 srcSize = p->p_filesz;
			const u32 dstSize = p->p_memsz;  // can be bigger than size-in-file (p_filesz), we'll zero the rest below. But cannot be smaller!
			u8 *dst = Memory::GetPointerWriteRangeOrException(writeAddr, dstSize);
			if (dst) {
				if (srcSize < dstSize) {
					memset(dst + srcSize, 0, dstSize - srcSize); // zero out the rest of the segment, this also applies to bss (which is all-zero)
					NotifyMemInfo(MemBlockFlags::WRITE, writeAddr + srcSize, dstSize - srcSize, "ELFZero");
				}

				memcpy(dst, src, srcSize);
				std::string tag = StringFromFormat("ELFLoad/%08x", writeAddr);
				NotifyMemInfo(MemBlockFlags::WRITE, writeAddr, srcSize, tag.c_str(), tag.size());
				DEBUG_LOG(Log::Loader, "Loadable Segment Copied to %08x, size %08x", writeAddr, (u32)p->p_memsz);
			} else {
				ERROR_LOG(Log::Loader, "Bad ELF segment. Trying to write %d bytes to %08x", dstSize, writeAddr);
			}
		}
	}
	memblock.ListBlocks(LogLevel::LDEBUG);

	DEBUG_LOG(Log::Loader, "%d sections:", header->e_shnum);

	sectionOffsets = new u32[GetNumSections()];
	sectionAddrs = new u32[GetNumSections()];

	for (int i = 0; i < GetNumSections(); i++)
	{
		const Elf32_Shdr *s = &sections[i];
		const char *name = GetSectionName(i);

		u32 writeAddr = s->sh_addr + baseAddress;
		sectionOffsets[i] = writeAddr - vaddr;
		sectionAddrs[i] = writeAddr;

		if (s->sh_flags & SHF_ALLOC)
		{
			std::string tag = name && name[0] ? StringFromFormat("%s/%s", modName.c_str(), name) : StringFromFormat("%s/%08x", modName.c_str(), writeAddr);
			NotifyMemInfo(MemBlockFlags::SUB_ALLOC, writeAddr, s->sh_size, tag.c_str(), tag.size());
			DEBUG_LOG(Log::Loader,"Data Section found: %s     Sitting at %08x, size %08x", name, writeAddr, (u32)s->sh_size);
		}
		else
		{
			DEBUG_LOG(Log::Loader,"NonData Section found: %s     Ignoring (size=%08x) (flags=%08x)", name, (u32)s->sh_size, (u32)s->sh_flags);
		}
	}

	DEBUG_LOG(Log::Loader, "Relocations:");

	// Second pass: Do necessary relocations

	for (int i = 0; i < GetNumSections(); i++)
	{
		const Elf32_Shdr *s = &sections[i];
		const char *name = GetSectionName(i);

		if (s->sh_type == SHT_PSPREL)
		{
			//We have a relocation table!
			int sectionToModify = s->sh_info;
			if (sectionToModify >= 0 && sectionToModify < GetNumSections())
			{
				if (!(sections[sectionToModify].sh_flags & SHF_ALLOC))
				{
					ERROR_LOG_REPORT(Log::Loader, "Trying to relocate non-loaded section %s", GetSectionName(sectionToModify));
					continue;
				}

				int numRelocs = s->sh_size / sizeof(Elf32_Rel);

				Elf32_Rel *rels = (Elf32_Rel *)GetSectionDataPtr(i);
				if (GetSectionDataOffset(i) + sizeof(Elf32_Rel) * numRelocs > size_)
					rels = nullptr;

				DEBUG_LOG(Log::Loader,"%s: Performing %i relocations on %s : offset = %08x", name, numRelocs, GetSectionName(sectionToModify), sections[i].sh_offset);
				if (!rels || !LoadRelocations(rels, numRelocs)) {
					WARN_LOG(Log::Loader, "LoadInto: Relocs failed, trying anyway");
				}			
			}
			else
			{
				WARN_LOG_REPORT(Log::Loader, "sectionToModify = %i - ignoring PSP relocation sector %i", sectionToModify, i);
			}
		}
		else if (s->sh_type == SHT_REL)
		{
			DEBUG_LOG(Log::Loader, "Traditional relocation section found.");
			if (!bRelocate)
			{
				DEBUG_LOG(Log::Loader, "Binary is prerelocated. Skipping relocations.");
			}
			else
			{
				//We have a relocation table!
				int sectionToModify = s->sh_info;
				if (sectionToModify >= 0 && sectionToModify < GetNumSections())
				{
					if (!(sections[sectionToModify].sh_flags & SHF_ALLOC))
					{
						// Generally stuff like debug info. We don't need it.
						INFO_LOG(Log::Loader, "Skipping relocation of non-loaded section %s", GetSectionName(sectionToModify));
						continue;
					}
				}
				else
				{
					WARN_LOG_REPORT(Log::Loader, "sectionToModify = %i - ignoring relocation sector %i", sectionToModify, i);
				}
				ERROR_LOG_REPORT(Log::Loader, "Traditional relocations unsupported.");
			}
		}
	}

	// Segment relocations (a few games use them)
	if (GetNumSections() == 0) {
		for (int i = 0; i < header->e_phnum; i++)
		{
			const Elf32_Phdr *p = &segments[i];
			if (p->p_type == PT_PSPREL1) {
				INFO_LOG(Log::Loader,"Loading segment relocations");
				int numRelocs = p->p_filesz / sizeof(Elf32_Rel);

				Elf32_Rel *rels = (Elf32_Rel *)GetSegmentPtr(i);
				if (p->p_offset + p->p_filesz > size_)
					rels = nullptr;
				if (!rels || !LoadRelocations(rels, numRelocs)) {
					ERROR_LOG(Log::Loader, "LoadInto: Relocs failed, trying anyway (2)");
				}
			} else if (p->p_type == PT_PSPREL2) {
				INFO_LOG(Log::Loader,"Loading segment relocations2");
				LoadRelocations2(i);
			}
		}
	}

	return SCE_KERNEL_ERROR_OK;
}


SectionID ElfReader::GetSectionByName(const char *name, int firstSection) const
{
	if (!name)
		return -1;
	for (int i = firstSection; i < header->e_shnum; i++) {
		const char *secname = GetSectionName(i);
		if (secname && strcmp(name, secname) == 0) {
			return i;
		}
	}
	return -1;
}

u32 ElfReader::GetTotalTextSize() const {
	u32 total = 0;
	for (int i = 0; i < GetNumSections(); ++i) {
		if (!(sections[i].sh_flags & SHF_WRITE) && (sections[i].sh_flags & SHF_ALLOC) && !(sections[i].sh_flags & SHF_STRINGS)) {
			total += sections[i].sh_size;
		}
	}
	return total;
}

u32 ElfReader::GetTotalTextSizeFromSeg() const {
	u32 total = 0;
	for (int i = 0; i < GetNumSegments(); ++i) {
		if ((segments[i].p_flags & PF_X) != 0) {
			total += segments[i].p_filesz;
		}
	}
	return total;
}

u32 ElfReader::GetTotalDataSize() const {
	u32 total = 0;
	for (int i = 0; i < GetNumSections(); ++i) {
		if ((sections[i].sh_flags & SHF_WRITE) && (sections[i].sh_flags & SHF_ALLOC) && !(sections[i].sh_flags & SHF_MASKPROC)) {
			total += sections[i].sh_size;
		}
	}
	return total;
}

u32 ElfReader::GetTotalSectionSizeByPrefix(const std::string &prefix) const {
	u32 total = 0;
	for (int i = 0; i < GetNumSections(); ++i) {
		const char *secname = GetSectionName(i);
		if (secname && !strncmp(secname, prefix.c_str(), prefix.length())) {
			total += sections[i].sh_size;
		}
	}
	return total;
}

std::vector<SectionID> ElfReader::GetCodeSections() const {
	std::vector<SectionID> ids;
	for (int i = 0; i < GetNumSections(); ++i) {
		u32 flags = sections[i].sh_flags;
		if ((flags & (SHF_ALLOC | SHF_EXECINSTR)) == (SHF_ALLOC | SHF_EXECINSTR)) {
			ids.push_back(i);
		}
	}
	return ids;
}

bool ElfReader::LoadSymbols()
{
	bool hasSymbols = false;
	SectionID sec = GetSectionByName(".symtab");
	if (sec != -1)
	{
		int stringSection = sections[sec].sh_link;

		const char *stringBase = (const char*)GetSectionDataPtr(stringSection);
		u32 stringOffset = GetSectionDataOffset(stringSection);

		//We have a symbol table!
		Elf32_Sym *symtab = (Elf32_Sym *)(GetSectionDataPtr(sec));
		u32 symtabOffset = GetSectionDataOffset(sec);

		int numSymbols = sections[sec].sh_size / sizeof(Elf32_Sym);
		if (!stringBase || !symtab || (size_t)symtabOffset + sections[sec].sh_size > size_) {
			ERROR_LOG(Log::Loader, "Symbols truncated - ignoring");
			return false;
		}
		// Relocating a symbol needs the section addresses LoadInto computed.
		if (bRelocate && !sectionAddrs) {
			ERROR_LOG(Log::Loader, "LoadSymbols called before LoadInto - ignoring");
			return false;
		}
		
		for (int sym = 0; sym<numSymbols; sym++)
		{
			int size = symtab[sym].st_size;
			if (size == 0)
				continue;

			int bind = symtab[sym].st_info >> 4;
			int type = symtab[sym].st_info & 0xF;
			int sectionIndex = symtab[sym].st_shndx;
			int value = symtab[sym].st_value;
			const size_t nameOffset = (size_t)stringOffset + symtab[sym].st_name;
			if (nameOffset >= size_)
				continue;
			const char *name = stringBase + symtab[sym].st_name;
			// And make sure it's terminated inside the file, before anything strlen()s it.
			if (strnlen(name, size_ - nameOffset) == size_ - nameOffset)
				continue;

			if (bRelocate) {
				// st_shndx is a u16 that can hold reserved values rather than a section number -
				// SHN_ABS (0xFFF1) in particular is common and means the value is already final.
				// Indexing sectionAddrs (which has GetNumSections() entries) with one of those read
				// far out of bounds and added whatever it found to the symbol's address.
				if (sectionIndex == SHN_UNDEF || sectionIndex >= SHN_LORESERVE) {
					// Undefined, absolute or common - nothing of ours to relocate against.
					continue;
				}
				if (sectionIndex >= GetNumSections()) {
					WARN_LOG(Log::Loader, "Symbol '%s' refers to bad section %d, skipping", name, sectionIndex);
					continue;
				}
				value += sectionAddrs[sectionIndex];
			}

			switch (type)
			{
			case STT_OBJECT:
				g_symbolMap->AddData(value,size,DATATYPE_BYTE);
				break;
			case STT_FUNC:
				// C++ homebrew is otherwise a wall of _ZN... - see DemangleSymbolName.
				g_symbolMap->AddFunction(DemangleSymbolName(name).c_str(),value,size);
				break;
			default:
				continue;
			}
			hasSymbols = true;
			//...
		}
	}
	return hasSymbols;
}

// Adds the STT_FUNC/STT_OBJECT symbols from one candidate ELF, if it looks like it belongs to a
// module of this size. Returns the number added, 0 if it doesn't match or has nothing to offer.
// Does this ELF describe the module we just loaded? Split out from the symbol loader so line info
// can reuse it: the two want the same identity check but different sections, and an ELF built with
// -g but stripped of its symbol table still has usable line numbers.
static bool CompanionElfMatchesModule(const std::string &data, u32 moduleSize, const char **why) {
	*why = "too small";
	if (data.size() < sizeof(Elf32_Ehdr))
		return false;
	const Elf32_Ehdr *header = (const Elf32_Ehdr *)data.data();
	*why = "not an ELF";
	if (header->e_ident[EI_MAG0] != ELFMAG0 || header->e_ident[EI_MAG1] != ELFMAG1
		|| header->e_ident[EI_MAG2] != ELFMAG2 || header->e_ident[EI_MAG3] != ELFMAG3)
		return false;
	if (header->e_ident[EI_CLASS] != ELFCLASS32)
		return false;

	*why = "no section headers";
	if (!header->e_shoff || header->e_shentsize < sizeof(Elf32_Shdr))
		return false;
	if ((size_t)header->e_shoff + (size_t)header->e_shnum * header->e_shentsize > data.size())
		return false;

	// Identity check. The companion links at base 0 and covers the same image the module was
	// built into, so the top of its highest section should land within a page of the module's
	// size. Without this an unrelated ELF sitting in the same folder would happily contribute
	// nonsense names at real addresses, which is worse than having none.
	u32 top = 0;
	for (int i = 0; i < header->e_shnum; i++) {
		const Elf32_Shdr *s = (const Elf32_Shdr *)(data.data() + header->e_shoff + (size_t)i * header->e_shentsize);
		if (s->sh_addr)
			top = std::max(top, s->sh_addr + s->sh_size);
	}
	*why = "image size doesn't match the loaded module";
	if (top > moduleSize || top + 0x1000 < moduleSize)
		return false;

	*why = "ok";
	return true;
}

static int LoadSymbolsFromCompanion(const std::string &data, u32 moduleBase, u32 moduleSize, const char **why) {
	if (!CompanionElfMatchesModule(data, moduleSize, why))
		return 0;

	const Elf32_Ehdr *header = (const Elf32_Ehdr *)data.data();
	auto section = [&](int i) {
		return (const Elf32_Shdr *)(data.data() + header->e_shoff + (size_t)i * header->e_shentsize);
	};

	int symtabIndex = -1;
	for (int i = 0; i < header->e_shnum; i++) {
		if (section(i)->sh_type == SHT_SYMTAB)
			symtabIndex = i;
	}
	*why = "no symbol table";
	if (symtabIndex < 0)
		return 0;

	const Elf32_Shdr *symtab = section(symtabIndex);
	if (symtab->sh_link >= header->e_shnum || symtab->sh_entsize < sizeof(Elf32_Sym))
		return 0;
	const Elf32_Shdr *strtab = section(symtab->sh_link);
	if ((size_t)symtab->sh_offset + symtab->sh_size > data.size())
		return 0;
	if ((size_t)strtab->sh_offset + strtab->sh_size > data.size())
		return 0;

	const char *strings = data.data() + strtab->sh_offset;
	const int numSymbols = symtab->sh_size / symtab->sh_entsize;
	int added = 0;
	for (int i = 0; i < numSymbols; i++) {
		const Elf32_Sym *sym = (const Elf32_Sym *)(data.data() + symtab->sh_offset + (size_t)i * symtab->sh_entsize);
		if (!sym->st_size || sym->st_name >= strtab->sh_size)
			continue;
		if (sym->st_value > moduleSize)
			continue;
		const char *name = strings + sym->st_name;
		if (!name[0])
			continue;

		const u32 addr = moduleBase + sym->st_value;
		// C++ homebrew is otherwise a wall of _ZN... - see DemangleSymbolName.
		const std::string readable = DemangleSymbolName(name);
		switch (sym->st_info & 0xF) {
		case STT_FUNC:
			// updateName: these are the names a human wrote, so they beat the analyzer's
			// z_un_<address> placeholders rather than losing to whichever got there first.
			g_symbolMap->AddFunction(readable.c_str(), addr, sym->st_size, -1, true);
			added++;
			break;
		case STT_OBJECT:
			g_symbolMap->AddData(addr, sym->st_size, DATATYPE_BYTE);
			g_symbolMap->AddLabel(readable.c_str(), addr, -1, true);
			added++;
			break;
		default:
			break;
		}
	}
	*why = added ? "ok" : "symbol table had nothing usable";
	return added;
}

int LoadCompanionElfDebugInfo(const Path &gameFile, u32 moduleBase, u32 moduleSize) {
	if (gameFile.empty() || gameFile.Type() != PathType::NATIVE)
		return 0;

	// fileToStart is the game's own directory for folder-launched homebrew
	// (IdentifiedFileType::PSP_PBP_DIRECTORY - the normal case when you pick one in the UI), and
	// the EBOOT/ISO itself otherwise. Navigating up from a directory lands in PSP/GAME and lists
	// sibling *games*, so the companion right there next to the EBOOT was never found - which is
	// exactly the interactive case this exists for.
	const Path dir = File::IsDirectory(gameFile) ? gameFile : gameFile.NavigateUp();
	std::vector<File::FileInfo> files;
	if (!File::GetFilesInDir(dir, &files, "elf:"))
		return 0;

	for (const File::FileInfo &file : files) {
		if (file.isDirectory || file.size < sizeof(Elf32_Ehdr) || file.size > 256 * 1024 * 1024)
			continue;
		std::string data;
		if (!File::ReadBinaryFileToString(file.fullName, &data))
			continue;
		const char *why = "";
		if (!CompanionElfMatchesModule(data, moduleSize, &why)) {
			DEBUG_LOG(Log::Loader, "Companion ELF '%s' skipped: %s", file.name.c_str(), why);
			continue;
		}

		// A companion links at base 0, so its line table needs the module's base added.
		const int lines = g_lineInfo.AddModule(data, moduleBase, moduleSize, moduleBase);

		const int added = LoadSymbolsFromCompanion(data, moduleBase, moduleSize, &why);
		if (added > 0)
			g_symbolMap->SortSymbols();

		if (lines > 0 || added > 0) {
			INFO_LOG(Log::Loader, "Companion ELF '%s': %d symbols, %d line rows", file.name.c_str(), added, lines);
			return added;
		}
		DEBUG_LOG(Log::Loader, "Companion ELF '%s' matched but had nothing usable: %s", file.name.c_str(), why);
	}
	return 0;
}
