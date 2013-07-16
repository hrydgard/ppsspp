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
#include "Common.h"




const HLEFunction MaiPrx[] =
{
	{0x28B71731, 0, "MaiPrx_28B71731"},
	{0xD9CB2E61, 0, "MaiPrx_D9CB2E6"},
	{0xD8FC2DA7, 0, "MaiPrx_D8FC2DA7"},
	{0x022F77FC, 0, "MaiPrx_022F77FC"},
	{0x67e8536d, 0, "MaiPrx_67e8536d"},
	{0x3E2F9AA4, 0, "MaiPrx_3E2F9AA4"},
	{0xB56A47D9, 0, "MaiPrx_B56A47D9"},
	{0xCB3F3E4B, 0, "MaiPrx_CB3F3E4B"},
	{0xD2FBAAF1, 0, "MaiPrx_D2FBAAF1"},
	{0x85E00CEB, 0, "MaiPrx_85E00CEB"},
	{0xF7810C20, 0, "MaiPrx_F7810C20"},
	{0xD580F15A, 0, "MaiPrx_D580F15A"},
	{0x025F56F8, 0, "MaiPrx_025F56F8"},

};

void Register_MaiPrx()
{
	RegisterModule("MaiPrx", ARRAY_SIZE(MaiPrx), MaiPrx);
}
