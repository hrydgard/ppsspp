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

#include "Core/HLE/HLE.h"
#include "Core/Reporting.h"

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

int sceCccStrlenUTF16(u32 strUTF16)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccStrlenUTF16(%08x)", strUTF16);
	return 0;
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

int sceCccDecodeUTF8(u32 dstAddrAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccDecodeUTF8(%08x)", dstAddrAddr);
	return 0;
}

const HLEFunction sceCcc[] =
{	
	{0xB4D1CBBF, WrapI_UU<sceCccSetTable>, "sceCccSetTable"},
	{0x00D1378F, WrapI_UIU<sceCccUTF8toUTF16>, "sceCccUTF8toUTF16"},
	{0x6F82EE03, WrapI_UIU<sceCccUTF8toSJIS>, "sceCccUTF8toSJIS"},
	{0xF1B73D12, WrapI_UIU<sceCccUTF16toSJIS>, "sceCccUTF16toSJIS"},
	{0xA62E6E80, WrapI_UIU<sceCccSJIStoUTF8>, "sceCccSJIStoUTF8"},
	{0xBEB47224, WrapI_UIU<sceCccSJIStoUTF16>, "sceCccSJIStoUTF16"},
	{0x4BDEB2A8, WrapI_U<sceCccStrlenUTF16>, "sceCccStrlenUTF16"},
	{0x92C05851, WrapI_UU<sceCccEncodeUTF8>, "sceCccEncodeUTF8"},
	{0x8406F469, WrapI_UU<sceCccEncodeUTF16>, "sceCccEncodeUTF16"},
	{0xc6a8bee2, WrapI_U<sceCccDecodeUTF8>, "sceCccDecodeUTF8"},
};

void Register_sceCcc()
{
	RegisterModule("sceCcc", ARRAY_SIZE(sceCcc), sceCcc);
}
