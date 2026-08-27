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

// These functions tends to be slow in debug mode.
// Comment this out if debugging the symbol map itself.
#if defined(_MSC_VER) && defined(_DEBUG)
#pragma optimize("gty", on)
#endif

#include "ppsspp_config.h"
#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <WindowsX.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "zlib.h"

#include "ext/armips/Core/Assembler.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/Buffer.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Util/PathUtil.h"

SymbolMap *g_symbolMap;

// Not per-instance, so that versions from a map that's been thrown away (one is created and
// destroyed per game boot) can't collide with the current one's. See SymbolMap::Version.
static uint32_t g_symbolMapVersionCounter;

SymbolMap::SymbolMap() {
	Bump();
}

void SymbolMap::Bump() {
	version_ = ++g_symbolMapVersionCounter;
}

void SymbolMap::SortSymbols() {
	AssignFunctionIndices();
}

void SymbolMap::Clear() {
	Bump();
	functions.clear();
	labels.clear();
	data.clear();
	activeFunctions.clear();
	activeLabels.clear();
	activeData.clear();
	activeModuleEnds.clear();
	modules.clear();
	activeNeedUpdate_ = false;
}

bool SymbolMap::LoadSymbolMap(const Path &filename) {
	Clear();

	// TODO(scoped): Use gzdopen instead.

#if defined(_WIN32) && defined(UNICODE)
	gzFile f = gzopen_w(filename.ToWString().c_str(), "r");
#else
	gzFile f = gzopen(filename.c_str(), "r");
#endif

	if (f == Z_NULL)
		return false;

	//char temp[256];
	//fgets(temp,255,f); //.text section layout
	//fgets(temp,255,f); //  Starting        Virtual
	//fgets(temp,255,f); //  address  Size   address
	//fgets(temp,255,f); //  -----------------------

	bool started = false;
	bool hasModules = false;

	while (!gzeof(f)) {
		char line[512], temp[256] = {0};
		char *p = gzgets(f, line, 512);
		if (p == NULL)
			break;

		// Chop any newlines off.
		for (size_t i = strlen(line) - 1; i > 0; i--) {
			if (line[i] == '\r' || line[i] == '\n') {
				line[i] = '\0';
			}
		}

		if (strlen(line) < 4 || sscanf(line, "%255s", temp) != 1)
			continue;

		if (strcmp(temp,"UNUSED")==0) continue;
		if (strcmp(temp,".text")==0)  {started=true;continue;};
		if (strcmp(temp,".init")==0)  {started=true;continue;};
		if (strcmp(temp,"Starting")==0) continue;
		if (strcmp(temp,"extab")==0) continue;
		if (strcmp(temp,".ctors")==0) break;
		if (strcmp(temp,".dtors")==0) break;
		if (strcmp(temp,".rodata")==0) continue;
		if (strcmp(temp,".data")==0) continue;
		if (strcmp(temp,".sbss")==0) continue;
		if (strcmp(temp,".sdata")==0) continue;
		if (strcmp(temp,".sdata2")==0) continue;
		if (strcmp(temp,"address")==0)  continue;
		if (strcmp(temp,"-----------------------")==0)  continue;
		if (strcmp(temp,".sbss2")==0) break;
		if (temp[1]==']') continue;

		if (!started) continue;

		u32 address = -1, size = 0, vaddress = -1;
		int moduleIndex = 0;
		int typeInt = ST_NONE;
		SymbolType type;
		char name[128] = {0};

		if (sscanf(line, ".module %x %08x %08x %127c", (unsigned int *)&moduleIndex, &address, &size, name) >= 3) {
			// Found a module definition.
			ModuleEntry mod;
			mod.index = moduleIndex;
			strcpy(mod.name, name);
			mod.start = address;
			mod.size = size;
			modules.push_back(mod);
			hasModules = true;
			continue;
		}

		const int matched = sscanf(line, "%08x %08x %x %i %127c", &address, &size, &vaddress, &typeInt, name);
		if (matched < 1)
			continue;
		type = (SymbolType) typeInt;
		if (!hasModules) {
			if (!Memory::IsValidAddress(vaddress)) {
				ERROR_LOG(Log::Loader, "Invalid address in symbol file: %08x (%s)", vaddress, name);
				continue;
			}
		} else {
			// The 3rd field is now used for the module index.
			moduleIndex = vaddress;
			vaddress = GetModuleAbsoluteAddr(address, moduleIndex);
			if (!Memory::IsValidAddress(vaddress)) {
				ERROR_LOG(Log::Loader, "Invalid address in symbol file: %08x (%s)", vaddress, name);
				continue;
			}
		}

		if (type == ST_DATA && size == 0)
			size = 4;

		// Ignore syscalls, will be recognized from stubs.
		// Note: it's still useful to save these for grepping and importing into other tools.
		if (strncmp(name, "zz_sce", 6) == 0)
			continue;
		// Also ignore unresolved imports, which will similarly be replaced.
		if (strncmp(name, "zz_[UNK", 7) == 0)
			continue;

		if (!strcmp(name, ".text") || !strcmp(name, ".init") || strlen(name) <= 1) {
			// Ignored
		} else {
			// Seems legit
			switch (type) {
			case ST_FUNCTION:
				AddFunction(name, vaddress, size, moduleIndex);
				break;
			case ST_DATA:
				AddData(vaddress,size,DATATYPE_BYTE, moduleIndex);
				if (name[0] != 0)
					AddLabel(name, vaddress, moduleIndex);
				break;
			case ST_NONE:
			case ST_ALL:
				// Shouldn't be possible.
				break;
			}
		}
	}
	gzclose(f);
	activeNeedUpdate_ = true;
	SortSymbols();
	return started;
}

bool SymbolMap::SaveSymbolMap(const Path &filename) const {
	// Don't bother writing a blank file.
	if (!File::Exists(filename) && functions.empty() && data.empty()) {
		return true;
	}

	Buffer buf;
	buf.Printf(".text\n");
	for (const auto &module : modules) {
		buf.Printf(".module %x %08x %08x %s\n", module.index, module.start, module.size, module.name);
	}

	for (const auto &[key, e] : functions) {
		buf.Printf("%08x %08x %x %i %s\n", e.start, e.size, e.module, ST_FUNCTION, GetLabelNameRel(e.start, e.module));
	}

	for (const auto &[key, e] : data) {
		buf.Printf("%08x %08x %x %i %s\n", e.start, e.size, e.module, ST_DATA, GetLabelNameRel(e.start, e.module));
	}

	std::string data;
	buf.TakeAll(&data);
	FILE *file = File::OpenCFile(filename, "wb");
	if (file == nullptr) {
		return false;
	}
	if (g_Config.bCompressSymbols) {
		uInt out_size = 4096;
		Bytef *out_data = (Bytef *)std::malloc(out_size);
		if (out_data == nullptr) {
			fclose(file);
			return false;
		}
		z_stream strm;
		strm.zalloc = nullptr;
		strm.zfree = nullptr;
		strm.opaque = nullptr;
		if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
			std::free(out_data);
			fclose(file);
			return false;
		}
		strm.next_in = (Bytef *)data.data();
		strm.avail_in = (u32)data.size();
		strm.next_out = out_data;
		strm.avail_out = out_size;
		int flush = Z_NO_FLUSH;
		for (;;) {
			int status = deflate(&strm, flush);
			switch (status) {
				case Z_OK:
				case Z_STREAM_END:
					if (strm.avail_out != out_size) {
						fwrite(out_data, 1, out_size - strm.avail_out, file);
					}
					break;
				case Z_BUF_ERROR:
					{
						std::free(out_data);
						uInt new_out_size = 2 * out_size;
						if (new_out_size < out_size) {
							deflateEnd(&strm);
							fclose(file);
							return false;
						}
						out_size = new_out_size;
						out_data = (Bytef *)std::malloc(out_size);
						if (out_data == nullptr) {
							deflateEnd(&strm);
							fclose(file);
							return false;
						}
					}
					break;
				default:
					deflateEnd(&strm);
					std::free(out_data);
					fclose(file);
					return false;
			}
			if (status == Z_STREAM_END) {
				break;
			}
			if (strm.avail_in == 0) {
				flush = Z_FINISH;
			}
			strm.next_out = out_data;
			strm.avail_out = out_size;
		}
		deflateEnd(&strm);
		std::free(out_data);
	} else {
		// Just plain write it.
		fwrite(data.data(), 1, data.size(), file);
	}
	fclose(file);
	return true;
}

bool SymbolMap::LoadNocashSym(const Path &filename) {
	FILE *f = File::OpenCFile(filename, "r");
	if (!f)
		return false;

	while (!feof(f)) {
		char line[256], value[256] = {0};
		char *p = fgets(line, 256, f);
		if (p == NULL)
			break;

		u32 address;
		if (sscanf(line, "%08X %255s", &address, value) != 2)
			continue;
		if (address == 0 && strcmp(value, "0") == 0)
			continue;

		if (value[0] == '.') {
			// data directives
			char* s = strchr(value, ':');
			if (s != NULL) {
				*s = 0;

				u32 size = 0;
				if (sscanf(s + 1, "%04X", &size) != 1)
					continue;

				if (strcasecmp(value, ".byt") == 0) {
					AddData(address, size, DATATYPE_BYTE, 0);
				} else if (strcasecmp(value, ".wrd") == 0) {
					AddData(address, size, DATATYPE_HALFWORD, 0);
				} else if (strcasecmp(value, ".dbl") == 0) {
					AddData(address, size, DATATYPE_WORD, 0);
				} else if (strcasecmp(value, ".asc") == 0) {
					AddData(address, size, DATATYPE_ASCII, 0);
				}
			}
		} else {				// labels
			unsigned int size = 1;
			char *separator = strchr(value, ',');
			if (separator != NULL) {
				*separator = '\0';
				sscanf(separator + 1, "%08X", &size);
			}

			if (size != 1) {
				AddFunction(value, address, size, 0);
			} else {
				AddLabel(value, address, 0);
			}
		}
	}
	fclose(f);
	return true;
}

bool SymbolMap::SaveNocashSym(const Path &filename) const {
	// Don't bother writing a blank file.
	if (!File::Exists(filename) && functions.empty() && data.empty()) {
		return false;
	}

	FILE *f = File::OpenCFile(filename, "w");
	if (!f)
		return false;

	// only write functions, the rest isn't really interesting
	for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
		const FunctionEntry& e = it->second;
		fprintf(f, "%08X %s,%04X\n", GetModuleAbsoluteAddr(e.start,e.module), GetLabelNameRel(e.start, e.module), e.size);
	}

	fclose(f);
	return true;
}

static const char *DataTypeName(DataType type) {
	switch (type) {
	case DATATYPE_BYTE: return "byte";
	case DATATYPE_HALFWORD: return "halfword";
	case DATATYPE_WORD: return "word";
	case DATATYPE_ASCII: return "ascii";
	default: return "byte";
	}
}

static bool DataTypeFromName(const char *s, DataType *out) {
	if (!strcmp(s, "byte")) *out = DATATYPE_BYTE;
	else if (!strcmp(s, "halfword")) *out = DATATYPE_HALFWORD;
	else if (!strcmp(s, "word")) *out = DATATYPE_WORD;
	else if (!strcmp(s, "ascii")) *out = DATATYPE_ASCII;
	else return false;
	return true;
}

// Returns a pointer to the start of the (count+1)th whitespace-separated token in line, or to
// the trailing '\0' if there aren't that many - used instead of sscanf's %s/%[^\n] for the
// trailing name field below, since scanf's "match one or more characters" conversions fail
// (rather than matching an empty string) when a data/label entry legitimately has no name,
// which would otherwise silently drop the whole line instead of just leaving the name blank.
static const char *SkipTokens(const char *line, int count) {
	const char *p = line;
	for (int i = 0; i < count; i++) {
		while (*p == ' ' || *p == '\t') p++;
		while (*p && *p != ' ' && *p != '\t') p++;
	}
	while (*p == ' ' || *p == '\t') p++;
	return p;
}

// Module names and disc IDs come straight from game/homebrew data (an ELF's module-info string,
// PARAM.SFO), so they have to go through SanitizeString before ending up in a filename.
static std::string SymbolFileStem(const std::string &name, const char *fallback) {
	std::string stem = SanitizeString(name, StringRestriction::FileName);
	return stem.empty() ? fallback : stem;
}

Path SymbolMap::GetModuleSymbolsPath(const std::string &moduleName, u32 crc) {
	// Deliberately keyed by module name + crc, NOT by game - the exact same module (e.g. a
	// kernel/driver module, or a homebrew's own statically-linked library) commonly gets loaded
	// by many different games/homebrew, and symbols for it are equally valid for all of them.
	return GetSysDirectory(DIRECTORY_SYSTEM) / "SYMBOLS" / StringFromFormat("%s_%08x.ppsym", SymbolFileStem(moduleName, "module").c_str(), crc);
}

Path SymbolMap::GetGameSymbolsPath(const std::string &gameID) {
	// The opposite trade-off from GetModuleSymbolsPath: symbols that aren't inside any module are
	// addresses in this game's own RAM layout, so they're worth nothing to any other game.
	// The "_syms" suffix can't be mistaken for a module file, which always ends in _<8 hex digits>.
	return GetSysDirectory(DIRECTORY_SYSTEM) / "SYMBOLS" / StringFromFormat("%s_syms.ppsym", SymbolFileStem(gameID, "unknown").c_str());
}

// Names that loading the module produces again by itself, so there's nothing to preserve:
//  - "zz_*" is an import stub, named from the stub table on every load - either "zz_<funcName>"
//    or "zz_<moduleName>_<nid>" when the NID isn't known (see KernelImportModuleFuncs).
//    LoadSymbolMap skips these on the way in for the same reason.
//  - "z_un_<8 hex digits>" is what MIPSAnalyst::ScanForFunctions calls every function it finds,
//    i.e. a placeholder for "there's a function here, we don't know what it is".
// Writing them out would bury the handful of names a human actually chose (in one real module:
// four, among five hundred of these), and on the next run they'd be loaded back as authoritative
// and beat the module's own symbols to the address.
static bool IsRegeneratedSymbolName(const char *name) {
	if (!name)
		return true;
	if (startsWith(name, "zz_"))
		return true;
	if (!startsWith(name, "z_un_"))
		return false;
	const char *p = name + 5;
	for (int i = 0; i < 8; i++, p++) {
		if (!isxdigit((unsigned char)*p))
			return false;
	}
	return *p == '\0';
}

u32 SymbolMap::GetModuleCrc(int moduleIndex) const {
	for (const auto &module : modules) {
		if (module.index == moduleIndex)
			return module.crc;
	}
	return 0;
}

// File format is a simple, human-editable text format - deliberately not the denser gzipped
// .map format LoadSymbolMap/SaveSymbolMap use, since these files are meant to be hand-tweaked
// (e.g. after manually naming a function) and diffed/version-controlled if the user wants to.
//
//   .ppsym 1
//   crc <hex, 0 if unknown>
//   # game <gameID> <gameTitle>                -- informational only, see SaveModuleSymbols
//   F <relAddr hex> <size hex> <name>          -- function
//   D <relAddr hex> <size hex> <type> <name>   -- data (type: byte/halfword/word/ascii)
//   L <relAddr hex> <name>                     -- bare label (not a function or data start)
//
// moduleIndex 0 means "symbols not inside any module" - addresses the user attached to RAM
// directly (heap, stack, scratchpad, hardware registers). Those have no module to be relative to,
// so the addresses are simply absolute; everything else about the format is the same. They're
// per-game rather than per-module, hence GetGameSymbolsPath instead of GetModuleSymbolsPath.
bool SymbolMap::SaveModuleSymbols(int moduleIndex, const Path &filename, const std::string &gameID, const std::string &gameTitle) const {
	u32 crc = 0;
	if (moduleIndex != 0) {
		bool found = false;
		for (const auto &module : modules) {
			if (module.index == moduleIndex) {
				crc = module.crc;
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}

	// Built up first so we can tell whether anything survived the filtering below. Most modules
	// contribute nothing a human chose, and writing a header-only file for each of them would
	// bury the few that matter.
	Buffer body;
	int count = 0;
	for (const auto &[key, e] : functions) {
		if (key.first != moduleIndex)
			continue;
		// Only functions someone actually named are worth keeping - the rest are rediscovered
		// (with the same boundaries) by the scan on every load. See IsRegeneratedSymbolName.
		const char *name = GetLabelNameRel(e.start, moduleIndex);
		if (IsRegeneratedSymbolName(name))
			continue;
		body.Printf("F %08x %08x %s\n", e.start, e.size, name);
		count++;
	}
	for (const auto &[key, e] : data) {
		if (key.first != moduleIndex)
			continue;
		const char *name = GetLabelNameRel(e.start, moduleIndex);
		body.Printf("D %08x %08x %s %s\n", e.start, e.size, DataTypeName(e.type), name ? name : "");
		count++;
	}
	for (const auto &[key, e] : labels) {
		if (key.first != moduleIndex)
			continue;
		// Functions/data already saved their own (function/data-start) label above - only save
		// the remainder here, labels that aren't at a function or data start.
		if (functions.find(key) != functions.end() || data.find(key) != data.end())
			continue;
		if (IsRegeneratedSymbolName(e.name))
			continue;
		body.Printf("L %08x %s\n", e.addr, e.name);
		count++;
	}

	if (count == 0) {
		// Nothing worth keeping. Remove any previous file rather than leaving one behind that
		// would restore symbols the user has since deleted.
		if (File::Exists(filename))
			File::Delete(filename);
		return true;
	}

	File::CreateFullPath(filename.NavigateUp());
	FILE *f = File::OpenCFile(filename, "w");
	if (!f)
		return false;

	fprintf(f, ".ppsym 1\n");
	fprintf(f, "crc %08x\n", crc);
	// This file may be shared between multiple games that all load this module - this comment
	// just records who saved it most recently, purely for a human's benefit (e.g. to recognize
	// where a set of names came from); it's never read back by LoadModuleSymbols.
	fprintf(f, "# game %s %s\n", gameID.empty() ? "?" : gameID.c_str(),
		SanitizeString(gameTitle, StringRestriction::NoLineBreaksOrSpecials).c_str());

	std::string text;
	body.TakeAll(&text);
	fwrite(text.data(), 1, text.size(), f);

	fclose(f);
	return true;
}

bool SymbolMap::LoadModuleSymbols(int moduleIndex, const Path &filename) {
	if (!IsModuleActive(moduleIndex))
		return false;

	FILE *f = File::OpenCFile(filename, "r");
	if (!f)
		return false;

	u32 currentCrc = 0;
	// 0 means "don't range check" - either module 0 (absolute addresses, no range to speak of) or
	// a module we somehow have no entry for.
	u32 moduleSize = 0;
	for (const auto &module : modules) {
		if (module.index == moduleIndex) {
			currentCrc = module.crc;
			moduleSize = module.size;
			break;
		}
	}
	// A file can outlive the build of the module it was saved from (that's what the crc warning
	// below is for), and it's meant to be hand-editable, so don't trust the addresses in it to
	// land inside the module - a symbol placed outside would show up at a nonsense address.
	auto inRange = [moduleSize](u32 relAddr) {
		return moduleSize == 0 || relAddr < moduleSize;
	};
	int skipped = 0;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (line[0] == '\0' || line[0] == '#' || startsWith(line, ".ppsym"))
			continue;

		u32 addr, size;
		char field[192];
		if (startsWith(line, "crc ")) {
			u32 fileCrc = 0;
			if (sscanf(line + 4, "%x", &fileCrc) == 1 && currentCrc != 0 && fileCrc != 0 && fileCrc != currentCrc) {
				WARN_LOG(Log::Loader, "LoadModuleSymbols: crc mismatch for '%s' (file %08x, loaded %08x) - symbols may not match this build of the module", filename.c_str(), fileCrc, currentCrc);
			}
		} else if (line[0] == 'F' && sscanf(line, "F %x %x", &addr, &size) == 2) {
			if (!inRange(addr)) {
				skipped++;
				continue;
			}
			const u32 absAddr = GetModuleAbsoluteAddr(addr, moduleIndex);
			const char *name = SkipTokens(line, 3);
			if (!name[0]) {
				// Shouldn't happen from our own writer, but the file is hand-editable. Register
				// the function under the scan's usual placeholder rather than an empty name, and
				// don't let it displace a name the module's own symbols may supply.
				const std::string placeholder = StringFromFormat("z_un_%08x", absAddr);
				AddFunction(placeholder.c_str(), absAddr, size, moduleIndex, false);
			} else {
				AddFunction(name, absAddr, size, moduleIndex, true);
			}
		} else if (line[0] == 'D' && sscanf(line, "D %x %x %191s", &addr, &size, field) == 3) {
			if (!inRange(addr)) {
				skipped++;
				continue;
			}
			DataType type;
			if (!DataTypeFromName(field, &type))
				type = DATATYPE_BYTE;
			u32 absAddr = GetModuleAbsoluteAddr(addr, moduleIndex);
			AddData(absAddr, size, type, moduleIndex);
			const char *name = SkipTokens(line, 4);
			if (name[0])
				AddLabel(name, absAddr, moduleIndex, true);
		} else if (line[0] == 'L' && sscanf(line, "L %x", &addr) == 1) {
			if (!inRange(addr)) {
				skipped++;
				continue;
			}
			const char *name = SkipTokens(line, 2);
			if (name[0])
				AddLabel(name, GetModuleAbsoluteAddr(addr, moduleIndex), moduleIndex, true);
		}
	}

	fclose(f);
	if (skipped > 0) {
		WARN_LOG(Log::Loader, "LoadModuleSymbols: skipped %d symbol(s) in '%s' that fall outside the module (%08x bytes) - stale file?", skipped, filename.c_str(), moduleSize);
	}
	SortSymbols();
	return true;
}

SymbolType SymbolMap::GetSymbolType(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	if (activeFunctions.find(address) != activeFunctions.end())
		return ST_FUNCTION;
	if (activeData.find(address) != activeData.end())
		return ST_DATA;
	return ST_NONE;
}

bool SymbolMap::GetSymbolInfo(SymbolInfo *info, u32 address, SymbolType symbolMask) {
	u32 functionAddress = INVALID_ADDRESS;
	u32 dataAddress = INVALID_ADDRESS;

	if (symbolMask & ST_FUNCTION) {
		functionAddress = GetFunctionStart(address);

		// If both are found, we always return the function, so just do that early.
		if (functionAddress != INVALID_ADDRESS) {
			if (info != NULL) {
				info->type = ST_FUNCTION;
				info->address = functionAddress;
				info->size = GetFunctionSize(functionAddress);
				info->moduleAddress = GetFunctionModuleAddress(functionAddress);
			}

			return true;
		}
	}

	if (symbolMask & ST_DATA) {
		dataAddress = GetDataStart(address);
		
		if (dataAddress != INVALID_ADDRESS) {
			if (info != NULL) {
				info->type = ST_DATA;
				info->address = dataAddress;
				info->size = GetDataSize(dataAddress);
				info->moduleAddress = GetDataModuleAddress(dataAddress);
			}

			return true;
		}
	}

	return false;
}

u32 SymbolMap::GetNextSymbolAddress(u32 address, SymbolType symbolMask) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	const auto functionEntry = (symbolMask & ST_FUNCTION) ? activeFunctions.upper_bound(address) : activeFunctions.end();
	const auto dataEntry = (symbolMask & ST_DATA) ? activeData.upper_bound(address) : activeData.end();

	if (functionEntry == activeFunctions.end() && dataEntry == activeData.end())
		return INVALID_ADDRESS;

	u32 funcAddress = (functionEntry != activeFunctions.end()) ? functionEntry->first : 0xFFFFFFFF;
	u32 dataAddress = (dataEntry != activeData.end()) ? dataEntry->first : 0xFFFFFFFF;

	if (funcAddress <= dataAddress)
		return funcAddress;
	else
		return dataAddress;
}

std::string SymbolMap::GetDescription(u32 address) {
	u32 funcStart = GetFunctionStart(address);
	const char *labelName = nullptr;
	if (funcStart != INVALID_ADDRESS) {
		labelName = GetLabelName(funcStart);
	} else {
		u32 dataStart = GetDataStart(address);
		if (dataStart != INVALID_ADDRESS) {
			labelName = GetLabelName(dataStart);
		}
	}

	if (labelName) {
		return std::string(labelName);
	}

	char descriptionTemp[32];
	snprintf(descriptionTemp, sizeof(descriptionTemp), "(%08x)", address);
	return descriptionTemp;
}

std::vector<SymbolEntry> SymbolMap::GetAllActiveSymbols(SymbolType symbolMask) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::vector<SymbolEntry> result;

	if (symbolMask & ST_FUNCTION) {
		for (auto &[key, func] : activeFunctions) {
			SymbolEntry entry;
			entry.address = key;
			entry.size = GetFunctionSize(entry.address);
			const char* name = GetLabelName(entry.address);
			if (name)
				entry.name = name;
			result.push_back(entry);
		}
	}

	if (symbolMask & ST_DATA) {
		for (auto &[key, data] : activeData) {
			SymbolEntry entry;
			entry.address = key;
			entry.size = GetDataSize(entry.address);
			const char* name = GetLabelName(entry.address);
			if (name)
				entry.name = name;
			result.push_back(entry);
		}
	}

	return result;
}

void SymbolMap::AddModule(const char *name, u32 address, u32 size, u32 crc) {
	Bump();

	for (auto &module : modules) {
		if (equals(module.name, name)) {
			// A name match alone isn't proof it's really the same module reloading - some
			// module names are generic enough to collide between unrelated binaries. If both
			// sides know their crc and they disagree, treat this as a different module instead
			// of falling through to reactivate (and thus reusing/polluting) the old one's
			// symbol table; crc == 0 on either side means "unknown" and we fall back to
			// matching by name alone, same as before crc existed.
			if (module.crc != 0 && crc != 0 && module.crc != crc)
				continue;

			// Just reactivate that one.
			module.start = address;
			module.size = size;
			if (crc != 0)
				module.crc = crc;
			activeModuleEnds.emplace(module.start + module.size, module);
			activeNeedUpdate_ = true;
			return;
		}
	}

	ModuleEntry mod;
	truncate_cpy(mod.name, name);
	mod.start = address;
	mod.size = size;
	mod.crc = crc;
	mod.index = (int)modules.size() + 1;

	modules.push_back(mod);
	activeModuleEnds.emplace(mod.start + mod.size, mod);
	activeNeedUpdate_ = true;
}

void SymbolMap::UnloadModule(u32 address, u32 size) {
	Bump();
	activeModuleEnds.erase(address + size);
	activeNeedUpdate_ = true;
}

u32 SymbolMap::GetModuleRelativeAddr(u32 address, int moduleIndex) const {
	if (moduleIndex == -1) {
		moduleIndex = GetModuleIndex(address);
	}

	for (const auto &module : modules) {
		if (module.index == moduleIndex) {
			return address - module.start;
		}
	}
	return address;
}

u32 SymbolMap::GetModuleAbsoluteAddr(u32 relative, int moduleIndex) const {
	for (const auto &module : modules) {
		if (module.index == moduleIndex) {
			return module.start + relative;
		}
	}
	return relative;
}

int SymbolMap::GetModuleIndex(u32 address) const {
	// activeModuleEnds is keyed by each active module's END address, so upper_bound() finds
	// the first module whose end is > address. That alone doesn't prove address falls inside
	// it though - address could just as well be sitting in the gap before that module's start
	// (e.g. between two active modules, or before the very first one) - so start must be
	// checked too, or addresses in such a gap get silently misattributed to the wrong module.
	auto iter = activeModuleEnds.upper_bound(address);
	if (iter == activeModuleEnds.end())
		return -1;
	if (address < iter->second.start)
		return -1;
	return iter->second.index;
}

int SymbolMap::ResolveModuleIndex(u32 address, int moduleIndex) {
	if (moduleIndex == -1) {
		// -1 from a caller means "work it out from the address".
		moduleIndex = GetModuleIndex(address);
		if (moduleIndex < 0) {
			// Not inside any loaded module - the heap, the stack, scratchpad, a hardware
			// register. That's module 0, "absolute address", not an error. Leaving it at -1
			// would file the symbol under a module index that is never active, so it would
			// never reach the active maps: invisible to every lookup and lost on save.
			moduleIndex = 0;
		}
	}
	if (moduleIndex == 0)
		sawUnknownModule = true;
	return moduleIndex;
}

int SymbolMap::GetModuleIndexByName(const std::string &name) const {
	// Prefer a currently active module if the name is ambiguous (e.g. two distinct modules
	// that happen to share a name - see AddModule's crc handling).
	for (const auto &[key, module] : activeModuleEnds) {
		if (name == module.name)
			return module.index;
	}
	// Not active (or never was) - fall back to the most recently added entry with that name.
	int found = -1;
	for (const auto &module : modules) {
		if (name == module.name)
			found = module.index;
	}
	return found;
}

bool SymbolMap::IsModuleActive(int moduleIndex) {
	if (moduleIndex == 0) {
		return true;
	}

	for (const auto &module : activeModuleEnds) {
		if (module.second.index == moduleIndex) {
			return true;
		}
	}
	return false;
}

std::vector<LoadedModuleInfo> SymbolMap::getAllModules() const {
	std::vector<LoadedModuleInfo> result;
	for (const auto &module : modules) {
		LoadedModuleInfo m;
		m.name = module.name;
		m.address = module.start;
		m.size = module.size;

		u32 key = module.start + module.size;
		m.active = activeModuleEnds.find(key) != activeModuleEnds.end();

		result.push_back(m);
	}

	return result;
}

void SymbolMap::AddFunction(const char* name, u32 address, u32 size, int moduleIndex, bool updateName) {
	Bump();

	moduleIndex = ResolveModuleIndex(address, moduleIndex);

	// Is there an existing one?
	u32 relAddress = GetModuleRelativeAddr(address, moduleIndex);
	auto symbolKey = std::make_pair(moduleIndex, relAddress);
	auto existing = functions.find(symbolKey);
	if (sawUnknownModule && existing == functions.end()) {
		// Fall back: maybe it's got moduleIndex = 0.
		existing = functions.find(std::make_pair(0, address));
	}

	if (existing != functions.end()) {
		existing->second.size = size;
		if (existing->second.module != moduleIndex) {
			FunctionEntry func = existing->second;
			func.start = relAddress;
			func.module = moduleIndex;
			functions.erase(existing);
			// Re-point at the entry's new home: erase() invalidated the old iterator, and the
			// refresh below still reads through it.
			existing = functions.insert_or_assign(symbolKey, func).first;
		}

		// Refresh the active item if it exists.
		auto active = activeFunctions.find(address);
		if (active != activeFunctions.end() && active->second.module == moduleIndex) {
			activeFunctions.erase(active);
			activeFunctions.emplace(address, existing->second);
		}
	} else {
		FunctionEntry func;
		func.start = relAddress;
		func.size = size;
		func.index = (int)functions.size();
		func.module = moduleIndex;
		functions[symbolKey] = func;

		if (IsModuleActive(moduleIndex)) {
			activeFunctions.emplace(address, func);
		}
	}

	AddLabel(name, address, moduleIndex, updateName);
}

u32 SymbolMap::GetFunctionStart(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeFunctions.upper_bound(address);
	if (it == activeFunctions.end()) {
		// check last element
		auto rit = activeFunctions.rbegin();
		if (rit != activeFunctions.rend()) {
			u32 start = rit->first;
			u32 size = rit->second.size;
			if (start <= address && start+size > address)
				return start;
		}
		// otherwise there's no function that contains this address
		return INVALID_ADDRESS;
	}

	if (it != activeFunctions.begin()) {
		it--;
		u32 start = it->first;
		u32 size = it->second.size;
		if (start <= address && start+size > address)
			return start;
	}

	return INVALID_ADDRESS;
}

u32 SymbolMap::FindPossibleFunctionAtAfter(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeFunctions.lower_bound(address);
	if (it == activeFunctions.end()) {
		return (u32)-1;
	}
	return it->first;
}

u32 SymbolMap::GetFunctionSize(u32 startAddress) {
	if (activeNeedUpdate_) {

		// This is common, from the jit.  Direct lookup is faster than updating active symbols.
		auto mod = activeModuleEnds.lower_bound(startAddress);
		std::pair<int, u32> funcKey;
		if (mod == activeModuleEnds.end()) {
			// Could still be mod 0, backwards compatibility.
			if (!sawUnknownModule)
				return INVALID_ADDRESS;
			funcKey.first = 0;
			funcKey.second = startAddress;
		} else {
			if (mod->second.start > startAddress)
				return INVALID_ADDRESS;
			funcKey.first = mod->second.index;
			funcKey.second = startAddress - mod->second.start;
		}

		auto func = functions.find(funcKey);
		if (func == functions.end())
			return INVALID_ADDRESS;

		return func->second.size;
	}

	auto it = activeFunctions.find(startAddress);
	if (it == activeFunctions.end())
		return INVALID_ADDRESS;

	return it->second.size;
}

u32 SymbolMap::GetFunctionModuleAddress(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeFunctions.find(startAddress);
	if (it == activeFunctions.end())
		return INVALID_ADDRESS;

	return GetModuleAbsoluteAddr(0, it->second.module);
}

int SymbolMap::GetFunctionNum(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	u32 start = GetFunctionStart(address);
	if (start == INVALID_ADDRESS)
		return INVALID_ADDRESS;

	auto it = activeFunctions.find(start);
	if (it == activeFunctions.end())
		return INVALID_ADDRESS;

	return it->second.index;
}

void SymbolMap::AssignFunctionIndices() {
	int index = 0;
	for (auto mod = activeModuleEnds.begin(), modend = activeModuleEnds.end(); mod != modend; ++mod) {
		int moduleIndex = mod->second.index;
		auto begin = functions.lower_bound(std::make_pair(moduleIndex, 0));
		auto end = functions.upper_bound(std::make_pair(moduleIndex, 0xFFFFFFFF));
		for (auto it = begin; it != end; ++it) {
			it->second.index = index++;
		}
	}
}

// Copies functions, labels and data to the active set depending on which modules are "active".
void SymbolMap::UpdateActiveSymbols() {
	activeFunctions.clear();
	activeLabels.clear();
	activeData.clear();

	// On startup and shutdown, we can skip the rest.  Tiny optimization.
	// Note: deliberately not skipping when only activeModuleEnds is empty. Symbols with module
	// index 0 are absolute - they belong to no module by design (a label put on a heap or stack
	// address, say) - and the loops below handle that case fine with no modules loaded. Bailing
	// out here left activeData/activeLabels/activeFunctions cleared, so those symbols vanished
	// whenever the last module was unloaded, and didn't exist before the first one was loaded.
	if (functions.empty() && labels.empty() && data.empty()) {
		return;
	}

	std::unordered_map<int, u32> activeModuleIndexes;
	for (auto it = activeModuleEnds.begin(), end = activeModuleEnds.end(); it != end; ++it) {
		activeModuleIndexes[it->second.index] = it->second.start;
	}

	for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
		const auto mod = activeModuleIndexes.find(it->second.module);
		if (it->second.module == 0) {
			activeFunctions.emplace(it->second.start, it->second);
		} else if (mod != activeModuleIndexes.end()) {
			activeFunctions.emplace(mod->second + it->second.start, it->second);
		}
	}

	for (auto it = labels.begin(), end = labels.end(); it != end; ++it) {
		const auto mod = activeModuleIndexes.find(it->second.module);
		if (it->second.module == 0) {
			activeLabels.emplace(it->second.addr, it->second);
		} else if (mod != activeModuleIndexes.end()) {
			activeLabels.emplace(mod->second + it->second.addr, it->second);
		}
	}

	for (auto it = data.begin(), end = data.end(); it != end; ++it) {
		const auto mod = activeModuleIndexes.find(it->second.module);
		if (it->second.module == 0) {
			activeData.emplace(it->second.start, it->second);
		} else if (mod != activeModuleIndexes.end()) {
			activeData.emplace(mod->second + it->second.start, it->second);
		}
	}

	AssignFunctionIndices();
	activeNeedUpdate_ = false;
}

bool SymbolMap::SetFunctionSize(u32 startAddress, u32 newSize) {
	Bump();

	if (activeNeedUpdate_)
		UpdateActiveSymbols();


	auto funcInfo = activeFunctions.find(startAddress);
	if (funcInfo != activeFunctions.end()) {
		auto symbolKey = std::make_pair(funcInfo->second.module, funcInfo->second.start);
		auto func = functions.find(symbolKey);
		if (func != functions.end()) {
			func->second.size = newSize;
			activeFunctions.erase(funcInfo);
			activeFunctions.emplace(startAddress, func->second);
		}
	}

	// TODO: check for overlaps
	return true;
}

bool SymbolMap::RemoveFunction(u32 startAddress, bool removeName) {
	Bump();

	if (activeNeedUpdate_)
		UpdateActiveSymbols();


	auto it = activeFunctions.find(startAddress);
	if (it == activeFunctions.end())
		return false;

	auto symbolKey = std::make_pair(it->second.module, it->second.start);
	auto it2 = functions.find(symbolKey);
	if (it2 != functions.end()) {
		functions.erase(it2);
	}
	activeFunctions.erase(it);

	if (removeName) {
		auto labelIt = activeLabels.find(startAddress);
		if (labelIt != activeLabels.end()) {
			symbolKey = std::make_pair(labelIt->second.module, labelIt->second.addr);
			auto labelIt2 = labels.find(symbolKey);
			if (labelIt2 != labels.end()) {
				labels.erase(labelIt2);
			}
			activeLabels.erase(labelIt);
		}
	}

	return true;
}

void SymbolMap::AddLabel(const char* name, u32 address, int moduleIndex, bool updateName) {
	Bump();

	moduleIndex = ResolveModuleIndex(address, moduleIndex);

	// Is there an existing one?
	u32 relAddress = GetModuleRelativeAddr(address, moduleIndex);
	auto symbolKey = std::make_pair(moduleIndex, relAddress);
	auto existing = labels.find(symbolKey);
	if (sawUnknownModule && existing == labels.end()) {
		// Fall back: maybe it's got moduleIndex = 0.
		existing = labels.find(std::make_pair(0, address));
	}

	if (existing != labels.end()) {
		// By default we leave an existing label's name alone, rather than overwriting it (see
		// updateName's doc comment in the header).
		bool nameChanged = false;
		if (updateName && !equals(existing->second.name, name)) {
			truncate_cpy(existing->second.name, name);
			nameChanged = true;
		}

		// We'll still upgrade it to the correct module / relative address.
		if (existing->second.module != moduleIndex) {
			LabelEntry label = existing->second;
			label.addr = relAddress;
			label.module = moduleIndex;
			labels.erase(existing);
			labels[symbolKey] = label;

			// Refresh the active item if it exists.
			auto active = activeLabels.find(address);
			if (active != activeLabels.end() && active->second.module == moduleIndex) {
				activeLabels.erase(active);
				activeLabels.emplace(address, label);
			}
		} else if (nameChanged) {
			// Module/address didn't change, but the name did - activeLabels still needs a
			// refresh, since it holds a separate flattened (and const-valued) copy, not a
			// reference into labels.
			auto active = activeLabels.find(address);
			if (active != activeLabels.end()) {
				activeLabels.erase(active);
				activeLabels.emplace(address, existing->second);
			}
		}
	} else {
		LabelEntry label;
		label.addr = relAddress;
		label.module = moduleIndex;
		truncate_cpy(label.name, name);

		labels[symbolKey] = label;
		if (IsModuleActive(moduleIndex)) {
			activeLabels.emplace(address, label);
		}
	}
}

void SymbolMap::SetLabelName(const char* name, u32 address) {
	Bump();

	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto labelInfo = activeLabels.find(address);
	if (labelInfo == activeLabels.end()) {
		AddLabel(name, address);
	} else {
		auto symbolKey = std::make_pair(labelInfo->second.module, labelInfo->second.addr);
		auto label = labels.find(symbolKey);
		if (label != labels.end()) {
			truncate_cpy(label->second.name, name);
			label->second.name[127] = 0;

			// Refresh the active item if it exists.
			auto active = activeLabels.find(address);
			if (active != activeLabels.end() && active->second.module == label->second.module) {
				activeLabels.erase(active);
				activeLabels.emplace(address, label->second);
			}
		}
	}
}

const char *SymbolMap::GetLabelName(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeLabels.find(address);
	if (it == activeLabels.end())
		return NULL;

	return it->second.name;
}

const char *SymbolMap::GetLabelNameRel(u32 relAddress, int moduleIndex) const {
	auto it = labels.find(std::make_pair(moduleIndex, relAddress));
	if (it == labels.end())
		return NULL;

	return it->second.name;
}

std::string SymbolMap::GetLabelString(u32 address) {
	const char *label = GetLabelName(address);
	if (label == NULL)
		return "";
	return label;
}

bool SymbolMap::GetLabelValue(const char* name, u32& dest) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	for (const auto &[key, label] : activeLabels) {
		if (equalsNoCase(name, label.name)) {
			dest = key;
			return true;
		}
	}

	return false;
}

void SymbolMap::AddData(u32 address, u32 size, DataType type, int moduleIndex) {
	Bump();

	moduleIndex = ResolveModuleIndex(address, moduleIndex);

	// Is there an existing one?
	u32 relAddress = GetModuleRelativeAddr(address, moduleIndex);
	auto symbolKey = std::make_pair(moduleIndex, relAddress);
	auto existing = data.find(symbolKey);
	if (sawUnknownModule && existing == data.end()) {
		// Fall back: maybe it's got moduleIndex = 0.
		existing = data.find(std::make_pair(0, address));
	}

	if (existing != data.end()) {
		existing->second.size = size;
		existing->second.type = type;
		if (existing->second.module != moduleIndex) {
			DataEntry entry = existing->second;
			entry.module = moduleIndex;
			entry.start = relAddress;
			data.erase(existing);
			// Re-point at the entry's new home: erase() invalidated the old iterator, and the
			// refresh below still reads through it.
			existing = data.insert_or_assign(symbolKey, entry).first;
		}

		// Refresh the active item if it exists.
		auto active = activeData.find(address);
		if (active != activeData.end() && active->second.module == moduleIndex) {
			activeData.erase(active);
			activeData.emplace(address, existing->second);
		}
	} else {
		DataEntry entry;
		entry.start = relAddress;
		entry.size = size;
		entry.type = type;
		entry.module = moduleIndex;

		data[symbolKey] = entry;
		if (IsModuleActive(moduleIndex)) {
			activeData.emplace(address, entry);
		}
	}
}

u32 SymbolMap::GetDataStart(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeData.upper_bound(address);
	if (it == activeData.end()) {
		// check last element
		auto rit = activeData.rbegin();
		if (rit != activeData.rend()) {
			u32 start = rit->first;
			u32 size = rit->second.size;
			if (start <= address && start + size > address) {
				return start;
			}
		}
		// otherwise there's no data that contains this address
		return INVALID_ADDRESS;
	}

	if (it != activeData.begin()) {
		it--;
		u32 start = it->first;
		u32 size = it->second.size;
		if (start <= address && start + size > address) {
			return start;
		}
	}

	return INVALID_ADDRESS;
}

u32 SymbolMap::GetDataSize(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeData.find(startAddress);
	if (it == activeData.end())
		return INVALID_ADDRESS;
	return it->second.size;
}

u32 SymbolMap::GetDataModuleAddress(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeData.find(startAddress);
	if (it == activeData.end())
		return INVALID_ADDRESS;
	return GetModuleAbsoluteAddr(0, it->second.module);
}

DataType SymbolMap::GetDataType(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeData.find(startAddress);
	if (it == activeData.end())
		return DATATYPE_NONE;
	return it->second.type;
}

bool SymbolMap::RemoveData(u32 startAddress, bool removeName) {
	Bump();

	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	auto it = activeData.find(startAddress);
	if (it == activeData.end())
		return false;

	auto symbolKey = std::make_pair(it->second.module, it->second.start);
	auto it2 = data.find(symbolKey);
	if (it2 != data.end()) {
		data.erase(it2);
	}
	activeData.erase(it);

	if (removeName) {
		auto labelIt = activeLabels.find(startAddress);
		if (labelIt != activeLabels.end()) {
			symbolKey = std::make_pair(labelIt->second.module, labelIt->second.addr);
			auto labelIt2 = labels.find(symbolKey);
			if (labelIt2 != labels.end()) {
				labels.erase(labelIt2);
			}
			activeLabels.erase(labelIt);
		}
	}

	return true;
}

// Transforms the labels to lowercase when returning. Why?
void SymbolMap::GetLabels(std::vector<LabelDefinition> &dest) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	for (const auto &[key, label] : activeLabels) {
		LabelDefinition entry;
		entry.value = key;
		std::string name = label.name;
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		entry.name = Identifier(name);
		dest.push_back(entry);
	}
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

struct DefaultSymbol {
	u32 address;
	const char* name;
};

static const DefaultSymbol defaultSymbols[]= {
	{ 0x08800000,	"User memory" },
	{ 0x08804000,	"Default load address" },
	{ 0x04000000,	"VRAM" },
	{ 0x88000000,	"Kernel memory" },
	{ 0x00010000,	"Scratchpad" },
};

void SymbolMap::FillSymbolListBox(HWND listbox,SymbolType symType) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	wchar_t temp[256];

	SendMessage(listbox, WM_SETREDRAW, FALSE, 0);
	ListBox_ResetContent(listbox);

	switch (symType) {
	case ST_FUNCTION:
		{
			SendMessage(listbox, LB_INITSTORAGE, (WPARAM)activeFunctions.size(), (LPARAM)activeFunctions.size() * 30);

			for (const auto &[key, function] : activeFunctions) {
				const char* name = GetLabelName(key);
				if (name != NULL)
					wsprintf(temp, L"%S", name);
				else
					wsprintf(temp, L"0x%08X", key);
				int index = ListBox_AddString(listbox, temp);
				ListBox_SetItemData(listbox, index, key);
			}
		}
		break;

	case ST_DATA:
		{
			size_t count = ARRAYSIZE(defaultSymbols)+activeData.size();
			SendMessage(listbox, LB_INITSTORAGE, (WPARAM)count, (LPARAM)count * 30);

			for (int i = 0; i < ARRAYSIZE(defaultSymbols); i++) {
				wsprintf(temp, L"0x%08X (%S)", defaultSymbols[i].address, defaultSymbols[i].name);
				int index = ListBox_AddString(listbox,temp);
				ListBox_SetItemData(listbox,index,defaultSymbols[i].address);
			}

			for (auto it = activeData.begin(), end = activeData.end(); it != end; ++it) {
				const char* name = GetLabelName(it->first);

				if (name != NULL)
					wsprintf(temp, L"%S", name);
				else
					wsprintf(temp, L"0x%08X", it->first);

				int index = ListBox_AddString(listbox,temp);
				ListBox_SetItemData(listbox,index,it->first);
			}
		}
		break;
	case ST_NONE:
	case ST_ALL:
		break;
	}

	SendMessage(listbox, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(listbox, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}
#endif
