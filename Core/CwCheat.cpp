#include "i18n/i18n.h"
#include "UI/OnScreenDisplay.h"
#include "Common/StringUtils.h"
#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/CwCheat.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPS.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/MIPS/JitCommon/NativeJit.h"

#ifdef _WIN32
#include "util/text/utf8.h"
#endif

static int CheatEvent = -1;
std::string gameTitle;
std::string activeCheatFile;
static CWCheatEngine *cheatEngine;
static bool cheatsEnabled;
void hleCheat(u64 userdata, int cyclesLate);
void trim2(std::string& str);

static void __CheatStop() {
	if (cheatEngine != 0) {
		cheatEngine->Exit();
		delete cheatEngine;
		cheatEngine = 0;
	}
	cheatsEnabled = false;
}

static void __CheatStart() {
	__CheatStop();

	gameTitle = g_paramSFO.GetValueString("DISC_ID");

	activeCheatFile = GetSysDirectory(DIRECTORY_CHEATS) + gameTitle + ".ini";
	File::CreateFullPath(GetSysDirectory(DIRECTORY_CHEATS));

	if (!File::Exists(activeCheatFile)) {
		FILE *f = File::OpenCFile(activeCheatFile, "wb");
		if (f) {
			fwrite("\xEF\xBB\xBF", 1, 3, f);
			fclose(f);
		}
		if (!File::Exists(activeCheatFile)) {
			I18NCategory *err = GetI18NCategory("Error");
			osm.Show(err->T("Unable to create cheat file, disk may be full"));
		}

	}

	cheatEngine = new CWCheatEngine();
	cheatEngine->CreateCodeList();
	g_Config.bReloadCheats = false;
	cheatsEnabled = true;
}

void __CheatInit() {
	// Always register the event, want savestates to be compatible whether cheats on or off.
	CheatEvent = CoreTiming::RegisterEvent("CheatEvent", &hleCheat);

	if (g_Config.bEnableCheats) {
		__CheatStart();
	}

	int refresh = g_Config.iCwCheatRefreshRate;

	// Only check once a second for cheats to be enabled.
	CoreTiming::ScheduleEvent(msToCycles(cheatsEnabled ? refresh : 1000), CheatEvent, 0);
}

void __CheatShutdown() {
	__CheatStop();
}

void __CheatDoState(PointerWrap &p) {
	auto s = p.Section("CwCheat", 0, 2);
	if (!s) {
		return;
	}

	p.Do(CheatEvent);
	CoreTiming::RestoreRegisterEvent(CheatEvent, "CheatEvent", &hleCheat);

	int refresh = g_Config.iCwCheatRefreshRate;

	if (s < 2) {
		// Before this we didn't have a checkpoint, so reset didn't work.
		// Let's just force one in.
		CoreTiming::RemoveEvent(CheatEvent);
		CoreTiming::ScheduleEvent(msToCycles(cheatsEnabled ? refresh : 1000), CheatEvent, 0);
	}
}

void hleCheat(u64 userdata, int cyclesLate) {
	if (cheatsEnabled != g_Config.bEnableCheats) {
		// Okay, let's move to the desired state, then.
		if (g_Config.bEnableCheats) {
			__CheatStart();
		} else {
			__CheatStop();
		}
	}

	int refresh = g_Config.iCwCheatRefreshRate;

	// Only check once a second for cheats to be enabled.
	CoreTiming::ScheduleEvent(msToCycles(cheatsEnabled ? refresh : 1000), CheatEvent, 0);

	if (!cheatEngine || !cheatsEnabled)
		return;
	
	if (g_Config.bReloadCheats) { //Checks if the "reload cheats" button has been pressed.
		cheatEngine->CreateCodeList();
		g_Config.bReloadCheats = false;
	}
	cheatEngine->Run();
}

CWCheatEngine::CWCheatEngine() {

}

void CWCheatEngine::Exit() {
	exit2 = true;
}

// Takes a single code line and creates a two-part vector for each code. Feeds to CreateCodeList
static inline std::vector<std::string> makeCodeParts(const std::vector<std::string>& CodesList) {
	std::string currentcode;
	std::vector<std::string> finalList;
	char split_char = '\n';
	char empty = ' ';
	for (size_t i = 0; i < CodesList.size(); i++) {
		currentcode = CodesList[i];
		for (size_t j=0; j < currentcode.length(); j++) {
			if (currentcode[j] == empty) {
				currentcode[j] = '\n';
			}
		}
		trim2(currentcode);
		std::istringstream iss(currentcode);
		std::string each;
		while (std::getline(iss, each, split_char)) {
			finalList.push_back(each);
		}
	}
	return finalList;
}

void CWCheatEngine::CreateCodeList() { //Creates code list to be used in function GetNextCode
	initialCodesList = GetCodesList();
	std::string currentcode, codename;
	std::vector<std::string> codelist;
	for (size_t i = 0; i < initialCodesList.size(); i ++) {
		if (initialCodesList[i].substr(0,2) == "_S") {
			continue; //Line indicates Disc ID, not needed for cheats
		}
		if (initialCodesList[i].substr(0,2) == "_G") {
			continue; //Line indicates game Title, also not needed for cheats
		}
		if (initialCodesList[i].substr(0,2) == "//") {
			continue; //Line indicates comment, also not needed for cheats.
		}
		if (initialCodesList[i].substr(0,3) == "_C1") {
			cheatEnabled = true;
			codename = initialCodesList[i];
			codename.erase (codename.begin(), codename.begin()+4);
			codeNameList.push_back(codename); //Import names for GUI, will be implemented later.
			continue;
		}
		if (initialCodesList[i].substr(0,2) == "_L") {
			if (cheatEnabled == true) {
				currentcode = initialCodesList[i];
				currentcode.erase(currentcode.begin(), currentcode.begin() + 3);
				codelist.push_back(currentcode);
			}
			continue;
		}
		if (initialCodesList[i].substr(0,3) == "_C0") {
			cheatEnabled = false;
			codename = initialCodesList[i];
			codename.erase (codename.begin(), codename.begin()+4);
			codeNameList.push_back(codename); //Import names for GUI, will be implemented later.
			continue;
		}
	}
	parts = makeCodeParts(codelist);
}

std::vector<int> CWCheatEngine::GetNextCode() { // Feeds a size-2 vector of ints to Run() which contains the address and value of one cheat.
	std::string code1;
	std::string code2;
	std::vector<std::string> splitCode;
	std::vector<int> finalCode;
	std::string modifier2 = "0";
	while (true)  {
		if (currentCode >= parts.size()) {
			code1.clear();
			code2.clear();
			break;
		}
		code1 = parts[currentCode++];
		trim2(code1);
		code2 = parts[currentCode++];
		trim2(code2);
		splitCode.push_back(code1);
		splitCode.push_back(code2);

		int var1 = (int) parseHexLong(splitCode[0]);
		int var2 = (int) parseHexLong(splitCode[1]);
		finalCode.push_back(var1);
		finalCode.push_back(var2);
		if (splitCode[0].substr(0,1) == modifier2) {
			break;
		}
	}
	return finalCode;
}

void CWCheatEngine::SkipCodes(int count) {
	for (int i = 0; i < count; i ++) {
		auto code = GetNextCode();
		if (code.empty())
		{
			WARN_LOG(COMMON, "CWCHEAT: Tried to skip more codes than there are, the cheat is most likely wrong");
			break;
		}
		if (code[0] == 0) {
			break;
		}
	}
}

void CWCheatEngine::SkipAllCodes() {
	currentCode = codes.size() - 1;
}

int CWCheatEngine::GetAddress(int value) { //Returns static address used by ppsspp. Some games may not like this, and causes cheats to not work without offset
	int address = (value + 0x08800000) & 0x3FFFFFFF;
	if (gameTitle == "ULUS10563" || gameTitle == "ULJS-00351" || gameTitle == "NPJH50352" ) //Offset to make God Eater Burst codes work
		address -= 0x7EF00;
	return address;
}


inline void trim2(std::string& str) {
	size_t pos = str.find_last_not_of(' ');
	if(pos != std::string::npos) {
		str.erase(pos + 1);
		pos = str.find_first_not_of(' ');
		if(pos != std::string::npos) str.erase(0, pos);
	}
	else str.erase(str.begin(), str.end());
}

std::vector<std::string> CWCheatEngine::GetCodesList() { //Reads the entire cheat list from the appropriate .ini.
	std::string line;
	std::vector<std::string> codesList;  // Read from INI here
#ifdef _WIN32
	std::ifstream list(ConvertUTF8ToWString(activeCheatFile));
#else
	std::ifstream list(activeCheatFile.c_str());
#endif
	if (!list) {
		return codesList;
	}
	for (int i = 0; !list.eof(); i ++) {
		getline(list, line, '\n');
		if (line.length() > 3 && (line.substr(0,1) == "_"||line.substr(0,2) == "//")){
			codesList.push_back(line);
		}
	}
	for(size_t i = 0; i < codesList.size(); i++) {
		trim2(codesList[i]);
	}
	return codesList;
}

void CWCheatEngine::InvalidateICache(u32 addr, int size) {
	if (MIPSComp::jit) {
		MIPSComp::jit->GetBlockCache()->InvalidateICache(addr & ~3, size);
	}
}

void CWCheatEngine::Run() {
	exit2 = false;
	while (!exit2) {
		currentCode = 0;

		while (true) {
			std::vector<int> code = GetNextCode();
			if (code.size() < 2) {
				Exit();
				break;
			}

			int value;
			unsigned int comm = code[0];
			u32 arg = code[1];
			int addr = GetAddress(comm & 0x0FFFFFFF);

			switch (comm >> 28) {
			case 0: // 8-bit write.But need more check
				if (Memory::IsValidAddress(addr)) {
					InvalidateICache(addr & ~3, 4);
					if (arg < 0x00000100) // 8-bit 
						Memory::Write_U8((u8) arg, addr);
					else if (arg < 0x00010000) // 16-bit
						Memory::Write_U16((u16) arg, addr);
					else // 32-bit
						Memory::Write_U32((u32) arg, addr);
				}
				break;
			case 0x1: // 16-bit write
				if (Memory::IsValidAddress(addr)) {
					InvalidateICache(addr & ~3, 4);
					Memory::Write_U16((u16) arg, addr);
				}
				break;
			case 0x2: // 32-bit write
				if (Memory::IsValidAddress(addr)){
					InvalidateICache(addr & ~3, 4);
					Memory::Write_U32((u32) arg, addr);
				}
				break;
			case 0x3: // Increment/Decrement
				{
					addr = GetAddress(arg & 0x0FFFFFFF);
					InvalidateICache(addr & ~3, 4);
					value = 0;
					int increment = 0;
					// Read value from memory
					switch ((comm >> 20) & 0xF) {
					case 1:
					case 2: // 8-bit
						value = Memory::Read_U8(addr);
						increment = comm & 0xFF;
						break;
					case 3:
					case 4: // 16-bit
						value = Memory::Read_U16(addr);
						increment = comm & 0xFFFF;
						break;
					case 5:
					case 6: // 32-bit
						value = Memory::Read_U32(addr);
						code = GetNextCode();
						if (code[0] != 0) {
							increment = code[0];
						}
						break;
					}
					// Increment/Decrement value
					switch ((comm >> 20) & 0xF) {
					case 1:
					case 3:
					case 5: // increment
						value += increment;
						break;
					case 2:
					case 4:
					case 6: // Decrement
						value -= increment;
						break;
					}
					// Write value back to memory
					switch ((comm >> 20) & 0xF) {
					case 1:
					case 2: // 8-bit
						Memory::Write_U8((u8) value, addr);
						break;
					case 3:
					case 4: // 16-bit
						Memory::Write_U16((u16) value, addr);
						break;
					case 5:
					case 6: // 32-bit
						Memory::Write_U32((u32) value, addr);
						break;
					}
					break;
				}
			case 0x4: // 32-bit patch code
				code = GetNextCode();
				if (true) {
					int data = code[0];
					int dataAdd = code[1];

					int count = (arg >> 16) & 0xFFFF;
					int stepAddr = (arg & 0xFFFF) * 4;

					InvalidateICache(addr, count * stepAddr);
					for (int a  = 0; a < count; a++) {
						if (Memory::IsValidAddress(addr)) {
							Memory::Write_U32((u32) data, addr);
						}
						addr += stepAddr;
						data += dataAdd;
					}
				}
				break;
			case 0x5: // Memcpy command
				code = GetNextCode();
				if (true) {
					int destAddr = GetAddress(code[0]);
					int len = arg;
					InvalidateICache(destAddr, len);
					if (Memory::IsValidAddress(addr) && Memory::IsValidAddress(destAddr)) {
						Memory::MemcpyUnchecked(destAddr, addr, len);
					}
				}
				break;
			case 0x6: // Pointer commands
				code = GetNextCode();
				if (code.size() >= 2) {
					int arg2 = code[0];
					int offset = code[1];
					int baseOffset = (arg2 >> 20) * 4;
					InvalidateICache(addr + baseOffset, 4);
					int base = Memory::Read_U32(addr + baseOffset);
					int count = arg2 & 0xFFFF;
					int type = (arg2 >> 16) & 0xF;
					for (int i = 1; i < count; i ++ ) {
						if (i+1 < count) {
							code = GetNextCode();
							if (code.size() < 2) {
								// Code broken. Should warn but would be very spammy...
								break;
							}
							int arg3 = code[0];
							int arg4 = code[1];
							int comm3 = arg3 >> 28;
							switch (comm3) {
							case 0x1: // type copy byte
								{
									int srcAddr = Memory::Read_U32(addr) + offset;
									int dstAddr = Memory::Read_U16(addr + baseOffset) + (arg3 & 0x0FFFFFFF);
									if (Memory::IsValidAddress(dstAddr) && Memory::IsValidAddress(srcAddr)) {
										Memory::MemcpyUnchecked(dstAddr, srcAddr, arg);
									}
									type = -1; //Done
									break; }
							case 0x2:
							case 0x3: // type pointer walk
								{
									int walkOffset = arg3 & 0x0FFFFFFF;
									if (comm3 == 0x3) {
										walkOffset = -walkOffset;
									}
									base = Memory::Read_U32(base + walkOffset);
									int comm4 = arg4 >> 28;
									switch (comm4) {
									case 0x2:
									case 0x3: // type pointer walk
										walkOffset = arg4 & 0x0FFFFFFF;
										if (comm4 == 0x3) {
											walkOffset = -walkOffset;
										}
										base = Memory::Read_U32(base + walkOffset);
										break;
									}
									break; }
							case 0x9: // type multi address write
								base += arg3 & 0x0FFFFFFF;
								arg += arg4;
								break;
							default:
								break;

							}
						}
					}

					switch (type) {
					case 0: // 8 bit write
						Memory::Write_U8((u8) arg, base + offset);
						break;
					case 1: // 16-bit write
						Memory::Write_U16((u16) arg, base + offset);
						break;
					case 2: // 32-bit write
						Memory::Write_U32((u32) arg, base + offset);
						break;
					case 3: // 8 bit inverse write
						Memory::Write_U8((u8) arg, base - offset);
						break;
					case 4: // 16-bit inverse write
						Memory::Write_U16((u16) arg, base - offset);
						break;
					case 5: // 32-bit inverse write
						Memory::Write_U32((u32) arg, base - offset);
						break;
					case -1: // Operation already performed, nothing to do
						break;
					}
				}
				break;
			case 0x7: // Boolean commands.
				switch (arg >> 16) {
				case 0x0000: // 8-bit OR.
					if (Memory::IsValidAddress(addr)) {
						InvalidateICache(addr & ~3, 4);
						int val1 = (int) (arg & 0xFF);
						int val2 = (int) Memory::Read_U8(addr);
						Memory::Write_U8((u8) (val1 | val2), addr);
					}
					break;
				case 0x0002: // 8-bit AND.
					if (Memory::IsValidAddress(addr)) {
						InvalidateICache(addr & ~3, 4);
						int val1 = (int) (arg & 0xFF);
						int val2 = (int) Memory::Read_U8(addr);
						Memory::Write_U8((u8) (val1 & val2), addr);
					}
					break;
				case 0x0004: // 8-bit XOR.
					if (Memory::IsValidAddress(addr)) {
						InvalidateICache(addr & ~3, 4);
						int val1 = (int) (arg & 0xFF);
						int val2 = (int) Memory::Read_U8(addr);
						Memory::Write_U8((u8) (val1 ^ val2), addr);
					}
					break;
				case 0x0001: // 16-bit OR.
					if (Memory::IsValidAddress(addr)) {
						InvalidateICache(addr & ~3, 4);
						short val1 = (short) (arg & 0xFFFF);
						short val2 = (short) Memory::Read_U16(addr);
						Memory::Write_U16((u16) (val1 | val2), addr);
					}
					break;
				case 0x0003: // 16-bit AND.
					if (Memory::IsValidAddress(addr)) {
						InvalidateICache(addr & ~3, 4);
						short val1 = (short) (arg & 0xFFFF);
						short val2 = (short) Memory::Read_U16(addr);
						Memory::Write_U16((u16) (val1 & val2), addr);
					}
					break;
				case 0x0005: // 16-bit OR.
					if (Memory::IsValidAddress(addr)) {
						InvalidateICache(addr & ~3, 4);
						short val1 = (short) (arg & 0xFFFF);
						short val2 = (short) Memory::Read_U16(addr);
						Memory::Write_U16((u16) (val1 ^ val2), addr);
					}
					break;
				}
				break;
			case 0x8: // 8-bit and 16-bit patch code
				code = GetNextCode();
				if (code.size() >= 2) {
					int data = code[0];
					int dataAdd = code[1];

					bool is8Bit = (data >> 16) == 0x0000;
					int count = (arg >> 16) & 0xFFFF;
					int stepAddr = (arg & 0xFFFF) * (is8Bit ? 1 : 2);
					InvalidateICache(addr, count * stepAddr);
					for (int a = 0; a < count; a++) {
						if (Memory::IsValidAddress(addr)) {
							if (is8Bit) {
								Memory::Write_U8((u8) (data & 0xFF), addr);
							}
							else {
								Memory::Write_U16((u16) (data & 0xFFFF), addr);
							}
						}
						addr += stepAddr;
						data += dataAdd;
					}
				}
				break;
			case 0xB: // Time command (not sure what to do?)
				break;
			case 0xC: // Code stopper
				if (Memory::IsValidAddress(addr)) { 
					InvalidateICache(addr, 4);
					value = Memory::Read_U32(addr);
					if ((u32)value != arg) {
						SkipAllCodes();
					}
				}
				break;
			case 0xD: // Test commands & Jocker codes
				if ((arg >> 28) == 0x0 || (arg >> 28) == 0x2) { // 8Bit & 16Bit ignore next line cheat code
					bool is8Bit = (arg >> 28) == 0x2;
					addr = GetAddress(comm & 0x0FFFFFFF);
					if (Memory::IsValidAddress(addr)) {
						int memoryValue = is8Bit ? Memory::Read_U8(addr) : Memory::Read_U16(addr);
						int testValue = arg & (is8Bit ? 0xFF : 0xFFFF);
						bool executeNextLines = false;
						switch ((arg >> 20) & 0xF) {
						case 0x0: // Equal
							executeNextLines = memoryValue == testValue;
							break;
						case 0x1: // Not Equal
							executeNextLines = memoryValue != testValue;
							break;
						case 0x2: // Less Than
							executeNextLines = memoryValue < testValue;
							break;
						case 0x3: // Greater Than
							executeNextLines = memoryValue > testValue;
							break;
						default:
							break;
						}
						if (!executeNextLines)
							SkipCodes(1);
					}
					break;
				}
				else if ((arg >> 28) == 0x1 || (arg >> 28) == 0x3) { // Buttons dependent ignore cheat code
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
					u32 buttonStatus = __CtrlPeekButtons();
					int skip = (comm & 0xFF) + 1;
					u32 mask = arg & 0x0FFFFFFF;
					if ((arg >> 28) == 0x1)
						// Old, too specific check: if (buttonStatus == (arg & 0x0FFFFFFF))	// cheat code likes: 0xD00000nn 0x1bbbbbbb;
						if ((buttonStatus & mask) == mask)	// cheat code likes: 0xD00000nn 0x1bbbbbbb;
							break;
						else
							SkipCodes(skip);
					else // (arg >> 28) == 2?
						// Old, too specific check: if (buttonStatus != (arg & 0x0FFFFFFF))	// cheat code likes: 0xD00000nn 0x3bbbbbbb;
						if ((buttonStatus & mask) == mask)	// cheat code likes: 0xD00000nn 0x3bbbbbbb;
							SkipCodes(skip);
						else
							break;
					break;
				}
				else if ((arg >> 28) == 0x4 || (arg >> 28) == 0x5 || (arg >> 28) == 0x6 || (arg >> 28) == 0x7) {
					int addr1 = GetAddress(comm & 0x0FFFFFFF);
					int addr2 = GetAddress(arg & 0x0FFFFFFF);
					code = GetNextCode();
					if (code.size() >= 2)
						if (Memory::IsValidAddress(addr1) && Memory::IsValidAddress(addr2)) {
							int comm2 = code[0];
							int arg2 = code[1];
							int skip = (comm2 & 0xFFFFFFFF);
							int memoryValue1 = 0;
							int memoryValue2 = 0;
							switch (arg2 & 0xF) {
							case 0x0: // 8Bit
								memoryValue1 = Memory::Read_U8(addr1);
								memoryValue2 = Memory::Read_U8(addr2);
								break;
							case 0x1: // 16Bit
								memoryValue1 = Memory::Read_U16(addr1);
								memoryValue2 = Memory::Read_U16(addr2);
								break;
							case 0x2: // 32Bit
								memoryValue1 = Memory::Read_U32(addr1);
								memoryValue2 = Memory::Read_U32(addr2);
								break;
							default:
								break;
							}
							switch (arg >> 28) {
							case 0x4: // Equal
								if (memoryValue1 != memoryValue2)
									SkipCodes(skip);
								break;
							case 0x5: // Not Equal
								if (memoryValue1 == memoryValue2)
									SkipCodes(skip);
								break;
							case 0x6: // Less Than
								if (memoryValue1 >= memoryValue2)
									SkipCodes(skip);
								break;
							case 0x7: // Greater Than
								if (memoryValue1 <= memoryValue2)
									SkipCodes(skip);
								break;
							default:
								break;
							}
						}
				}
				else
					break;
			case 0xE: // Test commands, multiple skip
				{
					bool is8Bit = (comm >> 24) == 0xE1;
					addr = GetAddress(arg & 0x0FFFFFFF);
					if (Memory::IsValidAddress(addr)) {
						int memoryValue = is8Bit ? Memory::Read_U8(addr) : Memory::Read_U16(addr);
						int testValue = comm & (is8Bit ? 0xFF : 0xFFFF);
						bool executeNextLines = false;
						switch ( arg >> 28) {
						case 0x0: // Equal
							executeNextLines = memoryValue == testValue;
							break;
						case 0x1: // Not Equal
							executeNextLines = memoryValue != testValue;
							break;
						case 0x2: // Less Than
							executeNextLines = memoryValue < testValue;
							break;
						case 0x3: // Greater Than
							executeNextLines = memoryValue > testValue;
							break;
						}
						if (!executeNextLines) {
							int skip = (comm >> 16) & (is8Bit ? 0xFF : 0xFFF);
							SkipCodes(skip);
						}
					}
					break;
				}
			default:
				{
					break;
				}
			}
		}
	}
	// exiting...
	Exit();
}

bool CWCheatEngine::HasCheats() {
	return !parts.empty();
}

bool CheatsInEffect() {
	if (!cheatEngine || !cheatsEnabled)
		return false;
	return cheatEngine->HasCheats();
}

