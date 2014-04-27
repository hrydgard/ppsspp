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
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/scePauth.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"

int scePauth_F7AA47F6(u32 srcPtr, int srcLength, u32 destLengthPtr, u32 workArea)
{
	u8 *src, *key;
	u32 crc;
	char name[256];
	std::string hostPath;
	FILE *fp;
	int size;

	INFO_LOG(HLE, "scePauth_F7AA47F6(%08x, %08x, %08x, %08x)", srcPtr, srcLength, destLengthPtr, workArea);

	sprintf(name, "ms0:/PAUTH");
	pspFileSystem.GetHostPath(std::string(name), hostPath);

	src = (u8*)Memory::GetPointer(srcPtr);
	key = (u8*)Memory::GetPointer(workArea);
	crc = crc32(0, src, srcLength);

	sprintf(name, "%s/pauth_%08x.bin.decrypt", hostPath.c_str(), crc);
	fp = fopen(name, "rb");
	if (fp){
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		fread(src, 1, size, fp);
		fclose(fp);
		Memory::Write_U32(size, destLengthPtr);
		INFO_LOG(HLE, "Read from decrypted file %s", name);
		return 0;
	}

	pspFileSystem.MkDir("ms0:/PAUTH");

	sprintf(name, "%s/pauth_%08x.bin", hostPath.c_str(), crc);
	ERROR_LOG(HLE, "No decrypted file found! save as %s", name);

	fp = fopen(name, "wb");
	fwrite(src, 1, srcLength, fp);
	fclose(fp);

	sprintf(name, "%s/pauth_%08x.key", hostPath.c_str(), crc);
	fp = fopen(name, "wb");
	fwrite(key, 1, 16, fp);
	fclose(fp);

	return -1;
}

int scePauth_98B83B5D(u32 srcPtr, int srcLength, u32 destLengthPtr, u32 workArea)
{
	u8 *src, *key;
	u32 crc;
	char name[256];
	std::string hostPath;
	FILE *fp;
	int size;

	INFO_LOG(HLE, "scePauth_98B83B5D(%08x, %08x, %08x, %08x)", srcPtr, srcLength, destLengthPtr, workArea);

	sprintf(name, "ms0:/PAUTH");
	pspFileSystem.GetHostPath(std::string(name), hostPath);

	src = (u8*) Memory::GetPointer(srcPtr);
	key = (u8*) Memory::GetPointer(workArea);
	crc = crc32(0, src, srcLength);

	sprintf(name, "%s/pauth_%08x.bin.decrypt", hostPath.c_str(), crc);
	fp = fopen(name, "rb");
	if(fp){
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		fread(src, 1, size, fp);
		fclose(fp);
		Memory::Write_U32(size, destLengthPtr);
		INFO_LOG(HLE, "Read from decrypted file %s", name);
		return 0;
	}

	pspFileSystem.MkDir("ms0:/PAUTH");

	sprintf(name, "%s/pauth_%08x.bin", hostPath.c_str(), crc);
	ERROR_LOG(HLE, "No decrypted file found! save as %s", name);

	fp = fopen(name, "wb");
	fwrite(src, 1, srcLength, fp);
	fclose(fp);

	sprintf(name, "%s/pauth_%08x.key", hostPath.c_str(), crc);
	fp = fopen(name, "wb");
	fwrite(key, 1, 16, fp);
	fclose(fp);

	return -1;
}

const HLEFunction scePauth[] = {
	{0xF7AA47F6, &WrapI_UIUU<scePauth_F7AA47F6>, "scePauth_F7AA47F6"},
	{0x98B83B5D, &WrapI_UIUU<scePauth_98B83B5D>, "scePauth_98B83B5D"},
};

void Register_scePauth()
{
	RegisterModule("scePauth", ARRAY_SIZE(scePauth), scePauth);
}
