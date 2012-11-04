// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include "../MIPSAnalyst.h"
#include "ArmEmitter.h"

using namespace ArmGen;
enum FlushMode
{
	FLUSH_ALL
};

enum GrabMode
{
	M_READ = 1,
	M_WRITE = 2, 
	M_READWRITE = 3,
};

enum Loc {
	LOC_IMM,
	LOC_REG,
	LOC_MEM
};

struct Location
{
	Loc loc;
	bool IsSimpleReg() const {return loc == LOC_REG;}
	ARMReg GetSimpleReg() const {return reg;}
	bool IsImm() const { return loc == LOC_IMM; }
	void SetImm32(u32 i) {loc = LOC_IMM; imm = i;}
	void SetM(void *p) {loc = LOC_MEM; ptr = (u32 *)p;}
	void SetReg(ARMReg r) {loc = LOC_REG; reg = r;}
	
	union {
		u32 *ptr;
		ARMReg reg;
		u32 imm;
	};
};

struct MIPSCachedReg
{
	Location location;
	bool away;	// value not in source register
};

struct ARMCachedReg
{
	int ppcReg;
	bool dirty;
	bool free;
};

typedef int XReg;
typedef int PReg;

#define NUMARMREGS 15

class RegCache
{
private:
	bool locks[32];
	bool saved_locks[32];
	bool saved_xlocks[NUMARMREGS];

protected:
	bool xlocks[NUMARMREGS];
	MIPSCachedReg regs[32];
	ARMCachedReg xregs[NUMARMREGS];

	MIPSCachedReg saved_regs[32];
	ARMCachedReg saved_xregs[NUMARMREGS];

	virtual const int *GetAllocationOrder(int &count) = 0;
	
	ARMXEmitter *emit;

public:
	MIPSState *mips;
	RegCache();

	virtual ~RegCache() {}
	virtual void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats) = 0;

	void DiscardRegContentsIfCached(int preg);
	void SetEmitter(ARMXEmitter *emitter) {emit = emitter;}

	void FlushR(ARMReg reg); 
	void FlushR(ARMReg reg, ARMReg reg2) {FlushR(reg); FlushR(reg2);}
	void FlushLockX(ARMReg reg) {
		FlushR(reg);
		LockX(reg);
	}
	void FlushLockX(ARMReg reg1, ARMReg reg2) {
		FlushR(reg1); FlushR(reg2);
		LockX(reg1); LockX(reg2);
	}
	virtual void Flush(FlushMode mode);
	// virtual void Flush(PPCAnalyst::CodeOp *op) {Flush(FLUSH_ALL);}
	int SanityCheck() const;
	void KillImmediate(int preg, bool doLoad, bool makeDirty);

	//TODO - instead of doload, use "read", "write"
	//read only will not set dirty flag
	virtual void BindToRegister(int preg, bool doLoad = true, bool makeDirty = true) = 0;
	virtual void StoreFromRegister(int preg) = 0;

	const Location &R(int preg) const {return regs[preg].location;}
	ARMReg RX(int preg) const
	{
		if (regs[preg].away && regs[preg].location.IsSimpleReg()) 
			return regs[preg].location.GetSimpleReg(); 
		PanicAlert("Not so simple - %i", preg); 
		return (ARMReg)-1;
	}
	virtual Location GetDefaultLocation(int reg) const = 0;

	// Register locking. A locked registers will not be spilled when trying to find a new free register.
	void Lock(int p1, int p2=0xff, int p3=0xff, int p4=0xff);
	void LockX(int x1, int x2=0xff, int x3=0xff, int x4=0xff);
	void UnlockAll();
	void UnlockAllX();

	bool IsFreeX(int xreg) const;

	ARMReg GetFreeXReg();

	void SaveState();
	void LoadState();
};

class GPRRegCache : public RegCache
{
public:
	void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats);
	void BindToRegister(int preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(int preg);
	Location GetDefaultLocation(int reg) const;
	const int *GetAllocationOrder(int &count);
	void SetImmediate32(int preg, u32 immValue);
};


class FPURegCache : public RegCache
{
public:
	void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats);
	void BindToRegister(int preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(int preg);
	const int *GetAllocationOrder(int &count);
	Location GetDefaultLocation(int reg) const;
};
