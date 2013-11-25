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

#include "DisassemblyManager.h"
#include "DebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"
#include "Common/Common.h"
#include "ext/xxhash.h"

#include <algorithm>

std::map<u32,DisassemblyEntry*> DisassemblyManager::entries;
DebugInterface* DisassemblyManager::cpu;

bool isInInterval(u32 start, u32 size, u32 value)
{
	return start <= value && value < start+size;
}

void parseDisasm(const char* disasm, char* opcode, char* arguments, bool insertSymbols)
{
	// copy opcode
	while (*disasm != 0 && *disasm != '\t')
	{
		*opcode++ = *disasm++;
	}
	*opcode = 0;

	if (*disasm++ == 0)
	{
		*arguments = 0;
		return;
	}

	const char* jumpAddress = strstr(disasm,"->$");
	const char* jumpRegister = strstr(disasm,"->");
	while (*disasm != 0)
	{
		// parse symbol
		if (disasm == jumpAddress)
		{
			u32 branchTarget;
			sscanf(disasm+3,"%08x",&branchTarget);

			const char* addressSymbol = DisassemblyManager::getCpu()->findSymbolForAddress(branchTarget);
			if (addressSymbol != NULL && insertSymbols)
			{
				arguments += sprintf(arguments,"%s",addressSymbol);
			} else {
				arguments += sprintf(arguments,"0x%08X",branchTarget);
			}
			
			disasm += 3+8;
			continue;
		}

		if (disasm == jumpRegister)
			disasm += 2;

		if (*disasm == ' ')
		{
			disasm++;
			continue;
		}
		*arguments++ = *disasm++;
	}

	*arguments = 0;
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
		auto it = findDisassemblyEntry(entries,address,false);
		if (it != entries.end())
		{
			DisassemblyEntry* entry = it->second;
			entry->recheck();
			address = entry->getLineAddress(0)+entry->getTotalSize();
			continue;
		}

		SymbolInfo info;
		if (symbolMap.GetSymbolInfo(&info,address) && info.size >= 4)
		{
			DisassemblyFunction* function = new DisassemblyFunction(info.address,info.size);
			entries[info.address] = function;
			address = info.address+info.size;
		} else {
			u32 next = symbolMap.GetNextSymbolAddress(address+1);

			// let's just assume anything otuside a function is a normal opcode
			DisassemblyOpcode* opcode = new DisassemblyOpcode(address,(next-address)/4);
			entries[address] = opcode;
			
			address = next;
		}
	}

}

std::vector<BranchLine> DisassemblyManager::getBranchLines(u32 start, u32 size)
{
	std::vector<BranchLine> result;
	
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

void DisassemblyManager::getLine(u32 address, bool insertSymbols, DisassemblyLineInfo& dest)
{
	auto it = findDisassemblyEntry(entries,address,false);
	if (it == entries.end())
	{
		analyze(address);
		it = findDisassemblyEntry(entries,address,false);

		if (it == entries.end())
		{
			dest.totalSize = 4;
			dest.name = "ERROR";
			dest.params = "Disassembly failure";
			return;
		}
	}

	DisassemblyEntry* entry = it->second;
	
	dest.info = MIPSAnalyst::GetOpcodeInfo(cpu,address);
	if (entry->disassemble(address,dest,insertSymbols))
		return;

	dest.totalSize = 4;
	dest.name = "ERROR";
	dest.params = "Disassembly failure";
}

u32 DisassemblyManager::getStartAddress(u32 address)
{
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
	while (Memory::IsValidAddress(address))
	{
		auto it = findDisassemblyEntry(entries,address,false);
	
		while (it != entries.end())
		{
			DisassemblyEntry* entry = it->second;
			int oldLineNum = entry->getLineNum(address,true);
			int oldNumLines = entry->getNumLines();
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
	
	return address-n*4;
}

u32 DisassemblyManager::getNthNextAddress(u32 address, int n)
{
	while (Memory::IsValidAddress(address))
	{
		auto it = findDisassemblyEntry(entries,address,false);
	
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

	return address+n*4;
}

void DisassemblyManager::clear()
{
	for (auto it = entries.begin(); it != entries.end(); it++)
	{
		delete it->second;
	}
	entries.clear();
}

DisassemblyFunction::DisassemblyFunction(u32 _address, u32 _size): address(_address), size(_size)
{
	hash = computeHash();
	load();
}

u32 DisassemblyFunction::computeHash()
{
	return XXH32(Memory::GetPointer(address),size,0xBACD7814);
}

void DisassemblyFunction::recheck()
{
	u32 newHash = computeHash();
	if (hash != newHash)
	{
		hash = newHash;
		clear();
		load();
	}
}

int DisassemblyFunction::getNumLines()
{
	return lineAddresses.size();
}

int DisassemblyFunction::getLineNum(u32 address, bool findStart)
{
	for (int i = 0; i < lineAddresses.size(); i++)
	{
		u32 next = (i == lineAddresses.size()-1) ? this->address+this->size : lineAddresses[i+1];

		if (lineAddresses[i] == address)
			return i;
		if (findStart && lineAddresses[i] <= address && next > address)
			return i;
	}

	return 0;
}

u32 DisassemblyFunction::getLineAddress(int line)
{
	return lineAddresses[line];
}

bool DisassemblyFunction::disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols)
{
	auto it = findDisassemblyEntry(entries,address,false);
	if (it == entries.end())
		return false;

	return it->second->disassemble(address,dest,insertSymbols);
}

void DisassemblyFunction::getBranchLines(u32 start, u32 size, std::vector<BranchLine>& dest)
{
	for (int i = 0; i < lines.size(); i++)
	{
		dest.push_back(lines[i]);
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

	DebugInterface* cpu = DisassemblyManager::getCpu();
	for (u32 funcPos = address; funcPos < end; funcPos += 4)
	{
		MIPSAnalyst::MipsOpcodeInfo opInfo = MIPSAnalyst::GetOpcodeInfo(cpu,funcPos);

		bool inFunction = (opInfo.branchTarget >= address && opInfo.branchTarget < end);
		if (opInfo.isBranch && !opInfo.isBranchToRegister && !opInfo.isLinkedBranch && inFunction)
		{
			BranchLine line;
			if (opInfo.branchTarget < funcPos)
			{
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
			// error
			continue;
		}

		lanes[lane].end = lines[i].second;
		lanes[lane].used = true;
		lines[i].laneIndex = lane;
	}
}


void DisassemblyFunction::load()
{
	generateBranchLines();

	// gather all branch targets
	std::set<u32> branchTargets;
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
		}
	}
	
	DebugInterface* cpu = DisassemblyManager::getCpu();
	u32 funcPos = address;
	u32 funcEnd = address+size;

	while (funcPos < funcEnd)
	{
		MIPSAnalyst::MipsOpcodeInfo opInfo = MIPSAnalyst::GetOpcodeInfo(cpu,funcPos);
		u32 opAddress = funcPos;
		funcPos += 4;

		// skip branches and their delay slots
		if (opInfo.isBranch)
		{
			DisassemblyOpcode* opcode = new DisassemblyOpcode(opAddress,2);
			entries[opAddress] = opcode;
			lineAddresses.push_back(opAddress);
			lineAddresses.push_back(opAddress+4);

			funcPos += 4;
			continue;
		}

		// lui
		if (MIPS_GET_OP(opInfo.encodedOpcode) == 0x0F && funcPos < funcEnd)
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
					
					int dataSize;
					switch (nextInfo & MEMTYPE_MASK) {
					case MEMTYPE_BYTE:
						dataSize = 1;
						break;
					case MEMTYPE_HWORD:
						dataSize = 2;
						break;
					case MEMTYPE_WORD:
					case MEMTYPE_FLOAT:
						dataSize = 4;
						break;
					case MEMTYPE_VQUAD:
						dataSize = 16;
						break;
					}

					macro->setMacroMemory(MIPSGetName(next),immediate,rt,dataSize);
					funcPos += 4;
					break;
				}

				if (macro != NULL)
				{
					entries[opAddress] = macro;
					for (int i = 0; i < macro->getNumLines(); i++)
					{
						lineAddresses.push_back(macro->getLineAddress(i));
					}
					continue;
				}
			}
		}

		// just a normal opcode
		DisassemblyOpcode* opcode = new DisassemblyOpcode(opAddress,1);
		entries[opAddress] = opcode;
		lineAddresses.push_back(opAddress);
	}
}

void DisassemblyFunction::clear()
{
	for (auto it = entries.begin(); it != entries.end(); it++)
	{
		delete it->second;
	}

	entries.clear();
	lines.clear();
	lineAddresses.clear();
	hash = 0;
}

bool DisassemblyOpcode::disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols)
{
	char opcode[64],arguments[256];
	const char *dizz = DisassemblyManager::getCpu()->disasm(address,4);
	parseDisasm(dizz,opcode,arguments,insertSymbols);
	dest.name = opcode;
	dest.params = arguments;
	dest.totalSize = 4;
	return true;
}

void DisassemblyOpcode::getBranchLines(u32 start, u32 size, std::vector<BranchLine>& dest)
{
	int lane = 0;
	for (u32 pos = start; pos < start+size; pos += 4)
	{
		MIPSAnalyst::MipsOpcodeInfo info = MIPSAnalyst::GetOpcodeInfo(DisassemblyManager::getCpu(),pos);
		if (info.isBranch && !info.isBranchToRegister && !info.isLinkedBranch)
		{
			BranchLine line;
			line.laneIndex = lane++;

			if (info.branchTarget < pos)
			{
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

void DisassemblyMacro::setMacroMemory(std::string _name, u32 _immediate, u8 _rt, int _dataSize)
{
	type = MACRO_MEMORYIMM;
	name = _name;
	immediate = _immediate;
	rt = _rt;
	dataSize = _dataSize;
	numOpcodes = 2;
}

bool DisassemblyMacro::disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols)
{
	char buffer[64];

	const char* addressSymbol;
	switch (type)
	{
	case MACRO_LI:
		dest.name = name;
		
		addressSymbol = DisassemblyManager::getCpu()->findSymbolForAddress(immediate);
		if (addressSymbol != NULL && insertSymbols)
		{
			sprintf(buffer,"%s,%s",DisassemblyManager::getCpu()->GetRegName(0,rt),addressSymbol);
		} else {
			sprintf(buffer,"%s,0x%08X",DisassemblyManager::getCpu()->GetRegName(0,rt),immediate);
		}

		dest.params = buffer;
		
		dest.info.hasRelevantAddress = true;
		dest.info.releventAddress = immediate;
		break;
	case MACRO_MEMORYIMM:
		dest.name = name;

		addressSymbol = DisassemblyManager::getCpu()->findSymbolForAddress(immediate);
		if (addressSymbol != NULL && insertSymbols)
		{
			sprintf(buffer,"%s,%s",DisassemblyManager::getCpu()->GetRegName(0,rt),addressSymbol);
		} else {
			sprintf(buffer,"%s,0x%08X",DisassemblyManager::getCpu()->GetRegName(0,rt),immediate);
		}

		dest.params = buffer;

		dest.info.isDataAccess = true;
		dest.info.dataAddress = immediate;
		dest.info.dataSize = dataSize;

		dest.info.hasRelevantAddress = true;
		dest.info.releventAddress = immediate;
		break;
	default:
		return false;
	}

	dest.totalSize = getTotalSize();
	return true;
}
