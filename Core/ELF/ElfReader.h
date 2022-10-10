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

#include <vector>
#include "Common/CommonTypes.h"
#include "Core/ELF/ElfTypes.h"

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

enum KnownElfTypes {
	KNOWNELF_PSP = 0,
	KNOWNELF_DS = 1,
	KNOWNELF_GBA = 2,
};

typedef int SectionID;

class ElfReader {
public:
	ElfReader(const void *ptr, size_t size) {
		base = (const char*)ptr;
		base32 = (const u32 *)ptr;
		header = (const Elf32_Ehdr*)ptr;
		segments = (const Elf32_Phdr *)(base + header->e_phoff);
		sections = (const Elf32_Shdr *)(base + header->e_shoff);
		size_ = size;
	}

	~ElfReader() {
		delete[] sectionOffsets;
		delete[] sectionAddrs;
	}

	u32 Read32(int off) const {
		return base32[off >> 2];
	}

	// Quick accessors
	ElfType GetType() const { return (ElfType)(u16)(header->e_type); }
	ElfMachine GetMachine() const { return (ElfMachine)(u16)(header->e_machine); }
	u32 GetEntryPoint() const { return entryPoint; }
	u32 GetFlags() const { return (u32)(header->e_flags); }

	int GetNumSegments() const { return (int)(header->e_phnum); }
	int GetNumSections() const { return (int)(header->e_shnum); }
	const char *GetSectionName(int section) const;
	const u8 *GetPtr(u32 offset) const {
		return (const u8*)base + offset;
	}
	// Note: zero is not a valid output, means unavailable.
	u32 GetSectionDataOffset(int section) const {
		if (section < 0 || section >= header->e_shnum)
			return 0;
		if (sections[section].sh_type == SHT_NOBITS)
			return 0;
		return sections[section].sh_offset;
	}
	const u8 *GetSectionDataPtr(int section) const {
		u32 offset = GetSectionDataOffset(section);
		if (offset == 0 || offset > size_)
			return nullptr;
		return GetPtr(offset);
	}
	const u8 *GetSegmentPtr(int segment) const {
		if (segments[segment].p_offset > size_)
			return nullptr;
		return GetPtr(segments[segment].p_offset);
	}
	u32 GetSectionAddr(SectionID section) const {
		return sectionAddrs[section];
	}
	int GetSectionSize(SectionID section) const {
		return sections[section].sh_size;
	}

	//-1 for not found
	SectionID GetSectionByName(const char *name, int firstSection = 0) const;

	u32 GetSegmentPaddr(int segment) const {
		return segments[segment].p_paddr;
	}
	u32 GetSegmentOffset(int segment) const {
		return segments[segment].p_offset;
	}
	u32 GetSegmentVaddr(int segment) const {
		return segmentVAddr[segment];
	}
	u32 GetSegmentDataSize(int segment) const {
		return segments[segment].p_filesz;
	}
	u32 GetSegmentMemSize(int segment) const {
		return segments[segment].p_memsz;
	}

	u32 GetFirstSegmentAlign() const {
		return firstSegAlign;
	}

	bool DidRelocate() const {
		return bRelocate;
	}

	u32 GetVaddr() const {
		return vaddr;
	}

	u32 GetTotalSize() const {
		return totalSize;
	}

	u32 GetTotalTextSize() const;
	u32 GetTotalTextSizeFromSeg() const;
	u32 GetTotalDataSize() const;
	u32 GetTotalSectionSizeByPrefix(const std::string &prefix) const;

	std::vector<SectionID> GetCodeSections() const;

	int LoadInto(u32 vaddr, bool fromTop);
	bool LoadSymbols();
	bool LoadRelocations(const Elf32_Rel *rels, int numRelocs);
	void LoadRelocations2(int rel_seg);

private:
	const char *base = nullptr;
	const u32 *base32 = nullptr;
	const Elf32_Ehdr *header = nullptr;
	const Elf32_Phdr *segments = nullptr;
	const Elf32_Shdr *sections = nullptr;
	u32 *sectionOffsets = nullptr;
	u32 *sectionAddrs = nullptr;
	bool bRelocate = false;
	u32 entryPoint = 0;
	u32 totalSize = 0;
	u32 vaddr = 0;
	u32 segmentVAddr[32]{};
	size_t size_ = 0;
	u32 firstSegAlign = 0;
};
