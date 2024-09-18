#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>

#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/System/OSD.h"
#include "Common/File/FileUtil.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/CwCheat.h"
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

static int CheatEvent = -1;
static CWCheatEngine *cheatEngine;
static bool cheatsEnabled;
using namespace SceCtrl;

void hleCheat(u64 userdata, int cyclesLate);

static inline std::string TrimString(const std::string &s) {
	auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) {
		// isspace() expects 0 - 255, so convert any sign-extended value.
	   return std::isspace(c & 0xFF);
   });
   auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){
	   return std::isspace(c & 0xFF);
   }).base();
   return wsback > wsfront ? std::string(wsfront, wsback) : std::string();
}

class CheatFileParser {
public:
	CheatFileParser(const Path &filename, const std::string &gameID = "") {
		fp_ = File::OpenCFile(filename, "rt");
		validGameID_ = ReplaceAll(gameID, "-", "");
	}
	~CheatFileParser() {
		if (fp_)
			fclose(fp_);
	}

	bool Parse();

	std::vector<std::string> GetErrors() const {
		return errors_;
	}

	std::vector<CheatCode> GetCheats() const {
		return cheats_;
	}

	std::vector<CheatFileInfo> GetFileInfo() const {
		return cheatInfo_;
	}

protected:
	void Flush();
	void FlushCheatInfo();
	void AddError(const std::string &msg);
	void ParseLine(const std::string &line);
	void ParseDataLine(const std::string &line, CheatCodeFormat format);
	bool ValidateGameID(const std::string &gameID);

	FILE *fp_ = nullptr;
	std::string validGameID_;

	int line_ = 0;
	int games_ = 0;
	std::vector<std::string> errors_;
	std::vector<CheatFileInfo> cheatInfo_;
	std::vector<CheatCode> cheats_;
	std::vector<CheatLine> pendingLines_;
	CheatCodeFormat codeFormat_ = CheatCodeFormat::UNDEFINED;
	CheatFileInfo lastCheatInfo_;
	bool gameEnabled_ = true;
	bool gameRiskyEnabled_ = false;
	bool cheatEnabled_ = false;
};

bool CheatFileParser::Parse() {
	for (line_ = 1; fp_ && !feof(fp_); ++line_) {
		char temp[2048];
		char *tempLine = fgets(temp, sizeof(temp), fp_);
		if (!tempLine)
			continue;

		// Detect UTF-8 BOM sequence, and ignore it.
		if (line_ == 1 && memcmp(tempLine, "\xEF\xBB\xBF", 3) == 0)
			tempLine += 3;
		std::string line = TrimString(tempLine);

		// Minimum length 5 is shortest possible _ lines name of the game "_G N+"
		// and a minimum of 1 displayable character in cheat name string "_C0 1"
		// which both equal to 5 characters.
		if (line.length() >= 5 && line[0] == '_') {
			ParseLine(line);
		} else if (line.length() >= 2 && line[0] == '/' && line[1] == '/') {
			// Comment, ignore.
		} else if (line.length() >= 1 && line[0] == '#') {
			// Comment, ignore.
		} else if (line.length() > 0) {
			errors_.push_back(StringFromFormat("Unrecognized content on line %d: expecting _", line_));
		}
	}

	Flush();

	return errors_.empty();
}

void CheatFileParser::Flush() {
	if (!pendingLines_.empty()) {
		FlushCheatInfo();
		cheats_.push_back({ codeFormat_, pendingLines_ });
		pendingLines_.clear();
	}
	codeFormat_ = CheatCodeFormat::UNDEFINED;
}

void CheatFileParser::FlushCheatInfo() {
	if (lastCheatInfo_.lineNum != 0) {
		cheatInfo_.push_back(lastCheatInfo_);
		lastCheatInfo_ = { 0 };
	}
}

void CheatFileParser::AddError(const std::string &err) {
	errors_.push_back(StringFromFormat("Error on line %d: %s", line_, err.c_str()));
}

void CheatFileParser::ParseLine(const std::string &line) {
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
			lastCheatInfo_ = { line_, line.length() >= 5 ? line.substr(4) : "", true };
			cheatEnabled_ = true;
		} else if (line.length() >= 3 && line[2] == '0') {
			lastCheatInfo_ = { line_, line.length() >= 5 ? line.substr(4) : "", false };
			cheatEnabled_ = false;
		} else {
			AddError("could not parse cheat name line");
			cheatEnabled_ = false;
			return;
		}
		return;

	case 'L':
		// CwCheat data line.
		ParseDataLine(line.substr(2), CheatCodeFormat::CWCHEAT);
		return;

	case 'M':
		// TempAR data line.
		ParseDataLine(line.substr(2), CheatCodeFormat::TEMPAR);
		return;

	default:
		AddError("unknown line type");
		return;
	}
}

void CheatFileParser::ParseDataLine(const std::string &line, CheatCodeFormat format) {
	if (codeFormat_ == CheatCodeFormat::UNDEFINED) {
		codeFormat_ = format;
	} else if (codeFormat_ != format) {
		AddError("mixed code format (cwcheat/tempar)");
		lastCheatInfo_ = { 0 };
		pendingLines_.clear();
		cheatEnabled_ = false;
	}

	if (!gameEnabled_) {
		return;
	}
	if (!cheatEnabled_) {
		FlushCheatInfo();
		return;
	}

	CheatLine cheatLine;
	int len = 0;
	if (sscanf(line.c_str(), "%x %x %n", &cheatLine.part1, &cheatLine.part2, &len) == 2) {
		if ((size_t)len < line.length()) {
			AddError("junk after line data");
		}
		pendingLines_.push_back(cheatLine);
	} else {
		AddError("expecting two values");
	}
}

bool CheatFileParser::ValidateGameID(const std::string &gameID) {
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

	if (!cheatsEnabled)
		refresh = 1000;

	// Horrible hack for Tony Hawk - Underground 2. See #3854. Avoids crashing somehow
	// but still causes regular JIT invalidations which causes stutters.
	if (PSP_CoreParameter().compat.flags().JitInvalidationHack) {
		refresh = 2;
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
}

void __CheatDoState(PointerWrap &p) {
	auto s = p.Section("CwCheat", 0, 2);
	if (!s) {
		CheatEvent = -1;
		CoreTiming::RestoreRegisterEvent(CheatEvent, "CheatEvent", &hleCheat);
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

	if (PSP_CoreParameter().compat.flags().JitInvalidationHack) {
		std::string gameTitle = g_paramSFO.GetValueString("DISC_ID");

		// Horrible hack for Tony Hawk - Underground 2. See #3854. Avoids crashing somehow
		// but still causes regular JIT invalidations which causes stutters.
		if (gameTitle == "ULUS10014") {
			cheatEngine->InvalidateICache(0x08865600, 72);
			cheatEngine->InvalidateICache(0x08865690, 4);
		} else if (gameTitle == "ULES00033" || gameTitle == "ULES00034" || gameTitle == "ULES00035") {  // euro, also 34 and 35
			cheatEngine->InvalidateICache(0x088655D8, 72);
			cheatEngine->InvalidateICache(0x08865668, 4);
		} else if (gameTitle == "ULUS10138") {  // MTX MotoTrax US
			cheatEngine->InvalidateICache(0x0886DCC0, 72);
			cheatEngine->InvalidateICache(0x0886DC20, 4);
			cheatEngine->InvalidateICache(0x0886DD40, 4);
		} else if (gameTitle == "ULES00581") {  // MTX MotoTrax EU (ported from US cwcheat codes)
			cheatEngine->InvalidateICache(0x0886E1D8, 72);
			cheatEngine->InvalidateICache(0x0886E138, 4);
			cheatEngine->InvalidateICache(0x0886E258, 4);
		}
	}

	if (!cheatEngine || !cheatsEnabled)
		return;

	if (g_Config.bReloadCheats) { //Checks if the "reload cheats" button has been pressed.
		cheatEngine->ParseCheats();
		g_Config.bReloadCheats = false;
	}
	cheatEngine->Run();
}

CWCheatEngine::CWCheatEngine(const std::string &gameID) : gameID_(gameID) {
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

Path CWCheatEngine::CheatFilename() {
	return filename_;
}

void CWCheatEngine::ParseCheats() {
	CheatFileParser parser(filename_, gameID_);

	parser.Parse();
	// TODO: Report errors.

	cheats_ = parser.GetCheats();
}

u32 CWCheatEngine::GetAddress(u32 value) {
	// Returns static address used by ppsspp. Some games may not like this, and causes cheats to not work without offset
	u32 address = (value + 0x08800000) & 0x3FFFFFFF;
	return address;
}

std::vector<CheatFileInfo> CWCheatEngine::FileInfo() {
	CheatFileParser parser(filename_, gameID_);

	parser.Parse();
	return parser.GetFileInfo();
}

void CWCheatEngine::InvalidateICache(u32 addr, int size) {
	// Round start down and size up to the nearest word.
	u32 aligned = addr & ~3;
	int alignedSize = (addr + size - aligned + 3) & ~3;
	currentMIPS->InvalidateICache(aligned, alignedSize);
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

CheatOperation CWCheatEngine::InterpretNextTempAR(const CheatCode &cheat, size_t &i) {
	// TODO
	return { CheatOp::Invalid };
}

CheatOperation CWCheatEngine::InterpretNextOp(const CheatCode &cheat, size_t &i) {
	if (cheat.fmt == CheatCodeFormat::CWCHEAT)
		return InterpretNextCwCheat(cheat, i);
	else if (cheat.fmt == CheatCodeFormat::TEMPAR)
		return InterpretNextTempAR(cheat, i);
	else {
		// This shouldn't happen, but apparently does: #14082
		// Either I'm missing a path or we have memory corruption.
		// Not sure whether to log here though, feels like we could end up with a
		// ton of logspam...
		return { CheatOp::Invalid };
	}
}

void CWCheatEngine::ApplyMemoryOperator(const CheatOperation &op, uint32_t(*oper)(uint32_t, uint32_t)) {
	if (Memory::IsValidRange(op.addr, op.sz)) {
		InvalidateICache(op.addr, op.sz);
		if (op.sz == 1)
			Memory::Write_U8((u8)oper(Memory::Read_U8(op.addr), op.val), op.addr);
		else if (op.sz == 2)
			Memory::Write_U16((u16)oper(Memory::Read_U16(op.addr), op.val),op. addr);
		else if (op.sz == 4)
			Memory::Write_U32((u32)oper(Memory::Read_U32(op.addr), op.val), op.addr);
	}
}

bool CWCheatEngine::TestIf(const CheatOperation &op, bool(*oper)(int, int)) {
	if (Memory::IsValidRange(op.addr, op.sz)) {
		InvalidateICache(op.addr, op.sz);

		int memoryValue = 0;
		if (op.sz == 1)
			memoryValue = (int)Memory::Read_U8(op.addr);
		else if (op.sz == 2)
			memoryValue = (int)Memory::Read_U16(op.addr);
		else if (op.sz == 4)
			memoryValue = (int)Memory::Read_U32(op.addr);

		return oper(memoryValue, (int)op.val);
	}
	return false;
}

bool CWCheatEngine::TestIfAddr(const CheatOperation &op, bool(*oper)(int, int)) {
	if (Memory::IsValidRange(op.addr, op.sz) && Memory::IsValidRange(op.ifAddrTypes.compareAddr, op.sz)) {
		InvalidateICache(op.addr, op.sz);
		InvalidateICache(op.addr, op.ifAddrTypes.compareAddr);

		int memoryValue1 = 0;
		int memoryValue2 = 0;
		if (op.sz == 1) {
			memoryValue1 = (int)Memory::Read_U8(op.addr);
			memoryValue2 = (int)Memory::Read_U8(op.ifAddrTypes.compareAddr);
		} else if (op.sz == 2) {
			memoryValue1 = (int)Memory::Read_U16(op.addr);
			memoryValue2 = (int)Memory::Read_U16(op.ifAddrTypes.compareAddr);
		} else if (op.sz == 4) {
			memoryValue1 = (int)Memory::Read_U32(op.addr);
			memoryValue2 = (int)Memory::Read_U32(op.ifAddrTypes.compareAddr);
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
				Memory::Write_U8((u8)op.val, op.addr);
			else if (op.sz == 2)
				Memory::Write_U16((u16)op.val, op.addr);
			else if (op.sz == 4)
				Memory::Write_U32((u32)op.val, op.addr);
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
		if (Memory::IsValidAddress(op.addr)) {
			InvalidateICache(op.addr, op.multiWrite.count * op.multiWrite.step + op.sz);

			uint32_t data = op.val;
			uint32_t addr = op.addr;
			for (uint32_t a = 0; a < op.multiWrite.count; a++) {
				if (Memory::IsValidAddress(addr)) {
					if (op.sz == 1)
						Memory::Write_U8((u8)data, addr);
					else if (op.sz == 2)
						Memory::Write_U16((u16)data, addr);
					else if (op.sz == 4)
						Memory::Write_U32((u32)data, addr);
				}
				addr += op.multiWrite.step;
				data += op.multiWrite.add;
			}
		}
		break;

	case CheatOp::CopyBytesFrom:
		if (Memory::IsValidRange(op.addr, op.val) && Memory::IsValidRange(op.copyBytesFrom.destAddr, op.val)) {
			InvalidateICache(op.addr, op.val);
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
			uint16_t checkLeftVibration = Memory::Read_U16(op.addr);
			uint16_t checkRightVibration = Memory::Read_U16(op.addr + 0x2);
			if (checkLeftVibration > 0) {
				SetLeftVibration(checkLeftVibration);
				SetVibrationLeftDropout(Memory::Read_U8(op.addr + 0x4));
			}
			if (checkRightVibration > 0) {
				SetRightVibration(checkRightVibration);
				SetVibrationRightDropout(Memory::Read_U8(op.addr + 0x6));
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
				value.u = Memory::Read_U32(op.addr);
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
			InvalidateICache(op.addr, 4);
			if (Memory::Read_U32(op.addr) != op.val) {
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
			InvalidateICache(op.addr + op.pointerCommands.baseOffset, 4);
			u32 base = Memory::Read_U32(op.addr + op.pointerCommands.baseOffset);
			u32 val = op.val;
			int type = op.pointerCommands.type;
			for (int a = 0; a < op.pointerCommands.count; ++a) {
				const CheatLine &line = cheat.lines[i++];
				switch (line.part1 >> 28) {
				case 0x1: // type copy byte
					{
						InvalidateICache(op.addr, 4);
						u32 srcAddr = Memory::Read_U32(op.addr) + op.pointerCommands.offset;
						u32 dstAddr = Memory::Read_U32(op.addr + op.pointerCommands.baseOffset) + (line.part1 & 0x0FFFFFFF);
						if (Memory::IsValidRange(dstAddr, val) && Memory::IsValidRange(srcAddr, val)) {
							InvalidateICache(dstAddr, val);
							InvalidateICache(srcAddr, val);
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
						InvalidateICache(base + walkOffset, 4);
						base = Memory::Read_U32(base + walkOffset);
						switch (line.part2 >> 28) {
						case 0x2:
						case 0x3: // type pointer walk
							walkOffset = line.part2 & 0x0FFFFFFF;
							if ((line.part2 >> 28) == 0x3) {
								walkOffset = -walkOffset;
							}
							InvalidateICache(base + walkOffset, 4);
							base = Memory::Read_U32(base + walkOffset);
							break;

						default:
							// Unexpected value in cheat line?
							break;
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
				InvalidateICache(base + op.pointerCommands.offset, 1);
				Memory::Write_U8((u8)val, base + op.pointerCommands.offset);
				break;
			case 1: // 16-bit write
				InvalidateICache(base + op.pointerCommands.offset, 2);
				Memory::Write_U16((u16)val, base + op.pointerCommands.offset);
				break;
			case 2: // 32-bit write
				InvalidateICache(base + op.pointerCommands.offset, 4);
				Memory::Write_U32((u32)val, base + op.pointerCommands.offset);
				break;
			case 3: // 8 bit inverse write
				InvalidateICache(base - op.pointerCommands.offset, 1);
				Memory::Write_U8((u8)val, base - op.pointerCommands.offset);
				break;
			case 4: // 16-bit inverse write
				InvalidateICache(base - op.pointerCommands.offset, 2);
				Memory::Write_U16((u16)val, base - op.pointerCommands.offset);
				break;
			case 5: // 32-bit inverse write
				InvalidateICache(base - op.pointerCommands.offset, 4);
				Memory::Write_U32((u32)val, base - op.pointerCommands.offset);
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

void CWCheatEngine::Run() {
	if (Achievements::HardcoreModeActive()) {
		return;
	}

	for (CheatCode cheat : cheats_) {
		// InterpretNextOp and ExecuteOp move i.
		for (size_t i = 0; i < cheat.lines.size(); ) {
			CheatOperation op = InterpretNextOp(cheat, i);
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
