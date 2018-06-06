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

#include <mutex>
#include "Common/CommonTypes.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPSAnalyst.h"

#ifdef _M_X64
typedef u64 HashType;
#else
typedef u32 HashType;
#endif

enum DisassemblyLineType { DISTYPE_OPCODE, DISTYPE_MACRO, DISTYPE_DATA, DISTYPE_OTHER };

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
	virtual bool disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols) = 0;
	virtual void getBranchLines(u32 start, u32 size, std::vector<BranchLine>& dest) { };
};

class DisassemblyFunction: public DisassemblyEntry
{
public:
	DisassemblyFunction(u32 _address, u32 _size);
	~DisassemblyFunction();
	void recheck() override;
	int getNumLines() override;
	int getLineNum(u32 address, bool findStart) override;
	u32 getLineAddress(int line) override;
	u32 getTotalSize() override { return size; };
	bool disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols) override;
	void getBranchLines(u32 start, u32 size, std::vector<BranchLine>& dest) override;

private:
	void generateBranchLines();
	void load();
	void clear();
	void addOpcodeSequence(u32 start, u32 end);

	u32 address;
	u32 size;
	HashType hash;
	std::vector<BranchLine> lines;
	std::map<u32,DisassemblyEntry*> entries;
	std::vector<u32> lineAddresses;
	std::recursive_mutex lock_;
};

class DisassemblyOpcode: public DisassemblyEntry
{
public:
	DisassemblyOpcode(u32 _address, int _num): address(_address), num(_num) { };
	virtual ~DisassemblyOpcode() { };
	void recheck() override { };
	int getNumLines() override { return num; };
	int getLineNum(u32 address, bool findStart) override { return (address - this->address) / 4; };
	u32 getLineAddress(int line) override { return address + line * 4; };
	u32 getTotalSize() override { return num * 4; };
	bool disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols) override;
	void getBranchLines(u32 start, u32 size, std::vector<BranchLine>& dest) override;

private:
	u32 address;
	int num;
};


class DisassemblyMacro: public DisassemblyEntry
{
public:
	DisassemblyMacro(u32 _address): address(_address) { };
	virtual ~DisassemblyMacro() { };

	void setMacroLi(u32 _immediate, u8 _rt);
	void setMacroMemory(std::string _name, u32 _immediate, u8 _rt, int _dataSize);

	void recheck() override { };
	int getNumLines() override { return 1; };
	int getLineNum(u32 address, bool findStart) override { return 0; };
	u32 getLineAddress(int line) override { return address; };
	u32 getTotalSize() override { return numOpcodes * 4; };
	bool disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols) override;
private:
	enum MacroType { MACRO_LI, MACRO_MEMORYIMM };

	MacroType type;
	std::string name;
	u32 immediate;
	u32 address;
	u32 numOpcodes;
	u8 rt;
	int dataSize;
};


class DisassemblyData: public DisassemblyEntry
{
public:
	DisassemblyData(u32 _address, u32 _size, DataType _type);
	virtual ~DisassemblyData() { };

	void recheck() override;
	int getNumLines() override { return (int)lines.size(); };
	int getLineNum(u32 address, bool findStart) override;
	u32 getLineAddress(int line) override { return lineAddresses[line]; };
	u32 getTotalSize() override { return size; };
	bool disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols) override;

private:
	void createLines();

	struct DataEntry
	{
		std::string text;
		u32 size;
		int lineNum;
	};

	u32 address;
	u32 size;
	HashType hash;
	DataType type;
	std::map<u32,DataEntry> lines;
	std::vector<u32> lineAddresses;
	std::recursive_mutex lock_;
};

class DisassemblyComment: public DisassemblyEntry
{
public:
	DisassemblyComment(u32 _address, u32 _size, std::string name, std::string param);
	virtual ~DisassemblyComment() { };

	void recheck() override { };
	int getNumLines() override { return 1; };
	int getLineNum(u32 address, bool findStart) override { return 0; };
	u32 getLineAddress(int line) override { return address; };
	u32 getTotalSize() override { return size; };
	bool disassemble(u32 address, DisassemblyLineInfo& dest, bool insertSymbols) override;

private:
	u32 address;
	u32 size;
	std::string name;
	std::string param;
};

class DebugInterface;

class DisassemblyManager
{
public:
	~DisassemblyManager();

	void clear();

	void setCpu(DebugInterface* _cpu) { cpu = _cpu; };
	void setMaxParamChars(int num) { maxParamChars = num; clear(); };
	void getLine(u32 address, bool insertSymbols, DisassemblyLineInfo& dest);
	void analyze(u32 address, u32 size);
	std::vector<BranchLine> getBranchLines(u32 start, u32 size);

	u32 getStartAddress(u32 address);
	u32 getNthPreviousAddress(u32 address, int n = 1);
	u32 getNthNextAddress(u32 address, int n = 1);

	static DebugInterface* getCpu() { return cpu; };
	static int getMaxParamChars() { return maxParamChars; };
private:
	static std::map<u32,DisassemblyEntry*> entries;
	static std::recursive_mutex entriesLock_;
	static DebugInterface* cpu;
	static int maxParamChars;
};

bool isInInterval(u32 start, u32 size, u32 value);
