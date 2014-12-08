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

#include "Common/CommonTypes.h"
#include "Core/ELF/PBPReader.h"

class FileLoader;

class BlockDevice
{
public:
	virtual ~BlockDevice() {}
	virtual bool ReadBlock(int blockNumber, u8 *outPtr) = 0;
	virtual bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
		for (int b = 0; b < count; ++b) {
			if (!ReadBlock(minBlock + b, outPtr)) {
				return false;
			}
			outPtr += GetBlockSize();
		}
		return true;
	}
	int GetBlockSize() const { return 2048;}  // forced, it cannot be changed by subclasses
	virtual u32 GetNumBlocks() = 0;
};


class CISOFileBlockDevice : public BlockDevice
{
public:
	CISOFileBlockDevice(FileLoader *fileLoader);
	~CISOFileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr) override;
	bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) override;
	u32 GetNumBlocks() override { return numBlocks; }

private:
	FileLoader *fileLoader_;
	u32 *index;
	u8 *readBuffer;
	u8 *zlibBuffer;
	u32 zlibBufferFrame;
	u8 indexShift;
	u8 blockShift;
	u32 frameSize;
	u32 numBlocks;
	u32 numFrames;
};


class FileBlockDevice : public BlockDevice
{
public:
	FileBlockDevice(FileLoader *fileLoader);
	~FileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr) override;
	bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) override;
	u32 GetNumBlocks() override {return (u32)(filesize_ / GetBlockSize());}

private:
	FileLoader *fileLoader_;
	u64 filesize_;
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
	NPDRMDemoBlockDevice(FileLoader *fileLoader);
	~NPDRMDemoBlockDevice();

	bool ReadBlock(int blockNumber, u8 *outPtr) override;
	u32 GetNumBlocks() override {return (u32)lbaSize;}

private:
	FileLoader *fileLoader_;
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

// This simply fully reads another block device and caches it in RAM.
// A bit slow to initialize.
class RAMBlockDevice : public BlockDevice
{
public:
	RAMBlockDevice(BlockDevice *device);
	~RAMBlockDevice();

	bool ReadBlock(int blockNumber, u8 *outPtr) override;
	u32 GetNumBlocks() override;

private:
	int totalBlocks_;
	u8 *image_;
};


BlockDevice *constructBlockDevice(FileLoader *fileLoader);
