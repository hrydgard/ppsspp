#include "CwCheat.h"
#include "../Core/CoreTiming.h"
#include "../Core/CoreParameter.h"
using namespace std;

static int CheatEvent = -1;
CheatsGUI member;
CheatsGUI cheatsThread;

void hleCheat(u64 userdata, int cyclesLate);


void __CheatInit() {
	
	CheatEvent = CoreTiming::RegisterEvent("CheatEvent", &hleCheat);
	CoreTiming::ScheduleEvent(msToCycles(600), CheatEvent, 0);
}
void __CheatShutdown() {
	member.Exit();
}

void hleCheat(u64 userdata, int cyclesLate) {
	CoreTiming::ScheduleEvent(msToCycles(660), CheatEvent, 0);
	member.Run();
}
CheatsGUI::CheatsGUI() {
}

void CheatsGUI::Exit() {
	exit2 = true;
}

string CheatsGUI::GetNextCode() {
	string code;
	string modifier = "_L";
	char modifier2 = '	';
	while (true)  {
		if (currentCode >= codes.size()) {
			code.clear();
			break;
		}

		code = codes[currentCode++];
		trim2(code);

		if (code.substr(0,2) == modifier) {
			code = code.substr(3);
		}
		else if (code[0] == modifier2) {
			break;
		}
	}
	return code;
}

void CheatsGUI::skipCodes(int count) {
	for (int i = 0; i < count; i ++) {
		if (GetNextCode() == "") {
			break;
		}
	}
}
	
void CheatsGUI::skipAllCodes() {
	currentCode = codes.size();
}

int CheatsGUI::getAddress(int value) {
	// The User space base address has to be added to given value
	return (value + 0x08800000) & 0x3FFFFFFF;
}


				

void CheatsGUI::AddCheatLine(string& line) {
		//Need GUI text area here
		string cheatCodes;
		if (cheatCodes.length() <= 0) {
			cheatCodes = line;
		} else {
			cheatCodes += "\n" + line;
		}
	}


inline static long parseHexLong(string s) {
         long value = 0;

         if (s.substr(0,2) == "0x") {
             s = s.substr(2);
         }
         value = strtol(s.c_str(),NULL, 16);
         return value;
     }
inline static long parseLong(string s) {
         long value = 0;
         if (s.substr(0,2) == "0x") {
			 s = s.substr(2);
             value = strtol(s.c_str(),NULL, 16);
         } else {
             value = strtol(s.c_str(),NULL, 10);
         }
         return value;
     }


inline void trim2(string& str)
{
  string::size_type pos = str.find_last_not_of(' ');
  if(pos != string::npos) {
    str.erase(pos + 1);
    pos = str.find_first_not_of(' ');
    if(pos != string::npos) str.erase(0, pos);
  }
  else str.erase(str.begin(), str.end());
}

inline vector<string> makeCodeParts(string l) {
	vector<string> parts;
	char split_char = '\n';
	char empty = ' ';
	
	for (int i=0; i < l.length(); i++) {
		if (l[i] == empty) {
			l[i] = '\n';
		}
	}
	trim2(l);
	istringstream iss (l);
	for (std::string each; std::getline(iss, each, split_char);parts.push_back(each))
	{}
	return parts;
}

vector<string> CheatsGUI::GetCodesList() {
	string text = "0x203BFA00 0x05F5E0FF";
	vector<string> codeslist;
	codeslist = makeCodeParts(text);
	trim2(codeslist[0]);
	trim2(codeslist[1]);
	return codeslist;
}

void CheatsGUI::OnCheatsThreadEnded() {
        test = 0;
    }
void CheatsGUI::Dispose() {
}

void CheatsGUI::Run() {
	CheatsGUI cheats;
	exit2 = false;
	while (!exit2) {
		codes = cheats.GetCodesList(); //UI Member
		currentCode = 0;
		
		while (true) {
			string code = "0x203BFA00 0x05F5E0FF";
			vector<string>parts = makeCodeParts(code);
			int value;
			trim2(parts[0]);
			trim2(parts[1]);
			cout << parts[0] << endl << parts[1];
			unsigned int comm = (unsigned int)parseHexLong(parts[0]);
			int arg = (int)parseHexLong(parts[1]);
			int addr = getAddress(comm & 0x0FFFFFFF);

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
				addr = getAddress(arg);
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
					parts = makeCodeParts(code);
					trim2(parts[0]);
					if ( parts[0] != "") {
						increment = (int) parseHexLong(parts[0]);
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
				break; }
			case 0x4: // 32-bit patch code
				code = GetNextCode();
				parts = makeCodeParts(code);
				trim2(parts[0]);
				trim2(parts[1]);
				if (parts[0] != "") {
					int data = (int) parseHexLong(parts[0]);
					int dataAdd = (int) parseHexLong(parts[1]);

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
				parts = makeCodeParts(code);
				trim2(parts[0]);
				trim2(parts[1]);
				if (parts[0] != "") {
					int destAddr = (int) parseHexLong(parts[0]);
					if (Memory::IsValidAddress(addr) && Memory::IsValidAddress(destAddr)) {
						Memory::Memcpy(destAddr, Memory::GetPointer(addr), arg);
					}
				}
				break;
			case 0x6: // Pointer commands
				code = GetNextCode();
				parts = makeCodeParts(code);
				trim2(parts[0]);
				trim2(parts[1]);
				if (parts[0] != "") {
					int arg2 = (int) parseHexLong(parts[0]);
					int offset = (int) parseHexLong(parts[1]);
					int baseOffset = (arg2 >> 20) * 4;
					int base = Memory::Read_U32(addr + baseOffset);
					int count = arg2 & 0xFFFF;
					int type = (arg2 >> 16) & 0xF;
					for (int i = 1; i < count; i ++ ) {
						if (i+1 < count) {
							code = GetNextCode();
							parts = makeCodeParts(code);
							trim2(parts[0]);
							trim2(parts[1]);
							int arg3 = (int) parseHexLong(parts[0]);
							int arg4 = (int) parseHexLong(parts[1]);
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
				parts = makeCodeParts(code);
				trim2(parts[0]);
				trim2(parts[1]);
				if (parts[0] != "") {
					int data = (int) parseHexLong(parts[0]);
					int dataAdd = (int) parseHexLong(parts[1]);

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
						skipAllCodes();
					}
				}
				break;
			case 0xD: // Test commands & Jocker codes ( Someone will have to help me with these)
				break;
			case 0xE: // Test commands, multiple skip
				{
				bool is8Bit = (comm >> 24) == 0x1;
				addr = getAddress(arg & 0x0FFFFFFF);
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
						skipCodes(skip);
					}
				}
				break;
				}
			default:
				break;
				}
				Exit();
				break;
				}
				}
				// exiting...
				cheats.OnCheatsThreadEnded();
				}



