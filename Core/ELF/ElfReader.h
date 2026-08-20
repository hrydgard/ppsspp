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
#include "Common/File/Path.h"
#include "Core/ELF/PSPElfTypes.h"

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

// Placeholder in segmentVAddr for a program header that isn't PT_LOAD. Those have no address to
// relocate against, and a relocation naming one is a defect in the file rather than something to
// quietly treat as segment zero. Not a plausible load address, and distinct from 0, which a
// prerelocated module's first segment could in principle have.
constexpr u32 SEGMENT_NOT_LOADED = 0xFFFFFFFF;

class ElfReader {
public:
	ElfReader(const void *ptr, size_t size) {
		base = (const char*)ptr;
		base32 = (const u32 *)ptr;
		size_ = size;
		// Don't read the header to find the segment and section tables before we know it's there.
		// LoadInto() rejects anything this small; header stays null so nothing else can use it either.
		if (size < sizeof(Elf32_Ehdr))
			return;
		header = (const Elf32_Ehdr*)ptr;
		segments = (const Elf32_Phdr *)(base + header->e_phoff);
		sections = (const Elf32_Shdr *)(base + header->e_shoff);
	}

	~ElfReader() {
		delete[] sectionOffsets;
		delete[] sectionAddrs;
	}

	u32 Read32(int off) const {
		return base32[off >> 2];
	}

	// Quick accessors. header is null if we weren't even handed a full ELF header, see the
	// constructor - so these all have to cope with that.
	ElfType GetType() const { return header ? (ElfType)(u16)(header->e_type) : (ElfType)0; }
	ElfMachine GetMachine() const { return header ? (ElfMachine)(u16)(header->e_machine) : (ElfMachine)0; }
	u32 GetEntryPoint() const { return entryPoint; }
	u32 GetFlags() const { return header ? (u32)(header->e_flags) : 0; }

	int GetNumSegments() const { return header ? (int)(header->e_phnum) : 0; }
	int GetNumSections() const { return header ? (int)(header->e_shnum) : 0; }
	const char *GetSectionName(int section) const;
	const u8 *GetPtr(u32 offset) const {
		return (const u8*)base + offset;
	}
	// Note: zero is not a valid output, means unavailable.
	u32 GetSectionDataOffset(int section) const {
		if (section < 0 || section >= GetNumSections())
			return 0;
		if (sections[section].sh_type == SHT_NOBITS)
			return 0;
		return sections[section].sh_offset;
	}
	const u8 *GetSectionDataPtr(int section) const {
		u32 offset = GetSectionDataOffset(section);
		// Note >=: an offset exactly at the end of the file addresses no bytes at all.
		if (offset == 0 || offset >= size_)
			return nullptr;
		return GetPtr(offset);
	}
	const u8 *GetSegmentPtr(int segment) const {
		if (segment < 0 || segment >= GetNumSegments())
			return nullptr;
		if (segments[segment].p_offset >= size_)
			return nullptr;
		return GetPtr(segments[segment].p_offset);
	}
	u32 GetSectionAddr(SectionID section) const {
		if (section < 0 || section >= GetNumSections() || !sectionAddrs)
			return 0;
		return sectionAddrs[section];
	}
	int GetSectionSize(SectionID section) const {
		if (section < 0 || section >= GetNumSections())
			return 0;
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
		// Answers 0 for a segment we didn't load, which is what this returned back when the table
		// was a zero-initialized array - sceKernelModule stores this straight into the module info.
		if (segment < 0 || (size_t)segment >= segmentVAddr.size() || segmentVAddr[segment] == SEGMENT_NOT_LOADED)
			return 0;
		return segmentVAddr[segment];
	}

	// True if this program header is one we loaded, so relocations can refer to it.
	bool SegmentIsLoaded(int segment) const {
		return segment >= 0 && (size_t)segment < segmentVAddr.size() && segmentVAddr[segment] != SEGMENT_NOT_LOADED;
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
	// One entry per program header, sized in LoadInto(). Real modules have a handful of segments,
	// but not always - PES 2014's EBOOT has 71 - and e_phnum is a u16, so this can't be a fixed
	// array that callers are trusted to stay inside.
	std::vector<u32> segmentVAddr;
	size_t size_ = 0;
	u32 firstSegAlign = 0;
};

// Homebrew usually ships the unstripped ELF it was built from next to the EBOOT (app.elf beside
// app.prx, and similar) - prxgen strips the symbol table on the way to the PRX, so the module
// PPSSPP actually loads has no names in it and every function ends up called z_un_<address>.
// This looks for such a companion in the game's own directory and, if one plausibly belongs to
// this module, adds its function and data symbols at the module's load address.
//
// Symbols and DWARF line info both come out of that file, and both load unconditionally -
// bAutoSaveLoadSymbols governs writing .ppsym files back out, not reading debug info that's
// already sitting next to the game. Same reason the main ELF's own symbols aren't gated either.
//
// Returns the number of symbols added, 0 if none were or no matching ELF was found.
int LoadCompanionElfDebugInfo(const Path &gameFile, u32 moduleBase, u32 moduleSize);
