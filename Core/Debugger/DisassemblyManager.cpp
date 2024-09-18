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

#include "ppsspp_config.h"

#include <string>
#include <algorithm>
#include <map>

#include "ext/xxhash.h"

#include "Common/CommonTypes.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Debugger/DisassemblyManager.h"

std::map<u32, DisassemblyEntry*> DisassemblyManager::entries;
std::recursive_mutex DisassemblyManager::entriesLock_;
DebugInterface* DisassemblyManager::cpu;
int DisassemblyManager::maxParamChars = 29;

bool isInInterval(u32 start, u32 size, u32 value)
{
	return start <= value && value <= (start+size-1);
}

bool IsLikelyStringAt(uint32_t addr) {
	uint32_t maxLen = Memory::ValidSize(addr, 128);
	if (maxLen <= 1)
		return false;
	const char *p = Memory::GetCharPointer(addr);
	// If there's no terminator nearby, let's say no.
	if (memchr(p, 0, maxLen) == nullptr)
		return false;

	// Allow tabs and newlines.
	static constexpr bool validControl[] = {
		false, false, false, false, false, false, false, false,
		false, true, true, true, false, true, false, false,
		false, false, false, false, false, false, false, false,
		false, false, false, false, false, false, false, false,
	};

	// Check that there's some bytes before the terminator that look like a string.
	UTF8 utf(p);
	if (utf.end())
		return false;

	char verify[4];
	while (!utf.end()) {
		if (utf.invalid())
			return false;

		int pos = utf.byteIndex();
		uint32_t c = utf.next();
		int len = UTF8::encode(verify, c);
		// Our decoder is a bit lax, so let's verify this is a normal encoding.
		// This prevents us from trying to output invalid encodings in the debugger.
		if (memcmp(p + pos, verify, len) != 0 || pos + len != utf.byteIndex())
			return false;

		if (c < ARRAY_SIZE(validControl) && !validControl[c])
			return false;
		if (c > 0x0010FFFF)
			return false;
	}

	return true;
}

static HashType computeHash(u32 address, u32 size)
{
	if (!Memory::IsValidAddress(address))
		return 0;

	size = Memory::ValidSize(address, size);
#if PPSSPP_ARCH(AMD64)
	return XXH3_64bits(Memory::GetPointerUnchecked(address), size);
#else
	return XXH3_64bits(Memory::GetPointerUnchecked(address), size) & 0xFFFFFFFF;
#endif
}


static void parseDisasm(const char *disasm, char *opcode, size_t opcodeSize, char *arguments, size_t argumentsSize, bool insertSymbols) {
	// copy opcode
	size_t opcodePos = 0;
	while (*disasm != 0 && *disasm != '\t' && opcodePos + 1 < opcodeSize) {
		opcode[opcodePos++] = *disasm++;
	}
	opcode[opcodePos] = 0;

	// Otherwise it's a tab, and we skip intentionally.
	if (*disasm++ == 0) {
		*arguments = 0;
		return;
	}

	const char* jumpAddress = strstr(disasm,"->$");
	const char* jumpRegister = strstr(disasm,"->");
	size_t argumentsPos = 0;
	while (*disasm != 0 && argumentsPos + 1 < argumentsSize) {
		// parse symbol
		if (disasm == jumpAddress) {
			u32 branchTarget = 0;
			sscanf(disasm+3, "%08x", &branchTarget);
			const std::string addressSymbol = g_symbolMap->GetLabelString(branchTarget);
			if (!addressSymbol.empty() && insertSymbols) {
				argumentsPos += snprintf(&arguments[argumentsPos], argumentsSize - argumentsPos, "%s", addressSymbol.c_str());
			} else {
				argumentsPos += snprintf(&arguments[argumentsPos], argumentsSize - argumentsPos, "0x%08X", branchTarget);
			}

			disasm += 3+8;
			continue;
		}

		if (disasm == jumpRegister)
			disasm += 2;

		if (*disasm == ' ') {
			disasm++;
			continue;
		}
		arguments[argumentsPos++] = *disasm++;
	}

	arguments[argumentsPos] = 0;
}

std::map<u32,DisassemblyEntry*>::iterator findDisassemblyEntry(std::map<u32,DisassemblyEntry*>& entries, u32 address, bool exact)
{
	if (exact)
		return entries.find(address);

	if (entries.size() == 0)
		return entries.end();

	// find first elem that's >= address
	auto it = entries.lower_bound(address);
	if (it != entries.end())
	{
		// it may be an exact match
		if (isInInterval(it->second->getLineAddress(0),it->second->getTotalSize(),address))
			return it;

		// otherwise it may point to the next
		if (it != entries.begin())
		{
			it--;
			if (isInInterval(it->second->getLineAddress(0),it->second->getTotalSize(),address))
				return it;
		}
	}

	// check last entry manually
	auto rit = entries.rbegin();
	if (isInInterval(rit->second->getLineAddress(0),rit->second->getTotalSize(),address))
	{
		return (++rit).base();
	}

	// no match otherwise
	return entries.end();
}

void DisassemblyManager::analyze(u32 address, u32 size = 1024)
{
	u32 end = address+size;

	address &= ~3;
	u32 start = address;

	while (address < end && start <= address)
	{
		if (!PSP_IsInited())
			return;

		auto memLock = Memory::Lock();
		std::lock_guard<std::recursive_mutex> guard(entriesLock_);
		auto it = findDisassemblyEntry(entries, address, false);
		if (it != entries.end())
		{
			DisassemblyEntry* entry = it->second;
			entry->recheck();
			address = entry->getLineAddress(0)+entry->getTotalSize();
			continue;
		}

		SymbolInfo info;
		if (!g_symbolMap->GetSymbolInfo(&info,address,ST_ALL))
		{
			if (address % 4)
			{
				u32 next = std::min<u32>((address+3) & ~3,g_symbolMap->GetNextSymbolAddress(address,ST_ALL));
				DisassemblyData* data = new DisassemblyData(address,next-address,DATATYPE_BYTE);
				entries[address] = data;
				address = next;
				continue;
			}

			u32 next = g_symbolMap->GetNextSymbolAddress(address,ST_ALL);

			if ((next % 4) && next != (u32)-1)
			{
				u32 alignedNext = next & ~3;

				if (alignedNext != address)
				{
					DisassemblyOpcode* opcode = new DisassemblyOpcode(address,(alignedNext-address)/4);
					entries[address] = opcode;
				}

				DisassemblyData* data = new DisassemblyData(address,next-alignedNext,DATATYPE_BYTE);
				entries[alignedNext] = data;
			} else {
				DisassemblyOpcode* opcode = new DisassemblyOpcode(address,(next-address)/4);
				entries[address] = opcode;
			}

			address = next;
			continue;
		}

		switch (info.type)
		{
		case ST_FUNCTION:
			{
				DisassemblyFunction* function = new DisassemblyFunction(info.address,info.size);
				entries[info.address] = function;
				address = info.address+info.size;
			}
			break;
		case ST_DATA:
			{
				DisassemblyData* data = new DisassemblyData(info.address,info.size,g_symbolMap->GetDataType(info.address));
				entries[info.address] = data;
				address = info.address+info.size;
			}
			break;
		default:
			break;
		}
	}

}

std::vector<BranchLine> DisassemblyManager::getBranchLines(u32 start, u32 size)
{
	std::vector<BranchLine> result;

	std::lock_guard<std::recursive_mutex> guard(entriesLock_);
	auto it = findDisassemblyEntry(entries,start,false);
	if (it != entries.end())
	{
		do 
		{
			it->second->getBranchLines(start,size,result);
			it++;
		} while (it != entries.end() && start+size > it->second->getLineAddress(0));
	}

	return result;
}

void DisassemblyManager::getLine(u32 address, bool insertSymbols, DisassemblyLineInfo &dest, DebugInterface *cpuDebug)
{
	// This is here really to avoid lock ordering issues.
	auto memLock = Memory::Lock();
	std::lock_guard<std::recursive_mutex> guard(entriesLock_);
	auto it = findDisassemblyEntry(entries,address,false);
	if (it == entries.end())
	{
		analyze(address);
		it = findDisassemblyEntry(entries,address,false);
	}

	if (it != entries.end()) {
		DisassemblyEntry *entry = it->second;
		if (entry->disassemble(address, dest, insertSymbols, cpuDebug))
			return;
	}

	dest.type = DISTYPE_OTHER;
	memset(&dest.info, 0, sizeof(dest.info));
	dest.info.opcodeAddress = address;
	if (address % 4)
		dest.totalSize = ((address+3) & ~3)-address;
	else
		dest.totalSize = 4;
	if (Memory::IsValidRange(address, 4)) {
		dest.name = "ERROR";
		dest.params = "Disassembly failure";
	} else {
		dest.name = "-";
		dest.params.clear();
	}
}

u32 DisassemblyManager::getStartAddress(u32 address)
{
	auto memLock = Memory::Lock();
	std::lock_guard<std::recursive_mutex> guard(entriesLock_);
	auto it = findDisassemblyEntry(entries,address,false);
	if (it == entries.end())
	{
		analyze(address);
		it = findDisassemblyEntry(entries,address,false);
		if (it == entries.end())
			return address;
	}
	
	DisassemblyEntry* entry = it->second;
	int line = entry->getLineNum(address,true);
	return entry->getLineAddress(line);
}

u32 DisassemblyManager::getNthPreviousAddress(u32 address, int n)
{
	auto memLock = Memory::Lock();
	std::lock_guard<std::recursive_mutex> guard(entriesLock_);
	while (Memory::IsValidAddress(address))
	{
		auto it = findDisassemblyEntry(entries,address,false);
		if (it == entries.end())
			break;
		while (it != entries.end())
		{
			DisassemblyEntry* entry = it->second;
			int oldLineNum = entry->getLineNum(address,true);
			if (n <= oldLineNum)
			{
				return entry->getLineAddress(oldLineNum-n);
			}

			address = entry->getLineAddress(0)-1;
			n -= oldLineNum+1;
			it = findDisassemblyEntry(entries,address,false);
		}
	
		analyze(address-127,128);
	}
	
	return (address - n * 4) & ~3;
}

u32 DisassemblyManager::getNthNextAddress(u32 address, int n)
{
	auto memLock = Memory::Lock();
	std::lock_guard<std::recursive_mutex> guard(entriesLock_);
	while (Memory::IsValidAddress(address))
	{
		auto it = findDisassemblyEntry(entries,address,false);
		if (it == entries.end()) {
			break;
		}

		while (it != entries.end())
		{
			DisassemblyEntry* entry = it->second;
			int oldLineNum = entry->getLineNum(address,true);
			int oldNumLines = entry->getNumLines();
			if (oldLineNum+n < oldNumLines)
			{
				return entry->getLineAddress(oldLineNum+n);
			}

			address = entry->getLineAddress(0)+entry->getTotalSize();
			n -= (oldNumLines-oldLineNum);
			it = findDisassemblyEntry(entries,address,false);
		}

		analyze(address);
	}

	return (address + n * 4) & ~3;
}

DisassemblyManager::~DisassemblyManager() {
}

void DisassemblyManager::clear()
{
	auto memLock = Memory::Lock();
	std::lock_guard<std::recursive_mutex> guard(entriesLock_);
	for (auto it = entries.begin(); it != entries.end(); it++)
	{
		delete it->second;
	}
	entries.clear();
}

DisassemblyFunction::DisassemblyFunction(u32 _address, u32 _size): address(_address), size(_size)
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	hash = computeHash(address,size);
	load();
}

DisassemblyFunction::~DisassemblyFunction() {
	clear();
}

void DisassemblyFunction::recheck()
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	HashType newHash = computeHash(address,size);
	if (hash != newHash)
	{
		hash = newHash;
		clear();
		load();
	}
}

int DisassemblyFunction::getNumLines()
{
	std::lock_guard<std::recursive_mutex> guard(lock_);
	return (int) lineAddresses.size();
}

int DisassemblyFunction::getLineNum(u32 address, bool findStart)
{
	std::lock_guard<std::recursive_mutex> guard(lock_);
	if (findStart)
	{
		int last = (int)lineAddresses.size() - 1;
		for (int i = 0; i < last; i++)
		{
			u32 next = lineAddresses[i + 1];
			if (lineAddresses[i] <= address && next > address)
				return i;
		}
		if (lineAddresses[last] <= address && this->address + this->size > address)
			return last;
	}
	else
	{
		int last = (int)lineAddresses.size() - 1;
		for (int i = 0; i < last; i++)
		{
			if (lineAddresses[i] == address)
				return i;
		}
		if (lineAddresses[last] == address)
			return last;
	}

	return 0;
}

u32 DisassemblyFunction::getLineAddress(int line)
{
	std::lock_guard<std::recursive_mutex> guard(lock_);
	return lineAddresses[line];
}

bool DisassemblyFunction::disassemble(u32 address, DisassemblyLineInfo &dest, bool insertSymbols, DebugInterface *cpuDebug)
{
	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = findDisassemblyEntry(entries,address,false);
	if (it == entries.end())
		return false;

	return it->second->disassemble(address, dest, insertSymbols, cpuDebug);
}

void DisassemblyFunction::getBranchLines(u32 start, u32 size, std::vector<BranchLine>& dest)
{
	u32 end = start+size;

	std::lock_guard<std::recursive_mutex> guard(lock_);
	for (size_t i = 0; i < lines.size(); i++)
	{
		BranchLine& line = lines[i];

		u32 first = line.first;
		u32 second = line.second;

		// skip branches that are entirely before or entirely after the window
		if ((first < start && second < start) ||
			(first > end && second > end))
			continue;

		dest.push_back(line);
	}
}

#define NUM_LANES 16

void DisassemblyFunction::generateBranchLines()
{
	struct LaneInfo
	{
		bool used;
		u32 end;
	};

	LaneInfo lanes[NUM_LANES];
	for (int i = 0; i < NUM_LANES; i++)
		lanes[i].used = false;

	u32 end = address+size;

	std::lock_guard<std::recursive_mutex> guard(lock_);
	DebugInterface* cpu = DisassemblyManager::getCpu();
	for (u32 funcPos = address; funcPos < end; funcPos += 4)
	{
		MIPSAnalyst::MipsOpcodeInfo opInfo = MIPSAnalyst::GetOpcodeInfo(cpu,funcPos);

		bool inFunction = (opInfo.branchTarget >= address && opInfo.branchTarget < end);
		if (opInfo.isBranch && !opInfo.isBranchToRegister && !opInfo.isLinkedBranch && inFunction) {
			if (!Memory::IsValidAddress(opInfo.branchTarget))
				continue;

			BranchLine line;
			if (opInfo.branchTarget < funcPos) {
				line.first = opInfo.branchTarget;
				line.second = funcPos;
				line.type = LINE_UP;
			} else {
				line.first = funcPos;
				line.second = opInfo.branchTarget;
				line.type = LINE_DOWN;
			}

			lines.push_back(line);
		}
	}
			
	std::sort(lines.begin(),lines.end());
	for (size_t i = 0; i < lines.size(); i++)
	{
		for (int l = 0; l < NUM_LANES; l++)
		{
			if (lines[i].first > lanes[l].end)
				lanes[l].used = false;
		}

		int lane = -1;
		for (int l = 0; l < NUM_LANES; l++)
		{
			if (lanes[l].used == false)
			{
				lane = l;
				break;
			}
		}

		if (lane == -1)
		{
			// Let's just pile on.
			lines[i].laneIndex = 15;
			continue;
		}

		lanes[lane].end = lines[i].second;
		lanes[lane].used = true;
		lines[i].laneIndex = lane;
	}
}

void DisassemblyFunction::addOpcodeSequence(u32 start, u32 end)
{
	DisassemblyOpcode* opcode = new DisassemblyOpcode(start,(end-start)/4);
	std::lock_guard<std::recursive_mutex> guard(lock_);
	entries[start] = opcode;
	lineAddresses.reserve((end - start) / 4);
	for (u32 pos = start; pos < end; pos += 4)
	{
		lineAddresses.push_back(pos);
	}
}

void DisassemblyFunction::load()
{
	generateBranchLines();

	// gather all branch targets
	std::set<u32> branchTargets;
	{
		std::lock_guard<std::recursive_mutex> guard(lock_);
		for (size_t i = 0; i < lines.size(); i++)
		{
			switch (lines[i].type)
			{
			case LINE_DOWN:
				branchTargets.insert(lines[i].second);
				break;
			case LINE_UP:
				branchTargets.insert(lines[i].first);
				break;
			default:
				break;
			}
		}
	}
	
	DebugInterface* cpu = DisassemblyManager::getCpu();
	u32 funcPos = address;
	u32 funcEnd = address+size;

	u32 nextData = g_symbolMap->GetNextSymbolAddress(funcPos-1,ST_DATA);
	u32 opcodeSequenceStart = funcPos;
	while (funcPos < funcEnd)
	{
		if (funcPos == nextData)
		{
			if (opcodeSequenceStart != funcPos)
				addOpcodeSequence(opcodeSequenceStart,funcPos);

			DisassemblyData* data = new DisassemblyData(funcPos,g_symbolMap->GetDataSize(funcPos),g_symbolMap->GetDataType(funcPos));
			std::lock_guard<std::recursive_mutex> guard(lock_);
			entries[funcPos] = data;
			lineAddresses.push_back(funcPos);
			funcPos += data->getTotalSize();

			nextData = g_symbolMap->GetNextSymbolAddress(funcPos-1,ST_DATA);
			opcodeSequenceStart = funcPos;
			continue;
		}

		// force align
		if (funcPos % 4)
		{
			u32 nextPos = (funcPos+3) & ~3;

			DisassemblyComment* comment = new DisassemblyComment(funcPos,nextPos-funcPos,".align","4");
			std::lock_guard<std::recursive_mutex> guard(lock_);
			entries[funcPos] = comment;
			lineAddresses.push_back(funcPos);
			
			funcPos = nextPos;
			opcodeSequenceStart = funcPos;
			continue;
		}

		MIPSAnalyst::MipsOpcodeInfo opInfo = MIPSAnalyst::GetOpcodeInfo(cpu,funcPos);
		u32 opAddress = funcPos;
		funcPos += 4;

		// skip branches and their delay slots
		if (opInfo.isBranch)
		{
			funcPos += 4;
			continue;
		}

		// lui
		if (MIPS_GET_OP(opInfo.encodedOpcode) == 0x0F && funcPos < funcEnd && funcPos != nextData)
		{
			MIPSOpcode next = Memory::Read_Instruction(funcPos);
			MIPSInfo nextInfo = MIPSGetInfo(next);

			u32 immediate = ((opInfo.encodedOpcode & 0xFFFF) << 16) + (s16)(next.encoding & 0xFFFF);
			int rt = MIPS_GET_RT(opInfo.encodedOpcode);

			int nextRs = MIPS_GET_RS(next.encoding);
			int nextRt = MIPS_GET_RT(next.encoding);

			// both rs and rt of the second op have to match rt of the first,
			// otherwise there may be hidden consequences if the macro is displayed.
			// also, don't create a macro if something branches into the middle of it
			if (nextRs == rt && nextRt == rt && branchTargets.find(funcPos) == branchTargets.end())
			{
				DisassemblyMacro* macro = NULL;
				switch (MIPS_GET_OP(next.encoding))
				{
				case 0x09:	// addiu
					macro = new DisassemblyMacro(opAddress);
					macro->setMacroLi(immediate,rt);
					funcPos += 4;
					break;
				case 0x20:	// lb
				case 0x21:	// lh
				case 0x23:	// lw
				case 0x24:	// lbu
				case 0x25:	// lhu
				case 0x28:	// sb
				case 0x29:	// sh
				case 0x2B:	// sw
					macro = new DisassemblyMacro(opAddress);
					
					int dataSize = MIPSGetMemoryAccessSize(next);
					if (dataSize == 0) {
						delete macro;
						return;
					}

					macro->setMacroMemory(MIPSGetName(next),immediate,rt,dataSize);
					funcPos += 4;
					break;
				}

				if (macro != NULL)
				{
					if (opcodeSequenceStart != opAddress)
						addOpcodeSequence(opcodeSequenceStart,opAddress);

					std::lock_guard<std::recursive_mutex> guard(lock_);
					entries[opAddress] = macro;
					for (int i = 0; i < macro->getNumLines(); i++)
					{
						lineAddresses.push_back(macro->getLineAddress(i));
					}

					opcodeSequenceStart = funcPos;
					continue;
				}
			}
		}

		// just a normal opcode
	}

	if (opcodeSequenceStart != funcPos)
		addOpcodeSequence(opcodeSequenceStart,funcPos);
}

void DisassemblyFunction::clear()
{
	std::lock_guard<std::recursive_mutex> guard(lock_);
	for (auto it = entries.begin(); it != entries.end(); it++)
	{
		delete it->second;
	}

	entries.clear();
	lines.clear();
	lineAddresses.clear();
	hash = 0;
}

bool DisassemblyOpcode::disassemble(u32 address, DisassemblyLineInfo &dest, bool insertSymbols, DebugInterface *cpuDebug)
{
	if (!cpuDebug)
		cpuDebug = DisassemblyManager::getCpu();

	char opcode[64],arguments[256];
	char dizz[512];
	cpuDebug->DisAsm(address, dizz, sizeof(dizz));
	parseDisasm(dizz, opcode, sizeof(opcode), arguments, sizeof(arguments), insertSymbols);
	dest.type = DISTYPE_OPCODE;
	dest.name = opcode;
	dest.params = arguments;
	dest.totalSize = 4;
	dest.info = MIPSAnalyst::GetOpcodeInfo(cpuDebug, address);
	return true;
}

void DisassemblyOpcode::getBranchLines(u32 start, u32 size, std::vector<BranchLine>& dest)
{
	if (start < address)
	{
		size = start+size-address;
		start = address;
	}

	if (start+size > address+num*4)
		size = address+num*4-start;

	int lane = 0;
	for (u32 pos = start; pos < start+size; pos += 4)
	{
		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(DisassemblyManager::getCpu(),pos);
		if (info.isBranch && !info.isBranchToRegister && !info.isLinkedBranch) {
			if (!Memory::IsValidAddress(info.branchTarget))
				continue;

			BranchLine line;
			line.laneIndex = lane++;

			if (info.branchTarget < pos) {
				line.first = info.branchTarget;
				line.second = pos;
				line.type = LINE_UP;
			} else {
				line.first = pos;
				line.second = info.branchTarget;
				line.type = LINE_DOWN;
			}

			dest.push_back(line);
		}
	}
}


void DisassemblyMacro::setMacroLi(u32 _immediate, u8 _rt)
{
	type = MACRO_LI;
	name = "li";
	immediate = _immediate;
	rt = _rt;
	numOpcodes = 2;
}

void DisassemblyMacro::setMacroMemory(const std::string &_name, u32 _immediate, u8 _rt, int _dataSize)
{
	type = MACRO_MEMORYIMM;
	name = _name;
	immediate = _immediate;
	rt = _rt;
	dataSize = _dataSize;
	numOpcodes = 2;
}

bool DisassemblyMacro::disassemble(u32 address, DisassemblyLineInfo &dest, bool insertSymbols, DebugInterface *cpuDebug)
{
	if (!cpuDebug)
		cpuDebug = DisassemblyManager::getCpu();

	char buffer[64];
	dest.type = DISTYPE_MACRO;
	dest.info = MIPSAnalyst::GetOpcodeInfo(cpuDebug, address);

	std::string addressSymbol;
	switch (type)
	{
	case MACRO_LI:
		dest.name = name;
		
		addressSymbol = g_symbolMap->GetLabelString(immediate);
		if (!addressSymbol.empty() && insertSymbols) {
			snprintf(buffer, sizeof(buffer), "%s,%s", cpuDebug->GetRegName(0, rt).c_str(), addressSymbol.c_str());
		} else {
			snprintf(buffer, sizeof(buffer), "%s,0x%08X", cpuDebug->GetRegName(0, rt).c_str(), immediate);
		}

		dest.params = buffer;
		
		dest.info.hasRelevantAddress = true;
		dest.info.relevantAddress = immediate;
		break;
	case MACRO_MEMORYIMM:
		dest.name = name;

		addressSymbol = g_symbolMap->GetLabelString(immediate);
		if (!addressSymbol.empty() && insertSymbols) {
			snprintf(buffer, sizeof(buffer), "%s,%s", cpuDebug->GetRegName(0, rt).c_str(), addressSymbol.c_str());
		} else {
			snprintf(buffer, sizeof(buffer), "%s,0x%08X", cpuDebug->GetRegName(0, rt).c_str(), immediate);
		}

		dest.params = buffer;

		dest.info.isDataAccess = true;
		dest.info.dataAddress = immediate;
		dest.info.dataSize = dataSize;

		dest.info.hasRelevantAddress = true;
		dest.info.relevantAddress = immediate;
		break;
	default:
		return false;
	}

	dest.totalSize = getTotalSize();
	return true;
}


DisassemblyData::DisassemblyData(u32 _address, u32 _size, DataType _type): address(_address), size(_size), type(_type)
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	hash = computeHash(address,size);
	createLines();
}

void DisassemblyData::recheck()
{
	auto memLock = Memory::Lock();
	if (!PSP_IsInited())
		return;

	HashType newHash = computeHash(address,size);
	if (newHash != hash)
	{
		hash = newHash;
		createLines();
	}
}

bool DisassemblyData::disassemble(u32 address, DisassemblyLineInfo &dest, bool insertSymbols, DebugInterface *cpuDebug)
{
	dest.type = DISTYPE_DATA;

	switch (type)
	{
	case DATATYPE_BYTE:
		dest.name = ".byte";
		break;
	case DATATYPE_HALFWORD:
		dest.name = ".half";
		break;
	case DATATYPE_WORD:
		dest.name = ".word";
		break;
	case DATATYPE_ASCII:
		dest.name = ".ascii";
		break;
	default:
		return false;
	}

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = lines.find(address);
	if (it == lines.end())
		return false;

	dest.params = it->second.text;
	dest.totalSize = it->second.size;
	return true;
}

int DisassemblyData::getLineNum(u32 address, bool findStart)
{
	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = lines.upper_bound(address);
	if (it != lines.end())
	{
		if (it == lines.begin())
			return 0;
		it--;
		return it->second.lineNum;
	}

	return lines.rbegin()->second.lineNum;
}

void DisassemblyData::createLines()
{
	std::lock_guard<std::recursive_mutex> guard(lock_);
	lines.clear();
	lineAddresses.clear();

	u32 pos = address;
	u32 end = address+size;
	u32 maxChars = DisassemblyManager::getMaxParamChars();
	
	std::string currentLine;
	u32 currentLineStart = pos;

	int lineCount = 0;
	if (type == DATATYPE_ASCII)
	{
		bool inString = false;
		while (pos < end)
		{
			u8 b = Memory::Read_U8(pos++);
			if (b >= 0x20 && b <= 0x7F)
			{
				if (currentLine.size()+1 >= maxChars)
				{
					if (inString == true)
						currentLine += "\"";

					DataEntry entry = {currentLine,pos-1-currentLineStart,lineCount++};
					lines[currentLineStart] = entry;
					lineAddresses.push_back(currentLineStart);
					
					currentLine.clear();
					currentLineStart = pos-1;
					inString = false;
				}

				if (inString == false)
					currentLine += "\"";
				currentLine += (char)b;
				inString = true;
			} else {
				char buffer[64];
				if (pos == end && b == 0)
					truncate_cpy(buffer, "0");
				else
					snprintf(buffer, sizeof(buffer), "0x%02X", b);

				if (currentLine.size()+strlen(buffer) >= maxChars)
				{
					if (inString == true)
						currentLine += "\"";
					
					DataEntry entry = {currentLine,pos-1-currentLineStart,lineCount++};
					lines[currentLineStart] = entry;
					lineAddresses.push_back(currentLineStart);
					
					currentLine.clear();
					currentLineStart = pos-1;
					inString = false;
				}

				bool comma = false;
				if (currentLine.size() != 0)
					comma = true;

				if (inString)
					currentLine += "\"";

				if (comma)
					currentLine += ",";

				currentLine += buffer;
				inString = false;
			}
		}

		if (inString == true)
			currentLine += "\"";

		if (currentLine.size() != 0)
		{
			DataEntry entry = {currentLine,pos-currentLineStart,lineCount++};
			lines[currentLineStart] = entry;
			lineAddresses.push_back(currentLineStart);
		}
	} else {
		while (pos < end)
		{
			char buffer[256];
			u32 value;

			u32 currentPos = pos;

			switch (type)
			{
			case DATATYPE_BYTE:
				value = Memory::Read_U8(pos);
				snprintf(buffer, sizeof(buffer), "0x%02X", value);
				pos++;
				break;
			case DATATYPE_HALFWORD:
				value = Memory::Read_U16(pos);
				snprintf(buffer, sizeof(buffer), "0x%04X", value);
				pos += 2;
				break;
			case DATATYPE_WORD:
				{
					value = Memory::Read_U32(pos);
					const std::string label = g_symbolMap->GetLabelString(value);
					if (!label.empty())
						snprintf(buffer, sizeof(buffer), "%s", label.c_str());
					else
						snprintf(buffer, sizeof(buffer), "0x%08X", value);
					pos += 4;
				}
				break;
			default:
				break;
			}

			size_t len = strlen(buffer);
			if (currentLine.size() != 0 && currentLine.size()+len >= maxChars)
			{
				DataEntry entry = {currentLine,currentPos-currentLineStart,lineCount++};
				lines[currentLineStart] = entry;
				lineAddresses.push_back(currentLineStart);

				currentLine.clear();
				currentLineStart = currentPos;
			}

			if (currentLine.size() != 0)
				currentLine += ",";
			currentLine += buffer;
		}

		if (currentLine.size() != 0) {
			DataEntry entry = {currentLine,pos-currentLineStart,lineCount++};
			lines[currentLineStart] = entry;
			lineAddresses.push_back(currentLineStart);
		}
	}
}


DisassemblyComment::DisassemblyComment(u32 _address, u32 _size, std::string _name, std::string _param)
	: address(_address), size(_size), name(_name), param(_param)
{

}

bool DisassemblyComment::disassemble(u32 address, DisassemblyLineInfo &dest, bool insertSymbols, DebugInterface *cpuDebug)
{
	dest.type = DISTYPE_OTHER;
	dest.name = name;
	dest.params = param;
	dest.totalSize = size;
	return true;
}
