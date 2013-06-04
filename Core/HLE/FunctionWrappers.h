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

#include "../../Globals.h"
// For easy parameter parsing and return value processing.

// 64bit wrappers

template<u64 func()> void WrapU64_V() {
	u64 retval = func();
	currentMIPS->r[2] = retval & 0xFFFFFFFF;
	currentMIPS->r[3] = (retval >> 32) & 0xFFFFFFFF;
}

template<u64 func(u32)> void WrapU64_U() {
	u64 retval = func(PARAM(0));
	currentMIPS->r[2] = retval & 0xFFFFFFFF;
	currentMIPS->r[3] = (retval >> 32) & 0xFFFFFFFF;
}

template<int func(u32, u64)> void WrapI_UU64() {
	u64 param_one = currentMIPS->r[6];
	param_one |= (u64)(currentMIPS->r[7]) << 32;
	int retval = func(PARAM(0), param_one);
	RETURN(retval);
}

template<u32 func(u32, u64)> void WrapU_UU64() {
	u64 param_one = currentMIPS->r[6];
	param_one |= (u64)(currentMIPS->r[7]) << 32;
	u32 retval = func(PARAM(0), param_one);
	RETURN(retval);
}

template<int func(u32, u32, u64)> void WrapI_UUU64() {
	u64 param_two = currentMIPS->r[6];
	param_two |= (u64)(currentMIPS->r[7]) << 32;
	int retval = func(PARAM(0), PARAM(1), param_two);
	RETURN(retval);
}

template<u32 func(int, s64, int)> void WrapU_II64I() {
	s64 param_one = currentMIPS->r[6];
	param_one |= (s64)(currentMIPS->r[7]) << 32;
	u32 retval = func(PARAM(0), param_one, PARAM(4));
	RETURN(retval);
}

template<u32 func(u32, u64, u32, u32)> void WrapU_UU64UU() {
	u64 param_one = currentMIPS->r[6];
	param_one |= (u64)(currentMIPS->r[7]) << 32;
	u32 retval = func(PARAM(0), param_one, PARAM(4), PARAM(5));
	RETURN(retval);
}

template<s64 func(int, s64, int)> void WrapI64_II64I() {
	s64 param_one = currentMIPS->r[6];
	param_one |= (s64)(currentMIPS->r[7]) << 32;
	s64 retval = func(PARAM(0), param_one, PARAM(4));
	currentMIPS->r[2] = (retval >> 0) & 0xFFFFFFFF;
	currentMIPS->r[3] = (retval >> 32) & 0xFFFFFFFF;
}

//32bit wrappers
template<void func()> void WrapV_V() {
	func();
}

template<u32 func()> void WrapU_V() {
	RETURN(func());
}

template<u32 func(int, void *, int)> void WrapU_IVI() {
	u32 retval = func(PARAM(0), Memory::GetPointer(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<int func(int, const char *, u32, void *, void *, u32, int)> void WrapI_ICUVVUI() {
	u32 retval = func(PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), Memory::GetPointer(PARAM(3)),Memory::GetPointer(PARAM(4)), PARAM(5), PARAM(6) );
	RETURN(retval);
}

template<u32 func(int, void *)> void WrapU_IV() {
	u32 retval = func(PARAM(0), Memory::GetPointer(PARAM(1)));
	RETURN(retval);
}

template<float func()> void WrapF_V() {
	RETURNF(func());
}

// TODO: Not sure about the floating point parameter passing
template<float func(int, float, u32)> void WrapF_IFU() {
	RETURNF(func(PARAM(0), PARAMF(0), PARAM(1)));
}

template<u32 func(u32)> void WrapU_U() {
	u32 retval = func(PARAM(0));
	RETURN(retval);
}

template<u32 func(u32, int)> void WrapU_UI() {
	u32 retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<int func(u32)> void WrapI_U() {
	int retval = func(PARAM(0));
	RETURN(retval);
}

template<int func(u32, int)> void WrapI_UI() {
	int retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<int func(u32, int, int, u32)> void WrapI_UIIU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(int, u32, int)> void WrapU_IUI() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<int func(u32, u32)> void WrapI_UU() {
	int retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<int func(u32, float, float)> void WrapI_UFF() {
	// Not sure about the float arguments.
	int retval = func(PARAM(0), currentMIPS->f[12], currentMIPS->f[13]);
	RETURN(retval);
}

template<int func(u32, u32, u32)> void WrapI_UUU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<int func(u32, u32, u32, int)> void WrapI_UUUI() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<int func(u32, u32, u32, int, int, int,int )> void WrapI_UUUIIII() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<int func(u32, u32, u32, u32)> void WrapI_UUUU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<int func(u32, u32, u32, u32, u32)> void WrapI_UUUUU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<int func()> void WrapI_V() {
	int retval = func();
	RETURN(retval);
}

template<u32 func(int)> void WrapU_I() {
	u32 retval = func(PARAM(0));
	RETURN(retval);
}

template<u32 func(int, int, u32)> void WrapU_IIU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<int func(int)> void WrapI_I() {
	int retval = func(PARAM(0));
	RETURN(retval);
}

template<void func(u32)> void WrapV_U() {
	func(PARAM(0));
}

template<void func(int)> void WrapV_I() {
	func(PARAM(0));
}

template<void func(u32, u32)> void WrapV_UU() {
	func(PARAM(0), PARAM(1));
}

template<void func(int, int)> void WrapV_II() {
	func(PARAM(0), PARAM(1));
}

template<void func(u32, const char *)> void WrapV_UC() {
	func(PARAM(0), Memory::GetCharPointer(PARAM(1)));
}

template<int func(u32, const char *)> void WrapI_UC() {
	int retval = func(PARAM(0), Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<u32 func(u32, int , int , int, int, int)> void WrapU_UIIIII() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<u32 func(u32, int , int , int, u32)> void WrapU_UIIIU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<u32 func(u32, int , int , int, int, int, int)> void WrapU_UIIIIII() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<u32 func(u32, u32)> void WrapU_UU() {
	u32 retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<u32 func(u32, u32, int)> void WrapU_UUI() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<u32 func(u32, u32, int, int)> void WrapU_UUII() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(const char *, u32, u32, u32)> void WrapU_CUUU() {
	u32 retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<void func(u32, int, u32, int, int)> void WrapV_UIUII() {
	func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
}

template<u32 func(u32, int, u32, int, int)> void WrapU_UIUII() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<u32 func(u32, int, u32, int)> void WrapU_UIUI() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(u32, int, u32)> void WrapU_UIU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<u32 func(u32, int, u32, u32)> void WrapU_UIUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(u32, int, int)> void WrapU_UII() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<u32 func(u32, int, int, u32)> void WrapU_UIIU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<int func(u32, int, int, u32, u32)> void WrapI_UIIUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<void func(u32, int, int, int)> void WrapV_UIII() {
	func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
}

template<void func(u32, int, int, int, int, int)> void WrapV_UIIIII() {
	func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
}

template<void func(u32, int, int)> void WrapV_UII() {
	func(PARAM(0), PARAM(1), PARAM(2));
}

template<u32 func(int, u32)> void WrapU_IU() {
	int retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<int func(int, u32)> void WrapI_IU() {
	int retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<int func(u32, u32, int)> void WrapI_UUI() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<int func(int, int)> void WrapI_II() {
	int retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<int func(int, int, int)> void WrapI_III() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<int func(int, int, int, int)> void WrapI_IIII() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<int func(int, const char *, int, u32, u32)> void WrapI_ICIUU() {
	int retval = func(PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<int func(int, int, u32)> void WrapI_IIU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<void func(int, u32)> void WrapV_IU() {
	func(PARAM(0), PARAM(1));
}

template<void func(u32, int)> void WrapV_UI() {
	func(PARAM(0), PARAM(1));
}

template<u32 func(const char *)> void WrapU_C() {
	u32 retval = func(Memory::GetCharPointer(PARAM(0)));
	RETURN(retval);
}

template<u32 func(const char *, const char *, const char *, u32)> void WrapU_CCCU() {
	u32 retval = func(Memory::GetCharPointer(PARAM(0)),
			Memory::GetCharPointer(PARAM(1)), Memory::GetCharPointer(PARAM(2)),
			PARAM(3));
	RETURN(retval);
}

template<int func(const char *)> void WrapI_C() {
	int retval = func(Memory::GetCharPointer(PARAM(0)));
	RETURN(retval);
}

template<int func(const char *, u32)> void WrapI_CU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
	RETURN(retval);
}

template<int func(const char *, u32, u32)> void WrapI_CUU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<int func(const char *, u32, u32, u32)> void WrapI_CUUU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3));
	RETURN(retval);
}

template<int func(const char *, u32, u32, int, u32, u32)> void WrapI_CUUIUU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<int func(const char *, int, u32, u32, u32)> void WrapI_CIUUU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<int func(const char *, u32, u32, u32, u32, u32)> void WrapI_CUUUUU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<u32 func(const char *, u32)> void WrapU_CU() {
	u32 retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
	RETURN((u32) retval);
}

template<u32 func(u32, const char *)> void WrapU_UC() {
	u32 retval = func(PARAM(0), Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<u32 func(const char *, u32, u32)> void WrapU_CUU() {
	u32 retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN((u32) retval);
}

template<u32 func(int, int, int)> void WrapU_III() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<u32 func(int, int)> void WrapU_II() {
	u32 retval = func(PARAM(0), PARAM(1));
	RETURN(retval);
}

template<u32 func(int, int, int, int)> void WrapU_IIII() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(int, u32, u32)> void WrapU_IUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<u32 func(int, u32, u32, u32)> void WrapU_IUUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(int, u32, u32, u32, u32)> void WrapU_IUUUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<u32 func(u32, u32, u32)> void WrapU_UUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<void func(int, u32, u32)> void WrapV_IUU() {
	func(PARAM(0), PARAM(1), PARAM(2));
}

template<void func(int, int, u32)> void WrapV_IIU() {
	func(PARAM(0), PARAM(1), PARAM(2));
}

template<void func(u32, int, u32)> void WrapV_UIU() {
	func(PARAM(0), PARAM(1), PARAM(2));
}

template<int func(u32, int, u32)> void WrapI_UIU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<void func(int, u32, u32, u32, u32)> void WrapV_IUUUU() {
	func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
}

template<void func(u32, u32, u32)> void WrapV_UUU() {
	func(PARAM(0), PARAM(1), PARAM(2));
}

template<void func(u32, u32, u32, u32)> void WrapV_UUUU() {
	func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
}

template<void func(const char *, u32, int, u32)> void WrapV_CUIU() {
	func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3));
}

template<int func(const char *, u32, int, u32)> void WrapI_CUIU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<void func(u32, const char *, u32, int, u32)> void WrapV_UCUIU() {
	func(PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3),
			PARAM(4));
}

template<int func(u32, const char *, u32, int, u32)> void WrapI_UCUIU() {
	int retval = func(PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<void func(const char *, u32, int, int, u32)> void WrapV_CUIIU() {
	func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3),
			PARAM(4));
}

template<int func(const char *, u32, int, int, u32)> void WrapI_CUIIU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<u32 func(u32, u32, u32, u32)> void WrapU_UUUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(u32, const char *, u32, u32)> void WrapU_UCUU() {
	u32 retval = func(PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(u32, u32, u32, int)> void WrapU_UUUI() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(u32, u32, u32, int, u32)> void WrapU_UUUIU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<u32 func(u32, u32, u32, int, u32, int)> void WrapU_UUUIUI() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<u32 func(u32, u32, int, u32)> void WrapU_UUIU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<u32 func(u32, int, int, int)> void WrapU_UIII() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<int func(int, u32, u32, u32, u32)> void WrapI_IUUUU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<int func(int, u32, int, int)> void WrapI_IUII() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}
template<u32 func(u32, u32, u32, u32, u32)> void WrapU_UUUUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<void func(u32, u32, u32, u32, u32)> void WrapV_UUUUU() {
	func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
}

template<u32 func(const char *, const char *)> void WrapU_CC() {
	int retval = func(Memory::GetCharPointer(PARAM(0)),
			Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<void func(const char *, int)> void WrapV_CI() {
	func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
}

template<u32 func(const char *, int)> void WrapU_CI() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
	RETURN(retval);
}

template<u32 func(const char *, int, int)> void WrapU_CII() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<int func(const char *, int, u32, int, u32)> void WrapU_CIUIU() {
	int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<u32 func(const char *, int, u32, int, u32, int)> void WrapU_CIUIUI() {
	u32 retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<u32 func(u32, u32, u32, u32, u32, u32)> void WrapU_UUUUUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4),
			PARAM(5));
	RETURN(retval);
}

template<int func(int, u32, u32, u32)> void WrapI_IUUU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<int func(int, u32, u32)> void WrapI_IUU() {
	int retval = func(PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<u32 func(u32, u32, u32, u32, u32, u32, u32)> void WrapU_UUUUUUU() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
  RETURN(retval);
}

template<int func(u32, int, u32, u32)> void WrapI_UIUU() {
	u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}
