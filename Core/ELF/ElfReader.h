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

#pragma once

#include "../../Globals.h"

#include "ElfTypes.h"

enum {
	R_MIPS_NONE,
	R_MIPS_16,
	R_MIPS_32,
	R_MIPS_REL32,
	R_MIPS_26,
	R_MIPS_HI16,
	R_MIPS_LO16,
	R_MIPS_GPREL16,
	R_MIPS_LITERAL,
	R_MIPS_GOT16,
	R_MIPS_PC16,
	R_MIPS_CALL16,
	R_MIPS_GPREL32
};

enum KnownElfTypes
{
	KNOWNELF_PSP = 0,
	KNOWNELF_DS = 1,
	KNOWNELF_GBA = 2,
};

typedef int SectionID;

class ElfReader
{
public:
	ElfReader(void *ptr) :
		sectionOffsets(0),
		sectionAddrs(0),
		bRelocate(false),
		entryPoint(0),
		vaddr(0) {
		INFO_LOG(LOADER, "ElfReader: %p", ptr);
		base = (char*)ptr;
		base32 = (u32 *)ptr;
		header = (Elf32_Ehdr*)ptr;
		segments = (Elf32_Phdr *)(base + header->e_phoff);
		sections = (Elf32_Shdr *)(base + header->e_shoff);
	}

	~ElfReader() {
		delete [] sectionOffsets;
		delete [] sectionAddrs;
	}

	u32 Read32(int off) {
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

	u32 GetSegmentPaddr(int segment)
	{
	    return segments[segment].p_paddr;
	}
	u32 GetSegmentOffset(int segment)
	{
	    return segments[segment].p_offset;
	}
	u32 GetSegmentVaddr(int segment)
	{
		return segmentVAddr[segment];
	}

	bool DidRelocate() {
		return bRelocate;
	}

	u32 GetVaddr()
	{
		return vaddr;
	}

	u32 GetTotalSize()
	{
		return totalSize;
	}

	// More indepth stuff:)
	bool LoadInto(u32 vaddr);
	bool LoadSymbols();
	void LoadRelocations(Elf32_Rel *rels, int numRelocs);
	void LoadRelocations2(int rel_seg);


private:
	char *base;
	u32 *base32;
	Elf32_Ehdr *header;
	Elf32_Phdr *segments;
	Elf32_Shdr *sections;
	u32 *sectionOffsets;
	u32 *sectionAddrs;
	bool bRelocate;
	u32 entryPoint;
	u32 totalSize;
	u32 vaddr;
	u32 segmentVAddr[32];
};
