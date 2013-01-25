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

#include "x64Emitter.h"
#include "../MIPSAnalyst.h"
using namespace Gen;
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

struct MIPSCachedReg
{
	OpArg location;
	bool away;  // value not in source register
};

struct X64CachedReg
{
	int mipsReg;
	bool dirty;
	bool free;
};

typedef int XReg;
typedef int PReg;

#ifdef _M_X64
#define NUMXREGS 16
#elif _M_IX86
#define NUMXREGS 8
#endif

class RegCache
{
private:
	bool locks[32];
	bool saved_locks[32];
	bool saved_xlocks[NUMXREGS];

protected:
	bool xlocks[NUMXREGS];
	MIPSCachedReg regs[32];
	X64CachedReg xregs[NUMXREGS];

	MIPSCachedReg saved_regs[32];
	X64CachedReg saved_xregs[NUMXREGS];

	virtual const int *GetAllocationOrder(int &count) = 0;
	
	XEmitter *emit;

public:
  MIPSState *mips;
	RegCache();

	virtual ~RegCache() {}
	virtual void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats) = 0;

	void DiscardRegContentsIfCached(int preg);
	void SetEmitter(XEmitter *emitter) {emit = emitter;}

	void FlushR(X64Reg reg); 
	void FlushR(X64Reg reg, X64Reg reg2) {FlushR(reg); FlushR(reg2);}
	void FlushLockX(X64Reg reg) {
		FlushR(reg);
		LockX(reg);
	}
	void FlushLockX(X64Reg reg1, X64Reg reg2) {
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

	const OpArg &R(int preg) const {return regs[preg].location;}
	X64Reg RX(int preg) const
	{
		if (regs[preg].away && regs[preg].location.IsSimpleReg()) 
			return regs[preg].location.GetSimpleReg(); 
		PanicAlert("Not so simple - %i", preg); 
		return (X64Reg)-1;
	}
	virtual OpArg GetDefaultLocation(int reg) const = 0;

	// Register locking.
	void Lock(int p1, int p2=0xff, int p3=0xff, int p4=0xff);
	void LockX(int x1, int x2=0xff, int x3=0xff, int x4=0xff);
	void UnlockAll();
	void UnlockAllX();

	bool IsFreeX(int xreg) const;

	X64Reg GetFreeXReg();

	void SaveState();
	void LoadState();
};

class GPRRegCache : public RegCache
{
public:
	void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats);
	void BindToRegister(int preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(int preg);
	OpArg GetDefaultLocation(int reg) const;
	const int *GetAllocationOrder(int &count);
	void SetImmediate32(int preg, u32 immValue);
	bool IsImmediate(int preg) const;
	u32 GetImmediate32(int preg) const;
};


class FPURegCache : public RegCache
{
public:
	void Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats);
	void BindToRegister(int preg, bool doLoad = true, bool makeDirty = true);
	void StoreFromRegister(int preg);
	const int *GetAllocationOrder(int &count);
	OpArg GetDefaultLocation(int reg) const;
};
