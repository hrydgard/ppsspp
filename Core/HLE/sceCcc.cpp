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

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Encoding/Utf16.h"
#include "Common/Data/Encoding/Shiftjis.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceCcc.h"
#include "Core/Reporting.h"

typedef PSPPointer<char> PSPCharPointer;
typedef PSPPointer<char16_t> PSPWCharPointer;
typedef PSPPointer<const char> PSPConstCharPointer;
typedef PSPPointer<const char16_t> PSPConstWCharPointer;

static u16 errorUTF8;
static u16 errorUTF16;
static u16 errorSJIS;

// These tables point directly to PSP memory and map all 64k possible u16 values.
static PSPWCharPointer ucs2jisTable;
static PSPWCharPointer jis2ucsTable;

void __CccInit()
{
	errorUTF8 = 0;
	errorUTF16 = 0;
	errorSJIS = 0;
	ucs2jisTable = 0;
	jis2ucsTable = 0;
}

void __CccDoState(PointerWrap &p)
{
	auto s = p.Section("sceCcc", 1);
	if (!s)
		return;

	Do(p, errorUTF8);
	Do(p, errorUTF16);
	Do(p, errorSJIS);
	Do(p, ucs2jisTable);
	Do(p, jis2ucsTable);
}

static u32 __CccUCStoJIS(u32 c, u32 alt)
{
	// JIS can only be 16-bit at most, UCS can be 32 (even if the table only supports UCS-2.)
	alt &= 0xFFFF;

	// If it's outside the table or blank in the table, return alt.
	if (c > 0xFFFF || ucs2jisTable[c] == 0)
		return alt;
	return ucs2jisTable[c];
}

static u32 __CccJIStoUCS(u32 c, u32 alt)
{
	// JIS can only be 16-bit at most, UCS can be 32 (even if the table only supports UCS-2.)
	c &= 0xFFFF;
	if (jis2ucsTable[c] == 0)
		return alt;
	return jis2ucsTable[c];
}

static void sceCccSetTable(u32 jis2ucs, u32 ucs2jis)
{
	// Both tables jis2ucs and ucs2jis have a size of 0x20000 bytes.
	DEBUG_LOG(Log::sceMisc, "sceCccSetTable(%08x, %08x)", jis2ucs, ucs2jis);
	ucs2jisTable = ucs2jis;
	jis2ucsTable = jis2ucs;
}

static int sceCccUTF8toUTF16(u32 dstAddr, u32 dstSize, u32 srcAddr)
{
	const auto src = PSPConstCharPointer::Create(srcAddr);
	auto dst = PSPWCharPointer::Create(dstAddr);
	if (!dst.IsValid() || !src.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccUTF8toUTF16(%08x, %d, %08x): invalid pointers", dstAddr, dstSize, srcAddr);
		return 0;
	}

	// Round dstSize down if it represents half a character.
	const auto dstEnd = PSPWCharPointer::Create(dstAddr + (dstSize & ~1));

	DEBUG_LOG(Log::sceMisc, "sceCccUTF8toUTF16(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	UTF8 utf(src);
	int n = 0;
	while (u32 c = utf.next())
	{
		if (dst + UTF16LE::encodeUnits(c) >= dstEnd)
			break;
		dst += UTF16LE::encode(dst, c);
		n++;
	}

	if (dst < dstEnd)
		*dst++ = 0;

	NotifyMemInfo(MemBlockFlags::READ, srcAddr, utf.byteIndex(), "sceCcc");
	NotifyMemInfo(MemBlockFlags::WRITE, dstAddr, dst.ptr - dstAddr, "sceCcc");
	return n;
}

static int sceCccUTF8toSJIS(u32 dstAddr, u32 dstSize, u32 srcAddr)
{
	const auto src = PSPConstCharPointer::Create(srcAddr);
	auto dst = PSPCharPointer::Create(dstAddr);
	if (!dst.IsValid() || !src.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccUTF8toSJIS(%08x, %d, %08x): invalid pointers", dstAddr, dstSize, srcAddr);
		return 0;
	}
	if (!ucs2jisTable.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccUTF8toSJIS(%08x, %d, %08x): table not loaded", dstAddr, dstSize, srcAddr);
		return 0;
	}

	const auto dstEnd = PSPCharPointer::Create(dstAddr + dstSize);

	DEBUG_LOG(Log::sceMisc, "sceCccUTF8toSJIS(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	UTF8 utf(src);
	int n = 0;
	while (u32 c = utf.next())
	{
		if (dst + ShiftJIS::encodeUnits(c) >= dstEnd)
			break;
		dst += ShiftJIS::encode(dst, __CccUCStoJIS(c, errorSJIS));
		n++;
	}

	if (dst < dstEnd)
		*dst++ = 0;

	NotifyMemInfo(MemBlockFlags::READ, srcAddr, utf.byteIndex(), "sceCcc");
	NotifyMemInfo(MemBlockFlags::WRITE, dstAddr, dst.ptr - dstAddr, "sceCcc");
	return n;
}

static int sceCccUTF16toUTF8(u32 dstAddr, u32 dstSize, u32 srcAddr)
{
	const auto src = PSPConstWCharPointer::Create(srcAddr);
	auto dst = PSPCharPointer::Create(dstAddr);
	if (!dst.IsValid() || !src.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccUTF16toUTF8(%08x, %d, %08x): invalid pointers", dstAddr, dstSize, srcAddr);
		return 0;
	}

	const auto dstEnd = PSPCharPointer::Create(dstAddr + dstSize);

	DEBUG_LOG(Log::sceMisc, "sceCccUTF16toUTF8(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	UTF16LE utf(src);
	int n = 0;
	while (u32 c = utf.next())
	{
		if (dst + UTF8::encodeUnits(c) >= dstEnd)
			break;
		dst += UTF8::encode(dst, c);
		n++;
	}

	if (dst < dstEnd)
		*dst++ = 0;

	NotifyMemInfo(MemBlockFlags::READ, srcAddr, utf.shortIndex() * sizeof(uint16_t), "sceCcc");
	NotifyMemInfo(MemBlockFlags::WRITE, dstAddr, dst.ptr - dstAddr, "sceCcc");
	return n;
}

static int sceCccUTF16toSJIS(u32 dstAddr, u32 dstSize, u32 srcAddr)
{
	const auto src = PSPConstWCharPointer::Create(srcAddr);
	auto dst = PSPCharPointer::Create(dstAddr);
	if (!dst.IsValid() || !src.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccUTF16toSJIS(%08x, %d, %08x): invalid pointers", dstAddr, dstSize, srcAddr);
		return 0;
	}
	if (!ucs2jisTable.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccUTF16toSJIS(%08x, %d, %08x): table not loaded", dstAddr, dstSize, srcAddr);
		return 0;
	}

	const auto dstEnd = PSPCharPointer::Create(dstAddr + dstSize);

	DEBUG_LOG(Log::sceMisc, "sceCccUTF16toSJIS(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	UTF16LE utf(src);
	int n = 0;
	while (u32 c = utf.next())
	{
		if (dst + ShiftJIS::encodeUnits(c) >= dstEnd)
			break;
		dst += ShiftJIS::encode(dst, __CccUCStoJIS(c, errorSJIS));
		n++;
	}

	if (dst < dstEnd)
		*dst++ = 0;

	NotifyMemInfo(MemBlockFlags::READ, srcAddr, utf.shortIndex() * sizeof(uint16_t), "sceCcc");
	NotifyMemInfo(MemBlockFlags::WRITE, dstAddr, dst.ptr - dstAddr, "sceCcc");
	return n;
}

static int sceCccSJIStoUTF8(u32 dstAddr, u32 dstSize, u32 srcAddr)
{
	const auto src = PSPConstCharPointer::Create(srcAddr);
	auto dst = PSPCharPointer::Create(dstAddr);
	if (!dst.IsValid() || !src.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccSJIStoUTF8(%08x, %d, %08x): invalid pointers", dstAddr, dstSize, srcAddr);
		return 0;
	}
	if (!jis2ucsTable.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccSJIStoUTF8(%08x, %d, %08x): table not loaded", dstAddr, dstSize, srcAddr);
		return 0;
	}

	const auto dstEnd = PSPCharPointer::Create(dstAddr + dstSize);

	DEBUG_LOG(Log::sceMisc, "sceCccSJIStoUTF8(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	ShiftJIS sjis(src);
	int n = 0;
	while (u32 c = sjis.next())
	{
		if (dst + UTF8::encodeUnits(c) >= dstEnd)
			break;
		dst += UTF8::encode(dst, __CccJIStoUCS(c, errorUTF8));
		n++;
	}

	if (dst < dstEnd)
		*dst++ = 0;

	NotifyMemInfo(MemBlockFlags::READ, srcAddr, sjis.byteIndex(), "sceCcc");
	NotifyMemInfo(MemBlockFlags::WRITE, dstAddr, dst.ptr - dstAddr, "sceCcc");
	return n;
}

static int sceCccSJIStoUTF16(u32 dstAddr, u32 dstSize, u32 srcAddr)
{
	const auto src = PSPConstCharPointer::Create(srcAddr);
	auto dst = PSPWCharPointer::Create(dstAddr);
	if (!dst.IsValid() || !src.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccSJIStoUTF16(%08x, %d, %08x): invalid pointers", dstAddr, dstSize, srcAddr);
		return 0;
	}
	if (!jis2ucsTable.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccSJIStoUTF16(%08x, %d, %08x): table not loaded", dstAddr, dstSize, srcAddr);
		return 0;
	}

	const auto dstEnd = PSPWCharPointer::Create(dstAddr + (dstSize & ~1));

	DEBUG_LOG(Log::sceMisc, "sceCccSJIStoUTF16(%08x, %d, %08x)", dstAddr, dstSize, srcAddr);
	ShiftJIS sjis(src);
	int n = 0;
	while (u32 c = sjis.next())
	{
		if (dst + UTF16LE::encodeUnits(c) >= dstEnd)
			break;
		dst += UTF16LE::encode(dst, __CccJIStoUCS(c, errorUTF16));
		n++;
	}

	if (dst < dstEnd)
		*dst++ = 0;

	NotifyMemInfo(MemBlockFlags::READ, srcAddr, sjis.byteIndex(), "sceCcc");
	NotifyMemInfo(MemBlockFlags::WRITE, dstAddr, dst.ptr - dstAddr, "sceCcc");
	return n;
}

static int sceCccStrlenUTF8(u32 strAddr)
{
	const auto str = PSPConstCharPointer::Create(strAddr);
	if (!str.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccStrlenUTF8(%08x): invalid pointer", strAddr);
		return 0;
	}
	DEBUG_LOG(Log::sceMisc, "sceCccStrlenUTF8(%08x): invalid pointer", strAddr);
	return UTF8(str).length();
}

static int sceCccStrlenUTF16(u32 strAddr)
{
	const auto str = PSPConstWCharPointer::Create(strAddr);
	if (!str.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccStrlenUTF16(%08x): invalid pointer", strAddr);
		return 0;
	}
	DEBUG_LOG(Log::sceMisc, "sceCccStrlenUTF16(%08x): invalid pointer", strAddr);
	return UTF16LE(str).length();
}

static int sceCccStrlenSJIS(u32 strAddr)
{
	const auto str = PSPCharPointer::Create(strAddr);
	if (!str.IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccStrlenSJIS(%08x): invalid pointer", strAddr);
		return 0;
	}
	DEBUG_LOG(Log::sceMisc, "sceCccStrlenSJIS(%08x): invalid pointer", strAddr);
	return ShiftJIS(str).length();
}

static u32 sceCccEncodeUTF8(u32 dstAddrAddr, u32 ucs)
{
	auto dstp = PSPPointer<PSPCharPointer>::Create(dstAddrAddr);

	if (!dstp.IsValid() || !dstp->IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccEncodeUTF8(%08x, U+%04x): invalid pointer", dstAddrAddr, ucs);
		return 0;
	}
	DEBUG_LOG(Log::sceMisc, "sceCccEncodeUTF8(%08x, U+%04x)", dstAddrAddr, ucs);
	*dstp += UTF8::encode(*dstp, ucs);
	return dstp->ptr;
}

static void sceCccEncodeUTF16(u32 dstAddrAddr, u32 ucs)
{
	auto dstp = PSPPointer<PSPWCharPointer>::Create(dstAddrAddr);

	if (!dstp.IsValid() || !dstp->IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccEncodeUTF16(%08x, U+%04x): invalid pointer", dstAddrAddr, ucs);
		return;
	}
	DEBUG_LOG(Log::sceMisc, "sceCccEncodeUTF16(%08x, U+%04x)", dstAddrAddr, ucs);
	// Anything above 0x10FFFF is unencodable, and 0xD800 - 0xDFFF are reserved for surrogate pairs.
	if (ucs > 0x10FFFF || (ucs & 0xD800) == 0xD800)
		ucs = errorUTF16;
	*dstp += UTF16LE::encode(*dstp, ucs);
}

static u32 sceCccEncodeSJIS(u32 dstAddrAddr, u32 jis)
{
	auto dstp = PSPPointer<PSPCharPointer>::Create(dstAddrAddr);

	if (!dstp.IsValid() || !dstp->IsValid())
	{
		ERROR_LOG(Log::sceMisc, "sceCccEncodeSJIS(%08x, U+%04x): invalid pointer", dstAddrAddr, jis);
		return 0;
	}
	DEBUG_LOG(Log::sceMisc, "sceCccEncodeSJIS(%08x, U+%04x)", dstAddrAddr, jis);
	*dstp += ShiftJIS::encode(*dstp, jis);
	return dstp->ptr;
}

static u32 sceCccDecodeUTF8(u32 dstAddrAddr)
{
	auto dstp = PSPPointer<PSPConstCharPointer>::Create(dstAddrAddr);

	if (!dstp.IsValid() || !dstp->IsValid()) {
		ERROR_LOG(Log::sceMisc, "sceCccDecodeUTF8(%08x): invalid pointer", dstAddrAddr);
		// Should crash?
		return 0;
	}

	DEBUG_LOG(Log::sceMisc, "sceCccDecodeUTF8(%08x)", dstAddrAddr);
	UTF8 utf(*dstp);
	u32 result = utf.next();
	*dstp += utf.byteIndex();

	if (result == UTF8::INVALID)
		return errorUTF8;
	return result;
}

static u32 sceCccDecodeUTF16(u32 dstAddrAddr)
{
	auto dstp = PSPPointer<PSPConstWCharPointer>::Create(dstAddrAddr);

	if (!dstp.IsValid() || !dstp->IsValid()) {
		ERROR_LOG(Log::sceMisc, "sceCccDecodeUTF16(%08x): invalid pointer", dstAddrAddr);
		// Should crash?
		return 0;
	}

	DEBUG_LOG(Log::sceMisc, "sceCccDecodeUTF16(%08x)", dstAddrAddr);
	// TODO: Does it do any detection of BOM?
	UTF16LE utf(*dstp);
	u32 result = utf.next();
	*dstp += utf.shortIndex();

	if (result == UTF16LE::INVALID)
		return errorUTF16;
	return result;
}

static u32 sceCccDecodeSJIS(u32 dstAddrAddr)
{
	auto dstp = PSPPointer<PSPConstCharPointer>::Create(dstAddrAddr);

	if (!dstp.IsValid() || !dstp->IsValid()) {
		ERROR_LOG(Log::sceMisc, "sceCccDecodeSJIS(%08x): invalid pointer", dstAddrAddr);
		// Should crash?
		return 0;
	}

	DEBUG_LOG(Log::sceMisc, "sceCccDecodeSJIS(%08x)", dstAddrAddr);
	ShiftJIS sjis(*dstp);
	u32 result = sjis.next();
	*dstp += sjis.byteIndex();

	if (result == ShiftJIS::INVALID)
		return errorSJIS;
	return result;
}

static int sceCccIsValidUTF8(u32 c)
{
	WARN_LOG(Log::sceMisc, "UNIMPL sceCccIsValidUTF8(%08x)", c);
	return c != 0;
}

static int sceCccIsValidUTF16(u32 c)
{
	WARN_LOG(Log::sceMisc, "UNIMPL sceCccIsValidUTF16(%08x)", c);
	return c != 0;
}

static int sceCccIsValidSJIS(u32 c)
{
	WARN_LOG(Log::sceMisc, "UNIMPL sceCccIsValidSJIS(%08x)", c);
	return c != 0;
}

static int sceCccIsValidUCS2(u32 c)
{
	WARN_LOG(Log::sceMisc, "UNIMPL sceCccIsValidUCS2(%08x)", c);
	return c != 0;
}

static int sceCccIsValidUCS4(u32 c)
{
	WARN_LOG(Log::sceMisc, "UNIMPL sceCccIsValidUCS4(%08x)", c);
	return c != 0;
}

static int sceCccIsValidJIS(u32 c)
{
	WARN_LOG(Log::sceMisc, "UNIMPL sceCccIsValidJIS(%08x)", c);
	return c != 0;
}

static int sceCccIsValidUnicode(u32 c)
{
	WARN_LOG(Log::sceMisc, "UNIMPL sceCccIsValidUnicode(%08x)", c);
	return c != 0;
}

static u32 sceCccSetErrorCharUTF8(u32 c)
{
	DEBUG_LOG(Log::sceMisc, "sceCccSetErrorCharUTF8(%08x)", c);
	int result = errorUTF8;
	errorUTF8 = c;
	return result;
}

static u32 sceCccSetErrorCharUTF16(u32 c)
{
	DEBUG_LOG(Log::sceMisc, "sceCccSetErrorCharUTF16(%08x)", c);
	int result = errorUTF16;
	errorUTF16 = c;
	return result;
}

static u32 sceCccSetErrorCharSJIS(u32 c)
{
	DEBUG_LOG(Log::sceMisc, "sceCccSetErrorCharSJIS(%04x)", c);
	int result = errorSJIS;
	errorSJIS = c;
	return result;
}

static u32 sceCccUCStoJIS(u32 c, u32 alt)
{
	if (ucs2jisTable.IsValid())
	{
		DEBUG_LOG(Log::sceMisc, "sceCccUCStoJIS(%08x, %08x)", c, alt);
		return __CccUCStoJIS(c, alt);
	}
	else
	{
		ERROR_LOG(Log::sceMisc, "sceCccUCStoJIS(%08x, %08x): table not loaded", c, alt);
		return alt;
	}
}

static u32 sceCccJIStoUCS(u32 c, u32 alt)
{
	if (jis2ucsTable.IsValid())
	{
		DEBUG_LOG(Log::sceMisc, "sceCccUCStoJIS(%08x, %08x)", c, alt);
		return __CccJIStoUCS(c, alt);
	}
	else
	{
		ERROR_LOG(Log::sceMisc, "sceCccUCStoJIS(%08x, %08x): table not loaded", c, alt);
		return alt;
	}
}

const HLEFunction sceCcc[] =
{	
	{0XB4D1CBBF, &WrapV_UU<sceCccSetTable>,          "sceCccSetTable",          'v', "xx" },
	{0X00D1378F, &WrapI_UUU<sceCccUTF8toUTF16>,      "sceCccUTF8toUTF16",       'i', "xxx"},
	{0X6F82EE03, &WrapI_UUU<sceCccUTF8toSJIS>,       "sceCccUTF8toSJIS",        'i', "xxx"},
	{0X41B724A5, &WrapI_UUU<sceCccUTF16toUTF8>,      "sceCccUTF16toUTF8",       'i', "xxx"},
	{0XF1B73D12, &WrapI_UUU<sceCccUTF16toSJIS>,      "sceCccUTF16toSJIS",       'i', "xxx"},
	{0XA62E6E80, &WrapI_UUU<sceCccSJIStoUTF8>,       "sceCccSJIStoUTF8",        'i', "xxx"},
	{0XBEB47224, &WrapI_UUU<sceCccSJIStoUTF16>,      "sceCccSJIStoUTF16",       'i', "xxx"},
	{0XB7D3C112, &WrapI_U<sceCccStrlenUTF8>,         "sceCccStrlenUTF8",        'i', "x"  },
	{0X4BDEB2A8, &WrapI_U<sceCccStrlenUTF16>,        "sceCccStrlenUTF16",       'i', "x"  },
	{0XD9392CCB, &WrapI_U<sceCccStrlenSJIS>,         "sceCccStrlenSJIS",        'i', "x"  },
	{0X92C05851, &WrapU_UU<sceCccEncodeUTF8>,        "sceCccEncodeUTF8",        'x', "xx" },
	{0X8406F469, &WrapV_UU<sceCccEncodeUTF16>,       "sceCccEncodeUTF16",       'v', "xx" },
	{0X068C4320, &WrapU_UU<sceCccEncodeSJIS>,        "sceCccEncodeSJIS",        'x', "xx" },
	{0XC6A8BEE2, &WrapU_U<sceCccDecodeUTF8>,         "sceCccDecodeUTF8",        'x', "x"  },
	{0XE0CF8091, &WrapU_U<sceCccDecodeUTF16>,        "sceCccDecodeUTF16",       'x', "x"  },
	{0X953E6C10, &WrapU_U<sceCccDecodeSJIS>,         "sceCccDecodeSJIS",        'x', "x"  },
	{0X90521AC5, &WrapI_U<sceCccIsValidUTF8>,        "sceCccIsValidUTF8",       'i', "x"  },
	{0XCC0A8BDA, &WrapI_U<sceCccIsValidUTF16>,       "sceCccIsValidUTF16",      'i', "x"  },
	{0X67BF0D19, &WrapI_U<sceCccIsValidSJIS>,        "sceCccIsValidSJIS",       'i', "x"  },
	{0X76E33E9C, &WrapI_U<sceCccIsValidUCS2>,        "sceCccIsValidUCS2",       'i', "x"  },
	{0XD2B18485, &WrapI_U<sceCccIsValidUCS4>,        "sceCccIsValidUCS4",       'i', "x"  },
	{0XA2D5D209, &WrapI_U<sceCccIsValidJIS>,         "sceCccIsValidJIS",        'i', "x"  },
	{0XBD11EEF3, &WrapI_U<sceCccIsValidUnicode>,     "sceCccIsValidUnicode",    'i', "x"  },
	{0X17E1D813, &WrapU_U<sceCccSetErrorCharUTF8>,   "sceCccSetErrorCharUTF8",  'x', "x"  },
	{0XB8476CF4, &WrapU_U<sceCccSetErrorCharUTF16>,  "sceCccSetErrorCharUTF16", 'x', "x"  },
	{0XC56949AD, &WrapU_U<sceCccSetErrorCharSJIS>,   "sceCccSetErrorCharSJIS",  'x', "x"  },
	{0X70ECAA10, &WrapU_UU<sceCccUCStoJIS>,          "sceCccUCStoJIS",          'x', "xx" },
	{0XFB7846E2, &WrapU_UU<sceCccJIStoUCS>,          "sceCccJIStoUCS",          'x', "xx" },
};

void Register_sceCcc()
{
	RegisterModule("sceCcc", ARRAY_SIZE(sceCcc), sceCcc);
}
