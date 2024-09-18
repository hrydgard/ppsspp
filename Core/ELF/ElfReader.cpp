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

#include <atomic>

#include "Common/StringUtils.h"
#include "Common/Thread/ParallelLoop.h"

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/ThreadPools.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/ELF/ElfReader.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/SymbolMap.h"
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
	u32 test = (hi<<16) + lo;
	if (test != addr)
	{
		WARN_LOG_REPORT(Log::Loader, "HI16/LO16 relocation failure?");
	}
}

bool ElfReader::LoadRelocations(const Elf32_Rel *rels, int numRelocs) {
	std::vector<u32> relocOps;
	relocOps.resize(numRelocs);

	DEBUG_LOG(Log::Loader, "Loading %i relocations...", numRelocs);
	std::atomic<int> numErrors;
	numErrors.store(0);

	ParallelRangeLoop(&g_threadManager, [&](int l, int h) {
		for (int r = l; r < h; r++) {
			u32 info = rels[r].r_info;
			u32 addr = rels[r].r_offset;

			int type = info & 0xf;

			// Often: 0 = code, 1 = data.
			int readwrite = (info >> 8) & 0xff;
			if (readwrite >= (int)ARRAY_SIZE(segmentVAddr)) {
				if (numErrors < 10) {
					ERROR_LOG_REPORT(Log::Loader, "Bad segment number %i", readwrite);
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

			relocOps[r] = Memory::ReadUnchecked_Instruction(addr, true).encoding;
		}
	}, 0, numRelocs, 128, TaskPriority::HIGH);

	ParallelRangeLoop(&g_threadManager, [&](int l, int h) {
		for (int r = l; r < h; r++) {
			VERBOSE_LOG(Log::Loader, "Loading reloc %i  (%p)...", r, rels + r);
			u32 info = rels[r].r_info;
			u32 addr = rels[r].r_offset;

			int type = info & 0xf;
			int readwrite = (info >> 8) & 0xff;
			int relative = (info >> 16) & 0xff;

			if (readwrite >= (int)ARRAY_SIZE(segmentVAddr)) {
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
			u32 relocateTo = relative >= (int)ARRAY_SIZE(segmentVAddr) ? 0 : segmentVAddr[relative];

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

					u32 corrLoAddr = rels[t].r_offset + segmentVAddr[readwrite];

					// In MotorStorm: Arctic Edge (US), these are sometimes R_MIPS_16 (instead of LO16.)
					// It appears the PSP takes any relocation that is not a HI16.
					if (t_type != R_MIPS_LO16) {
						if (t_type != R_MIPS_16) {
							// Let's play it safe for now and skip.  We've only seen this type.
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
				if (!found) {
					ERROR_LOG_REPORT(Log::Loader, "R_MIPS_HI16: could not find R_MIPS_LO16 (r=%d of %d, addr=%08x)", r, numRelocs, addr);
				}
				op = (op & 0xFFFF0000) | hi;
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
	}, 0, numRelocs, 128, TaskPriority::HIGH);

	if (numErrors) {
		WARN_LOG(Log::Loader, "%i bad relocations found!!!", numErrors.load());
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

	const Elf32_Phdr *ph = segments + rel_seg;

	buf = (u8*)GetSegmentPtr(rel_seg);
	if (!buf) {
		ERROR_LOG_REPORT(Log::Loader, "Rel2 segment invalid");
		return;
	}
	end = buf+ph->p_filesz;

	flag_bits = buf[2];
	type_bits = buf[3];

	seg_bits = 1;
	while((1<<seg_bits)<rel_seg)
		seg_bits += 1;

	buf += 4;

	flag_table = buf;
	flag_table_size = flag_table[0];
	buf += flag_table_size;

	type_table = buf;
	type_table_size = type_table[0];
	buf += type_table_size;

	rel_base = 0;
	last_type = -1;
	while(buf<end){
		cmd = *(u16*)(buf);
		buf += 2;

		flag = ( cmd<<(16-flag_bits))&0xffff;
		flag = (flag>>(16-flag_bits))&0xffff;
		flag = flag_table[flag];

		seg = (cmd<<(16-seg_bits-flag_bits))&0xffff;
		seg = (seg>>(16-seg_bits))&0xffff;

		type = ( cmd<<(16-type_bits-seg_bits-flag_bits))&0xffff;
		type = (type>>(16-type_bits))&0xffff;
		type = type_table[type];

		if((flag&0x01)==0){
			off_seg = seg;
			if((flag&0x06)==0){
				rel_base = cmd>>(seg_bits+flag_bits);
			}else if((flag&0x06)==4){
				rel_base = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
				buf += 4;
			}else{
				ERROR_LOG_REPORT(Log::Loader, "Rel2: invalid size flag! %x", flag);
				rel_base = 0;
			}
		}else{
			addr_seg = seg;
			relocate_to = addr_seg >= (int)ARRAY_SIZE(segmentVAddr) ? 0 : segmentVAddr[addr_seg];
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
				rel_offset = (rel_offset<<16) | (buf[0]) | (buf[1]<<8);
				buf += 2;
				rel_base += rel_offset;
			}else if((flag&0x06)==0x04){
				rel_base = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
				buf += 4;
			}else{
				ERROR_LOG_REPORT(Log::Loader, "Rel2: invalid relocat size flag! %x", flag);
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
				lo16 = (buf[0]) | (buf[1]<<8);
				if(lo16&0x8000)
					lo16 |= 0xffff0000;
				buf += 2;
			}else{
				ERROR_LOG_REPORT(Log::Loader, "Rel2: invalid lo16 type! %x", flag);
			}

			op = Memory::Read_Instruction(rel_offset, true).encoding;
			DEBUG_LOG(Log::Loader, "Rel2: %5d: CMD=0x%04X flag=%x type=%d off_seg=%d offset=%08x addr_seg=%d op=%08x\n", rcount, cmd, flag, type, off_seg, rel_base, addr_seg, op);

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

			Memory::Write_U32(op, rel_offset);
			NotifyMemInfo(MemBlockFlags::WRITE, rel_offset, 4, "Relocation2");
			rcount += 1;
		}
	}

}


int ElfReader::LoadInto(u32 loadAddress, bool fromTop)
{
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
	for (int i = 0; i < header->e_phnum; i++) {
		const Elf32_Phdr *p = &segments[i];
		if (p->p_type == PT_LOAD) {
			if (p->p_vaddr < totalStart) {
				totalStart = p->p_vaddr;
				firstSegAlign = p->p_align;
			}
			if (p->p_vaddr + p->p_memsz > totalEnd)
				totalEnd = p->p_vaddr + p->p_memsz;
		}
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
		// Binary needs to be relocated: add loadAddress to the binary start address
		vaddr = memblock.AllocAt(loadAddress + totalStart, totalSize, modName.c_str());
	}
	else
	{
		// Just put it where there is room
		vaddr = memblock.Alloc(totalSize, fromTop, modName.c_str());
	}

	if (vaddr == (u32)-1) {
		ERROR_LOG_REPORT(Log::Loader, "Failed to allocate memory for ELF!");
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

	// First pass : Get the damn bits into RAM
	u32 baseAddress = bRelocate ? vaddr : 0;

	for (int i = 0; i < header->e_phnum; i++)
	{
		const Elf32_Phdr *p = segments + i;
		DEBUG_LOG(Log::Loader, "Type: %08x Vaddr: %08x Filesz: %08x Memsz: %08x ", (int)p->p_type, (u32)p->p_vaddr, (int)p->p_filesz, (int)p->p_memsz);

		if (p->p_type == PT_LOAD)
		{
			segmentVAddr[i] = baseAddress + p->p_vaddr;
			u32 writeAddr = segmentVAddr[i];

			const u8 *src = GetSegmentPtr(i);
			if (!src || p->p_offset + p->p_filesz > size_) {
				ERROR_LOG(Log::Loader, "Segment %d pointer invalid - truncated?", i);
				continue;
			}
			u32 srcSize = p->p_filesz;
			u32 dstSize = p->p_memsz;
			u8 *dst = Memory::GetPointerWriteRange(writeAddr, dstSize);
			if (dst) {
				if (srcSize < dstSize) {
					memset(dst + srcSize, 0, dstSize - srcSize); //zero out bss
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
	memblock.ListBlocks();

	DEBUG_LOG(Log::Loader,"%i sections:", header->e_shnum);

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
			if (sectionToModify >= 0)
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
				if (sectionToModify >= 0)
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
		if (!stringBase || !symtab || symtabOffset + sections[sec].sh_size > size_) {
			ERROR_LOG(Log::Loader, "Symbols truncated - ignoring");
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
			const char *name = stringBase + symtab[sym].st_name;
			if (stringOffset + symtab[sym].st_name >= size_)
				continue;

			if (bRelocate)
				value += sectionAddrs[sectionIndex];

			switch (type)
			{
			case STT_OBJECT:
				g_symbolMap->AddData(value,size,DATATYPE_BYTE);
				break;
			case STT_FUNC:
				g_symbolMap->AddFunction(name,value,size);
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
