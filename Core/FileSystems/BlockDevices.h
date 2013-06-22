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

// Abstractions around read-only blockdevices, such as PSP UMD discs.
// CISOFileBlockDevice implements compressed iso images, CISO format.
//
// The ISOFileSystemReader reads from a BlockDevice, so it automatically works
// with CISO images.

#include "../../Globals.h"
#include "Core/ELF/PBPReader.h"

class BlockDevice
{
public:
	virtual ~BlockDevice() {}
	virtual bool ReadBlock(int blockNumber, u8 *outPtr) = 0;
	int GetBlockSize() const { return 2048;}  // forced, it cannot be changed by subclasses
	virtual u32 GetNumBlocks() = 0;
};


class CISOFileBlockDevice : public BlockDevice
{
public:
	CISOFileBlockDevice(FILE *file);
	~CISOFileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr);
	u32 GetNumBlocks() { return numBlocks;}

private:
	FILE *f;
	u32 *index;
	int indexShift;
	u32 blockSize;
	u32 numBlocks;
};


class FileBlockDevice : public BlockDevice
{
public:
	FileBlockDevice(FILE *file);
	~FileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr);
	u32 GetNumBlocks() {return (u32)(filesize / GetBlockSize());}

private:
	FILE *f;
	size_t filesize;
};


// For encrypted ISOs in PBP files.

struct table_info {
	u8 mac[16];
	u32 offset;
	int size;
	int flag;
	int unk_1c;
};

class NPDRMDemoBlockDevice : public BlockDevice
{
public:
	NPDRMDemoBlockDevice(FILE *file);
	~NPDRMDemoBlockDevice();

	bool ReadBlock(int blockNumber, u8 *outPtr);
	u32 GetNumBlocks() {return (u32)lbaSize;}

private:
	FILE *f;
	u32 lbaSize;

	u32 psarOffset;
	int blockSize;
	int blockLBAs;
	u32 numBlocks;

	u8 vkey[16];
	u8 hkey[16];
	struct table_info *table;

	int currentBlock;
	u8 *blockBuf;
	u8 *tempBuf;
};


BlockDevice *constructBlockDevice(const char *filename);
