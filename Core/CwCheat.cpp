#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>

#include "Common/Data/Text/I18n.h"
#include "Common/LogReporting.h"
#include "Common/StringUtils.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/OSD.h"
#include "Common/File/FileUtil.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/CwCheat.h"
#include "Core/MemoryScanner.h"
#include "Core/Config.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/RetroAchievements.h"
#include "GPU/Common/PostShader.h"

#ifdef _WIN32
#include "Common/Data/Encoding/Utf8.h"
#endif

// Cache invalidation
//
// Writes invalidate the instruction cache (effectively, in PPSSPP's case, the JIT translation cache),
// so code a cheat patches gets recompiled. Reads don't need to: PSP memory never holds JIT data.

static int CheatEvent = -1;
static CWCheatEngine *cheatEngine;
static bool cheatsEnabled;
using namespace SceCtrl;

void hleCheat(u64 userdata, int cyclesLate);

static inline std::string TrimString(std::string_view s) {
	auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) {
		// isspace() expects 0 - 255, so convert any sign-extended value.
	   return std::isspace((u8)c);
   });
   auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){
	   return std::isspace((u8)c);
   }).base();
   return wsback > wsfront ? std::string(wsfront, wsback) : std::string();
}

CheatFileParser::CheatFileParser(const Path &filename, std::string_view gameID) {
	fp_ = File::OpenCFile(filename, "rt");
	validGameID_ = ReplaceAll(gameID, "-", "");
}

CheatFileParser::~CheatFileParser() {
	if (fp_)
		fclose(fp_);
}

bool CheatFileParser::Parse() {
	// Ugh, using a member variable as loop counter is bad.
	for (int lineNumber = 1; fp_ && !feof(fp_); ++lineNumber) {
		char temp[2048];
		char *tempLine = fgets(temp, sizeof(temp), fp_);
		if (!tempLine)
			continue;

		// Cheats copied from websites can carry invisible characters (like direction marks) that would
		// break the hex values. This also drops the UTF-8 BOM.
		std::string line = TrimString(SanitizeString(tempLine, StringRestriction::NoInvisibleFormatting));

		// Minimum length 5 is shortest possible _ lines name of the game "_G N+"
		// and a minimum of 1 displayable character in cheat name string "_C0 1"
		// which both equal to 5 characters.
		if (line.length() >= 5 && line[0] == '_') {
			ParseLine(line, lineNumber);
		} else if (line.length() >= 2 && line[0] == '/' && line[1] == '/') {
			// Comment, ignore.
		} else if (line.length() >= 1 && line[0] == '#') {
			// Comment, ignore.
		} else if (line.length() > 0) {
			errors_.push_back(StringFromFormat("Unrecognized content on line %d: expecting _", lineNumber));
		}
	}

	Flush();

	return errors_.empty();
}

void CheatFileParser::Flush() {
	if (!pendingLines_.empty()) {
		cheats_.push_back(CheatCode{lastCheatInfo_.name, pendingLines_, pendingFormat_});
		FlushCheatInfo();
		pendingLines_.clear();
	}
}

void CheatFileParser::FlushCheatInfo() {
	if (lastCheatInfo_.lineNum != 0) {
		cheatInfo_.push_back(lastCheatInfo_);
		lastCheatInfo_ = { 0 };
	}
}

void CheatFileParser::AddError(const std::string &err, int lineNumber) {
	errors_.push_back(StringFromFormat("Error on line %d: %s", lineNumber, err.c_str()));
}

void CheatFileParser::ParseLine(const std::string &line, int lineNumber) {
	switch (line[1]) {
	case 'S':
		// Disc ID, validate (for multi-disc cheat files)?
		Flush();
		++games_;

		if (ValidateGameID(line.substr(2))) {
			if (gameRiskyEnabled_) {
				// We found the right one, so let's not use this risky stuff.
				cheats_.clear();
				cheatInfo_.clear();
				gameRiskyEnabled_ = false;
			}
			gameEnabled_ = true;
		} else if (games_ == 1) {
			// Old behavior was to ignore.
			// For BC, let's allow if the game id doesn't match, but there's only one line.
			gameRiskyEnabled_ = true;
			gameEnabled_ = true;
		} else {
			if (gameRiskyEnabled_) {
				// There are multiple games here, kill the risky stuff.
				cheats_.clear();
				cheatInfo_.clear();
				gameRiskyEnabled_ = false;
			}
			gameEnabled_ = false;
		}
		return;

	case 'G':
		// Game title.
		return;

	case 'C':
		Flush();

		// Cheat name and activation status.
		if (line.length() >= 3 && line[2] >= '1' && line[2] <= '9') {
			lastCheatInfo_ = { lineNumber, line.length() >= 5 ? line.substr(4) : "", true };
			cheatEnabled_ = true;
		} else if (line.length() >= 3 && line[2] == '0') {
			lastCheatInfo_ = { lineNumber, line.length() >= 5 ? line.substr(4) : "", false };
			cheatEnabled_ = false;
		} else {
			AddError("could not parse cheat name line", lineNumber);
			cheatEnabled_ = false;
			return;
		}
		return;

	case 'L':
		// CwCheat data line.
		ParseDataLine(line.substr(2), lineNumber, CheatCodeFormat::CwCheat);
		return;

	case 'M':
		// PSPAR data line (TempAR's name for it).
		ParseDataLine(line.substr(2), lineNumber, CheatCodeFormat::PSPAR);
		return;

	case 'N':
		// TempAR's own extensions to the PSPAR code types.
		AddError("TempAR extended (_N) codes not supported", lineNumber);
		return;

	default:
		AddError("unknown line type", lineNumber);
		return;
	}
}

void CheatFileParser::ParseDataLine(const std::string &line, int lineNumber, CheatCodeFormat format) {
	if (!gameEnabled_) {
		return;
	}

	if (!cheatEnabled_) {
		FlushCheatInfo();
		return;
	}

	if (pendingLines_.empty()) {
		pendingFormat_ = format;
	} else if (pendingFormat_ != format) {
		AddError("can't mix _L and _M lines in one cheat", lineNumber);
		return;
	}

	CheatLine cheatLine;
	int len = 0;
	if (sscanf(line.c_str(), "%x %x %n", &cheatLine.part1, &cheatLine.part2, &len) == 2) {
		// A trailing comment is fine (TempAR's databases use them).
		if ((size_t)len < line.length() && line[len] != '#' && line.compare(len, 2, "//") != 0) {
			AddError("junk after line data", lineNumber);
		}
		pendingLines_.push_back(cheatLine);
	} else {
		AddError("expecting two values", lineNumber);
	}
}

bool CheatFileParser::ValidateGameID(std::string_view gameID) {
	return validGameID_.empty() || ReplaceAll(TrimString(gameID), "-", "") == validGameID_;
}

static void __CheatStop() {
	if (cheatEngine) {
		delete cheatEngine;
		cheatEngine = nullptr;
	}
	cheatsEnabled = false;
}

static void __CheatStart() {
	__CheatStop();

	cheatEngine = new CWCheatEngine(g_paramSFO.GetDiscID());
	// This only generates ini files on boot, let's leave homebrew ini file for UI.
	std::string realGameID = g_paramSFO.GetValueString("DISC_ID");
	if (!realGameID.empty()) {
		cheatEngine->CreateCheatFile();
	}

	cheatEngine->ParseCheats();
	g_Config.bReloadCheats = false;
	cheatsEnabled = true;
}

static int GetRefreshMs() {
	int refresh = g_Config.iCwCheatRefreshIntervalMs;
	if (!cheatsEnabled) {
		refresh = 1000;
	}
	return refresh;
}

void __CheatInit() {
	// Always register the event, want savestates to be compatible whether cheats on or off.
	CheatEvent = CoreTiming::RegisterEvent("CheatEvent", &hleCheat);

	if (g_Config.bEnableCheats) {
		__CheatStart();
	}

	// Only check once a second for cheats to be enabled.
	CoreTiming::ScheduleEvent(msToCycles(GetRefreshMs()), CheatEvent, 0);
}

void __CheatShutdown() {
	__CheatStop();
	// Scan results and frozen values belong to the game that's shutting down.
	GetMemoryScanner().Reset();
	GetMemoryScanner().ClearFreezes();
}

void __CheatDoState(PointerWrap &p) {
	auto s = p.Section("CwCheat", 0, 2);
	if (!s) {
		// A savestate from before this section existed. Registering the event type isn't enough -
		// CoreTiming::DoState has already replaced the queue with the state's, which has no cheat
		// event in it, so without scheduling one here hleCheat never runs again and cheats (plus
		// the enable/disable polling) stay dead for the rest of the session.
		// (Not when a save failed earlier and every section after comes back missing.)
		if (p.mode == PointerWrap::MODE_READ) {
			CheatEvent = -1;
			CoreTiming::RestoreRegisterEvent(CheatEvent, "CheatEvent", &hleCheat);
			CoreTiming::RemoveEvent(CheatEvent);
			CoreTiming::ScheduleEvent(msToCycles(GetRefreshMs()), CheatEvent, 0);
		}
		return;
	}

	Do(p, CheatEvent);
	CoreTiming::RestoreRegisterEvent(CheatEvent, "CheatEvent", &hleCheat);

	if (s < 2) {
		// Before this we didn't have a checkpoint, so reset didn't work.
		// Let's just force one in.
		CoreTiming::RemoveEvent(CheatEvent);
		CoreTiming::ScheduleEvent(msToCycles(GetRefreshMs()), CheatEvent, 0);
	}
}

void hleCheat(u64 userdata, int cyclesLate) {
	bool shouldBeEnabled = !Achievements::HardcoreModeActive() && g_Config.bEnableCheats;

	if (cheatsEnabled != shouldBeEnabled) {
		// Okay, let's move to the desired state, then.
		if (shouldBeEnabled) {
			__CheatStart();
		} else {
			__CheatStop();
		}
	}

	// Check periodically for cheats.
	CoreTiming::ScheduleEvent(msToCycles(GetRefreshMs()), CheatEvent, 0);

	if (!cheatEngine || !cheatsEnabled) {
		return;
	}

	if (g_Config.bReloadCheats) {  // Checks if the "reload cheats" button has been pressed.
		cheatEngine->ParseCheats();
		g_Config.bReloadCheats = false;
	}
	cheatEngine->Run();
	// Values frozen in the Memory Scanner are held like cheats, at the same interval.
	GetMemoryScanner().ApplyFreezes();
}

CWCheatEngine::CWCheatEngine(std::string_view gameID) : gameID_(gameID) {
	filename_ = GetSysDirectory(DIRECTORY_CHEATS) / (gameID_ + ".ini");
}

void CWCheatEngine::CreateCheatFile() {
	File::CreateFullPath(GetSysDirectory(DIRECTORY_CHEATS));

	if (!File::Exists(filename_)) {
		FILE *f = File::OpenCFile(filename_, "wb");
		if (f) {
			fwrite("\xEF\xBB\xBF\n", 1, 4, f);
			fclose(f);
		}
		if (!File::Exists(filename_)) {
			auto err = GetI18NCategory(I18NCat::ERRORS);
			g_OSD.Show(OSDType::MESSAGE_ERROR, err->T("Unable to create cheat file, disk may be full"));
		}
	}
}

void CWCheatEngine::ParseCheats() {
	CheatFileParser parser(filename_, gameID_);

	parser.Parse();
	// TODO: Report errors in a user-visible way.
	for (auto &error : parser.GetErrors()) {
		ERROR_LOG(Log::System, "CwCheat error: %s", error.c_str());
	}

	cheats_ = parser.GetCheats();
}

std::vector<CheatFileInfo> CWCheatEngine::FileInfo(std::vector<std::string> *errors) const {
	CheatFileParser parser(filename_, gameID_);
	parser.Parse();
	if (errors) {
		*errors = parser.GetErrors();
	}
	return parser.GetFileInfo();
}

void CWCheatEngine::InvalidateICache(u32 addr, int size) const {
	// Round start down and size up to the nearest word.
	u32 aligned = addr & ~3;
	int alignedSize = (addr + size - aligned + 3) & ~3;
	currentMIPS->InvalidateICacheRangeDeferred(aligned, alignedSize);
}

enum class CheatOp {
	Invalid,
	Noop,

	Write,
	Add,
	Subtract,
	Or,
	And,
	Xor,

	MultiWrite,

	CopyBytesFrom,
	Vibration,
	VibrationFromMemory,
	PostShader,
	PostShaderFromMemory,
	Delay,

	Assert,

	IfEqual,
	IfNotEqual,
	IfLess,
	IfGreater,

	IfAddrEqual,
	IfAddrNotEqual,
	IfAddrLess,
	IfAddrGreater,

	IfPressed,
	IfNotPressed,

	CwCheatPointerCommands,
};

struct CheatOperation {
	CheatOp op;
	uint32_t addr;
	int sz;
	uint32_t val;

	union {
		struct {
			uint32_t count;
			uint32_t step;
			uint32_t add;
		} multiWrite;
		struct {
			uint32_t destAddr;
		} copyBytesFrom;
		struct {
			uint32_t skip;
		} ifTypes;
		struct {
			uint32_t skip;
			uint32_t compareAddr;
		} ifAddrTypes;
		struct {
			int offset;
			int baseOffset;
			int count;
			int type;
		} pointerCommands;
		struct {
			uint16_t vibrL;
			uint16_t vibrR;
			uint8_t vibrLTime;
			uint8_t vibrRTime;
		} vibrationValues;
		struct {
			union {
				float f;
				uint32_t u;
			} value;
			uint8_t shader;
			uint8_t uniform;
			uint8_t format;
		} PostShaderUniform;
	};
};

CheatOperation CWCheatEngine::InterpretNextCwCheat(const CheatCode &cheat, size_t &i) {
	const CheatLine &line1 = cheat.lines[i++];
	const uint32_t &arg = line1.part2;

	// Filled as needed.
	u32 addr;

	int type = line1.part1 >> 28;
	switch (type) {
	case 0x0: // Write 8-bit data (up to 4 bytes.)
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		if (arg & 0xFFFF0000)
			return { CheatOp::Write, addr, 4, arg };
		else if (arg & 0x0000FF00)
			return { CheatOp::Write, addr, 2, arg };
		else
			return { CheatOp::Write, addr, 1, arg };

	case 0x1: // Write 16-bit data.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		return { CheatOp::Write, addr, 2, arg };

	case 0x2: // Write 32-bit data.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		return { CheatOp::Write, addr, 4, arg };

	case 0x3: // Increment/decrement data.
		addr = GetAddress(arg & 0x0FFFFFFF);
		switch ((line1.part1 >> 20) & 0xF) {
		case 1:
			return { CheatOp::Add, addr, 1, line1.part1 & 0xFF };
		case 2:
			return { CheatOp::Subtract, addr, 1, line1.part1 & 0xFF };
		case 3:
			return { CheatOp::Add, addr, 2, line1.part1 & 0xFFFF };
		case 4:
			return { CheatOp::Subtract, addr, 2, line1.part1 & 0xFFFF };
		case 5:
			if (i < cheat.lines.size())
				return { CheatOp::Add, addr, 4, cheat.lines[i++].part1 };
			return { CheatOp::Invalid };
		case 6:
			if (i < cheat.lines.size())
				return { CheatOp::Subtract, addr, 4, cheat.lines[i++].part1 };
			return { CheatOp::Invalid };

		default:
			return { CheatOp::Invalid };
		}
		break;

	case 0x4: // 32-bit multi-write patch data.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		if (i < cheat.lines.size()) {
			const CheatLine &line2 = cheat.lines[i++];

			CheatOperation op = { CheatOp::MultiWrite, addr, 4, line2.part1 };
			op.multiWrite.count = arg >> 16;
			op.multiWrite.step = (arg & 0xFFFF) * 4;
			op.multiWrite.add = line2.part2;
			return op;
		}
		return { CheatOp::Invalid };

	case 0x5: // Memcpy command.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		if (i < cheat.lines.size()) {
			const CheatLine &line2 = cheat.lines[i++];

			CheatOperation op = { CheatOp::CopyBytesFrom, addr, 0, arg };
			op.copyBytesFrom.destAddr = GetAddress(line2.part1 & 0x0FFFFFFF);
			return op;
		}
		return { CheatOp::Invalid };

	case 0x6: // Pointer commands.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		if (i < cheat.lines.size()) {
			const CheatLine &line2 = cheat.lines[i++];
			int count = (line2.part1 & 0xFFFF) - 1;

			// Validate lines to process - make sure we stay inside cheat.lines.
			if (i + count > cheat.lines.size())
				return { CheatOp::Invalid };

			CheatOperation op = { CheatOp::CwCheatPointerCommands, addr, 0, arg };
			op.pointerCommands.offset = (int)line2.part2;
			// TODO: Verify sign handling.  Is this really supposed to sign extend?
			op.pointerCommands.baseOffset = ((int)line2.part1 >> 20) * 4;
			op.pointerCommands.count = count;
			op.pointerCommands.type = (line2.part1 >> 16) & 0xF;
			return op;
		}
		return { CheatOp::Invalid };

	case 0x7: // Boolean data operations.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		switch (arg >> 16) {
		case 0x0000: // 8-bit OR.
			return { CheatOp::Or, addr, 1, arg & 0xFF };
		case 0x0001: // 16-bit OR.
			return { CheatOp::Or, addr, 2, arg & 0xFFFF };
		case 0x0002: // 8-bit AND.
			return { CheatOp::And, addr, 1, arg & 0xFF };
		case 0x0003: // 16-bit AND.
			return { CheatOp::And, addr, 2, arg & 0xFFFF };
		case 0x0004: // 8-bit XOR.
			return { CheatOp::Xor, addr, 1, arg & 0xFF };
		case 0x0005: // 16-bit XOR.
			return { CheatOp::Xor, addr, 2, arg & 0xFFFF };
		}
		return { CheatOp::Invalid };

	case 0x8: // 8-bit or 16-bit multi-write patch data.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		if (i < cheat.lines.size()) {
			const CheatLine &line2 = cheat.lines[i++];
			const bool is8Bit = (line2.part1 & 0xFFFF0000) == 0;
			const uint32_t val = is8Bit ? (line2.part1 & 0xFF) : (line2.part1 & 0xFFFF);

			CheatOperation op = { CheatOp::MultiWrite, addr, is8Bit ? 1 : 2, val };
			op.multiWrite.count = arg >> 16;
			op.multiWrite.step = (arg & 0xFFFF) * (is8Bit ? 1 : 2);
			op.multiWrite.add = line2.part2;
			return op;
		}
		return { CheatOp::Invalid };

	case 0xA: // PPSSPP specific cheats
		switch (line1.part1 >> 24 & 0xF) {
		case 0x0: // 0x0 sets gamepad vibration by cheat parameters
			{
				CheatOperation op = { CheatOp::Vibration };
				op.vibrationValues.vibrL = line1.part1 & 0x0000FFFF;
				op.vibrationValues.vibrR = line1.part2 & 0x0000FFFF;
				op.vibrationValues.vibrLTime = (line1.part1 >> 16) & 0x000000FF;
				op.vibrationValues.vibrRTime = (line1.part2 >> 16) & 0x000000FF;
				return op;
			}
		case 0x1: // 0x1 reads value for gamepad vibration from memory
			addr = line1.part2;
			return { CheatOp::VibrationFromMemory, addr };
		case 0x2: // 0x2 sets postprocessing shader uniform
			{
				CheatOperation op = { CheatOp::PostShader };
				op.PostShaderUniform.uniform = line1.part1 & 0x000000FF;
				op.PostShaderUniform.shader = (line1.part1 >> 16) & 0x000000FF;
				op.PostShaderUniform.value.u = line1.part2;
				return op;
			}
		case 0x3: // 0x3 sets postprocessing shader uniform from memory
			{
				addr = line1.part2;
				CheatOperation op = { CheatOp::PostShaderFromMemory, addr };
				op.PostShaderUniform.uniform = line1.part1 & 0x000000FF;
				op.PostShaderUniform.format = (line1.part1 >> 8) & 0x000000FF;
				op.PostShaderUniform.shader = (line1.part1 >> 16) & 0x000000FF;
				return op;
			}
		// Place for other PPSSPP specific cheats
		default:
			return { CheatOp::Invalid };
		}

	case 0xB: // Delay command.
		return { CheatOp::Delay, 0, 0, arg };

	case 0xC: // 32-bit equal check / code stopper.
		addr = GetAddress(line1.part1 & 0x0FFFFFFF);
		return { CheatOp::Assert, addr, 4, arg };

	case 0xD: // Line skip tests & joker codes.
		switch (arg >> 28) {
		case 0x0: // 16-bit next line skip test.
		case 0x2: // 8-bit next line skip test.
			addr = GetAddress(line1.part1 & 0x0FFFFFFF);
			{
				const bool is8Bit = (arg >> 28) == 0x2;
				const uint32_t val = is8Bit ? (arg & 0xFF) : (arg & 0xFFFF);

				CheatOp opcode;
				switch ((arg >> 20) & 0xF) {
				case 0x0:
					opcode = CheatOp::IfEqual;
					break;
				case 0x1:
					opcode = CheatOp::IfNotEqual;
					break;
				case 0x2:
					opcode = CheatOp::IfLess;
					break;
				case 0x3:
					opcode = CheatOp::IfGreater;
					break;
				default:
					return { CheatOp::Invalid };
				}

				CheatOperation op = { opcode, addr, is8Bit ? 1 : 2, val };
				op.ifTypes.skip = 1;
				return op;
			}

		case 0x1: // Joker code - button pressed.
		case 0x3: // Inverse joker code - button not pressed.
			{
				bool pressed = (arg >> 28) == 0x1;
				CheatOperation op = { pressed ? CheatOp::IfPressed : CheatOp::IfNotPressed, 0, 0, arg & 0x0FFFFFFF };
				op.ifTypes.skip = (line1.part1 & 0xFF) + 1;
				return op;
			}

		case 0x4: // Adress equal test.
		case 0x5: // Address not equal test.
		case 0x6: // Address less than test.
		case 0x7: // Address greater than test.
			addr = GetAddress(line1.part1 & 0x0FFFFFFF);
			if (i < cheat.lines.size()) {
				const CheatLine &line2 = cheat.lines[i++];
				const int sz = 1 << (line2.part2 & 0xF);

				CheatOp opcode;
				switch (arg >> 28) {
				case 0x4:
					opcode = CheatOp::IfAddrEqual;
					break;
				case 0x5:
					opcode = CheatOp::IfAddrNotEqual;
					break;
				case 0x6:
					opcode = CheatOp::IfAddrLess;
					break;
				case 0x7:
					opcode = CheatOp::IfAddrGreater;
					break;
				default:
					return { CheatOp::Invalid };
				}

				CheatOperation op = { opcode, addr, sz, 0 };
				op.ifAddrTypes.skip = line2.part1;
				op.ifAddrTypes.compareAddr = GetAddress(arg & 0x0FFFFFFF);
				return op;
			}
			return { CheatOp::Invalid };

		default:
			return { CheatOp::Invalid };
		}

	case 0xE: // Multiple line skip tests.
		addr = GetAddress(arg & 0x0FFFFFFF);
		{
			const bool is8Bit = (line1.part1 >> 24) == 0xE1;
			const uint32_t val = is8Bit ? (line1.part1 & 0xFF) : (line1.part1 & 0xFFFF);

			CheatOp opcode;
			switch (arg >> 28) {
			case 0x0:
				opcode = CheatOp::IfEqual;
				break;
			case 0x1:
				opcode = CheatOp::IfNotEqual;
				break;
			case 0x2:
				opcode = CheatOp::IfLess;
				break;
			case 0x3:
				opcode = CheatOp::IfGreater;
				break;
			default:
				return { CheatOp::Invalid };
			}

			CheatOperation op = { opcode, addr, is8Bit ? 1 : 2, val };
			op.ifTypes.skip = (line1.part1 >> 16) & (is8Bit ? 0xFF : 0xFFF);
			return op;
		}

	default:
		return { CheatOp::Invalid };
	}
}

void CWCheatEngine::ApplyMemoryOperator(const CheatOperation &op, uint32_t(*oper)(uint32_t, uint32_t)) {
	if (Memory::IsValidRange(op.addr, op.sz)) {
		InvalidateICache(op.addr, op.sz);
		if (op.sz == 1)
			Memory::WriteUnchecked_U8((u8)oper(Memory::ReadUnchecked_U8(op.addr), op.val), op.addr);
		else if (op.sz == 2)
			Memory::WriteUnchecked_U16((u16)oper(Memory::ReadUnchecked_U16(op.addr), op.val),op. addr);
		else if (op.sz == 4)
			Memory::WriteUnchecked_U32((u32)oper(Memory::ReadUnchecked_U32(op.addr), op.val), op.addr);
	} else {
		// Report memory error
	}
}

bool CWCheatEngine::TestIf(const CheatOperation &op, bool(*oper)(int, int)) const {
	if (Memory::IsValidRange(op.addr, op.sz)) {

		int memoryValue = 0;
		if (op.sz == 1)
			memoryValue = (int)Memory::ReadUnchecked_U8(op.addr);
		else if (op.sz == 2)
			memoryValue = (int)Memory::ReadUnchecked_U16(op.addr);
		else if (op.sz == 4)
			memoryValue = (int)Memory::ReadUnchecked_U32(op.addr);

		return oper(memoryValue, (int)op.val);
	}
	return false;
}

bool CWCheatEngine::TestIfAddr(const CheatOperation &op, bool(*oper)(int, int)) const {
	if (Memory::IsValidRange(op.addr, op.sz) && Memory::IsValidRange(op.ifAddrTypes.compareAddr, op.sz)) {

		int memoryValue1 = 0;
		int memoryValue2 = 0;
		if (op.sz == 1) {
			memoryValue1 = (int)Memory::ReadUnchecked_U8(op.addr);
			memoryValue2 = (int)Memory::ReadUnchecked_U8(op.ifAddrTypes.compareAddr);
		} else if (op.sz == 2) {
			memoryValue1 = (int)Memory::ReadUnchecked_U16(op.addr);
			memoryValue2 = (int)Memory::ReadUnchecked_U16(op.ifAddrTypes.compareAddr);
		} else if (op.sz == 4) {
			memoryValue1 = (int)Memory::ReadUnchecked_U32(op.addr);
			memoryValue2 = (int)Memory::ReadUnchecked_U32(op.ifAddrTypes.compareAddr);
		}

		return oper(memoryValue1, memoryValue2);
	}
	return false;
}

void CWCheatEngine::ExecuteOp(const CheatOperation &op, const CheatCode &cheat, size_t &i) {
	switch (op.op) {
	case CheatOp::Invalid:
		i = cheat.lines.size();
		break;

	case CheatOp::Noop:
		break;

	case CheatOp::Write:
		if (Memory::IsValidRange(op.addr, op.sz)) {
			InvalidateICache(op.addr, op.sz);
			if (op.sz == 1)
				Memory::WriteUnchecked_U8((u8)op.val, op.addr);
			else if (op.sz == 2)
				Memory::WriteUnchecked_U16((u16)op.val, op.addr);
			else if (op.sz == 4)
				Memory::WriteUnchecked_U32((u32)op.val, op.addr);
		}
		break;

	case CheatOp::Add:
		ApplyMemoryOperator(op, [](uint32_t a, uint32_t b) {
			return a + b;
		});
		break;

	case CheatOp::Subtract:
		ApplyMemoryOperator(op, [](uint32_t a, uint32_t b) {
			return a - b;
		});
		break;

	case CheatOp::Or:
		ApplyMemoryOperator(op, [](uint32_t a, uint32_t b) {
			return a | b;
		});
		break;

	case CheatOp::And:
		ApplyMemoryOperator(op, [](uint32_t a, uint32_t b) {
			return a & b;
		});
		break;

	case CheatOp::Xor:
		ApplyMemoryOperator(op, [](uint32_t a, uint32_t b) {
			return a ^ b;
		});
		break;

	case CheatOp::MultiWrite:
	{
		u64 range = (u64)op.multiWrite.count * (u64)op.multiWrite.step + op.sz;
		if (range < 24 * 1024 * 1024 && Memory::IsValidRange(op.addr, op.multiWrite.count * op.multiWrite.step + op.sz)) {
			InvalidateICache(op.addr, op.multiWrite.count * op.multiWrite.step + op.sz);

			uint32_t data = op.val;
			uint32_t addr = op.addr;
			// TODO: Can coalesce the range check.
			for (uint32_t a = 0; a < op.multiWrite.count; a++) {
				if (Memory::IsValidRange(addr, op.sz)) {
					if (op.sz == 1)
						Memory::WriteUnchecked_U8((u8)data, addr);
					else if (op.sz == 2)
						Memory::WriteUnchecked_U16((u16)data, addr);
					else if (op.sz == 4)
						Memory::WriteUnchecked_U32((u32)data, addr);
				}
				addr += op.multiWrite.step;
				data += op.multiWrite.add;
			}
		}
		break;
	}
	case CheatOp::CopyBytesFrom:
		if (Memory::IsValidRange(op.addr, op.val) && Memory::IsValidRange(op.copyBytesFrom.destAddr, op.val)) {
			InvalidateICache(op.copyBytesFrom.destAddr, op.val);

			Memory::Memcpy(op.copyBytesFrom.destAddr, op.addr, op.val, "CwCheat");
		}
		break;

	case CheatOp::Vibration:
		if (op.vibrationValues.vibrL > 0) {
			SetLeftVibration(op.vibrationValues.vibrL);
			SetVibrationLeftDropout(op.vibrationValues.vibrLTime);
		}
		if (op.vibrationValues.vibrR > 0) {
			SetRightVibration(op.vibrationValues.vibrR);
			SetVibrationRightDropout(op.vibrationValues.vibrRTime);
		}
		break;

	case CheatOp::VibrationFromMemory:
		if (Memory::IsValidRange(op.addr, 8)) {
			uint16_t checkLeftVibration = Memory::ReadUnchecked_U16(op.addr);
			uint16_t checkRightVibration = Memory::ReadUnchecked_U16(op.addr + 0x2);
			if (checkLeftVibration > 0) {
				SetLeftVibration(checkLeftVibration);
				SetVibrationLeftDropout(Memory::ReadUnchecked_U8(op.addr + 0x4));
			}
			if (checkRightVibration > 0) {
				SetRightVibration(checkRightVibration);
				SetVibrationRightDropout(Memory::ReadUnchecked_U8(op.addr + 0x6));
			}
		}
		break;

	case CheatOp::PostShader:
		{
			auto shaderChain = GetFullPostShadersChain(g_Config.vPostShaderNames);
			if (op.PostShaderUniform.shader < shaderChain.size()) {
				std::string shaderName = shaderChain[op.PostShaderUniform.shader]->section;
				g_Config.mPostShaderSetting[StringFromFormat("%sSettingCurrentValue%d", shaderName.c_str(), op.PostShaderUniform.uniform + 1)] = op.PostShaderUniform.value.f;
			}
		}
		break;

	case CheatOp::PostShaderFromMemory:
		{
			auto shaderChain = GetFullPostShadersChain(g_Config.vPostShaderNames);
			if (Memory::IsValidRange(op.addr, 4) && op.PostShaderUniform.shader < shaderChain.size()) {
				union {
					float f;
					uint32_t u;
				} value;
				value.u = Memory::ReadUnchecked_U32(op.addr);  // we check the range above
				std::string shaderName = shaderChain[op.PostShaderUniform.shader]->section;
				switch (op.PostShaderUniform.format) {
				case 0:
					g_Config.mPostShaderSetting[StringFromFormat("%sSettingCurrentValue%d", shaderName.c_str(), op.PostShaderUniform.uniform + 1)] = value.u & 0x000000FF;
					break;
				case 1:
					g_Config.mPostShaderSetting[StringFromFormat("%sSettingCurrentValue%d", shaderName.c_str(), op.PostShaderUniform.uniform + 1)] = value.u & 0x0000FFFF;
					break;
				case 2:
					g_Config.mPostShaderSetting[StringFromFormat("%sSettingCurrentValue%d", shaderName.c_str(), op.PostShaderUniform.uniform + 1)] = value.u;
					break;
				case 3:
					g_Config.mPostShaderSetting[StringFromFormat("%sSettingCurrentValue%d", shaderName.c_str(), op.PostShaderUniform.uniform + 1)] = value.f;
					break;
				}
			}
		}
		break;

	case CheatOp::Delay:
		// TODO: Not supported.
		break;

	case CheatOp::Assert:
		if (Memory::IsValidRange(op.addr, 4)) {
			if (Memory::ReadUnchecked_U32(op.addr) != op.val) {
				i = cheat.lines.size();
			}
		}
		break;

	case CheatOp::IfEqual:
		if (!TestIf(op, [](int a, int b) { return a == b; })) {
			i += (size_t)op.ifTypes.skip;
		}
		break;

	case CheatOp::IfNotEqual:
		if (!TestIf(op, [](int a, int b) { return a != b; })) {
			i += (size_t)op.ifTypes.skip;
		}
		break;

	case CheatOp::IfLess:
		if (!TestIf(op, [](int a, int b) { return a < b; })) {
			i += (size_t)op.ifTypes.skip;
		}
		break;

	case CheatOp::IfGreater:
		if (!TestIf(op, [](int a, int b) { return a > b; })) {
			i += (size_t)op.ifTypes.skip;
		}
		break;

	case CheatOp::IfAddrEqual:
		if (!TestIfAddr(op, [](int a, int b) { return a == b; })) {
			i += (size_t)op.ifAddrTypes.skip;
		}
		break;

	case CheatOp::IfAddrNotEqual:
		if (!TestIfAddr(op, [](int a, int b) { return a != b; })) {
			i += (size_t)op.ifAddrTypes.skip;
		}
		break;

	case CheatOp::IfAddrLess:
		if (!TestIfAddr(op, [](int a, int b) { return a < b; })) {
			i += (size_t)op.ifAddrTypes.skip;
		}
		break;

	case CheatOp::IfAddrGreater:
		if (!TestIfAddr(op, [](int a, int b) { return a > b; })) {
			i += (size_t)op.ifAddrTypes.skip;
		}
		break;

	case CheatOp::IfPressed:
		// Button	Code
		// SELECT	0x00000001
		// START	0x00000008
		// DPAD UP	0x00000010
		// DPAD RIGHT	0x00000020
		// DPAD DOWN	0x00000040
		// DPAD LEFT	0x00000080
		// L TRIGGER	0x00000100
		// R TRIGGER	0x00000200
		// TRIANGLE	0x00001000
		// CIRCLE	0x00002000
		// CROSS	0x00004000
		// SQUARE	0x00008000
		// HOME		0x00010000
		// HOLD		0x00020000
		// WLAN		0x00040000
		// REMOTE HOLD	0x00080000
		// VOLUME UP	0x00100000
		// VOLUME DOWN	0x00200000
		// SCREEN	0x00400000
		// NOTE		0x00800000
		if ((__CtrlPeekButtons() & op.val) != op.val) {
			i += (size_t)op.ifTypes.skip;
		}
		break;

	case CheatOp::IfNotPressed:
		if ((__CtrlPeekButtons() & op.val) == op.val) {
			i += (size_t)op.ifTypes.skip;
		}
		break;

	case CheatOp::CwCheatPointerCommands:
		{
			if (!Memory::IsValidRange(op.addr + op.pointerCommands.baseOffset, 4)) {
				break;
			}
			u32 base = Memory::ReadUnchecked_U32(op.addr + op.pointerCommands.baseOffset);
			u32 val = op.val;
			int type = op.pointerCommands.type;
			for (int a = 0; a < op.pointerCommands.count; ++a) {
				const CheatLine &line = cheat.lines[i++];
				switch (line.part1 >> 28) {
				case 0x1: // type copy byte
					if (Memory::IsValidRange(op.addr, 4) && Memory::IsValidRange(op.addr + op.pointerCommands.baseOffset, 4)) {
						u32 srcAddr = Memory::ReadUnchecked_U32(op.addr) + op.pointerCommands.offset;
						u32 dstAddr = Memory::ReadUnchecked_U32(op.addr + op.pointerCommands.baseOffset) + (line.part1 & 0x0FFFFFFF);
						if (Memory::IsValidRange(dstAddr, val) && Memory::IsValidRange(srcAddr, val)) {
							InvalidateICache(dstAddr, val);
							Memory::Memcpy(dstAddr, srcAddr, val, "CwCheat");
						}
						// Don't perform any further action.
						type = -1;
					}
					break;

				case 0x2:
				case 0x3: // type pointer walk
					{
						int walkOffset = (int)line.part1 & 0x0FFFFFFF;
						if ((line.part1 >> 28) == 0x3) {
							walkOffset = -walkOffset;
						}
						if (Memory::IsValidRange(base + walkOffset, 4)) {
							// TODO: I've seen crashes here. Presumably an unaligned pointer just off the edge of memory.
							// We should probably check pointer validity and invalidate the cheat if this happens.
							base = Memory::ReadUnchecked_U32(base + walkOffset);
							switch (line.part2 >> 28) {
							case 0x2:
							case 0x3: // type pointer walk
								walkOffset = line.part2 & 0x0FFFFFFF;
								if ((line.part2 >> 28) == 0x3) {
									walkOffset = -walkOffset;
								}
								if (Memory::IsValidRange(base + walkOffset, 4)) {
									base = Memory::ReadUnchecked_U32(base + walkOffset);
								}
								break;

							default:
								// Unexpected value in cheat line?
								break;
							}
						}
					}
					break;

				case 0x9: // type multi address write
					base += line.part1 & 0x0FFFFFFF;
					val += line.part2;
					break;

				default:
					// Unexpected value in cheat line?
					break;
				}
			}

			switch (type) {
			case 0: // 8 bit write
				if (Memory::IsValidAddress(base + op.pointerCommands.offset)) {
					InvalidateICache(base + op.pointerCommands.offset, 1);
					Memory::WriteUnchecked_U8((u8)val, base + op.pointerCommands.offset);
				}
				break;
			case 1: // 16-bit write
				if (Memory::IsValidRange(base + op.pointerCommands.offset, 2)) {
					InvalidateICache(base + op.pointerCommands.offset, 2);
					Memory::WriteUnchecked_U16((u16)val, base + op.pointerCommands.offset);
				}
				break;
			case 2: // 32-bit write
				if (Memory::IsValidRange(base + op.pointerCommands.offset, 4)) {
					InvalidateICache(base + op.pointerCommands.offset, 4);
					Memory::WriteUnchecked_U32((u32)val, base + op.pointerCommands.offset);
				}
				break;
			case 3: // 8 bit inverse write
				if (Memory::IsValidAddress(base - op.pointerCommands.offset)) {
					InvalidateICache(base - op.pointerCommands.offset, 1);
					Memory::WriteUnchecked_U8((u8)val, base - op.pointerCommands.offset);
				}
				break;
			case 4: // 16-bit inverse write
				if (Memory::IsValidRange(base - op.pointerCommands.offset, 2)) {
					InvalidateICache(base - op.pointerCommands.offset, 2);
					Memory::WriteUnchecked_U16((u16)val, base - op.pointerCommands.offset);
				}
				break;
			case 5: // 32-bit inverse write
				if (Memory::IsValidRange(base - op.pointerCommands.offset, 4)) {
					InvalidateICache(base - op.pointerCommands.offset, 4);
					Memory::WriteUnchecked_U32((u32)val, base - op.pointerCommands.offset);
				}
				break;
			case -1: // Operation already performed, nothing to do
				break;
			}
		}
		break;

	default:
		_assert_(false);
	}
}

// PSPAR (PSP Action Replay) code types.
//
// This follows cheat_apply_pspar() in TempAR (src/objects/cheat.c), the plugin most PSPAR codes are run
// with now, rather than Datel's documentation, which is unclear in places. TempAR's own extensions (_N
// lines, CHEAT_PSPAR_EXT there) aren't supported: 0xC1, 0xC2 and 0xC5 do nothing, and 0xD4 always adds.

// Where a cheat's own lines appear to its code, so the safe data store (0xC4) can point the offset at
// them. On a PSP that's wherever TempAR keeps the cheat in memory. This is above all PSP memory, so
// it's never translated as a relative address.
static const u32 PSPAR_SELF_BASE = 0xF0000000;
// TempAR reads the pad from fake addresses: buttons at +0, analog X at +4, analog Y at +8.
static const u32 PSPAR_PAD_BASE = 0x0A000000;
// Hard cap on the lines one cheat runs per pass, as a loop (0xC0) can repeat billions of times.
static const u32 PSPAR_MAX_LINES_PER_RUN = 0x400000;

// Addresses below user memory are relative to 0x08800000, like in CwCheat. Others are used as is.
static u32 PSPARResolveAddress(u32 addr) {
	if (addr < 0x08800000) {
		addr &= 0x0FFFFFFF;
		if (addr <= 0x017FFFFF) {
			addr += 0x08800000;
		}
	}
	return addr;
}

static u32 PSPARPadValue(u32 addr) {
	u32 value = 0;
	switch (addr & ~3) {
	case PSPAR_PAD_BASE:
		value = __CtrlPeekButtons();
		break;
	case PSPAR_PAD_BASE + 4:
	case PSPAR_PAD_BASE + 8:
		{
			float x, y;
			__CtrlPeekAnalog(CTRL_STICK_LEFT, &x, &y);
			// The inverse of __CtrlPeekAnalog's scaling.
			float v = (addr & ~3) == PSPAR_PAD_BASE + 4 ? x : -y;
			value = (u32)std::clamp((int)(v * 127.5f + 128.0f), 0, 255);
		}
		break;
	}
	return value >> (8 * (addr & 3));
}

static u32 SizeMask(int size) {
	return size == 4 ? 0xFFFFFFFF : (1U << (size * 8)) - 1;
}

u32 CWCheatEngine::PSPARRead(const CheatCode &cheat, u32 addr, int size) const {
	if ((addr & 0xFF000000) == PSPAR_PAD_BASE) {
		return PSPARPadValue(addr) & SizeMask(size);
	}

	addr = PSPARResolveAddress(addr) & ~(size - 1);
	if (addr >= PSPAR_SELF_BASE) {
		u32 offset = addr - PSPAR_SELF_BASE;
		if (offset / 8 >= cheat.lines.size()) {
			return 0;
		}
		const CheatLine &line = cheat.lines[offset / 8];
		u32 word = (offset & 4) ? line.part2 : line.part1;
		return (word >> (8 * (offset & 3))) & SizeMask(size);
	}

	addr &= 0x3FFFFFFF;
	if (!Memory::IsValidRange(addr, size)) {
		return 0;
	}
	if (size == 1)
		return Memory::ReadUnchecked_U8(addr);
	if (size == 2)
		return Memory::ReadUnchecked_U16(addr);
	return Memory::ReadUnchecked_U32(addr);
}

void CWCheatEngine::PSPARWrite(CheatCode &cheat, u32 addr, u32 value, int size) const {
	addr = PSPARResolveAddress(addr) & ~(size - 1);
	if (addr >= PSPAR_SELF_BASE) {
		u32 offset = addr - PSPAR_SELF_BASE;
		if (offset / 8 >= cheat.lines.size()) {
			return;
		}
		CheatLine &line = cheat.lines[offset / 8];
		u32 &word = (offset & 4) ? line.part2 : line.part1;
		const int shift = 8 * (offset & 3);
		word = (word & ~(SizeMask(size) << shift)) | ((value & SizeMask(size)) << shift);
		return;
	}

	addr &= 0x3FFFFFFF;
	if (!Memory::IsValidRange(addr, size)) {
		return;
	}
	InvalidateICache(addr, size);
	if (size == 1)
		Memory::WriteUnchecked_U8((u8)value, addr);
	else if (size == 2)
		Memory::WriteUnchecked_U16((u16)value, addr);
	else
		Memory::WriteUnchecked_U32(value, addr);
}

void CWCheatEngine::RunPSPAR(CheatCode &cheat) {
	// The "Dx" registers. Bit 0 of the execution status set means the codes are skipped, and each
	// condition pushes a bit that a terminator (0xD0) pops.
	u32 offset = 0;
	u32 repeatValue = 0;
	size_t repeatBegin = 0;
	u32 dataRegister = 0;
	u32 executionStatus = 0;
	u32 executionStatusOld = 0;

	const size_t count = cheat.lines.size();
	u32 linesRun = 0;
	for (size_t i = 0; i < count; i++) {
		if (++linesRun > PSPAR_MAX_LINES_PER_RUN) {
			WARN_LOG_ONCE(psparLoop, Log::System, "PSPAR cheat '%s' runs too many lines, stopping it", cheat.name.c_str());
			break;
		}

		const CheatLine &line = cheat.lines[i];
		u32 type;
		u32 addr;
		if ((line.part1 >> 28) == 0xC || (line.part1 >> 28) == 0xD) {
			type = line.part1 >> 24;
			addr = line.part1 & 0x00FFFFFF;
		} else {
			type = line.part1 >> 28;
			addr = line.part1 & 0x0FFFFFFF;
		}
		u32 value = line.part2;

		// Conditions and terminators run even while codes are being skipped.
		const bool isCondition = type >= 0x03 && type <= 0x0A;
		if ((executionStatus & 1) == 0 || isCondition || type == 0xD0 || type == 0xD1 || type == 0xD2) {
			switch (type) {
			case 0x00:  // 32-bit write
				PSPARWrite(cheat, addr + offset, value, 4);
				break;
			case 0x01:  // 16-bit write
				PSPARWrite(cheat, addr + offset, value, 2);
				break;
			case 0x02:  // 8-bit write
				PSPARWrite(cheat, addr + offset, value, 1);
				break;

			case 0x03: case 0x04: case 0x05: case 0x06:  // 32-bit conditions
			case 0x07: case 0x08: case 0x09: case 0x0A:  // 16-bit conditions with a mask
				{
					bool skip = false;
					// PSPAR's own button codes read its pad buffer, at the top of user memory. TempAR
					// can send them to the real pad instead, which is all we can do.
					if (addr >= 0x09F00000 && (type == 0x09 || type == 0x0A)) {
						if ((value & 0xFFFF) != 0) {
							addr = PSPAR_PAD_BASE;
							value = (value & 0xFFFF) | (~value << 16);
						} else {
							skip = true;
						}
					}
					if (addr == 0) {
						addr = offset;
					}

					bool pass = true;
					if (!skip && (executionStatus & 1) == 0) {
						if (type <= 0x06) {
							const u32 memValue = PSPARRead(cheat, addr, 4);
							switch (type) {
							case 0x03: pass = value > memValue; break;
							case 0x04: pass = value < memValue; break;
							case 0x05: pass = value == memValue; break;
							case 0x06: pass = value != memValue; break;
							}
						} else {
							const u32 compare = value & 0xFFFF;
							const u32 memValue = (~value >> 16) & PSPARRead(cheat, addr, 2);
							switch (type) {
							case 0x07: pass = compare > memValue; break;
							case 0x08: pass = compare < memValue; break;
							case 0x09: pass = compare == memValue; break;
							case 0x0A: pass = compare != memValue; break;
							}
						}
					}
					if (!skip) {
						executionStatus = (executionStatus << 1) | (((executionStatus & 1) != 0 || !pass) ? 1 : 0);
					}
				}
				break;

			case 0x0B:  // Load offset
				offset = PSPARRead(cheat, addr + offset, 4);
				break;

			case 0x0E:  // Patch: copies the bytes in the lines after this one
				{
					const u32 dest = PSPARResolveAddress(addr);
					const u32 src = PSPAR_SELF_BASE + (u32)(i + 1) * 8;
					for (u32 b = 0; b < value && (i + 1) * 8 + b < count * 8; b++) {
						PSPARWrite(cheat, dest + b, PSPARRead(cheat, src + b, 1), 1);
					}
				}
				break;

			case 0x0F:  // Memory copy from the offset
				{
					const u32 dest = PSPARResolveAddress(addr) & 0x3FFFFFFF;
					u32 src = offset;
					if (src < PSPAR_SELF_BASE) {
						// TempAR masks the source like an address in a code.
						src = PSPARResolveAddress(src & 0x0FFFFFFF);
					}
					if (src < PSPAR_SELF_BASE && Memory::IsValidRange(dest, value) && Memory::IsValidRange(src & 0x3FFFFFFF, value)) {
						InvalidateICache(dest, value);
						Memory::Memcpy(dest, src & 0x3FFFFFFF, value, "PSPAR");
					} else if (src >= PSPAR_SELF_BASE) {
						for (u32 b = 0; b < value && b < count * 8; b++) {
							PSPARWrite(cheat, dest + b, PSPARRead(cheat, src + b, 1), 1);
						}
					}
				}
				break;

			case 0xC0:  // Loop
				repeatValue = value;
				repeatBegin = i;
				executionStatusOld = executionStatus;
				break;

			case 0xC4:  // Safe data store: the offset points at this line
				offset = PSPAR_SELF_BASE + (u32)i * 8;
				break;

			case 0xC6:  // Write offset
				PSPARWrite(cheat, value, offset, 4);
				break;

			case 0xD0:  // Terminator
				executionStatus >>= 1;
				break;

			case 0xD1:  // Loop execute, keeping the Dx registers
			case 0xD2:  // Loop execute / full terminator
				if (repeatValue > 0) {
					executionStatus = executionStatusOld;
					i = repeatBegin;
					repeatValue--;
				} else if (type == 0xD1) {
					executionStatus = 0;
				} else {
					executionStatusOld = 0;
					executionStatus = 0;
					offset = 0;
					dataRegister = 0;
					repeatBegin = 0;
				}
				break;

			case 0xD3:  // Set offset
				offset = value;
				break;
			case 0xD4:  // Add to the data register
				dataRegister += value;
				break;
			case 0xD5:  // Set the data register
				dataRegister = value;
				break;
			case 0xD6:  // 32-bit incrementing write
				PSPARWrite(cheat, value + offset, dataRegister, 4);
				offset += 4;
				break;
			case 0xD7:  // 16-bit incrementing write
				PSPARWrite(cheat, value + offset, dataRegister, 2);
				offset += 2;
				break;
			case 0xD8:  // 8-bit incrementing write
				PSPARWrite(cheat, value + offset, dataRegister, 1);
				offset += 1;
				break;
			case 0xD9:  // 32-bit load
				dataRegister = PSPARRead(cheat, value + offset, 4);
				break;
			case 0xDA:  // 16-bit load
				dataRegister = PSPARRead(cheat, value + offset, 2);
				break;
			case 0xDB:  // 8-bit load
				dataRegister = PSPARRead(cheat, value + offset, 1);
				break;
			case 0xDC:  // Add to the offset
				offset += value;
				break;

			default:
				// 0xCF (folders and comments) and the TempAR extensions.
				break;
			}
		}

		// Data lines of a patch (and of TempAR's 0xC2) are never run as codes, nor are 0xC1's arguments.
		if (type == 0x0E || type == 0xC2) {
			i += value / 8 + (value % 8 != 0 ? 1 : 0);
		} else if (type == 0xC1) {
			i += 2;
		}

		// At the end of the cheat, a pending loop repeats without a terminator.
		if (i + 1 >= count && repeatValue > 0) {
			executionStatus = executionStatusOld;
			i = repeatBegin;
			repeatValue--;
		}
	}
}

void CWCheatEngine::Run() {
	if (Achievements::HardcoreModeActive()) {
		return;
	}

	for (CheatCode &cheat : cheats_) {
		if (cheat.format == CheatCodeFormat::PSPAR) {
			RunPSPAR(cheat);
			continue;
		}

		// InterpretNextOp and ExecuteOp move i.
		for (size_t i = 0; i < cheat.lines.size(); ) {
			CheatOperation op = InterpretNextCwCheat(cheat, i);
			ExecuteOp(op, cheat, i);
		}
	}
}

bool CWCheatEngine::HasCheats() {
	return !cheats_.empty();
}

bool CheatsInEffect() {
	if (!cheatEngine || !cheatsEnabled || Achievements::HardcoreModeActive())
		return false;
	return cheatEngine->HasCheats();
}

bool DetectCheatTitle(std::string_view name, std::string_view *title) {
	// Use Saramagrean title convention (the most common cheats.db file).
	const size_t firstOffset = name.find("_[>>>");
	const size_t secondOffset = name.rfind("<<<]_");
	const bool isTitle = firstOffset != std::string::npos && secondOffset != std::string::npos && firstOffset < secondOffset;
	if (!isTitle) {
		return false;
	}
	// Extract the title substring.
	*title = std::string_view(name.data() + firstOffset + 5, secondOffset - (firstOffset + 5));
	return true;
}

bool DetectCheatPostComment(std::string_view name, std::string_view *comment) {
	const size_t firstOffset = name.find("\xE2\x86\x91[#");  // Up arrow and [#
	const size_t secondOffset = name.rfind(']');
	const bool isPostComment = firstOffset != std::string::npos && secondOffset != std::string::npos && secondOffset > firstOffset;
	if (!isPostComment) {
		return false;
	}
	*comment = std::string_view(name.data() + firstOffset + 5, secondOffset - (firstOffset + 5));
	return true;
}
