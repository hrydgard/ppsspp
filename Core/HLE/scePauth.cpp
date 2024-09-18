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

#include "zlib.h"
#include <stdio.h>

#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/ELF/PrxDecrypter.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/scePauth.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Common/File/FileUtil.h"

static int scePauth_F7AA47F6(u32 srcPtr, int srcLength, u32 destLengthPtr, u32 workArea)
{
	auto src = Memory::GetPointerWrite(srcPtr);
	auto key = Memory::GetPointer(workArea);

	const auto decryptResult = pspDecryptPRX(src, src, srcLength, key);

	if (decryptResult < 0)
	{
		ERROR_LOG(Log::HLE, "Pauth decryption failed 0x%08X", decryptResult);
		return decryptResult;
	}

	Memory::Write_U32(decryptResult, destLengthPtr);
	return 0;
}

static int scePauth_98B83B5D(u32 srcPtr, int srcLength, u32 destLengthPtr, u32 workArea)
{
	auto src = Memory::GetPointerWrite(srcPtr);
	auto key = Memory::GetPointer(workArea);

	const auto decryptResult = pspDecryptPRX(src, src, srcLength, key);

	if (decryptResult < 0)
	{
		ERROR_LOG(Log::HLE, "Pauth decryption failed 0x%08X", decryptResult);
		return decryptResult;
	}

	Memory::Write_U32(decryptResult, destLengthPtr);
	return 0;
}

const HLEFunction scePauth[] = {
	{0XF7AA47F6, &WrapI_UIUU<scePauth_F7AA47F6>,     "scePauth_F7AA47F6", 'i', "xixx"},
	{0X98B83B5D, &WrapI_UIUU<scePauth_98B83B5D>,     "scePauth_98B83B5D", 'i', "xixx"},
};

void Register_scePauth()
{
	RegisterModule("scePauth", ARRAY_SIZE(scePauth), scePauth);
}
