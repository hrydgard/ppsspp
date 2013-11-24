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

#pragma once
#include "Globals.h"
#include "Core/MIPS/MIPSAnalyst.h"

enum DisassemblyLineType { DISTYPE_OPCODE, DISTYPE_DATA };

struct DisassemblyLineInfo
{
	DisassemblyLineType type;
	MIPSAnalyst::MipsOpcodeInfo info;
	std::string name;
	std::string params;
	u32 totalSize;
};

enum LineType { LINE_UP, LINE_DOWN, LINE_RIGHT };

struct BranchLine
{
	u32 first;
	u32 second;
	LineType type;
	int laneIndex;

	bool operator<(const BranchLine& other) const
	{
		return first < other.first;
	}
};

class DisassemblyEntry
{
public:
	virtual ~DisassemblyEntry() { };
	virtual void recheck() = 0;
	virtual int getNumLines() = 0;
	virtual int getLineNum(u32 address, bool findStart) = 0;
	virtual u32 getLineAddress(int line) = 0;
	virtual u32 getTotalSize() = 0;
	virtual bool disassemble(u32 address, DisassemblyLineInfo& dest) = 0;
};

class DisassemblyFunction: public DisassemblyEntry
{
public:
	DisassemblyFunction(u32 _address, u32 _size);
	virtual void recheck();
	virtual int getNumLines();
	virtual int getLineNum(u32 address, bool findStart);
	virtual u32 getLineAddress(int line);
	virtual u32 getTotalSize() { return size; };
	virtual bool disassemble(u32 address, DisassemblyLineInfo& dest);
private:
	u32 computeHash();
	void generateBranchLines();
	void load();
	void clear();

	u32 address;
	u32 size;
	u32 hash;
	std::vector<BranchLine> lines;
	std::map<u32,DisassemblyEntry*> entries;
	std::vector<u32> lineAddresses;
};

class DisassemblyOpcode: public DisassemblyEntry
{
public:
	DisassemblyOpcode(u32 _address): address(_address) { };
	virtual ~DisassemblyOpcode() { };
	virtual void recheck() { };
	virtual int getNumLines() { return 1; };
	virtual int getLineNum(u32 address, bool findStart) { return 0; };
	virtual u32 getLineAddress(int line) { return address; };
	virtual u32 getTotalSize() { return 4; };
	virtual bool disassemble(u32 address, DisassemblyLineInfo& dest);
private:
	u32 address;
};


class DisassemblyMacro: public DisassemblyEntry
{
public:
	DisassemblyMacro(u32 _address): address(_address) { };
	virtual ~DisassemblyMacro() { };
	
	void setMacroLi(u32 _immediate, u8 _rt);
	void setMacroMemory(std::string _name, u32 _immediate, u8 _rt);
	
	virtual void recheck() { };
	virtual int getNumLines() { return 1; };
	virtual int getLineNum(u32 address, bool findStart) { return 0; };
	virtual u32 getLineAddress(int line) { return address; };
	virtual u32 getTotalSize() { return numOpcodes*4; };
	virtual bool disassemble(u32 address, DisassemblyLineInfo& dest) ;
private:
	enum MacroType { MACRO_LI, MACRO_MEMORYIMM };

	MacroType type;
	std::string name;
	u32 immediate;
	u32 address;
	u32 numOpcodes;
	u8 rt;
};


class DebugInterface;

class DisassemblyManager
{
public:

	void setCpu(DebugInterface* _cpu) { cpu = _cpu; };
	DisassemblyLineInfo getLine(u32 address, bool insertSymbols);
	void analyze(u32 address, u32 size);

	u32 getStartAddress(u32 address);
	u32 getNthPreviousAddress(u32 address, int n = 1);
	u32 getNthNextAddress(u32 address, int n = 1);

	static DebugInterface* getCpu() { return cpu; };
private:
	DisassemblyEntry* getEntry(u32 address);
	static std::map<u32,DisassemblyEntry*> entries;
	static DebugInterface* cpu;
};