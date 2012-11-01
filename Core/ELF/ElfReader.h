// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "../../Globals.h"

#include "ElfTypes.h"

enum KnownElfTypes
{
	KNOWNELF_PSP = 0,
	KNOWNELF_DS = 1,
	KNOWNELF_GBA = 2,
};

typedef int SectionID;

class ElfReader
{
	char *base;
	u32 *base32;
	Elf32_Ehdr *header;
	Elf32_Phdr *segments;
	Elf32_Shdr *sections;
	u32 *sectionOffsets;
	u32 *sectionAddrs;
	bool bRelocate;
	u32 entryPoint;
public:
	ElfReader(void *ptr)
	{
		INFO_LOG(LOADER, "ElfReader: %p", ptr);
		base = (char*)ptr;
		base32 = (u32 *)ptr;
		header = (Elf32_Ehdr*)ptr;
		segments = (Elf32_Phdr *)(base + header->e_phoff);
		sections = (Elf32_Shdr *)(base + header->e_shoff);
	}

	~ElfReader()
	{

	}

	u32 Read32(int off)
	{
		return base32[off>>2];
	}

	// Quick accessors
	ElfType GetType() { return (ElfType)(header->e_type); }
	ElfMachine GetMachine() { return (ElfMachine)(header->e_machine); }
	u32 GetEntryPoint() { return entryPoint; }
	u32 GetFlags() { return (u32)(header->e_flags); }

	int GetNumSegments() { return (int)(header->e_phnum); }
	int GetNumSections() { return (int)(header->e_shnum); }
	const char *GetSectionName(int section);
	u8 *GetPtr(int offset)
	{
		return (u8*)base + offset;
	}
	u8 *GetSectionDataPtr(int section)
	{
		if (section < 0 || section >= header->e_shnum)
			return 0;
		if (sections[section].sh_type != SHT_NOBITS)
			return GetPtr(sections[section].sh_offset);
		else
			return 0;
	}
	u8 *GetSegmentPtr(int segment)
	{
		return GetPtr(segments[segment].p_offset);
	}
	u32 GetSectionAddr(SectionID section) {return sectionAddrs[section];}
	int GetSectionSize(SectionID section)
	{
		return sections[section].sh_size;
	}
	SectionID GetSectionByName(const char *name, int firstSection=0); //-1 for not found

	bool DidRelocate() {
		return bRelocate;
	}
	// More indepth stuff:)
	bool LoadInto(u32 vaddr);
	bool LoadSymbols();
};
