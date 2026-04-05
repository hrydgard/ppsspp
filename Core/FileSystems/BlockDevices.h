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

#include <mutex>
#include <memory>

#include "Common/CommonTypes.h"

#include "ext/libkirk/kirk_engine.h"

class FileLoader;

class BlockDevice {
public:
	BlockDevice(FileLoader *fileLoader) : fileLoader_(fileLoader) {}
	virtual ~BlockDevice() {}
	virtual bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) = 0;
	virtual bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
		for (int b = 0; b < count; ++b) {
			if (!ReadBlock(minBlock + b, outPtr)) {
				return false;
			}
			outPtr += GetBlockSize();
		}
		return true;
	}
	int constexpr GetBlockSize() const { return 2048;}  // forced, it cannot be changed by subclasses. If a subclass uses bigger blocks internally, it must cache and virtualize.
	virtual u32 GetNumBlocks() const = 0;
	virtual u64 GetUncompressedSize() const {
		return (u64)GetNumBlocks() * (u64)GetBlockSize();
	}
	virtual bool IsDisc() const = 0;

	void NotifyReadError();

	bool IsOK() const { return errorString_.empty(); }
	const std::string &ErrorString() { return errorString_; }

protected:
	FileLoader *fileLoader_;
	bool reportedError_ = false;

	std::string errorString_;
};

class CISOFileBlockDevice : public BlockDevice {
public:
	CISOFileBlockDevice(FileLoader *fileLoader);
	~CISOFileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) override;
	bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) override;
	u32 GetNumBlocks() const override { return numBlocks; }
	bool IsDisc() const override { return true; }

private:
	u32 *index = nullptr;
	u8 *readBuffer = nullptr;
	u8 *zlibBuffer = nullptr;
	u32 zlibBufferFrame = 0;
	u8 indexShift = 0;
	u8 blockShift = 0;
	u32 frameSize = 0;
	u32 numBlocks = 0;
	u32 numFrames = 0;
	int ver_ = 0;
};

class FileBlockDevice : public BlockDevice {
public:
	FileBlockDevice(FileLoader *fileLoader);
	~FileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) override;
	bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) override;
	u32 GetNumBlocks() const override {return (u32)(filesize_ / GetBlockSize());}
	bool IsDisc() const override { return true; }
	u64 GetUncompressedSize() const override {
		return filesize_;
	}
private:
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

class NPDRMDemoBlockDevice : public BlockDevice {
public:
	NPDRMDemoBlockDevice(FileLoader *fileLoader);
	~NPDRMDemoBlockDevice();

	bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) override;
	u32 GetNumBlocks() const override {return (u32)lbaSize_;}
	bool IsDisc() const override { return false; }

private:
	// This is in case two threads hit this same block device, which shouldn't really happen.
	std::mutex mutex_;

	u32 lbaSize_ = 0;

	u32 psarOffset = 0;
	int blockSize_ = 0;
	int blockLBAs_ = 0;
	u32 numBlocks_ = 0;

	u8 vkey[16]{};
	u8 hkey[16]{};
	struct table_info *table_ = nullptr;

	int currentBlock_ = 0;
	u8 *blockBuf_ = nullptr;
	u8 *tempBuf_ = nullptr;

	// Each block device gets its own private kirk. Multiple ones can be in flight
	// to load metadata.
	KirkState kirk_{};
};

struct CHDImpl;

struct ExtendedCoreFile;

class CHDFileBlockDevice : public BlockDevice {
public:
	CHDFileBlockDevice(FileLoader *fileLoader);
	~CHDFileBlockDevice();
	bool ReadBlock(int blockNumber, u8 *outPtr, bool uncached = false) override;
	bool ReadBlocks(u32 minBlock, int count, u8 *outPtr) override;
	u32 GetNumBlocks() const override { return numBlocks; }
	bool IsDisc() const override { return true; }
private:
	struct ExtendedCoreFile *core_file_ = nullptr;
	std::unique_ptr<CHDImpl> impl_;
	u8 *readBuffer = nullptr;
	u32 currentHunk = 0;
	u32 blocksPerHunk = 0;
	u32 numBlocks = 0;
};

BlockDevice *ConstructBlockDevice(FileLoader *fileLoader, std::string *errorString);
