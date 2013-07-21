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

#include "util/text/utf8.h"
#include "util/text/utf16.h"
#include "util/text/shiftjis.h"

#include "Core/HLE/HLE.h"
#include "Core/Reporting.h"

typedef PSPPointer<const char> PSPCharPointer;
typedef PSPPointer<const u16> PSPWCharPointer;

static u16 errorUTF8;
static u16 errorUTF16;
static u16 errorSJIS;

void __CccInit()
{
	errorUTF8 = 0;
	errorUTF16 = 0;
	errorSJIS = 0;
}

void __CccDoState(PointerWrap &p)
{
	p.Do(errorUTF8);
	p.Do(errorUTF16);
	p.Do(errorSJIS);
	p.DoMarker("sceCcc");
}

int sceCccSetTable(u32 jis2ucs, u32 ucs2jis)
{
	// Both tables jis2ucs and ucs2jis have a size of 0x20000 bytes
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccSetTable(%08x, %08x)", jis2ucs, ucs2jis);
	return 0;
}

int sceCccUTF8toUTF16(u32 dstAddr, int dstSize, u32 srcAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUTF8toUTF16(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	return 0;
}

int sceCccUTF8toSJIS(u32 dstAddr, int dstSize, u32 srcAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUTF8toSJIS(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	Memory::Memcpy(dstAddr, Memory::GetCharPointer(srcAddr), dstSize);
	return 0;
}

int sceCccUTF16toUTF8(u32 dstAddr, int dstSize, u32 srcAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUTF16toUTF8(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	return 0;
}

int sceCccUTF16toSJIS(u32 dstAddr, int dstSize, u32 srcAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUTF16toSJIS(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	return 0;
}

int sceCccSJIStoUTF8(u32 dstAddr, int dstSize, u32 srcAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccSJIStoUTF8(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	// TODO: Use the tables set in sceCccSetTable()?
	// Some characters are the same, so let's copy which is better than doing nothing.
	Memory::Memcpy(dstAddr, Memory::GetCharPointer(srcAddr), dstSize);
	return 0;
}

int sceCccSJIStoUTF16(u32 dstAddr, int dstSize, u32 srcAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccSJIStoUTF16(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	return 0;
}

int sceCccStrlenUTF8(u32 strAddr)
{
	PSPCharPointer str;
	str = strAddr;

	if (!str.IsValid())
	{
		ERROR_LOG(HLE, "sceCccStrlenUTF8(%08x): invalid pointer", strAddr);
		return 0;
	}
	DEBUG_LOG(HLE, "sceCccStrlenUTF8(%08x): invalid pointer", strAddr);
	return UTF8(str).length();
}

int sceCccStrlenUTF16(u32 strAddr)
{
	PSPWCharPointer str;
	str = strAddr;

	if (!str.IsValid())
	{
		ERROR_LOG(HLE, "sceCccStrlenUTF16(%08x): invalid pointer", strAddr);
		return 0;
	}
	DEBUG_LOG(HLE, "sceCccStrlenUTF16(%08x): invalid pointer", strAddr);
	return UTF16LE(str).length();
}

int sceCccStrlenSJIS(u32 strAddr)
{
	PSPCharPointer str;
	str = strAddr;

	if (!str.IsValid())
	{
		ERROR_LOG(HLE, "sceCccStrlenSJIS(%08x): invalid pointer", strAddr);
		return 0;
	}
	DEBUG_LOG(HLE, "sceCccStrlenSJIS(%08x): invalid pointer", strAddr);
	return ShiftJIS(str).length();
}

int sceCccEncodeUTF8(u32 dstAddr, u32 ucs)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccEncodeUTF8(%08x, U+%04x)", dstAddr, ucs);
	return 0;
}

int sceCccEncodeUTF16(u32 dstAddr, u32 ucs)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccEncodeUTF8(%08x, U+%04x)", dstAddr, ucs);
	return 0;
}

int sceCccEncodeSJIS(u32 dstAddr, u32 ucs)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccEncodeSJIS(%08x, U+%04x)", dstAddr, ucs);
	return 0;
}

int sceCccDecodeUTF8(u32 dstAddrAddr)
{
	PSPPointer<PSPCharPointer> dstp;
	dstp = dstAddrAddr;

	if (!dstp.IsValid() || !dstp->IsValid()) {
		ERROR_LOG(HLE, "sceCccDecodeUTF8(%08x): invalid pointer", dstAddrAddr);
		// Should crash?
		return 0;
	}

	DEBUG_LOG(HLE, "sceCccDecodeUTF8(%08x)", dstAddrAddr);
	UTF8 utf(*dstp);
	int result = utf.next();
	*dstp += utf.byteIndex();

	if (result == UTF8::INVALID)
		return errorUTF8;
	return result;
}

int sceCccDecodeUTF16(u32 dstAddrAddr)
{
	PSPPointer<PSPWCharPointer> dstp;
	dstp = dstAddrAddr;

	if (!dstp.IsValid() || !dstp->IsValid()) {
		ERROR_LOG(HLE, "sceCccDecodeUTF16(%08x): invalid pointer", dstAddrAddr);
		// Should crash?
		return 0;
	}

	DEBUG_LOG(HLE, "sceCccDecodeUTF16(%08x)", dstAddrAddr);
	// TODO: Does it do any detection of BOM?
	UTF16LE utf(*dstp);
	int result = utf.next();
	*dstp += utf.byteIndex();

	if (result == UTF16LE::INVALID)
		return errorUTF16;
	return result;
}

int sceCccDecodeSJIS(u32 dstAddrAddr)
{
	PSPPointer<PSPCharPointer> dstp;
	dstp = dstAddrAddr;

	if (!dstp.IsValid() || !dstp->IsValid()) {
		ERROR_LOG(HLE, "sceCccDecodeSJIS(%08x): invalid pointer", dstAddrAddr);
		// Should crash?
		return 0;
	}

	DEBUG_LOG(HLE, "sceCccDecodeSJIS(%08x)", dstAddrAddr);
	ShiftJIS sjis(*dstp);
	int result = sjis.next();
	*dstp += sjis.byteIndex();

	if (result == ShiftJIS::INVALID)
		return errorSJIS;
	return result;
}

u32 sceCccSetErrorCharUTF8(u32 c)
{
	DEBUG_LOG(HLE, "sceCccSetErrorCharUTF8(%08x)", c);
	int result = errorUTF8;
	errorUTF8 = c;
	return result;
}

u32 sceCccSetErrorCharUTF16(u32 c)
{
	DEBUG_LOG(HLE, "sceCccSetErrorCharUTF16(%08x)", c);
	int result = errorUTF16;
	errorUTF16 = c;
	return result;
}

u32 sceCccSetErrorCharSJIS(u32 c)
{
	DEBUG_LOG(HLE, "sceCccSetErrorCharSJIS(%04x)", c);
	int result = errorSJIS;
	errorSJIS = c;
	return result;
}

int sceCccUCStoJIS()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUCStoJIS(?)");
	return 0;
}

int sceCccJIStoUCS()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUCStoJIS(?)");
	return 0;
}

const HLEFunction sceCcc[] =
{	
	{0xB4D1CBBF, WrapI_UU<sceCccSetTable>, "sceCccSetTable"},
	{0x00D1378F, WrapI_UIU<sceCccUTF8toUTF16>, "sceCccUTF8toUTF16"},
	{0x6F82EE03, WrapI_UIU<sceCccUTF8toSJIS>, "sceCccUTF8toSJIS"},
	{0x41B724A5, WrapI_UIU<sceCccUTF16toUTF8>, "sceCccUTF16toUTF8"},
	{0xF1B73D12, WrapI_UIU<sceCccUTF16toSJIS>, "sceCccUTF16toSJIS"},
	{0xA62E6E80, WrapI_UIU<sceCccSJIStoUTF8>, "sceCccSJIStoUTF8"},
	{0xBEB47224, WrapI_UIU<sceCccSJIStoUTF16>, "sceCccSJIStoUTF16"},
	{0xb7d3c112, WrapI_U<sceCccStrlenUTF8>, "sceCccStrlenUTF8"},
	{0x4BDEB2A8, WrapI_U<sceCccStrlenUTF16>, "sceCccStrlenUTF16"},
	{0xd9392ccb, WrapI_U<sceCccStrlenSJIS>, "sceCccStrlenSJIS"},
	{0x92C05851, WrapI_UU<sceCccEncodeUTF8>, "sceCccEncodeUTF8"},
	{0x8406F469, WrapI_UU<sceCccEncodeUTF16>, "sceCccEncodeUTF16"},
	{0x068c4320, WrapI_UU<sceCccEncodeSJIS>, "sceCccEncodeSJIS"},
	{0xc6a8bee2, WrapI_U<sceCccDecodeUTF8>, "sceCccDecodeUTF8"},
	{0xe0cf8091, WrapI_U<sceCccDecodeUTF16>, "sceCccDecodeUTF16"},
	{0x953e6c10, WrapI_U<sceCccDecodeSJIS>, "sceCccDecodeSJIS"},
	{0x90521ac5, 0, "sceCccIsValidUTF8"},
	{0xcc0a8bda, 0, "sceCccIsValidUTF16"},
	{0x67bf0d19, 0, "sceCccIsValidSJIS"},
	{0x76e33e9c, 0, "sceCccIsValidUCS2"},
	{0xd2b18485, 0, "sceCccIsValidUCS4"},
	{0xa2d5d209, 0, "sceCccIsValidJIS"},
	{0x17e1d813, WrapU_U<sceCccSetErrorCharUTF8>, "sceCccSetErrorCharUTF8"},
	{0xb8476cf4, WrapU_U<sceCccSetErrorCharUTF16>, "sceCccSetErrorCharUTF16"},
	{0xc56949ad, WrapU_U<sceCccSetErrorCharSJIS>, "sceCccSetErrorCharSJIS"},
	{0x70ecaa10, WrapI_V<sceCccUCStoJIS>, "sceCccUCStoJIS"},
	{0xfb7846e2, WrapI_V<sceCccJIStoUCS>, "sceCccJIStoUCS"},
};

void Register_sceCcc()
{
	RegisterModule("sceCcc", ARRAY_SIZE(sceCcc), sceCcc);
}
