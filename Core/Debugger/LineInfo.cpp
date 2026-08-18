// Copyright (c) 2026- PPSSPP Project.

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

#include <algorithm>
#include <cstring>
#include <map>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/Debugger/LineInfo.h"
#include "Core/ELF/ElfReader.h"

LineInfoMap g_lineInfo;

namespace {

// Bounds-checked cursor over the section. Everything here is parsing a file we didn't produce, so
// a truncated or malformed one has to fail rather than read past the end - Fail() latches, and
// every read after it returns zero, which lets the parse loops check once at the end instead of
// after every field.
class Reader {
public:
	Reader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

	bool ok() const { return ok_; }
	size_t pos() const { return pos_; }
	void Seek(size_t pos) {
		if (pos > size_)
			Fail();
		else
			pos_ = pos;
	}
	bool AtEnd(size_t limit) const { return pos_ >= limit; }

	uint8_t U8() {
		if (pos_ + 1 > size_)
			return Fail();
		return data_[pos_++];
	}
	uint16_t U16() {
		if (pos_ + 2 > size_)
			return Fail();
		uint16_t v;
		memcpy(&v, data_ + pos_, 2);
		pos_ += 2;
		return v;
	}
	uint32_t U32() {
		if (pos_ + 4 > size_)
			return Fail();
		uint32_t v;
		memcpy(&v, data_ + pos_, 4);
		pos_ += 4;
		return v;
	}
	uint64_t ULEB() {
		uint64_t result = 0;
		int shift = 0;
		for (int i = 0; i < 10; i++) {
			const uint8_t b = U8();
			if (!ok_)
				return 0;
			if (shift < 64)
				result |= (uint64_t)(b & 0x7f) << shift;
			shift += 7;
			if (!(b & 0x80))
				return result;
		}
		return Fail();
	}
	int64_t SLEB() {
		int64_t result = 0;
		int shift = 0;
		for (int i = 0; i < 10; i++) {
			const uint8_t b = U8();
			if (!ok_)
				return 0;
			if (shift < 64)
				result |= (int64_t)(b & 0x7f) << shift;
			shift += 7;
			if (!(b & 0x80)) {
				if (shift < 64 && (b & 0x40))
					result -= (int64_t)1 << shift;
				return result;
			}
		}
		return Fail();
	}
	std::string Str() {
		const size_t start = pos_;
		while (pos_ < size_ && data_[pos_])
			pos_++;
		if (pos_ >= size_) {
			Fail();
			return std::string();
		}
		std::string s((const char *)data_ + start, pos_ - start);
		pos_++;
		return s;
	}

private:
	uint32_t Fail() {
		ok_ = false;
		pos_ = size_;
		return 0;
	}

	const uint8_t *data_;
	size_t size_;
	size_t pos_ = 0;
	bool ok_ = true;
};

// Standard opcode numbers we act on. The rest are skipped generically using the header's operand
// counts, which is what makes an unknown-but-well-formed producer harmless.
enum {
	DW_LNS_copy = 1,
	DW_LNS_advance_pc = 2,
	DW_LNS_advance_line = 3,
	DW_LNS_set_file = 4,
	DW_LNS_set_column = 5,
	DW_LNS_negate_stmt = 6,
	DW_LNS_set_basic_block = 7,
	DW_LNS_const_add_pc = 8,
	DW_LNS_fixed_advance_pc = 9,
	DW_LNE_end_sequence = 1,
	DW_LNE_set_address = 2,
	DW_LNE_define_file = 3,
};

}  // namespace

// One .debug_line unit: header, then a bytecode program that walks a virtual machine whose output
// rows are (address, file, line). See the DWARF spec, "Line Number Information".
static bool ParseUnit(Reader &r, size_t unitEnd, std::vector<LineEntry> *entries,
	std::vector<std::string> *files, std::map<std::string, u32> *fileIndex, u32 moduleStart, u32 moduleSize, u32 addressDelta) {
	const uint16_t version = r.U16();
	if (version < 2 || version > 4) {
		// v5 rewrote the file table to use form-coded entries, which is a different parser. Nothing
		// that targets the PSP emits it today (psp-gcc is on 2, Zig on 4), so skip rather than
		// risk decoding it wrong.
		return false;
	}

	const uint32_t headerLength = r.U32();
	const size_t programStart = r.pos() + headerLength;

	const uint8_t minInstLength = r.U8();
	if (version >= 4)
		r.U8();  // maximum_operations_per_instruction
	const uint8_t defaultIsStmt = r.U8();
	(void)defaultIsStmt;
	const int8_t lineBase = (int8_t)r.U8();
	const uint8_t lineRange = r.U8();
	const uint8_t opcodeBase = r.U8();
	if (!r.ok() || lineRange == 0 || minInstLength == 0 || opcodeBase == 0)
		return false;

	std::vector<uint8_t> stdOpcodeLengths(opcodeBase > 0 ? opcodeBase - 1 : 0);
	for (size_t i = 0; i < stdOpcodeLengths.size(); i++)
		stdOpcodeLengths[i] = r.U8();

	// include_directories, then file_names - each a list terminated by an empty string. We only
	// keep the names; the directory index is ignored, since a bare file name is what's actually
	// readable in a status bar or a backtrace.
	while (r.ok() && !r.Str().empty()) {
	}
	// Index 0 is unused in DWARF < 5; entries start at 1.
	std::vector<u32> unitFiles{ 0 };
	while (r.ok()) {
		const std::string name = r.Str();
		if (name.empty())
			break;
		r.ULEB();  // directory index
		r.ULEB();  // modification time
		r.ULEB();  // length
		// Deduped across units so a header included by 500 files is stored once.
		auto it = fileIndex->find(name);
		if (it == fileIndex->end()) {
			it = fileIndex->insert({ name, (u32)files->size() }).first;
			files->push_back(name);
		}
		unitFiles.push_back(it->second);
	}
	if (!r.ok())
		return false;

	r.Seek(programStart);

	u32 address = 0;
	u32 file = 1;
	int line = 1;
	bool sawAddress = false;

	auto emit = [&](u32 lineNo) {
		// Rows before a DW_LNE_set_address belong to no real code, and anything that doesn't land
		// inside the module after relocation would only produce bogus lookups.
		const u32 finalAddress = addressDelta + address;
		if (!sawAddress || finalAddress < moduleStart || finalAddress - moduleStart >= moduleSize)
			return;
		LineEntry e;
		e.address = finalAddress;
		e.line = lineNo;
		e.fileIndex = file < unitFiles.size() ? unitFiles[file] : 0;
		entries->push_back(e);
	};

	while (r.ok() && !r.AtEnd(unitEnd)) {
		const uint8_t op = r.U8();
		if (op >= opcodeBase) {
			// Special opcode: one byte encoding both an address advance and a line delta.
			const uint8_t adjusted = op - opcodeBase;
			address += (adjusted / lineRange) * minInstLength;
			line += lineBase + (adjusted % lineRange);
			emit(line > 0 ? (u32)line : 1);
		} else if (op == 0) {
			// Extended opcode, length-prefixed so unknown ones can be skipped.
			const uint64_t length = r.ULEB();
			const size_t next = r.pos() + (size_t)length;
			const uint8_t sub = r.U8();
			if (sub == DW_LNE_end_sequence) {
				// line 0 terminates the sequence - see the comment on LineEntry::line.
				emit(0);
				address = 0;
				file = 1;
				line = 1;
				sawAddress = false;
			} else if (sub == DW_LNE_set_address) {
				address = r.U32();
				sawAddress = true;
			}
			r.Seek(next);
		} else {
			switch (op) {
			case DW_LNS_copy:
				emit(line > 0 ? (u32)line : 1);
				break;
			case DW_LNS_advance_pc:
				address += (u32)r.ULEB() * minInstLength;
				break;
			case DW_LNS_advance_line:
				line += (int)r.SLEB();
				break;
			case DW_LNS_set_file:
				file = (u32)r.ULEB();
				break;
			case DW_LNS_set_column:
				r.ULEB();
				break;
			case DW_LNS_negate_stmt:
			case DW_LNS_set_basic_block:
				break;
			case DW_LNS_const_add_pc:
				address += ((255 - opcodeBase) / lineRange) * minInstLength;
				break;
			case DW_LNS_fixed_advance_pc:
				address += r.U16();
				break;
			default:
				// Known length, unknown meaning - skip its operands and carry on.
				for (uint8_t i = 0; op - 1 < (int)stdOpcodeLengths.size() && i < stdOpcodeLengths[op - 1]; i++)
					r.ULEB();
				break;
			}
		}
	}

	return r.ok();
}

int LineInfoMap::AddModule(std::string_view elfData, u32 moduleStart, u32 moduleSize, u32 addressDelta) {
	RemoveModule(moduleStart, moduleSize);

	const uint8_t *data = (const uint8_t *)elfData.data();
	const size_t size = elfData.size();
	if (size < sizeof(Elf32_Ehdr))
		return 0;
	if (data[0] != ELFMAG0 || data[1] != ELFMAG1 || data[2] != ELFMAG2 || data[3] != ELFMAG3)
		return 0;

	const Elf32_Ehdr *header = (const Elf32_Ehdr *)data;
	if (header->e_shoff == 0 || header->e_shnum == 0 || header->e_shentsize < sizeof(Elf32_Shdr))
		return 0;
	if ((size_t)header->e_shoff + (size_t)header->e_shnum * header->e_shentsize > size)
		return 0;
	if (header->e_shstrndx >= header->e_shnum)
		return 0;

	auto section = [&](int i) {
		return (const Elf32_Shdr *)(data + header->e_shoff + (size_t)i * header->e_shentsize);
	};

	const Elf32_Shdr *shstr = section(header->e_shstrndx);
	if ((size_t)shstr->sh_offset + shstr->sh_size > size)
		return 0;

	const Elf32_Shdr *debugLine = nullptr;
	for (int i = 0; i < header->e_shnum; i++) {
		const Elf32_Shdr *s = section(i);
		if (s->sh_name >= shstr->sh_size)
			continue;
		const char *name = (const char *)data + shstr->sh_offset + s->sh_name;
		if (!strcmp(name, ".debug_line")) {
			debugLine = s;
			break;
		}
	}
	if (!debugLine || debugLine->sh_size == 0)
		return 0;
	if ((size_t)debugLine->sh_offset + debugLine->sh_size > size)
		return 0;

	ModuleLines mod;
	mod.start = moduleStart;
	mod.size = moduleSize;
	mod.files.push_back("<unknown>");
	std::map<std::string, u32> fileIndex;

	Reader r(data + debugLine->sh_offset, debugLine->sh_size);
	int skippedUnits = 0;
	while (r.ok() && !r.AtEnd(debugLine->sh_size)) {
		const size_t unitStart = r.pos();
		const uint32_t unitLength = r.U32();
		if (!r.ok() || unitLength == 0)
			break;
		// 0xfffffff0 and up are reserved; 0xffffffff introduces 64-bit DWARF, which nothing here
		// produces, and misreading it would walk off into nonsense.
		if (unitLength >= 0xfffffff0)
			break;
		const size_t unitEnd = unitStart + 4 + unitLength;
		if (unitEnd > debugLine->sh_size)
			break;

		if (!ParseUnit(r, unitEnd, &mod.entries, &mod.files, &fileIndex, moduleStart, moduleSize, addressDelta))
			skippedUnits++;
		r.Seek(unitEnd);
	}

	if (skippedUnits > 0) {
		WARN_LOG(Log::Loader, "Line info: skipped %d compilation unit(s) - unsupported DWARF version?", skippedUnits);
	}
	if (mod.entries.empty())
		return 0;

	// Sorted for binary search. Rows at the same address are collapsed to the last one, which is
	// what a debugger wants: the innermost/most recent statement wins, and an end-of-sequence
	// marker sharing an address with the next sequence's first row loses to it.
	std::stable_sort(mod.entries.begin(), mod.entries.end(), [](const LineEntry &a, const LineEntry &b) {
		return a.address < b.address;
	});
	mod.entries.erase(std::unique(mod.entries.begin(), mod.entries.end(), [](const LineEntry &a, const LineEntry &b) {
		return a.address == b.address;
	}), mod.entries.end());

	const int count = (int)mod.entries.size();
	INFO_LOG(Log::Loader, "Line info: %d rows across %d files for module at %08x",
		count, (int)mod.files.size() - 1, moduleStart);
	modules_.push_back(std::move(mod));
	return count;
}

void LineInfoMap::RemoveModule(u32 moduleStart, u32 moduleSize) {
	modules_.erase(std::remove_if(modules_.begin(), modules_.end(), [&](const ModuleLines &m) {
		return m.start == moduleStart && m.size == moduleSize;
	}), modules_.end());
}

void LineInfoMap::Clear() {
	modules_.clear();
}

const LineInfoMap::ModuleLines *LineInfoMap::FindModule(u32 address) const {
	for (const ModuleLines &m : modules_) {
		if (address >= m.start && address < m.start + m.size)
			return &m;
	}
	return nullptr;
}

bool LineInfoMap::Lookup(u32 address, std::string *file, int *line) const {
	const ModuleLines *mod = FindModule(address);
	if (!mod)
		return false;

	// The row describing an address is the last one at or before it.
	auto it = std::upper_bound(mod->entries.begin(), mod->entries.end(), address,
		[](u32 addr, const LineEntry &e) { return addr < e.address; });
	if (it == mod->entries.begin())
		return false;
	--it;
	if (it->line == 0)
		return false;  // Past the end of that sequence - see LineEntry::line.

	if (file)
		*file = it->fileIndex < mod->files.size() ? mod->files[it->fileIndex] : mod->files[0];
	if (line)
		*line = (int)it->line;
	return true;
}

std::string LineInfoMap::LookupString(u32 address) const {
	std::string file;
	int line = 0;
	if (!Lookup(address, &file, &line))
		return std::string();
	return StringFromFormat("%s:%d", file.c_str(), line);
}
