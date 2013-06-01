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

#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "../MIPS/MIPSTables.h"
#include "ElfReader.h"
#include "../Debugger/SymbolMap.h"
#include "../HLE/sceKernelMemory.h"


const char *ElfReader::GetSectionName(int section)
{
	if (sections[section].sh_type == SHT_NULL)
		return 0;

	int nameOffset = sections[section].sh_name;
	char *ptr = (char*)GetSectionDataPtr(header->e_shstrndx);

	if (ptr)
		return ptr + nameOffset;
	else
		return 0;
}



void addrToHiLo(u32 addr, u16 &hi, s16 &lo)
{
	lo = (addr & 0xFFFF);
	u32 naddr = addr - lo;
	hi = naddr>>16;
	u32 test = (hi<<16) + lo;
	if (test != addr)
	{
		WARN_LOG_REPORT(LOADER, "HI16/LO16 relocation failure?");
	}
}

void ElfReader::LoadRelocations(Elf32_Rel *rels, int numRelocs)
{
	for (int r = 0; r < numRelocs; r++)
	{
		u32 info = rels[r].r_info;
		u32 addr = rels[r].r_offset;

		int type = info & 0xf;

		int readwrite = (info>>8) & 0xff; 
		int relative  = (info>>16) & 0xff;

		//0 = code
		//1 = data

		addr += segmentVAddr[readwrite];

		u32 op = Memory::ReadUnchecked_U32(addr);

		const bool log = false;
		//log=true;
		if (log)
		{
			DEBUG_LOG(LOADER,"rel at: %08x  type: %08x",addr,info);
		}
		u32 relocateTo = segmentVAddr[relative];

		switch (type) 
		{
		case R_MIPS_32:
			if (log)
				DEBUG_LOG(LOADER,"Full address reloc %08x", addr);
			//full address, no problemo
			op += relocateTo;
			break;

		case R_MIPS_26: //j, jal
			//add on to put in correct address space
			if (log)
				DEBUG_LOG(LOADER,"j/jal reloc %08x", addr);
			op = (op & 0xFC000000) | (((op&0x03FFFFFF)+(relocateTo>>2))&0x03FFFFFFF);
			break;

		case R_MIPS_HI16: //lui part of lui-addiu pairs
			{
				if (log)
					DEBUG_LOG(LOADER,"HI reloc %08x", addr);

				u32 cur = (op & 0xFFFF) << 16;
				u16 hi = 0;
				bool found = false;
				for (int t = r + 1; t<numRelocs; t++)
				{
					if ((rels[t].r_info & 0xF) == R_MIPS_LO16) 
					{
						u32 corrLoAddr = rels[t].r_offset + segmentVAddr[readwrite];
						if (log)
						{
							DEBUG_LOG(LOADER,"Corresponding lo found at %08x", corrLoAddr);
						}

						s16 lo = (s32)(s16)(u16)(Memory::ReadUnchecked_U32(corrLoAddr) & 0xFFFF); //signed??
						cur += lo;
						cur += relocateTo;
						addrToHiLo(cur, hi, lo);
						found = true;
						break;
					}
				}
				if (!found)
					ERROR_LOG_REPORT(LOADER, "R_MIPS_HI16: could not find R_MIPS_LO16");

				op = (op & 0xFFFF0000) | (hi);
			}
			break;

		case R_MIPS_LO16: //addiu part of lui-addiu pairs
			{
				if (log)
					DEBUG_LOG(LOADER,"LO reloc %08x", addr);
				u32 cur = op & 0xFFFF;
				cur += relocateTo;
				cur &= 0xFFFF;
				op = (op & 0xFFFF0000) | cur;
			}
			break;

		case R_MIPS_GPREL16: //gp
			{
				char temp[256];
				MIPSDisAsm(op, 0, temp);
				ERROR_LOG_REPORT(LOADER, "ARGH IT'S A GP!!!!!!!! %08x : %s", addr, temp);
			}
			break;

		case 0: // another GP reloc!
			{
				char temp[256];
				MIPSDisAsm(op, 0, temp);
				ERROR_LOG_REPORT(LOADER, "WARNING: GP reloc? @ %08x : 0 : %s", addr, temp);
			}
			break;

		default:
			{
				char temp[256];
				MIPSDisAsm(op, 0, temp);
				ERROR_LOG_REPORT(LOADER,"ARGH IT'S A UNKNOWN RELOCATION!!!!!!!! %08x, type=%d : %s", addr, type, temp);
			}
			break;
		}
		Memory::Write_U32(op, addr);
	}
}


void ElfReader::LoadRelocations2(int rel_seg)
{
	Elf32_Phdr *ph;
	u8 *buf, *end, *flag_table, *type_table;
	int flag_table_size, type_table_size;
	int flag_bits, seg_bits, type_bits;
	int cmd, flag, seg, type;
	int off_seg = 0, addr_seg, rel_base, rel_offset;
	int relocate_to, last_type, lo16;
	u32 op, addr;
	int rcount = 0;

	ph = segments + rel_seg;


	buf = (u8*)GetSegmentPtr(rel_seg);
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
	type_table_size = flag_table[0];
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
				ERROR_LOG_REPORT(LOADER, "Rel2: invalid size flag! %x", flag);
				rel_base = 0;
			}
		}else{
			addr_seg = seg;
			relocate_to = segmentVAddr[addr_seg];

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
				rel_base = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);;
				buf += 4;
			}else{
				ERROR_LOG_REPORT(LOADER, "Rel2: invalid relocat size flag! %x", flag);
			}


			rel_offset = rel_base+segmentVAddr[off_seg];

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
				ERROR_LOG_REPORT(LOADER, "Rel2: invalid lo16 type! %x", flag);
			}

			op = Memory::ReadUnchecked_U32(rel_offset);
			DEBUG_LOG(LOADER, "Rel2: %5d: CMD=0x%04X type=%d off_seg=%d offset=%08x addr_seg=%d op=%08x\n", rcount, cmd, type, off_seg, rel_base, addr_seg, op);

			switch(type){
			case 0:
				continue;
			case 2: // R_MIPS_32
				op += relocate_to;
				break;
			case 3: // R_MIPS_26
			case 6: // R_MIPS_J26
			case 7: // R_MIPS_JAL26
				op = (op&0xFC000000) | (((op&0x03FFFFFF)+(relocate_to>>2))&0x03FFFFFFF);
				break;
			case 4: // R_MIPS_HI16
				addr = ((op<<16)+lo16)+relocate_to;
				if(addr&0x8000)
					addr += 0x00010000;
				op = (op&0xffff0000) | (addr>>16 );
				break;
			case 1:
			case 5: // R)MIPS_LO16
				op = (op&0xffff0000) | (((op&0xffff)+relocate_to)&0xffff);
				break;
			default:
				break;
			}

			Memory::Write_U32(op, rel_offset);
			rcount += 1;
		}
	}

}


bool ElfReader::LoadInto(u32 loadAddress)
{
	DEBUG_LOG(LOADER,"String section: %i", header->e_shstrndx);

	//TODO - Check header->e_ident here
	//let's dump string section
	/*
	char *ptr = (char*)GetSectionDataPtr(header->e_shstrndx);
	if (*ptr == 0)
		ptr++;
	ptr+=513;
	while (*ptr != 0)
	{
		int len = strlen(ptr);
		LOG(LOADER,"XX %s",ptr);
		ptr+=len;
		ptr++;
	}*/

	sectionOffsets = new u32[GetNumSections()];
	sectionAddrs = new u32[GetNumSections()];

	// Should we relocate?
	bRelocate = (header->e_type != ET_EXEC);

	entryPoint = header->e_entry;
	u32 totalStart = 0xFFFFFFFF;
	u32 totalEnd = 0;
	for (int i = 0; i < header->e_phnum; i++) {
		Elf32_Phdr *p = &segments[i];
		if (p->p_type == PT_LOAD) {
			if (p->p_vaddr < totalStart)
				totalStart = p->p_vaddr;
			if (p->p_vaddr + p->p_memsz > totalEnd)
				totalEnd = p->p_vaddr + p->p_memsz;
		}
	}
	totalSize = totalEnd - totalStart;
	if (!bRelocate)
	{
		// Binary is prerelocated, load it where the first segment starts
		vaddr = userMemory.AllocAt(totalStart, totalSize, "ELF");
	}
	else if (loadAddress)
	{
		// Binary needs to be relocated: add loadAddress to the binary start address
		vaddr = userMemory.AllocAt(loadAddress + totalStart, totalSize, "ELF");
	}
	else
	{
		// Just put it where there is room
		vaddr = userMemory.Alloc(totalSize, false, "ELF");
	}

	if (vaddr == (u32)-1) {
		ERROR_LOG_REPORT(LOADER, "Failed to allocate memory for ELF!");
		return false;
	}
	
	if (bRelocate) {
		DEBUG_LOG(LOADER,"Relocatable module");
		entryPoint += vaddr;
	} else {
		DEBUG_LOG(LOADER,"Prerelocated executable");
	}

	DEBUG_LOG(LOADER,"%i segments:", header->e_phnum);

	// First pass : Get the damn bits into RAM
	u32 baseAddress = bRelocate?vaddr:0;

	for (int i=0; i<header->e_phnum; i++)
	{
		Elf32_Phdr *p = segments + i;
		DEBUG_LOG(LOADER, "Type: %08x Vaddr: %08x Filesz: %08x Memsz: %08x ", (int)p->p_type, (u32)p->p_vaddr, (int)p->p_filesz, (int)p->p_memsz);

		if (p->p_type == PT_LOAD)
		{
			segmentVAddr[i] = baseAddress + p->p_vaddr;
			u32 writeAddr = segmentVAddr[i];

			u8 *src = GetSegmentPtr(i);
			u8 *dst = Memory::GetPointer(writeAddr);
			u32 srcSize = p->p_filesz;
			u32 dstSize = p->p_memsz;

			if (srcSize < dstSize)
			{
				memset(dst + srcSize, 0, dstSize - srcSize); //zero out bss
			}

			memcpy(dst, src, srcSize);
			DEBUG_LOG(LOADER,"Loadable Segment Copied to %08x, size %08x", writeAddr, (u32)p->p_memsz);
		}
	}
	userMemory.ListBlocks();

	DEBUG_LOG(LOADER,"%i sections:", header->e_shnum);

	for (int i = 0; i < GetNumSections(); i++)
	{
		Elf32_Shdr *s = &sections[i];
		const char *name = GetSectionName(i);

		u32 writeAddr = s->sh_addr + baseAddress;
		sectionOffsets[i] = writeAddr - vaddr;
		sectionAddrs[i] = writeAddr;

		if (s->sh_flags & SHF_ALLOC)
		{
			DEBUG_LOG(LOADER,"Data Section found: %s     Sitting at %08x, size %08x", name, writeAddr, (u32)s->sh_size);
		}
		else
		{
			DEBUG_LOG(LOADER,"NonData Section found: %s     Ignoring (size=%08x) (flags=%08x)", name, (u32)s->sh_size, (u32)s->sh_flags);
		}
	}

	DEBUG_LOG(LOADER,"Relocations:");

	// Second pass: Do necessary relocations
	for (int i=0; i<GetNumSections(); i++)
	{
		Elf32_Shdr *s = &sections[i];
		const char *name = GetSectionName(i);

		if (s->sh_type == SHT_PSPREL)
		{
			//We have a relocation table!
			int sectionToModify = s->sh_info;

			if (sectionToModify >= 0)
			{
				if (!(sections[sectionToModify].sh_flags & SHF_ALLOC))
				{
					ERROR_LOG_REPORT(LOADER, "Trying to relocate non-loaded section %s", GetSectionName(sectionToModify));
					continue;
				}

				int numRelocs = s->sh_size / sizeof(Elf32_Rel);

				Elf32_Rel *rels = (Elf32_Rel *)GetSectionDataPtr(i);

				DEBUG_LOG(LOADER,"%s: Performing %i relocations on %s",name,numRelocs,GetSectionName(sectionToModify));
				LoadRelocations(rels, numRelocs);
			}
			else
			{
				WARN_LOG_REPORT(LOADER, "sectionToModify = %i - ignoring PSP relocation sector %i", sectionToModify, i);
			}
		}
		else if (s->sh_type == SHT_REL)
		{
			DEBUG_LOG(LOADER, "Traditional relocation section found.");
			if (!bRelocate)
			{
				DEBUG_LOG(LOADER, "Binary is prerelocated. Skipping relocations.");
			}
			else
			{
				//We have a relocation table!
				int sectionToModify = s->sh_info;
				if (sectionToModify >= 0)
				{
					if (!(sections[sectionToModify].sh_flags & SHF_ALLOC))
					{
						ERROR_LOG_REPORT(LOADER, "Trying to relocate non-loaded section %s, ignoring", GetSectionName(sectionToModify));
						continue;
					}
				}
				else
				{
					WARN_LOG_REPORT(LOADER, "sectionToModify = %i - ignoring relocation sector %i", sectionToModify, i);
				}
				ERROR_LOG_REPORT(LOADER, "Traditional relocations unsupported.");
			}
		}
	}

	// Segment relocations (a few games use them)
	if (GetNumSections() == 0)
	{
		for (int i=0; i<header->e_phnum; i++)
		{
			Elf32_Phdr *p = &segments[i];
			if (p->p_type == 0x700000A0)
			{
				INFO_LOG(LOADER,"Loading segment relocations");

				int numRelocs = p->p_filesz / sizeof(Elf32_Rel);

				Elf32_Rel *rels = (Elf32_Rel *)GetSegmentPtr(i);
				LoadRelocations(rels, numRelocs);
			} else if (p->p_type == 0x700000A1)
			{
				INFO_LOG(LOADER,"Loading segment relocations2");
				LoadRelocations2(i);
			}
		}
	}

	NOTICE_LOG(LOADER,"ELF loading completed successfully.");
	return true;
}


SectionID ElfReader::GetSectionByName(const char *name, int firstSection)
{
	for (int i = firstSection; i < header->e_shnum; i++)
	{
		const char *secname = GetSectionName(i);

		if (secname != 0 && strcmp(name, secname) == 0)
		{
			return i;
		}
	}
	return -1;
}

bool ElfReader::LoadSymbols()
{
	bool hasSymbols = false;
	SectionID sec = GetSectionByName(".symtab");
	if (sec != -1)
	{
		int stringSection = sections[sec].sh_link;

		const char *stringBase = (const char*)GetSectionDataPtr(stringSection);

		//We have a symbol table!
		Elf32_Sym *symtab = (Elf32_Sym *)(GetSectionDataPtr(sec));

		int numSymbols = sections[sec].sh_size / sizeof(Elf32_Sym);
		
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

			if (bRelocate)
				value += sectionAddrs[sectionIndex];
			SymbolType symtype = ST_DATA;

			switch (type)
			{
			case STT_OBJECT:
				symtype = ST_DATA; break;
			case STT_FUNC:
				symtype = ST_FUNCTION; break;
			default:
				continue;
			}
			symbolMap.AddSymbol(name, value, size, symtype);
			hasSymbols = true;
			//...
		}
	}
	return hasSymbols;
}




