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

#include <type_traits>

#include "Common/CommonTypes.h"
#include "Core/HLE/HLE.h"
#include "Core/MemMap.h"

// For easy parameter parsing and return value processing.

// Each wrapper names the signature it unpacks. The function may also take the MIPSState it runs on
// as an extra first parameter, which is how an implementation gets at the CPU (instead of currentMIPS).
template <typename Sig>
struct HLEFuncTypes;

template <typename R, typename... Args>
struct HLEFuncTypes<R(Args...)> {
	typedef R (*Plain)(Args...);
	typedef R (*WithState)(MIPSState *, Args...);
};

template <typename Sig, auto func, typename... CallArgs>
inline auto HLECall(MIPSState *mips, CallArgs... args) {
	typedef HLEFuncTypes<Sig> Types;
	typedef std::decay_t<decltype(func)> FuncType;
	if constexpr (std::is_same_v<FuncType, typename Types::WithState>) {
		return func(mips, args...);
	} else {
		static_assert(std::is_same_v<FuncType, typename Types::Plain>, "HLE function doesn't match the signature of its wrapper");
		return func(args...);
	}
}

// 64bit wrappers
// 64bit values are always "aligned" in regs (never start on an odd reg.)

template<auto func> void WrapU64_V(MIPSState *mips) {
	u64 retval = HLECall<u64(), func>(mips);
	RETURN64(retval);
}

template<auto func> void WrapU64_U(MIPSState *mips) {
	u64 retval = HLECall<u64(u32), func>(mips, PARAM(0));
	RETURN64(retval);
}

template<auto func> void WrapU64_I(MIPSState *mips) {
	u64 retval = HLECall<u64(int), func>(mips, PARAM(0));
	RETURN64(retval);
}

template<auto func> void WrapU64_UU64(MIPSState *mips) {
	u64 retval = HLECall<u64(u32, u64), func>(mips, PARAM(0), PARAM64(2));
	RETURN64(retval);
}

template<auto func> void WrapU64_IU64(MIPSState *mips) {
	u64 retval = HLECall<u64(int, u64), func>(mips, PARAM(0), PARAM64(2));
	RETURN64(retval);
}

template<auto func> void WrapI_IU64(MIPSState *mips) {
	int retval = HLECall<int(int, u64), func>(mips, PARAM(0), PARAM64(2));
	RETURN(retval);
}

template<auto func> void WrapI_UU64(MIPSState *mips) {
	int retval = HLECall<int(u32, u64), func>(mips, PARAM(0), PARAM64(2));
	RETURN(retval);
}

template<auto func> void WrapU_UU64(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u64), func>(mips, PARAM(0), PARAM64(2));
	RETURN(retval);
}

template<auto func> void WrapI_UUU64(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, u64), func>(mips, PARAM(0), PARAM(1), PARAM64(2));
	RETURN(retval);
}

template<auto func> void WrapU_II64I(MIPSState *mips) {
	u32 retval = HLECall<u32(int, s64, int), func>(mips, PARAM(0), PARAM64(2), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapU_UU64UU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u64, u32, u32), func>(mips, PARAM(0), PARAM64(2), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapU_IU64UU(MIPSState *mips) {
	u32 retval = HLECall<u32(int, u64, u32, u32), func>(mips, PARAM(0), PARAM64(2), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_IICU64(MIPSState *mips) {
	int retval = HLECall<int(int, int, const char*, u64), func>(mips, PARAM(0), PARAM(1), Memory::GetCharPointer(PARAM(2)), PARAM64(4));
	RETURN(retval);
}

template<auto func> void WrapI64_II64I(MIPSState *mips) {
	s64 retval = HLECall<s64(int, s64, int), func>(mips, PARAM(0), PARAM64(2), PARAM(4));
	RETURN64(retval);
}

//32bit wrappers
template<auto func> void WrapV_V(MIPSState *mips) {
	HLECall<void(), func>(mips);
}

template<auto func> void WrapU_V(MIPSState *mips) {
	RETURN((HLECall<u32(), func>(mips)));
}

template<auto func> void WrapU_IVI(MIPSState *mips) {
	u32 retval = HLECall<u32(int, void *, int), func>(mips, PARAM(0), Memory::GetPointerWriteOrException(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_CIIU(MIPSState *mips) {
	u32 retval = HLECall<int(const char *, int, int, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_ICUVVUI(MIPSState *mips) {
	u32 retval = HLECall<int(int, const char *, u32, void *, void *, u32, int), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), Memory::GetPointerWriteOrException(PARAM(3)),Memory::GetPointerWriteOrException(PARAM(4)), PARAM(5), PARAM(6) );
	RETURN(retval);
}

// Hm, do so many params get passed in registers?
template<auto func> void WrapI_CICIIIII(MIPSState *mips) {
	u32 retval = HLECall<int(const char *, int, const char *, int, int, int, int, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), Memory::GetCharPointer(PARAM(2)),
		PARAM(3), PARAM(4), PARAM(5), PARAM(6), PARAM(7));
	RETURN(retval);
}

// Hm, do so many params get passed in registers?
template<auto func> void WrapI_CIIIIII(MIPSState *mips) {
	u32 retval = HLECall<int(const char *, int, int, int, int, int, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
		PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<auto func> void WrapI_IIIC(MIPSState *mips) {
	u32 retval = HLECall<int(int, int, int, const char *), func>(mips, PARAM(0), PARAM(1), PARAM(2), Memory::GetCharPointer(PARAM(3)));
	RETURN(retval);
}

// Hm, do so many params get passed in registers?
template<auto func> void WrapI_IIIIIIU(MIPSState *mips) {
	u32 retval = HLECall<int(int, int, int, int, int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

// Hm, do so many params get passed in registers?
template<auto func> void WrapI_IIIIIIIIU(MIPSState *mips) {
	u32 param8 = *(const u32_le *)Memory::GetPointerWriteOrException(currentMIPS->r[29]); //Fixed 9th parameter, thanks to Kingcom
	u32 retval = HLECall<int(int, int, int, int, int, int, int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6), PARAM(7), param8);
	RETURN(retval);
}

template<auto func> void WrapU_IV(MIPSState *mips) {
	u32 retval = HLECall<u32(int, void *), func>(mips, PARAM(0), Memory::GetPointerWriteOrException(PARAM(1)));
	RETURN(retval);
}

template<auto func> void WrapF_V(MIPSState *mips) {
	RETURNF((HLECall<float(), func>(mips)));
}

// TODO: Not sure about the floating point parameter passing
template<auto func> void WrapF_IFU(MIPSState *mips) {
	RETURNF((HLECall<float(int, float, u32), func>(mips, PARAM(0), PARAMF(0), PARAM(1))));
}

template<auto func> void WrapU_U(MIPSState *mips) {
	u32 retval = HLECall<u32(u32), func>(mips, PARAM(0));
	RETURN(retval);
}

template<auto func> void WrapU_UI(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapI_U(MIPSState *mips) {
	int retval = HLECall<int(u32), func>(mips, PARAM(0));
	RETURN(retval);
}

template<auto func> void WrapI_UI(MIPSState *mips) {
	int retval = HLECall<int(u32, int), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapI_UIIU(MIPSState *mips) {
	int retval = HLECall<int(u32, int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_IUI(MIPSState *mips) {
	u32 retval = HLECall<u32(int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_UU(MIPSState *mips) {
	int retval = HLECall<int(u32, u32), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapI_UFF(MIPSState *mips) {
	// Not sure about the float arguments.
	int retval = HLECall<int(u32, float, float), func>(mips, PARAM(0), PARAMF(0), PARAMF(1));
	RETURN(retval);	
}

template<auto func> void WrapI_UUU(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_UUUI(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_UUUIIII(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, u32, int, int, int,int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<auto func> void WrapI_UUUU(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_UUUUU(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_UCUUI(MIPSState *mips) {
	int retval = HLECall<int(u32, const char*, u32, u32, int), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_UICUU(MIPSState *mips) {
	int retval = HLECall<int(u32, int, const char*, u32, u32), func>(mips, PARAM(0), PARAM(1), Memory::GetCharPointer(PARAM(2)), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_UUIC(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, int, const char*), func>(mips, PARAM(0), PARAM(1), PARAM(2), Memory::GetCharPointer(PARAM(3)));
	RETURN(retval);
}

template<auto func> void WrapI_UUIUI(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_V(MIPSState *mips) {
	int retval = HLECall<int(), func>(mips);
	RETURN(retval);
}

template<auto func> void WrapU_I(MIPSState *mips) {
	u32 retval = HLECall<u32(int), func>(mips, PARAM(0));
	RETURN(retval);
}

template<auto func> void WrapU_IIU(MIPSState *mips) {
	u32 retval = HLECall<u32(int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_I(MIPSState *mips) {
	int retval = HLECall<int(int), func>(mips, PARAM(0));
	RETURN(retval);
}

template<auto func> void WrapV_U(MIPSState *mips) {
	HLECall<void(u32), func>(mips, PARAM(0));
}

template<auto func> void WrapV_I(MIPSState *mips) {
	HLECall<void(int), func>(mips, PARAM(0));
}

template<auto func> void WrapV_UU(MIPSState *mips) {
	HLECall<void(u32, u32), func>(mips, PARAM(0), PARAM(1));
}

template<auto func> void WrapV_II(MIPSState *mips) {
	HLECall<void(int, int), func>(mips, PARAM(0), PARAM(1));
}

template<auto func> void WrapV_UC(MIPSState *mips) {
	HLECall<void(u32, const char *), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)));
}

template<auto func> void WrapI_UC(MIPSState *mips) {
	int retval = HLECall<int(u32, const char *), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<auto func> void WrapI_UCI(MIPSState *mips) {
	int retval = HLECall<int(u32, const char *, int), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_UIIUUU(MIPSState *mips) {
	int retval = HLECall<int(u32, int, int, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapU_UIIIII(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int , int , int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapU_IIIIIU(MIPSState *mips) {
	u32 retval = HLECall<u32(int, int, int, int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapU_UIIIU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int , int , int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapU_UIIIIII(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int , int , int, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<auto func> void WrapU_UU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapU_UUI(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_UUII(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_CUUU(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *, u32, u32, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapV_UIUII(MIPSState *mips) {
	HLECall<void(u32, int, u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
}

template<auto func> void WrapU_UIUII(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int, u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_UIUII(MIPSState *mips) {
	int retval = HLECall<int(u32, int, u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapU_UIUI(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_UIUI(MIPSState *mips) {
	u32 retval = HLECall<int(u32, int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_UIU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_UIUU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_UII(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_UIIU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_UIIUU(MIPSState *mips) {
	u32 retval = HLECall<int(u32, int, int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_UUII(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_UUIII(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapV_UIII(MIPSState *mips) {
	HLECall<void(u32, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
}

template<auto func> void WrapV_UIIIII(MIPSState *mips) {
	HLECall<void(u32, int, int, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
}

template<auto func> void WrapV_UII(MIPSState *mips) {
	HLECall<void(u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
}

template<auto func> void WrapU_IU(MIPSState *mips) {
	int retval = HLECall<u32(int, u32), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapI_IU(MIPSState *mips) {
	int retval = HLECall<int(int, u32), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapI_UUI(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_UUIU(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_II(MIPSState *mips) {
	int retval = HLECall<int(int, int), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapI_III(MIPSState *mips) {
	int retval = HLECall<int(int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_IUI(MIPSState *mips) {
	int retval = HLECall<int(int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_IIII(MIPSState *mips) {
	int retval = HLECall<int(int, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_UIII(MIPSState *mips) {
	int retval = HLECall<int(u32, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_IIIUI(MIPSState *mips) {
	int retval = HLECall<int(int, int, int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_IIIUU(MIPSState *mips) {
	int retval = HLECall<int(int, int, int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_IUUII(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_IUUIII(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_ICIUU(MIPSState *mips) {
	int retval = HLECall<int(int, const char *, int, u32, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_IIU(MIPSState *mips) {
	int retval = HLECall<int(int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapV_IU(MIPSState *mips) {
	HLECall<void(int, u32), func>(mips, PARAM(0), PARAM(1));
}

template<auto func> void WrapV_UI(MIPSState *mips) {
	HLECall<void(u32, int), func>(mips, PARAM(0), PARAM(1));
}

template<auto func> void WrapU_C(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *), func>(mips, Memory::GetCharPointer(PARAM(0)));
	RETURN(retval);
}

template<auto func> void WrapU_CCCU(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *, const char *, const char *, u32), func>(mips, Memory::GetCharPointer(PARAM(0)),
			Memory::GetCharPointer(PARAM(1)), Memory::GetCharPointer(PARAM(2)),
			PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_C(MIPSState *mips) {
	int retval = HLECall<int(const char *), func>(mips, Memory::GetCharPointer(PARAM(0)));
	RETURN(retval);
}

template<auto func> void WrapI_CU(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapI_ICU(MIPSState *mips) {
	int retval = HLECall<int(int, const char*, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_CUI(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_CUI(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *, u32, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_ICIU(MIPSState *mips) {
	int retval = HLECall<int(int, const char *, int, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_ICUU(MIPSState *mips) {
	int retval = HLECall<int(int, const char *, u32, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_ICUUU(MIPSState *mips) {
	int retval = HLECall<int(int, const char *, u32, u32, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_IIUU(MIPSState *mips) {
	int retval = HLECall<int(int, int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_CIU(MIPSState *mips) {
	int retval = HLECall<int(const char *, int, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_CUU(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_CUUU(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32, u32, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_CCII(MIPSState *mips) {
	int retval = HLECall<int(const char *, const char*, int, int), func>(mips, Memory::GetCharPointer(PARAM(0)), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_CUUIUU(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32, u32, int, u32, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_CIIUII(MIPSState *mips) {
	int retval = HLECall<int(const char *, int, int, u32, int, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
		PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_CIIIUUI(MIPSState *mips) {
	int retval = HLECall<int(const char *, int, int, int, u32, u32, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
		PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<auto func> void WrapI_CIUUU(MIPSState *mips) {
	int retval = HLECall<int(const char *, int, u32, u32, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_CUUUUU(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32, u32, u32, u32, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapU_CCU(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *, const char *, u32), func>(mips, Memory::GetCharPointer(PARAM(0)),
			Memory::GetCharPointer(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_CU(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1));
	RETURN((u32) retval);
}

template<auto func> void WrapU_UC(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, const char *), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<auto func> void WrapU_CUU(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *, u32, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN((u32) retval);
}

template<auto func> void WrapU_III(MIPSState *mips) {
	u32 retval = HLECall<u32(int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_II(MIPSState *mips) {
	u32 retval = HLECall<u32(int, int), func>(mips, PARAM(0), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapU_IIII(MIPSState *mips) {
	u32 retval = HLECall<u32(int, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_IUU(MIPSState *mips) {
	u32 retval = HLECall<u32(int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_IUUU(MIPSState *mips) {
	u32 retval = HLECall<u32(int, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_IUUUU(MIPSState *mips) {
	u32 retval = HLECall<u32(int, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapU_UUU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapV_IUU(MIPSState *mips) {
	HLECall<void(int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
}

template<auto func> void WrapV_IIU(MIPSState *mips) {
	HLECall<void(int, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
}

template<auto func> void WrapV_UIU(MIPSState *mips) {
	HLECall<void(u32, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
}

template<auto func> void WrapI_UIU(MIPSState *mips) {
	int retval = HLECall<int(u32, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapV_IUUUU(MIPSState *mips) {
	HLECall<void(int, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
}

template<auto func> void WrapV_UUU(MIPSState *mips) {
	HLECall<void(u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
}

template<auto func> void WrapV_UUUU(MIPSState *mips) {
	HLECall<void(u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
}

template<auto func> void WrapV_CUIU(MIPSState *mips) {
	HLECall<void(const char *, u32, int, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3));
}

template<auto func> void WrapI_CUIU(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32, int, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapV_UCUIU(MIPSState *mips) {
	HLECall<void(u32, const char *, u32, int, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3),
			PARAM(4));
}

template<auto func> void WrapI_UCUIU(MIPSState *mips) {
	int retval = HLECall<int(u32, const char *, u32, int, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapV_CUIIU(MIPSState *mips) {
	HLECall<void(const char *, u32, int, int, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3),
			PARAM(4));
}

template<auto func> void WrapI_CUIIU(MIPSState *mips) {
	int retval = HLECall<int(const char *, u32, int, int, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapU_UUUU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_UCUU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, const char *, u32, u32), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_UUUI(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_UUUIU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, u32, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapU_UUUIUI(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, u32, int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapU_UUIU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, int, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapU_UIII(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, int, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_IUUUU(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_IUUUUU(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_IUUIUU(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_IUII(MIPSState *mips) {
	int retval = HLECall<int(int, u32, int, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}
template<auto func> void WrapU_UUUUU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapV_UUUUU(MIPSState *mips) {
	HLECall<void(u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
}

template<auto func> void WrapU_CC(MIPSState *mips) {
	int retval = HLECall<u32(const char *, const char *), func>(mips, Memory::GetCharPointer(PARAM(0)),
			Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<auto func> void WrapV_CI(MIPSState *mips) {
	HLECall<void(const char *, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1));
}

template<auto func> void WrapU_CI(MIPSState *mips) {
	int retval = HLECall<u32(const char *, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1));
	RETURN(retval);
}

template<auto func> void WrapU_CII(MIPSState *mips) {
	int retval = HLECall<u32(const char *, int, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_CIUIU(MIPSState *mips) {
	int retval = HLECall<int(const char *, int, u32, int, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapU_CIUIUI(MIPSState *mips) {
	u32 retval = HLECall<u32(const char *, int, u32, int, u32, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2),
			PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapU_UUUUUU(MIPSState *mips) {
	u32 retval = HLECall<u32(u32, u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4),
			PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_IUUU(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_IUUI(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_IUIIUU(MIPSState *mips) {
	int retval = HLECall<int(int, u32, int, int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_IUIIUI(MIPSState *mips) {
	int retval = HLECall<int(int, u32, int, int, u32, int), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
	RETURN(retval);
}

template<auto func> void WrapI_IUU(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_UUUUUUU(MIPSState *mips) {
  u32 retval = HLECall<u32(u32, u32, u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
  RETURN(retval);
}

template<auto func> void WrapI_UUUUUUU(MIPSState *mips) {
	int retval = HLECall<int(u32, u32, u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<auto func> void WrapI_IUUUUUU(MIPSState *mips) {
	int retval = HLECall<int(int, u32, u32, u32, u32, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5), PARAM(6));
	RETURN(retval);
}

template<auto func> void WrapI_UIUU(MIPSState *mips) {
	u32 retval = HLECall<int(u32, int, u32, u32), func>(mips, PARAM(0), PARAM(1), PARAM(2), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_IC(MIPSState *mips) {
	int retval = HLECall<int(int, const char *), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<auto func> void WrapI_ICCUI(MIPSState *mips) {
	int retval = HLECall<int(int, const char *, const char *, u32, int), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), Memory::GetCharPointer(PARAM(2)), PARAM(3), PARAM(4));
	RETURN(retval);
}

template<auto func> void WrapI_ICCI(MIPSState *mips) {
	int retval = HLECall<int(int, const char *, const char *, int), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), Memory::GetCharPointer(PARAM(2)), PARAM(3));
	RETURN(retval);
}

template<auto func> void WrapI_CII(MIPSState *mips) {
	int retval = HLECall<int(const char *, int, int), func>(mips, Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_ICI(MIPSState *mips) {
	int retval = HLECall<int(int, const char *, int), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_IVVVVUI(MIPSState *mips) {
  // Any of the pointers may be NULL (sceNetAdhocPdpRecv takes NULL for the sender), so don't fault on it.
  auto ptr = [](u32 addr) -> void * { return addr ? Memory::GetPointerWriteOrException(addr) : nullptr; };
  u32 retval = HLECall<int(int, void *, void *, void *, void *, u32, int), func>(mips, PARAM(0), ptr(PARAM(1)), ptr(PARAM(2)), ptr(PARAM(3)), ptr(PARAM(4)), PARAM(5), PARAM(6) );
  RETURN(retval);
}

template<auto func> void WrapI_ICUVIII(MIPSState *mips) {
  // The buffer may be NULL (sceNetAdhocPdpSend with a length of 0 is fine with that), so don't fault on it.
  u32 retval = HLECall<int(int, const char *, u32, void *, int, int, int), func>(mips, PARAM(0), Memory::GetCharPointer(PARAM(1)), PARAM(2), PARAM(3) ? Memory::GetPointerWriteOrException(PARAM(3)) : nullptr, PARAM(4), PARAM(5), PARAM(6));
  RETURN(retval);
}

template<auto func> void WrapI_VUI(MIPSState *mips) {
	u32 retval = HLECall<int(void *, u32, int), func>(mips, Memory::GetPointerWriteOrException(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_VUU(MIPSState *mips) {
	u32 retval = HLECall<u32(void *, u32, u32), func>(mips, Memory::GetPointerWriteOrException(PARAM(0)), PARAM(1), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_VC(MIPSState *mips) {
	u32 retval = HLECall<u32(void *, const char *), func>(mips, Memory::GetPointerWriteOrException(PARAM(0)), Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<auto func> void WrapI_CCU(MIPSState *mips) {
	int retval = HLECall<int(const char *, const char*, u32), func>(mips, Memory::GetCharPointer(PARAM(0)), Memory::GetCharPointer(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapI_CC(MIPSState *mips) {
	int retval = HLECall<int(const char *, const char*), func>(mips, Memory::GetCharPointer(PARAM(0)), Memory::GetCharPointer(PARAM(1)));
	RETURN(retval);
}

template<auto func> void WrapU_VCU(MIPSState *mips) {
	u32 retval = HLECall<u32(void *, const char *, u32), func>(mips, Memory::GetPointerWriteOrException(PARAM(0)), Memory::GetCharPointer(PARAM(1)), PARAM(2));
	RETURN(retval);
}

template<auto func> void WrapU_VI(MIPSState *mips) {
	u32 retval = HLECall<u32(void *, int), func>(mips, Memory::GetPointerWriteOrException(PARAM(0)), PARAM(1));
	RETURN(retval);
}
