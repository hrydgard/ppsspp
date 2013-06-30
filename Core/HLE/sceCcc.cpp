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

int sceCccUTF8toUTF16()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUTF8toUTF16");
	return 0;
}

int sceCccStrlenUTF16(int strUTF16)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccStrlenUTF16");
	return 0;
}


int sceCccUTF8toSJIS(int dstAddr, int dstSize, int srcAddr)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUTF8toSJIS");
	return 0;
}

int sceCccSetTable(int jis2ucs, int ucs2jis)
{
	// Both tables jis2ucs and ucs2jis have a size of 0x20000 bytes
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccSetTable");
	return 0;
}

int sceCccSJIStoUTF16(int dstUTF16, int dstSize, int srcSJIS)
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccSJIStoUTF16");
	return 0;
}

int sceCccUTF16toSJIS()
{
	ERROR_LOG_REPORT(HLE, "UNIMPL sceCccUTF16toSJIS");
	return 0;
}

const HLEFunction sceCcc[] =
{	
	{0x00D1378F, WrapI_V<sceCccUTF8toUTF16>, "sceCccUTF8toUTF16"},
	{0x4BDEB2A8, WrapI_I<sceCccStrlenUTF16>, "sceCccStrlenUTF16"},
	{0x6F82EE03, WrapI_III<sceCccUTF8toSJIS>, "sceCccUTF8toSJIS"},
	{0xB4D1CBBF, WrapI_II<sceCccSetTable>, "sceCccSetTable"},
	{0xBEB47224, WrapI_III<sceCccSJIStoUTF16>, "sceCccSJIStoUTF16"},	
	{0xF1B73D12, WrapI_V<sceCccUTF16toSJIS>, "sceCccUTF16toSJIS"},
};

void Register_sceCcc()
{
	RegisterModule("sceCcc", ARRAY_SIZE(sceCcc), sceCcc);
}
