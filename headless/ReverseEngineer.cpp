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

// Static reverse-engineering dump for a single PRX, with no game running.
//
// Brings up just enough of the emulator to run the real module loader (decrypt, decompress,
// relocate, resolve imports/exports, scan for functions), then writes a report: an index of
// the module's exports/imports/functions, one annotated disassembly file per function, and a
// call graph.
//
// Deliberately reuses the emulator's own loader rather than parsing PRXes a second time - a
// separate parser would drift from the one that actually runs.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/ELF/PrxDecrypter.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/Loaders.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Util/KL4E.h"

#include "headless/ReverseEngineer.h"

namespace {

// MIPS register names, in encoding order.
const char *const kRegNames[32] = {
	"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
	"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
	"t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};

enum {
	REG_V0 = 2,
	REG_A0 = 4,
	REG_A3 = 7,
	REG_SP = 29,
	REG_RA = 31,
};

// What one function does with the registers it was handed. We report evidence rather than a
// signature: MIPS callers routinely leave an argument in place for a callee to pick up, so a
// register this function never touches can still be a parameter it is passing on. Deciding the
// real arity means looking at the whole call chain, which is a judgement call for a human, not
// something to guess here.
struct RegEvidence {
	bool readBeforeWritten[32] = {};
	bool written[32] = {};
	// Call sites where an argument register still held whatever the caller left in it.
	std::map<int, std::vector<u32>> forwardedAt;
	// Call sites where this function set the argument register itself.
	std::map<int, std::vector<u32>> setLocallyAt;
	bool writesV0 = false;
	bool hasCalls = false;
	bool hasIndirectCalls = false;
};

struct FuncInfo {
	u32 start = 0;
	u32 size = 0;
	std::string name;
	std::set<u32> callees;
	std::set<u32> callers;
	bool indirectCallees = false;
	RegEvidence regs;
};

bool IsJal(u32 op) {
	return (op >> 26) == 3;
}

bool IsJalr(u32 op) {
	return (op >> 26) == 0 && (op & 0x3f) == 9;
}

u32 JumpTarget(u32 addr, u32 op) {
	return (addr & 0xF0000000) | ((op & 0x03FFFFFF) << 2);
}

// Applies one instruction's register reads and writes to the evidence being accumulated.
void ApplyRegEffects(u32 op, RegEvidence *ev) {
	const MIPSInfo info = MIPSGetInfo(MIPSOpcode(op));
	const int rs = (op >> 21) & 0x1f;
	const int rt = (op >> 16) & 0x1f;
	const int rd = (op >> 11) & 0x1f;

	if (info & IN_RS) {
		if (!ev->written[rs]) {
			ev->readBeforeWritten[rs] = true;
		}
	}
	if (info & IN_RT) {
		if (!ev->written[rt]) {
			ev->readBeforeWritten[rt] = true;
		}
	}
	if (info & OUT_RT) {
		ev->written[rt] = true;
	}
	if (info & OUT_RD) {
		ev->written[rd] = true;
	}
	if (info & OUT_RA) {
		ev->written[REG_RA] = true;
	}
	if (((info & OUT_RT) && rt == REG_V0) || ((info & OUT_RD) && rd == REG_V0)) {
		ev->writesV0 = true;
	}
}

// Walks a function once, collecting the call graph and the register evidence.
//
// Delay slots matter here: the instruction after a jal executes *before* the call, so a
// "move a0, s0" sitting in the delay slot is setting up that call's argument, not the next
// one's. Getting this backwards would report an argument as forwarded when it was set locally.
void AnalyzeFunction(FuncInfo *func) {
	RegEvidence &ev = func->regs;
	const u32 end = func->start + func->size;

	for (u32 addr = func->start; addr < end; addr += 4) {
		if (!Memory::IsValidAddress(addr)) {
			break;
		}
		const u32 op = Memory::Read_Instruction(addr).encoding;
		const bool jal = IsJal(op);
		const bool jalr = IsJalr(op);

		if (!jal && !jalr) {
			ApplyRegEffects(op, &ev);
			continue;
		}

		// A call. The delay slot runs first.
		const u32 delayAddr = addr + 4;
		if (delayAddr < end && Memory::IsValidAddress(delayAddr)) {
			ApplyRegEffects(Memory::Read_Instruction(delayAddr).encoding, &ev);
		}

		ev.hasCalls = true;
		for (int reg = REG_A0; reg <= REG_A3; reg++) {
			if (ev.written[reg]) {
				ev.setLocallyAt[reg].push_back(addr);
			} else {
				ev.forwardedAt[reg].push_back(addr);
			}
		}

		if (jal) {
			func->callees.insert(JumpTarget(addr, op));
		} else {
			ev.hasIndirectCalls = true;
			func->indirectCallees = true;
		}
		ApplyRegEffects(op, &ev);
		// v0 is clobbered by the call, and a0-a3 are caller-saved - but for our purposes the
		// interesting question is only what the caller left in place, so nothing to reset.
		addr += 4;  // Skip the delay slot; already accounted for.
	}
}

// Names an address formed by a lui/addiu pair, if anything is known about it.
std::string DescribeAddr(u32 addr) {
	const std::string label = g_symbolMap->GetLabelString(addr);
	if (!label.empty()) {
		return " <" + label + ">";
	}
	return "";
}

std::string SanitizeForFilename(std::string_view name) {
	std::string out;
	out.reserve(name.size());
	for (char c : name) {
		if (isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-') {
			out.push_back(c);
		} else {
			out.push_back('_');
		}
	}
	return out;
}

// Brings up the minimum needed to load a module: memory map, timing, HLE tables (so imports
// resolve to named syscalls), the kernel memory allocators and the object pool. Notably this
// does *not* start threads, the GPU or any of the HLE subsystems - nothing is going to run.
bool InitMinimalPSP() {
	Memory::g_MemorySize = Memory::RAM_DOUBLE_SIZE;
	Memory::g_PSPModel = PSP_MODEL_SLIM;

	if (g_symbolMap) {
		delete g_symbolMap;
	}
	g_symbolMap = new SymbolMap();
	MIPSAnalyst::Reset();

	if (!Memory::Init(Memory::MemMapSetupFlags::Default)) {
		fprintf(stderr, "re: memory init failed\n");
		return false;
	}

	mipsr4k.Reset();
	CoreTiming::Init(&mipsr4k);
	HLEInit();
	kernelObjects.Clear();
	__KernelMemoryInit();
	return true;
}

void ShutdownMinimalPSP() {
	__KernelMemoryShutdown();
	kernelObjects.Clear();
	HLEShutdown();
	CoreTiming::Shutdown();
	Memory::Shutdown();
	delete g_symbolMap;
	g_symbolMap = nullptr;
}

// Accepts either a host path or a PSP-style "flash0:/kd/foo.prx", which is resolved against the
// configured NAND directory so firmware modules can be named the way they are on the PSP.
Path ResolveModulePath(const std::string &input) {
	if (startsWithNoCase(input, "flash0:/") || startsWithNoCase(input, "flash0:")) {
		std::string rest = input.substr(input.find(':') + 1);
		while (!rest.empty() && (rest[0] == '/' || rest[0] == '\\')) {
			rest = rest.substr(1);
		}
		return g_Config.nandRootDirectory / "flash0" / rest;
	}
	return Path(input);
}

void WriteRegEvidence(FILE *f, const FuncInfo &func) {
	const RegEvidence &ev = func.regs;
	fprintf(f, "; Register evidence (NOT a signature - see below):\n");
	for (int reg = REG_A0; reg <= REG_A3; reg++) {
		const char *name = kRegNames[reg];
		std::string verdict;
		if (ev.readBeforeWritten[reg]) {
			verdict = "READ before written  -> used as a parameter here";
		} else if (!ev.forwardedAt.count(reg) && !ev.written[reg]) {
			verdict = "never touched";
		} else if (!ev.readBeforeWritten[reg] && ev.forwardedAt.count(reg)) {
			verdict = "never read, but live across a call -> FORWARDED from our caller";
		} else if (ev.written[reg]) {
			verdict = "written before any read -> set up locally";
		} else {
			verdict = "unclear";
		}
		fprintf(f, ";   %-4s %s\n", name, verdict.c_str());

		auto fwd = ev.forwardedAt.find(reg);
		if (fwd != ev.forwardedAt.end() && !fwd->second.empty()) {
			fprintf(f, ";        forwarded at:");
			for (size_t i = 0; i < fwd->second.size() && i < 8; i++) {
				fprintf(f, " %08x", fwd->second[i]);
			}
			if (fwd->second.size() > 8) {
				fprintf(f, " (+%d more)", (int)(fwd->second.size() - 8));
			}
			fprintf(f, "\n");
		}
	}
	fprintf(f, ";   %-4s %s\n", "v0", ev.writesV0 ? "written -> returns a value" : "never written -> returns nothing");
	if (ev.hasIndirectCalls) {
		fprintf(f, ";   note: has indirect calls (jalr) - callee list is incomplete\n");
	}
	fprintf(f, ";\n");
	fprintf(f, "; A register this function never reads can still be a parameter: MIPS code often\n");
	fprintf(f, "; leaves an argument untouched for a callee to pick up. Treat 'FORWARDED' as\n");
	fprintf(f, "; evidence the real arity is larger than what is read here, and settle it by\n");
	fprintf(f, "; looking at what the callees do with it.\n");
}

}  // namespace

int RunDecryptFile(const std::string &inPath, const std::string &outPath) {
	const Path in = ResolveModulePath(inPath);
	std::string data;
	if (!File::ReadBinaryFileToString(in, &data)) {
		fprintf(stderr, "re-decrypt: couldn't read %s\n", in.c_str());
		return 1;
	}
	std::vector<u8> out;
	int outSize;

	bool inputIsKL3E = false;
	if (IsKL4EMagic((const u8 *)data.data(), data.size(), &inputIsKL3E)) {
		// Already-decrypted input, e.g. the second stream this tool splits out of an ME image.
		// Nothing to decrypt; fall straight through to the decompressor below.
		printf("re-decrypt: %s, %d bytes, no header - already a plain compressed stream\n",
			in.c_str(), (int)data.size());
		out.assign(data.begin(), data.end());
		outSize = (int)data.size();
	} else {
		if (data.size() < 0x150) {
			fprintf(stderr, "re-decrypt: %s is too small to hold a header (%d bytes)\n", in.c_str(), (int)data.size());
			return 1;
		}

		const u32 tag = *(const u32_le *)(data.data() + 0xD0);
		printf("re-decrypt: %s, %d bytes, tag %08X\n", in.c_str(), (int)data.size(), tag);

		// Decrypts in place on the PSP too, but keep the input around so a failure leaves it readable.
		out.resize(data.size());
		outSize = pspDecryptPRX((const u8 *)data.data(), out.data(), (u32)data.size());
		if (outSize <= 0) {
			fprintf(stderr, "re-decrypt: no key for tag %08X, or the data didn't decrypt (%d)\n", tag, outSize);
			return 1;
		}
	}

	// The plaintext is usually still compressed - the ME images are KL4E. Unpack it here rather
	// than leaving that to the caller, since the point is to get at the code.
	bool isKL3E = false;
	if (IsKL4EMagic(out.data(), outSize, &isKL3E)) {
		// The header's elf_size is the decompressed size, but it's zero in the ME images, so
		// just give the decompressor plenty of room and go by what it returns.
		const int maxOut = std::max(16 * 1024 * 1024, outSize * 16);
		std::vector<u8> unpacked(maxOut);
		const u8 *streamEnd = nullptr;
		const int unpackedSize = DecompressKL4E(unpacked.data(), maxOut, out.data() + 4, (size_t)outSize - 4, &streamEnd, isKL3E);
		if (streamEnd) {
			// An ME image is two streams back to back: the code, then a second blob the image
			// expects to find after itself. Write the remainder out so it can be looked at.
			const int consumed = (int)(streamEnd - out.data());
			const int leftover = outSize - consumed;
			printf("re-decrypt: %s stream consumed %d of %d bytes (%d left over)\n",
				isKL3E ? "KL3E" : "KL4E", consumed, outSize, leftover);
			if (leftover > 0) {
				const Path tailFile(outPath + ".tail");
				if (File::WriteDataToFile(false, out.data() + consumed, leftover, tailFile)) {
					printf("re-decrypt: wrote the %d leftover bytes to %s\n", leftover, tailFile.c_str());
				}
			}
		}
		if (unpackedSize < 0) {
			fprintf(stderr, "re-decrypt: %s decompression failed (%d)\n", isKL3E ? "KL3E" : "KL4E", unpackedSize);
			return 1;
		}
		printf("re-decrypt: %s: %d -> %d bytes\n", isKL3E ? "KL3E" : "KL4E", outSize, unpackedSize);
		unpacked.resize(unpackedSize);
		out = std::move(unpacked);
		outSize = unpackedSize;
	}

	const Path outFile(outPath);
	if (!File::WriteDataToFile(false, out.data(), outSize, outFile)) {
		fprintf(stderr, "re-decrypt: couldn't write %s\n", outFile.c_str());
		return 1;
	}
	printf("re-decrypt: wrote %d bytes to %s\n", outSize, outFile.c_str());
	return 0;
}

int RunReverseEngineer(const ReverseEngineerOptions &opts) {
	// A module inside a disc image rather than on the host - see ReverseEngineerOptions.
	const bool fromDisc = startsWithNoCase(opts.modulePath, "disc0:") || startsWithNoCase(opts.modulePath, "umd0:");
	if (fromDisc && opts.discPath.empty()) {
		fprintf(stderr, "re: a disc0: module path needs the disc image as the positional argument\n");
		return 1;
	}
	const Path modulePath = fromDisc ? Path(opts.discPath) : ResolveModulePath(opts.modulePath);
	if (!File::Exists(modulePath)) {
		fprintf(stderr, "re: no such file: %s\n", modulePath.c_str());
		return 1;
	}

	const Path outDir(opts.outDir);
	if (!File::CreateFullPath(outDir)) {
		fprintf(stderr, "re: couldn't create output directory: %s\n", outDir.c_str());
		return 1;
	}

	// Load every module for real, whatever HLE implementations we may have for it - the whole
	// point is to look at Sony's code, not to have it quietly replaced by ours.
	g_Config.iDisableHLE = -1;
	g_Config.iForceEnableHLE = 0;
	SetForceRealModuleLoads(true);
	g_Config.bAutoSaveLoadSymbols = false;

	if (!InitMinimalPSP()) {
		return 1;
	}

	// For a disc, the path inside it; otherwise the containing directory and the leaf name.
	std::string insideDisc;
	if (fromDisc) {
		insideDisc = opts.modulePath.substr(opts.modulePath.find(':') + 1);
		if (insideDisc.empty() || insideDisc[0] != '/') {
			insideDisc = "/" + insideDisc;
		}
	}
	const std::string dir = modulePath.GetDirectory();
	const std::string filename = fromDisc ? Path(insideDisc).GetFilename() : modulePath.GetFilename();

	PSPModule *module = nullptr;
	std::string moduleName;
	u32 base = 0;
	u32 blockSize = 0;

	if (opts.rawBase) {
		// A flat image: no header, no relocation, nothing to resolve. Just put it where it was
		// linked to run and let the function scanner loose on it.
		std::string data;
		if (!File::ReadBinaryFileToString(modulePath, &data)) {
			fprintf(stderr, "re: couldn't read %s\n", modulePath.c_str());
			ShutdownMinimalPSP();
			return 1;
		}
		if (!Memory::IsValid4AlignedRange(opts.rawBase, (u32)data.size())) {
			fprintf(stderr, "re: %08x + %d bytes isn't a valid aligned RAM range\n",
				opts.rawBase, (int)data.size());
			ShutdownMinimalPSP();
			return 1;
		}
		Memory::MemcpyUnchecked(opts.rawBase, data.data(), (u32)data.size());
		moduleName = filename;
		base = opts.rawBase;
		blockSize = (u32)data.size();
		printf("re: raw image %s at %08x, %d bytes\n", filename.c_str(), base, blockSize);
		MIPSAnalyst::ScanForFunctions(base, base + blockSize - 4, true);
	} else {
		// Mount whatever holds the module so the normal file-backed loader path can be used: the
		// disc itself, or the containing directory.
		std::string loadPath;
		if (fromDisc) {
			std::unique_ptr<FileLoader> loader(ConstructFileLoader(modulePath));
			std::string blockError;
			std::shared_ptr<BlockDevice> device(loader ? ConstructBlockDevice(loader.get(), &blockError) : nullptr);
			if (!device) {
				fprintf(stderr, "re: %s isn't a disc image we can read: %s\n", modulePath.c_str(), blockError.c_str());
				ShutdownMinimalPSP();
				return 1;
			}
			// The ISO filesystem takes ownership of the block device, which owns the loader.
			loader.release();
			auto isoFs = std::make_shared<ISOFileSystem>(&pspFileSystem, device);
			pspFileSystem.Mount("host0:", isoFs);
			loadPath = "host0:" + insideDisc;
		} else {
			auto hostFs = std::make_shared<DirectoryFileSystem>(&pspFileSystem, Path(dir), FileSystemFlags::FLASH);
			pspFileSystem.Mount("host0:", hostFs);
			loadPath = "host0:/" + filename;
		}

		std::string error;
		const SceUID uid = KernelLoadModule(loadPath, &error);
		if (uid < 0) {
			fprintf(stderr, "re: failed to load %s: %s\n", modulePath.c_str(), error.c_str());
			ShutdownMinimalPSP();
			return 1;
		}

		u32 kerr = 0;
		module = kernelObjects.Get<PSPModule>(uid, kerr);
		if (!module) {
			fprintf(stderr, "re: loaded module vanished (uid %d)\n", uid);
			ShutdownMinimalPSP();
			return 1;
		}
		if (module->isFake) {
			fprintf(stderr, "re: module was fake-loaded (HLE stub) rather than really loaded - can't analyze\n");
			ShutdownMinimalPSP();
			return 1;
		}

		moduleName = module->nm.name;
		base = module->memoryBlockAddr;
		blockSize = module->memoryBlockSize;
	}

	// The loader's function scan names everything z_un_<addr>. We know better for two whole
	// categories: exported functions have a NID the HLE tables can often name, and every import
	// stub is a known library function. Naming them here means every call site in the
	// disassembly below reads as a name instead of a bare address.
	const int moduleIdx = g_symbolMap->GetModuleIndexByName(moduleName);
	int namedExports = 0, namedImports = 0;
	if (module) {
		for (const FuncSymbolExport &exp : module->exportedFuncs) {
			const char *known = GetHLEFuncName(exp.moduleName, exp.nid);
			const std::string name = known ? known : StringFromFormat("%s_%08x", exp.moduleName, exp.nid);
			u32 size = g_symbolMap->GetFunctionSize(exp.symAddr);
			if (size == SymbolMap::INVALID_ADDRESS) {
				size = 4;
			}
			g_symbolMap->AddFunction(name.c_str(), exp.symAddr, size, moduleIdx, true);
			namedExports++;
		}
		for (const FuncSymbolImport &imp : module->importedFuncs) {
			const char *known = GetHLEFuncName(imp.moduleName, imp.nid);
			const std::string name = known ? known : StringFromFormat("%s_%08x", imp.moduleName, imp.nid);
			g_symbolMap->AddFunction(name.c_str(), imp.stubAddr, 8, moduleIdx, true);
			namedImports++;
		}
	}

	// Optional pre-existing names, so the disassembly comes out readable instead of a wall of
	// z_un_08801234. Uses the same .ppsym format the emulator saves, module-relative. Applied
	// after the automatic naming above so a hand-written name always wins.
	if (!opts.symsFile.empty()) {
		if (moduleIdx < 0) {
			fprintf(stderr, "re: warning: module '%s' not in the symbol map, can't apply %s\n",
				moduleName.c_str(), opts.symsFile.c_str());
		} else if (!g_symbolMap->LoadModuleSymbols(moduleIdx, Path(opts.symsFile))) {
			fprintf(stderr, "re: warning: couldn't load symbols from %s\n", opts.symsFile.c_str());
		}
	}

	// A few modules - sysmem.prx and loadcore.prx among them - carry modinfo pointers that are
	// file offsets rather than addresses, so the loader's own scan is left with nothing to look
	// at and finds no functions. For reverse engineering we would still like the disassembly, and
	// we know exactly which range is code, so scan it ourselves.
	if (module) {
		bool anyInModule = false;
		for (const SymbolEntry &sym : g_symbolMap->GetAllActiveSymbols(ST_FUNCTION)) {
			if (sym.address >= base && sym.address < base + blockSize) {
				anyInModule = true;
				break;
			}
		}
		if (!anyInModule && blockSize >= 8) {
			const u32 textStart = module->nm.text_addr ? (u32)module->nm.text_addr : base;
			u32 textEnd = textStart + (u32)module->nm.text_size;
			if (textEnd <= textStart || textEnd > base + blockSize) {
				textEnd = base + blockSize;
			}
			if (Memory::IsValid4AlignedRange(textStart, textEnd - textStart)) {
				printf("re: loader found no functions, scanning %08x-%08x directly\n", textStart, textEnd);
				MIPSAnalyst::ScanForFunctions(textStart, textEnd - 4, true);
			}
		}
	}

	// Collect the functions the loader's scan found, restricted to this module.
	std::vector<FuncInfo> funcs;
	std::map<u32, size_t> funcByAddr;
	for (const SymbolEntry &sym : g_symbolMap->GetAllActiveSymbols(ST_FUNCTION)) {
		if (sym.address < base || sym.address >= base + blockSize) {
			continue;
		}
		// A module with an odd segment layout can leave a symbol at an address that isn't
		// really code; reading an instruction there trips a debug assert deep in MemMap.
		if (!Memory::IsValid4AlignedAddress(sym.address) || sym.size == 0) {
			continue;
		}
		FuncInfo f;
		f.start = sym.address;
		f.size = sym.size;
		f.name = sym.name;
		funcByAddr[f.start] = funcs.size();
		funcs.push_back(f);
	}
	std::sort(funcs.begin(), funcs.end(), [](const FuncInfo &a, const FuncInfo &b) {
		return a.start < b.start;
	});
	funcByAddr.clear();
	for (size_t i = 0; i < funcs.size(); i++) {
		funcByAddr[funcs[i].start] = i;
	}

	for (FuncInfo &f : funcs) {
		AnalyzeFunction(&f);
	}
	// Second pass: invert the call graph.
	for (const FuncInfo &f : funcs) {
		for (u32 callee : f.callees) {
			auto it = funcByAddr.find(callee);
			if (it != funcByAddr.end()) {
				funcs[it->second].callers.insert(f.start);
			}
		}
	}

	auto nameOf = [&](u32 addr) -> std::string {
		auto it = funcByAddr.find(addr);
		if (it != funcByAddr.end()) {
			return funcs[it->second].name;
		}
		const std::string label = g_symbolMap->GetLabelString(addr);
		return label.empty() ? StringFromFormat("%08x", addr) : label;
	};

	// ---- index ----
	const Path indexPath = outDir / (SanitizeForFilename(moduleName) + ".index.md");
	FILE *f = File::OpenCFile(indexPath, "w");
	if (!f) {
		fprintf(stderr, "re: couldn't write %s\n", indexPath.c_str());
		ShutdownMinimalPSP();
		return 1;
	}

	fprintf(f, "# %s\n\n", moduleName.c_str());
	fprintf(f, "- file: `%s`\n", modulePath.GetFilename().c_str());
	if (!module) {
		fprintf(f, "- raw image, loaded at `%08x`, size `%08x`\n", base, blockSize);
		fprintf(f, "- no module header: no exports, imports, segments or relocation.\n\n");
	} else {
		fprintf(f, "- crc32: `%08x`  (matches `PSP/SYSTEM/SYMBOLS/%s_%08x.ppsym`)\n", module->crc, moduleName.c_str(), module->crc);
		fprintf(f, "- attribute: `%04x`%s\n", (u32)module->nm.attribute,
			(module->nm.attribute & PSP_MODULE_KERNEL_MODE) ? " (kernel mode)" : "");
		fprintf(f, "- version: %d.%d\n", module->nm.version[1], module->nm.version[0]);
		fprintf(f, "- loaded at: `%08x`, size `%08x`\n", base, blockSize);
		fprintf(f, "- text: `%08x`..`%08x`  data: `%x`  bss: `%x`  gp: `%08x`\n",
			(u32)module->nm.text_addr, (u32)module->nm.text_addr + (u32)module->nm.text_size,
			(u32)module->nm.data_size, (u32)module->nm.bss_size, (u32)module->nm.gp_value);
		fprintf(f, "- entry: `%08x`  module_start: `%08x`  module_stop: `%08x`\n\n",
			(u32)module->nm.entry_addr, (u32)module->nm.module_start_func, (u32)module->nm.module_stop_func);

		fprintf(f, "## Segments\n\n| # | address | size |\n|---|---|---|\n");
		for (u32 i = 0; i < module->nm.nsegment && i < 4; i++) {
			fprintf(f, "| %d | `%08x` | `%x` |\n", i, (u32)module->nm.segmentaddr[i], (u32)module->nm.segmentsize[i]);
		}

		fprintf(f, "\n## Exports (%d functions, %d variables)\n\n",
			(int)module->exportedFuncs.size(), (int)module->exportedVars.size());
		fprintf(f, "| library | NID | address | name |\n|---|---|---|---|\n");
		for (const FuncSymbolExport &exp : module->exportedFuncs) {
			const char *known = GetHLEFuncName(exp.moduleName, exp.nid);
			fprintf(f, "| `%s` | `%08x` | `%08x` | %s |\n", exp.moduleName, exp.nid, exp.symAddr,
				known ? known : nameOf(exp.symAddr).c_str());
		}
		for (const VarSymbolExport &exp : module->exportedVars) {
			fprintf(f, "| `%s` | `%08x` | `%08x` | *(variable)* |\n", exp.moduleName, exp.nid, exp.symAddr);
		}

		fprintf(f, "\n## Imports (%d functions, %d variables)\n\n",
			(int)module->importedFuncs.size(), (int)module->importedVars.size());
		fprintf(f, "| library | NID | stub | name |\n|---|---|---|---|\n");
		for (const FuncSymbolImport &imp : module->importedFuncs) {
			const char *known = GetHLEFuncName(imp.moduleName, imp.nid);
			fprintf(f, "| `%s` | `%08x` | `%08x` | %s |\n", imp.moduleName, imp.nid, imp.stubAddr,
				known ? known : "*(unknown NID)*");
		}
		for (const VarSymbolImport &imp : module->importedVars) {
			fprintf(f, "| `%s` | `%08x` | `%08x` | *(variable)* |\n", imp.moduleName, imp.nid, imp.stubAddr);
		}

	}

	fprintf(f, "\n## Functions (%d)\n\n", (int)funcs.size());
	fprintf(f, "| address | +offset | size | callers | callees | v0 | name |\n|---|---|---|---|---|---|---|\n");
	for (const FuncInfo &fn : funcs) {
		fprintf(f, "| `%08x` | `+%05x` | %d | %d | %d | %s | %s |\n",
			fn.start, fn.start - base, fn.size, (int)fn.callers.size(), (int)fn.callees.size(),
			fn.regs.writesV0 ? "y" : "-", fn.name.c_str());
	}
	fclose(f);

	// ---- call graph ----
	const Path xrefPath = outDir / (SanitizeForFilename(moduleName) + ".xref.json");
	f = File::OpenCFile(xrefPath, "w");
	if (f) {
		fprintf(f, "{\n  \"module\": \"%s\",\n  \"base\": %u,\n  \"functions\": [\n", moduleName.c_str(), base);
		for (size_t i = 0; i < funcs.size(); i++) {
			const FuncInfo &fn = funcs[i];
			fprintf(f, "    {\"addr\": \"%08x\", \"name\": \"%s\", \"size\": %d, \"callers\": [",
				fn.start, fn.name.c_str(), fn.size);
			bool first = true;
			for (u32 c : fn.callers) {
				fprintf(f, "%s\"%08x\"", first ? "" : ", ", c);
				first = false;
			}
			fprintf(f, "], \"callees\": [");
			first = true;
			for (u32 c : fn.callees) {
				fprintf(f, "%s\"%08x\"", first ? "" : ", ", c);
				first = false;
			}
			fprintf(f, "]}%s\n", i + 1 < funcs.size() ? "," : "");
		}
		fprintf(f, "  ]\n}\n");
		fclose(f);
	}

	// ---- per-function disassembly ----
	// Deliberately not DisassemblyManager: its analyze() refuses to do anything unless a game is
	// fully booted, and nothing is booted here. DisAsm() is the same formatter its opcode entries
	// use, minus the gate.

	const Path funcDir = outDir / SanitizeForFilename(moduleName);
	if (!File::CreateFullPath(funcDir)) {
		fprintf(stderr, "re: couldn't create %s\n", funcDir.c_str());
		ShutdownMinimalPSP();
		return 1;
	}

	int written = 0;
	for (const FuncInfo &fn : funcs) {
		if (!opts.funcFilter.empty()) {
			const bool byName = fn.name == opts.funcFilter;
			const bool byAddr = StringFromFormat("%08x", fn.start) == opts.funcFilter ||
				StringFromFormat("0x%08x", fn.start) == opts.funcFilter;
			if (!byName && !byAddr) {
				continue;
			}
		}

		const Path path = funcDir / StringFromFormat("%08x_%s.asm", fn.start, SanitizeForFilename(fn.name).c_str());
		FILE *out = File::OpenCFile(path, "w");
		if (!out) {
			continue;
		}

		fprintf(out, "; %s :: %s\n", moduleName.c_str(), fn.name.c_str());
		fprintf(out, "; %08x - %08x  (module +%05x, %d bytes)\n",
			fn.start, fn.start + fn.size, fn.start - base, fn.size);
		fprintf(out, ";\n");

		fprintf(out, "; Callers (%d):", (int)fn.callers.size());
		if (fn.callers.empty()) {
			fprintf(out, " none found (exported, or only called indirectly)");
		}
		for (u32 c : fn.callers) {
			fprintf(out, " %s", nameOf(c).c_str());
		}
		fprintf(out, "\n; Callees (%d):", (int)fn.callees.size());
		for (u32 c : fn.callees) {
			fprintf(out, " %s", nameOf(c).c_str());
		}
		if (fn.indirectCallees) {
			fprintf(out, " + indirect");
		}
		fprintf(out, "\n;\n");

		WriteRegEvidence(out, fn);
		fprintf(out, "\n");

		// Tracks lui-loaded upper halves so an "lui/addiu" or "lui/lw" pair can be reported as
		// the address it actually forms. Those pairs are how every global and constant table is
		// reached, so without this the interesting operands all read as bare halves.
		u32 luiVal[32] = {};
		bool luiSet[32] = {};

		const u32 end = fn.start + fn.size;
		for (u32 addr = fn.start; addr < end; addr += 4) {
			if (!Memory::IsValidAddress(addr)) {
				break;
			}
			const std::string label = g_symbolMap->GetLabelString(addr);
			if (!label.empty() && addr != fn.start) {
				fprintf(out, "\n%s:\n", label.c_str());
			}

			char text[512];
			DisAsm(addr, text, sizeof(text));
			// DisAsm separates mnemonic from operands with a tab.
			char *tab = strchr(text, '\t');
			std::string mnemonic = text;
			std::string operands;
			if (tab) {
				*tab = '\0';
				mnemonic = text;
				operands = tab + 1;
			}
			fprintf(out, "%08x  %-10s %-30s", addr, mnemonic.c_str(), operands.c_str());

			const u32 op = Memory::Read_Instruction(addr).encoding;
			const u32 opcode = op >> 26;
			const int rs = (op >> 21) & 0x1f;
			const int rt = (op >> 16) & 0x1f;
			const s32 imm = (s16)(op & 0xffff);

			if (IsJal(op)) {
				fprintf(out, " ; -> %s", nameOf(JumpTarget(addr, op)).c_str());
			} else if (opcode == 0x0f) {  // lui
				luiVal[rt] = (u32)(op & 0xffff) << 16;
				luiSet[rt] = true;
			} else if (opcode == 0x09 && luiSet[rs]) {  // addiu
				const u32 target = luiVal[rs] + imm;
				fprintf(out, " ; = %08x%s", target, DescribeAddr(target).c_str());
				luiVal[rt] = target;
				luiSet[rt] = true;
			} else if (opcode >= 0x20 && opcode <= 0x2e && luiSet[rs]) {  // load/store
				const u32 target = luiVal[rs] + imm;
				fprintf(out, " ; @ %08x%s", target, DescribeAddr(target).c_str());
			} else {
				// Anything else that writes a register invalidates what we thought it held.
				const MIPSInfo info = MIPSGetInfo(MIPSOpcode(op));
				if (info & OUT_RT) {
					luiSet[rt] = false;
				}
				if (info & OUT_RD) {
					luiSet[(op >> 11) & 0x1f] = false;
				}
			}
			fprintf(out, "\n");
		}
		fclose(out);
		written++;
	}

	if (module) {
		printf("re: %s (crc %08x) at %08x, %d bytes\n", moduleName.c_str(), module->crc, base, blockSize);
	}
	printf("re: %d exports (%d named), %d imports (%d named), %d functions; wrote %d disassembly file(s)\n",
		(int)module->exportedFuncs.size(), namedExports, (int)module->importedFuncs.size(), namedImports,
		(int)funcs.size(), written);
	printf("re: index at %s\n", indexPath.c_str());

	ShutdownMinimalPSP();
	return 0;
}
