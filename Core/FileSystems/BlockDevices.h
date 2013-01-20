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
#include <string>

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
	CISOFileBlockDevice(std::string _filename);
	~CISOFileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr);
	u32 GetNumBlocks() { return numBlocks;}

private:
	std::string filename;
	FILE *f;
	u32 *index;
	int indexShift;
	u32 blockSize;
	u32 numBlocks;
};


class FileBlockDevice : public BlockDevice
{
public:
	FileBlockDevice(std::string _filename);
	~FileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr);
	u32 GetNumBlocks() {return (u32)(filesize / GetBlockSize());}

private:
	std::string filename;
	FILE *f;
	size_t filesize;
};
