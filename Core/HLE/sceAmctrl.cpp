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

// amctrl.prx's PGD MAC and cipher functions, which the POPS manager uses on a store PS1 game's disc.
// libkirk's amctrl.c reimplements the module, and its MAC_KEY and CIPHER_KEY have the layout of the
// contexts callers keep in their memory, so they're used in place.

#include "ext/libkirk/amctrl.h"

#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceAmctrl.h"
#include "Core/HLE/sceChnnlsv.h"
#include "Core/MemMap.h"

static MAC_KEY *MacKey(u32 addr) {
	return Memory::IsValidRange(addr, sizeof(MAC_KEY)) ? (MAC_KEY *)Memory::GetPointerWriteUnchecked(addr) : nullptr;
}

static CIPHER_KEY *CipherKey(u32 addr) {
	return Memory::IsValidRange(addr, sizeof(CIPHER_KEY)) ? (CIPHER_KEY *)Memory::GetPointerWriteUnchecked(addr) : nullptr;
}

// A 16-byte key argument, which may be absent.
static u8 *Key16(u32 addr) {
	return Memory::IsValidRange(addr, 16) ? Memory::GetPointerWriteUnchecked(addr) : nullptr;
}

static int Hle_sceDrmBBMacInit(u32 mkeyAddr, int type) {
	MAC_KEY *mkey = MacKey(mkeyAddr);
	if (!mkey) {
		return hleLogError(Log::sceKernel, -1, "bad context");
	}
	return hleLogDebug(Log::sceKernel, sceDrmBBMacInit(mkey, type));
}

static int Hle_sceDrmBBMacUpdate(u32 mkeyAddr, u32 buf, int size) {
	MAC_KEY *mkey = MacKey(mkeyAddr);
	if (!mkey || size < 0 || !Memory::IsValidRange(buf, size)) {
		return hleLogError(Log::sceKernel, -1, "bad arguments");
	}
	return hleLogDebug(Log::sceKernel, sceDrmBBMacUpdate(__ChnnlsvKirkState(), mkey, Memory::GetPointerWriteUnchecked(buf), size));
}

static int Hle_sceDrmBBMacFinal(u32 mkeyAddr, u32 out, u32 vkey) {
	MAC_KEY *mkey = MacKey(mkeyAddr);
	if (!mkey || !Key16(out)) {
		return hleLogError(Log::sceKernel, -1, "bad arguments");
	}
	return hleLogDebug(Log::sceKernel, sceDrmBBMacFinal(__ChnnlsvKirkState(), mkey, Key16(out), Key16(vkey)));
}

// Compares with the expected MAC: 0x80510300 when they differ.
static int Hle_sceDrmBBMacFinal2(u32 mkeyAddr, u32 mac, u32 vkey) {
	MAC_KEY *mkey = MacKey(mkeyAddr);
	if (!mkey || !Key16(mac)) {
		return hleLogError(Log::sceKernel, -1, "bad arguments");
	}
	const int result = sceDrmBBMacFinal2(__ChnnlsvKirkState(), mkey, Key16(mac), Key16(vkey));
	if (result != 0) {
		return hleLogWarning(Log::sceKernel, result, "MAC mismatch");
	}
	return hleLogDebug(Log::sceKernel, result);
}

static int Hle_sceDrmBBCipherInit(u32 ckeyAddr, int type, int mode, u32 headerKey, u32 versionKey, u32 seed) {
	CIPHER_KEY *ckey = CipherKey(ckeyAddr);
	if (!ckey || !Key16(headerKey)) {
		return hleLogError(Log::sceKernel, -1, "bad arguments");
	}
	return hleLogDebug(Log::sceKernel, sceDrmBBCipherInit(__ChnnlsvKirkState(), ckey, type, mode, Key16(headerKey), Key16(versionKey), seed));
}

static int Hle_sceDrmBBCipherUpdate(u32 ckeyAddr, u32 data, int size) {
	CIPHER_KEY *ckey = CipherKey(ckeyAddr);
	if (!ckey || size < 0 || !Memory::IsValidRange(data, size)) {
		return hleLogError(Log::sceKernel, -1, "bad arguments");
	}
	const int result = sceDrmBBCipherUpdate(__ChnnlsvKirkState(), ckey, Memory::GetPointerWriteUnchecked(data), size);
	NotifyMemInfo(MemBlockFlags::WRITE, data, size, "BBCipher");
	return hleLogDebug(Log::sceKernel, result);
}

static int Hle_sceDrmBBCipherFinal(u32 ckeyAddr) {
	CIPHER_KEY *ckey = CipherKey(ckeyAddr);
	if (!ckey) {
		return hleLogError(Log::sceKernel, -1, "bad context");
	}
	return hleLogDebug(Log::sceKernel, sceDrmBBCipherFinal(ckey));
}

// 6.60 NIDs.
const HLEFunction sceAmctrl_driver[] = {
	{ 0x525B8218, &WrapI_UI<Hle_sceDrmBBMacInit>,            "sceDrmBBMacInit",      'i', "xi",     HLE_KERNEL_SYSCALL },
	{ 0x58163FBE, &WrapI_UUI<Hle_sceDrmBBMacUpdate>,         "sceDrmBBMacUpdate",    'i', "xxi",    HLE_KERNEL_SYSCALL },
	{ 0xEF95A213, &WrapI_UUU<Hle_sceDrmBBMacFinal>,          "sceDrmBBMacFinal",     'i', "xxx",    HLE_KERNEL_SYSCALL },
	{ 0xF5186D8E, &WrapI_UUU<Hle_sceDrmBBMacFinal2>,         "sceDrmBBMacFinal2",    'i', "xxx",    HLE_KERNEL_SYSCALL },
	{ 0x1CCB66D2, &WrapI_UIIUUU<Hle_sceDrmBBCipherInit>,     "sceDrmBBCipherInit",   'i', "xiixxx", HLE_KERNEL_SYSCALL },
	{ 0x0785C974, &WrapI_UUI<Hle_sceDrmBBCipherUpdate>,      "sceDrmBBCipherUpdate", 'i', "xxi",    HLE_KERNEL_SYSCALL },
	{ 0x9951C50F, &WrapI_U<Hle_sceDrmBBCipherFinal>,         "sceDrmBBCipherFinal",  'i', "x",      HLE_KERNEL_SYSCALL },
};

void Register_sceAmctrl_driver() {
	RegisterHLEModule("sceAmctrl_driver", ARRAY_SIZE(sceAmctrl_driver), sceAmctrl_driver);
}
