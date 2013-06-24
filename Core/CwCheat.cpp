#include "CwCheat.h"
#include "../Core/CoreTiming.h"
#include "../Core/CoreParameter.h"
#include "StringUtils.h"
#include "Common/FileUtil.h"
#include "Config.h"
#include "MIPS/MIPS.h"
#include "Core/Config.h"

const static std::string CHEATS_DIR = "cheats";

static std::string activeCheatFile;
static int CheatEvent = -1;
std::string gameTitle;
static CWCheatEngine *cheatEngine;

void hleCheat(u64 userdata, int cyclesLate);
void trim2(std::string& str);

void __CheatInit() {
	//Moved createFullPath to CheatInit from the constructor because it spams the log and constantly checks if exists. In here, only checks once.
	gameTitle = g_paramSFO.GetValueString("DISC_ID");
	activeCheatFile = CHEATS_DIR + "/" + gameTitle +".ini";

	File::CreateFullPath(CHEATS_DIR);
	if (g_Config.bEnableCheats) {
		if (!File::Exists(activeCheatFile)) {
			File::CreateEmptyFile(activeCheatFile);
		}

		cheatEngine = new CWCheatEngine();

		cheatEngine->CreateCodeList();
		g_Config.bReloadCheats = false;
		CheatEvent = CoreTiming::RegisterEvent("CheatEvent", &hleCheat);
		CoreTiming::ScheduleEvent(msToCycles(77), CheatEvent, 0);
	}
}

void __CheatShutdown() {
	if (cheatEngine != 0) {
		cheatEngine->Exit();
		delete cheatEngine;
		cheatEngine = 0;
	}
}

void hleCheat(u64 userdata, int cyclesLate) {
	CoreTiming::ScheduleEvent(msToCycles(77), CheatEvent, 0);

	if (!cheatEngine)
		return;
	
	if (g_Config.bReloadCheats) { //Checks if the "reload cheats" button has been pressed.
		cheatEngine->CreateCodeList();
		g_Config.bReloadCheats = false;
	}
	if (g_Config.bEnableCheats) {
		cheatEngine->Run();
	}
}

CWCheatEngine::CWCheatEngine() {

}

void CWCheatEngine::Exit() {
	exit2 = true;
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
			continue; //Line indicates game Title, also not needed for cheats.
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
inline std::vector<std::string> makeCodeParts(std::vector<std::string> CodesList) { //Takes a single code line and creates a two-part vector for each code. Feeds to CreateCodeList
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
		if (GetNextCode()[0] == 0) {
			break;
		}
	}
}

void CWCheatEngine::SkipAllCodes() {
	currentCode = codes.size();
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
	std::ifstream list(activeCheatFile.c_str());
	for (int i = 0; !list.eof(); i ++) {
		getline(list, line, '\n');
		codesList.push_back(line);
	}
	for(size_t i = 0; i < codesList.size(); i++) {
		trim2(codesList[i]);
	}
	return codesList;
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
			int arg = code[1];
			int addr = GetAddress(comm & 0x0FFFFFFF);

			switch (comm >> 28) {
			case 0: // 8-bit write.
				if (Memory::IsValidAddress(addr)){
					Memory::Write_U8((u8) arg, addr);
				}
				break;
			case 0x1: // 16-bit write
				if (Memory::IsValidAddress(addr)){
					Memory::Write_U16((u16) arg, addr);
				}
				break;
			case 0x2: // 32-bit write
				if (Memory::IsValidAddress(addr)){
					Memory::Write_U32((u32) arg, addr);
				}
				break;
			case 0x3: // Increment/Decrement
				{
					addr = GetAddress(arg);
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
						if ( code[0] != NULL) {
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
				if (code[0] != 0) {
					int data = code[0];
					int dataAdd = code[1];

					int maxAddr = (arg >> 16) & 0xFFFF;
					int stepAddr = (arg & 0xFFFF) * 4;
					for (int a  = 0; a < maxAddr; a++) {
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
				if (code[0] != NULL) {
					int destAddr = code[0];
					if (Memory::IsValidAddress(addr) && Memory::IsValidAddress(destAddr)) {
						Memory::Memcpy(destAddr, Memory::GetPointer(addr), arg);
					}
				}
				break;
			case 0x6: // Pointer commands
				code = GetNextCode();
				if (code[0] != NULL) {
					int arg2 = code[0];
					int offset = code[1];
					int baseOffset = (arg2 >> 20) * 4;
					int base = Memory::Read_U32(addr + baseOffset);
					int count = arg2 & 0xFFFF;
					int type = (arg2 >> 16) & 0xF;
					for (int i = 1; i < count; i ++ ) {
						if (i+1 < count) {
							code = GetNextCode();
							int arg3 = code[0];
							int arg4 = code[1];
							int comm3 = arg3 >> 28;
							switch (comm3) {
							case 0x1: // type copy byte
								{
									int srcAddr = Memory::Read_U32(addr) + offset;
									int dstAddr = Memory::Read_U16(addr + baseOffset) + (arg3 & 0x0FFFFFFF);
									Memory::Memcpy(dstAddr, Memory::GetPointer(srcAddr), arg);
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
						int val1 = (int) (arg & 0xFF);
						int val2 = (int) Memory::Read_U8(addr);
						Memory::Write_U8((u8) (val1 | val2), addr);
					}
					break;
				case 0x0002: // 8-bit AND.
					if (Memory::IsValidAddress(addr)) {
						int val1 = (int) (arg & 0xFF);
						int val2 = (int) Memory::Read_U8(addr);
						Memory::Write_U8((u8) (val1 & val2), addr);
					}
					break;
				case 0x0004: // 8-bit XOR.
					if (Memory::IsValidAddress(addr)) {
						int val1 = (int) (arg & 0xFF);
						int val2 = (int) Memory::Read_U8(addr);
						Memory::Write_U8((u8) (val1 ^ val2), addr);
					}
					break;
				case 0x0001: // 16-bit OR.
					if (Memory::IsValidAddress(addr)) {
						short val1 = (short) (arg & 0xFFFF);
						short val2 = (short) Memory::Read_U16(addr);
						Memory::Write_U16((u16) (val1 | val2), addr);
					}
					break;
				case 0x0003: // 16-bit AND.
					if (Memory::IsValidAddress(addr)) {
						short val1 = (short) (arg & 0xFFFF);
						short val2 = (short) Memory::Read_U16(addr);
						Memory::Write_U16((u16) (val1 & val2), addr);
					}
					break;
				case 0x0005: // 16-bit OR.
					if (Memory::IsValidAddress(addr)) {
						short val1 = (short) (arg & 0xFFFF);
						short val2 = (short) Memory::Read_U16(addr);
						Memory::Write_U16((u16) (val1 ^ val2), addr);
					}
					break;
				}
				break;
			case 0x8: // 8-bit and 16-bit patch code
				code = GetNextCode();
				if (code[0] != NULL) {
					int data = code[0];
					int dataAdd = code[1];

					bool is8Bit = (data >> 16) == 0x0000;
					int maxAddr = (arg >> 16) & 0xFFFF;
					int stepAddr = (arg & 0xFFFF) * (is8Bit ? 1 : 2);
					for (int a = 0; a < maxAddr; a++) {
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
					value = Memory::Read_U32(addr);
					if (value != arg) {
						SkipAllCodes();
					}
				}
				break;
			case 0xD: // Test commands & Jocker codes ( Someone will have to help me with these)
				break;
			case 0xE: // Test commands, multiple skip
				{
					bool is8Bit = (comm >> 24) == 0x1;
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
			}
		}
	}
	// exiting...
	Exit();
}



