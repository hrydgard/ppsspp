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

#include "../MemMap.h"
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

#define R_MIPS32 2
#define R_MIPS26 4
#define R_MIPS16_HI 5
#define R_MIPS16_LO 6

		switch (type) 
		{
		case R_MIPS32:
			if (log)
				DEBUG_LOG(LOADER,"Full address reloc %08x", addr);
			//full address, no problemo
			op += relocateTo;
			break;

		case R_MIPS26: //j, jal
			//add on to put in correct address space
			if (log)
				DEBUG_LOG(LOADER,"j/jal reloc %08x", addr);
			op = (op & 0xFC000000) | (((op&0x03FFFFFF)+(relocateTo>>2))&0x03FFFFFFF);
			break;

		case R_MIPS16_HI: //lui part of lui-addiu pairs
			{
				if (log)
					DEBUG_LOG(LOADER,"HI reloc %08x", addr);

				u32 cur = (op & 0xFFFF) << 16;
				u16 hi = 0;
				bool found = false;
				for (int t = r + 1; t<numRelocs; t++)
				{
					if ((rels[t].r_info & 0xF) == R_MIPS16_LO) 
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
					ERROR_LOG(LOADER, "R_MIPS16: not found");

				op = (op & 0xFFFF0000) | (hi);
			}
			break;

		case R_MIPS16_LO: //addiu part of lui-addiu pairs
			{
				if (log)
					DEBUG_LOG(LOADER,"LO reloc %08x", addr);
				u32 cur = op & 0xFFFF;
				cur += relocateTo;
				cur &= 0xFFFF;
				op = (op & 0xFFFF0000) | cur;
			}
			break;

		case 7: //gp
			if (log)
				ERROR_LOG(LOADER,"ARGH IT'S A GP!!!!!!!! %08x", addr);
			break;

		case 0: // another GP reloc!
			{
				char temp[256];
				MIPSDisAsm(op, 0, temp);
				ERROR_LOG(LOADER,"WARNING: GP reloc? @ %08x : 0 : %s", addr, temp );
			}
			break;

		default:
			ERROR_LOG(LOADER,"ARGH IT'S A UNKNOWN RELOCATION!!!!!!!! %08x", addr);
			break;
		}
		Memory::Write_U32(op, addr);
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
	u32 totalSize = totalEnd - totalStart;
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
		ERROR_LOG(LOADER, "Failed to allocate memory for ELF!");
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
		DEBUG_LOG(LOADER, "Type: %i Vaddr: %08x Filesz: %i Memsz: %i ", (int)p->p_type, (u32)p->p_vaddr, (int)p->p_filesz, (int)p->p_memsz);

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
					ERROR_LOG(LOADER,"Trying to relocate non-loaded section %s",GetSectionName(sectionToModify));
					continue;
				}

				int numRelocs = s->sh_size / sizeof(Elf32_Rel);

				Elf32_Rel *rels = (Elf32_Rel *)GetSectionDataPtr(i);

				DEBUG_LOG(LOADER,"%s: Performing %i relocations on %s",name,numRelocs,GetSectionName(sectionToModify));
				LoadRelocations(rels, numRelocs);
			}
			else
			{
				WARN_LOG(LOADER, "sectionToModify = %i - ignoring PSP relocation sector %i", sectionToModify, i);
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
						ERROR_LOG(LOADER,"Trying to relocate non-loaded section %s, ignoring",GetSectionName(sectionToModify));
						continue;
					}
				}
				else
				{
					WARN_LOG(LOADER, "sectionToModify = %i - ignoring relocation sector %i", sectionToModify, i);
				}
				ERROR_LOG(LOADER,"Traditional relocations unsupported.");
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




