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

#include <algorithm>
#include <cstring>

#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"
#include "Common/Log.h"
#include "Common/Swap.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/StringUtils.h"
#include "Core/Loaders.h"
#include "Core/FileSystems/BlockDevices.h"
#include "libchdr/chd.h"

extern "C"
{
#include "zlib.h"
#include "ext/libkirk/amctrl.h"
#include "ext/libkirk/kirk_engine.h"
};

BlockDevice *ConstructBlockDevice(FileLoader *fileLoader, std::string *errorString) {
	if (!fileLoader->Exists()) {
		// Shouldn't get here really.
		*errorString = "File doesn't exist";
		return nullptr;
	}
	if (fileLoader->IsDirectory()) {
		*errorString = "Can't open directory directly as block device: ";
		*errorString += fileLoader->GetPath().ToString();
		return nullptr;
	}

	char buffer[8]{};
	size_t size = fileLoader->ReadAt(0, 1, 8, buffer);
	if (size != 8) {
		// Bad or empty file
		*errorString = "File is empty";
		return nullptr;
	}

	BlockDevice *device = nullptr;

	// Check for CISO
	if (!memcmp(buffer, "CISO", 4)) {
		device = new CISOFileBlockDevice(fileLoader);
	} else if (!memcmp(buffer, "\x00PBP", 4)) {
		uint32_t psarOffset = 0;
		size = fileLoader->ReadAt(0x24, 1, 4, &psarOffset);
		if (size == 4 && psarOffset < fileLoader->FileSize())
			device = new NPDRMDemoBlockDevice(fileLoader);
	} else if (!memcmp(buffer, "MComprHD", 8)) {
		device = new CHDFileBlockDevice(fileLoader);
	}

	// No check above passed, should be just a regular ISO file. Let's open it as a plain block device and let the other systems take over.
	if (!device) {
		device = new FileBlockDevice(fileLoader);
	}

	if (!device->IsOK()) {
		*errorString = device->ErrorString();
		delete device;
		return nullptr;
	}

	return device;
}

void BlockDevice::NotifyReadError() {
	if (!reportedError_) {
		auto err = GetI18NCategory(I18NCat::ERRORS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, err->T("Game disc read error - ISO corrupt"), fileLoader_->GetPath().ToVisualString(), 6.0f);
		reportedError_ = true;
	}
}

FileBlockDevice::FileBlockDevice(FileLoader *fileLoader)
	: BlockDevice(fileLoader) {
	filesize_ = fileLoader->FileSize();
}

FileBlockDevice::~FileBlockDevice() {}

bool FileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr, bool uncached) {
	FileLoader::Flags flags = uncached ? FileLoader::Flags::HINT_UNCACHED : FileLoader::Flags::NONE;
	size_t retval = fileLoader_->ReadAt((u64)blockNumber * (u64)GetBlockSize(), 1, 2048, outPtr, flags);
	if (retval != 2048) {
		DEBUG_LOG(Log::FileSystem, "Could not read 2048 byte block, at block offset %d. Only got %d bytes", blockNumber, (int)retval);
		return false;
	}
	return true;
}

bool FileBlockDevice::ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
	size_t retval = fileLoader_->ReadAt((u64)minBlock * (u64)GetBlockSize(), 2048, count, outPtr);
	if (retval != (size_t)count) {
		ERROR_LOG(Log::FileSystem, "Could not read %d blocks, at block offset %d. Only got %d blocks", count, minBlock, (int)retval);
		return false;
	}
	return true;
}

// .CSO format

// compressed ISO(9660) header format
typedef struct ciso_header
{
	unsigned char magic[4];         // +00 : 'C','I','S','O'
	u32_le header_size;             // +04 : header size (==0x18)
	u64_le total_bytes;             // +08 : number of original data size
	u32_le block_size;              // +10 : number of compressed block size
	unsigned char ver;              // +14 : version 01
	unsigned char align;            // +15 : align of index value
	unsigned char rsv_06[2];        // +16 : reserved
#if 0
	// INDEX BLOCK
	unsigned int index[0];          // +18 : block[0] index
	unsigned int index[1];          // +1C : block[1] index
	:
	:
	unsigned int index[last];       // +?? : block[last]
	unsigned int index[last+1];     // +?? : end of last data point
	// DATA BLOCK
	unsigned char data[];           // +?? : compressed or plain sector data
#endif
} CISO_H;


// TODO: Need much better error handling.

static const u32 CSO_READ_BUFFER_SIZE = 256 * 1024;

CISOFileBlockDevice::CISOFileBlockDevice(FileLoader *fileLoader)
	: BlockDevice(fileLoader)
{
	// CISO format is fairly simple, but most tools do not write the header_size.

	CISO_H hdr;
	size_t readSize = fileLoader->ReadAt(0, sizeof(CISO_H), 1, &hdr);
	if (readSize != 1 || memcmp(hdr.magic, "CISO", 4) != 0) {
		errorString_ = "Invalid CSO!";
		return;
	}
	if (hdr.ver > 1) {
		errorString_ = "CSO version too high!";
		return;
	}

	frameSize = hdr.block_size;
	if ((frameSize & (frameSize - 1)) != 0) {
		errorString_ = StringFromFormat("CSO block size %i unsupported, must be a power of two", frameSize);
		return;
	} else if (frameSize < 0x800) {
		errorString_ = StringFromFormat("CSO block size %i unsupported, must be at least one sector", frameSize);
		return;
	}

	// Determine the translation from block to frame.
	blockShift = 0;
	for (u32 i = frameSize; i > 0x800; i >>= 1)
		++blockShift;

	indexShift = hdr.align;
	const u64 totalSize = hdr.total_bytes;
	numFrames = (u32)((totalSize + frameSize - 1) / frameSize);
	numBlocks = (u32)(totalSize / GetBlockSize());
	VERBOSE_LOG(Log::Loader, "CSO numBlocks=%i numFrames=%i align=%i", numBlocks, numFrames, indexShift);

	// We might read a bit of alignment too, so be prepared.
	if (frameSize + (1 << indexShift) < CSO_READ_BUFFER_SIZE)
		readBuffer = new u8[CSO_READ_BUFFER_SIZE];
	else
		readBuffer = new u8[frameSize + (1 << indexShift)];
	zlibBuffer = new u8[frameSize + (1 << indexShift)];
	zlibBufferFrame = numFrames;

	const u32 indexSize = numFrames + 1;
	const size_t headerEnd = hdr.ver > 1 ? (size_t)hdr.header_size : sizeof(hdr);

#if COMMON_LITTLE_ENDIAN
	index = new u32[indexSize];
	if (fileLoader->ReadAt(headerEnd, sizeof(u32), indexSize, index) != indexSize) {
		NotifyReadError();
		memset(index, 0, indexSize * sizeof(u32));
	}
#else
	index = new u32[indexSize];
	u32_le *indexTemp = new u32_le[indexSize];

	if (fileLoader->ReadAt(headerEnd, sizeof(u32), indexSize, indexTemp) != indexSize) {
		NotifyReadError();
		memset(indexTemp, 0, indexSize * sizeof(u32_le));
	}

	for (u32 i = 0; i < indexSize; i++)
		index[i] = indexTemp[i];

	delete[] indexTemp;
#endif

	ver_ = hdr.ver;

	// Double check that the CSO is not truncated.  In most cases, this will be the exact size.
	u64 fileSize = fileLoader->FileSize();
	u64 lastIndexPos = index[indexSize - 1] & 0x7FFFFFFF;
	u64 expectedFileSize = lastIndexPos << indexShift;
	if (expectedFileSize > fileSize) {
		errorString_ = StringFromFormat("CSO file incomplete: expected %s, but is %s", NiceSizeFormat(expectedFileSize).c_str(), NiceSizeFormat(fileSize).c_str());
		return;
	}

	// all ok.
	_dbg_assert_(errorString_.empty());
}

CISOFileBlockDevice::~CISOFileBlockDevice()
{
	delete [] index;
	delete [] readBuffer;
	delete [] zlibBuffer;
}

bool CISOFileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr, bool uncached)
{
	FileLoader::Flags flags = uncached ? FileLoader::Flags::HINT_UNCACHED : FileLoader::Flags::NONE;
	if ((u32)blockNumber >= numBlocks) {
		memset(outPtr, 0, GetBlockSize());
		return false;
	}

	const u32 frameNumber = blockNumber >> blockShift;
	const u32 idx = index[frameNumber];
	const u32 indexPos = idx & 0x7FFFFFFF;
	const u32 nextIndexPos = index[frameNumber + 1] & 0x7FFFFFFF;
	z_stream z{};

	const u64 compressedReadPos = (u64)indexPos << indexShift;
	const u64 compressedReadEnd = (u64)nextIndexPos << indexShift;
	const size_t compressedReadSize = (size_t)(compressedReadEnd - compressedReadPos);
	const u32 compressedOffset = (blockNumber & ((1 << blockShift) - 1)) * GetBlockSize();

	bool plain = (idx & 0x80000000) != 0;
	if (ver_ >= 2) {
		// CSO v2+ requires blocks be uncompressed if large enough to be.  High bit means other things.
		plain = compressedReadSize >= frameSize;
	}
	if (plain) {
		int readSize = (u32)fileLoader_->ReadAt(compressedReadPos + compressedOffset, 1, GetBlockSize(), outPtr, flags);
		if (readSize < GetBlockSize())
			memset(outPtr + readSize, 0, GetBlockSize() - readSize);
	} else if (zlibBufferFrame == frameNumber) {
		// We already have it.  Just apply the offset and copy.
		memcpy(outPtr, zlibBuffer + compressedOffset, GetBlockSize());
	} else {
		const u32 readSize = (u32)fileLoader_->ReadAt(compressedReadPos, 1, compressedReadSize, readBuffer, flags);

		z.zalloc = Z_NULL;
		z.zfree = Z_NULL;
		z.opaque = Z_NULL;
		if (inflateInit2(&z, -15) != Z_OK) {
			ERROR_LOG(Log::Loader, "GetBlockSize() ERROR: %s\n", (z.msg) ? z.msg : "?");
			NotifyReadError();
			return false;
		}
		z.avail_in = readSize;
		z.next_out = frameSize == (u32)GetBlockSize() ? outPtr : zlibBuffer;
		z.avail_out = frameSize;
		z.next_in = readBuffer;

		int status = inflate(&z, Z_FINISH);
		if (status != Z_STREAM_END) {
			ERROR_LOG(Log::Loader, "block %d: inflate : %s[%d]\n", blockNumber, (z.msg) ? z.msg : "error", status);
			NotifyReadError();
			inflateEnd(&z);
			memset(outPtr, 0, GetBlockSize());
			return false;
		}
		if (z.total_out != frameSize) {
			ERROR_LOG(Log::Loader, "block %d: block size error %d != %d\n", blockNumber, (u32)z.total_out, frameSize);
			NotifyReadError();
			inflateEnd(&z);
			memset(outPtr, 0, GetBlockSize());
			return false;
		}
		inflateEnd(&z);

		if (frameSize != (u32)GetBlockSize()) {
			zlibBufferFrame = frameNumber;
			memcpy(outPtr, zlibBuffer + compressedOffset, GetBlockSize());
		}
	}
	return true;
}

bool CISOFileBlockDevice::ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
	if (count == 1) {
		return ReadBlock(minBlock, outPtr);
	}
	if (minBlock >= numBlocks) {
		memset(outPtr, 0, GetBlockSize() * count);
		return false;
	}

	const u32 lastBlock = std::min(minBlock + count, numBlocks) - 1;
	const u32 missingBlocks = (lastBlock + 1 - minBlock) - count;
	if (lastBlock < minBlock + count) {
		memset(outPtr + GetBlockSize() * (count - missingBlocks), 0, GetBlockSize() * missingBlocks);
	}

	const u32 minFrameNumber = minBlock >> blockShift;
	const u32 lastFrameNumber = lastBlock >> blockShift;
	const u32 afterLastIndexPos = index[lastFrameNumber + 1] & 0x7FFFFFFF;
	const u64 totalReadEnd = (u64)afterLastIndexPos << indexShift;

	z_stream z{};
	if (inflateInit2(&z, -15) != Z_OK) {
		ERROR_LOG(Log::Loader, "Unable to initialize inflate: %s\n", (z.msg) ? z.msg : "?");
		return false;
	}

	u64 readBufferStart = 0;
	u64 readBufferEnd = 0;
	u32 block = minBlock;
	const u32 blocksPerFrame = 1 << blockShift;
	for (u32 frame = minFrameNumber; frame <= lastFrameNumber; ++frame) {
		const u32 idx = index[frame];
		const u32 indexPos = idx & 0x7FFFFFFF;
		const u32 nextIndexPos = index[frame + 1] & 0x7FFFFFFF;

		const u64 frameReadPos = (u64)indexPos << indexShift;
		const u64 frameReadEnd = (u64)nextIndexPos << indexShift;
		const u32 frameReadSize = (u32)(frameReadEnd - frameReadPos);
		const u32 frameBlockOffset = block & ((1 << blockShift) - 1);
		const u32 frameBlocks = std::min(lastBlock - block + 1, blocksPerFrame - frameBlockOffset);

		if (frameReadEnd > readBufferEnd) {
			const s64 maxNeeded = totalReadEnd - frameReadPos;
			const size_t chunkSize = (size_t)std::min(maxNeeded, (s64)std::max(frameReadSize, CSO_READ_BUFFER_SIZE));

			const u32 readSize = (u32)fileLoader_->ReadAt(frameReadPos, 1, chunkSize, readBuffer);
			if (readSize < chunkSize) {
				memset(readBuffer + readSize, 0, chunkSize - readSize);
			}

			readBufferStart = frameReadPos;
			readBufferEnd = frameReadPos + readSize;
		}

		u8 *rawBuffer = &readBuffer[frameReadPos - readBufferStart];
		const int plain = idx & 0x80000000;
		if (plain) {
			memcpy(outPtr, rawBuffer + frameBlockOffset * GetBlockSize(), frameBlocks * GetBlockSize());
		} else {
			z.avail_in = frameReadSize;
			z.next_out = frameBlocks == blocksPerFrame ? outPtr : zlibBuffer;
			z.avail_out = frameSize;
			z.next_in = rawBuffer;

			int status = inflate(&z, Z_FINISH);
			if (status != Z_STREAM_END) {
				ERROR_LOG(Log::Loader, "Inflate frame %d: failed - %s[%d]\n", frame, (z.msg) ? z.msg : "error", status);
				NotifyReadError();
				memset(outPtr, 0, frameBlocks * GetBlockSize());
			} else if (z.total_out != frameSize) {
				ERROR_LOG(Log::Loader, "Inflate frame %d: block size error %d != %d\n", frame, (u32)z.total_out, frameSize);
				NotifyReadError();
				memset(outPtr, 0, frameBlocks * GetBlockSize());
			} else if (frameBlocks != blocksPerFrame) {
				memcpy(outPtr, zlibBuffer + frameBlockOffset * GetBlockSize(), frameBlocks * GetBlockSize());
				// In case we end up reusing it in a single read later.
				zlibBufferFrame = frame;
			}

			inflateReset(&z);
		}

		block += frameBlocks;
		outPtr += frameBlocks * GetBlockSize();
	}

	inflateEnd(&z);
	return true;
}

NPDRMDemoBlockDevice::NPDRMDemoBlockDevice(FileLoader *fileLoader)
	: BlockDevice(fileLoader)
{
	MAC_KEY mkey;
	CIPHER_KEY ckey;
	u8 np_header[256];
	u32 tableOffset_, tableSize_;

	fileLoader_->ReadAt(0x24, 1, 4, &psarOffset);
	size_t readSize = fileLoader_->ReadAt(psarOffset, 1, 256, &np_header);
	if (readSize != 256){
		errorString_ = "Invalid NPUMDIMG header!";
		return;
	}

	u32 psar_id;
	fileLoader->ReadAt(psarOffset, 4, 1, &psar_id);

	INFO_LOG(Log::Loader, "NPDRM: PSAR ID: %08x", psar_id);
	// PS1 PSAR begins with "PSISOIMG0000"
	if (psar_id == 'SISP') {
		lbaSize_ = 0;  // Mark invalid
		ERROR_LOG(Log::Loader, "PSX not supported! Should have been caught earlier.");
		errorString_ = "PSX ISOs not supported!";
		return;
	}

	std::lock_guard<std::mutex> guard(mutex_);

	// Local kirk instance to not clash with other block devices and other decryption things.
	kirk_init(&kirk_);

	// getkey
	sceDrmBBMacInit(&mkey, 3);
	sceDrmBBMacUpdate(&kirk_, &mkey, np_header, 0xc0);
	bbmac_getkey(&kirk_, &mkey, np_header+0xc0, vkey);

	// decrypt NP header
	memcpy(hkey, np_header+0xa0, 0x10);
	sceDrmBBCipherInit(&kirk_, &ckey, 1, 2, hkey, vkey, 0);
	sceDrmBBCipherUpdate(&kirk_, &ckey, np_header+0x40, 0x60);
	sceDrmBBCipherFinal(&ckey);

	u32 lbaStart = *(u32*)(np_header+0x54); // LBA start
	u32 lbaEnd   = *(u32*)(np_header+0x64); // LBA end
	lbaSize_     = (lbaEnd-lbaStart+1);     // LBA size of ISO
	blockLBAs_   = *(u32*)(np_header+0x0c); // block size in LBA

	char psarStr[5] = {};
	memcpy(psarStr, &psar_id, 4);

	// Protect against a badly decrypted header, and send information through the assert about what's being played (implicitly).
	_dbg_assert_msg_(blockLBAs_ <= 4096, "Bad blockLBAs in header: %08x (%s) psar: %s", blockLBAs_, fileLoader->GetPath().ToVisualString().c_str(), psarStr);

	// When we remove the above assert, let's just try to survive.
	if (blockLBAs_ > 4096) {
		errorString_ = StringFromFormat("Bad blockLBAs in header: %08x (%s) psar: %s", blockLBAs_, fileLoader->GetPath().ToVisualString().c_str(), psarStr);
		return;
	}

	blockSize_ = blockLBAs_ * 2048;
	numBlocks_ = (lbaSize_ + blockLBAs_-1) / blockLBAs_; // total blocks;

	blockBuf_ = new u8[blockSize_];
	tempBuf_  = new u8[blockSize_];

	tableOffset_ = *(u32*)(np_header+0x6c); // table offset

	tableSize_ = numBlocks_ * 32;
	table_ = new table_info[numBlocks_];

	readSize = fileLoader_->ReadAt(psarOffset + tableOffset_, 1, tableSize_, table_);
	if (readSize != tableSize_){
		errorString_ = "Invalid NPUMDIMG table!";
		return;
	}

	u32 *p = (u32*)table_;
	u32 i, k0, k1, k2, k3;
	for (i=0; i<numBlocks_; i++){
		k0 = p[0]^p[1];
		k1 = p[1]^p[2];
		k2 = p[0]^p[3];
		k3 = p[2]^p[3];
		p[4] ^= k3;
		p[5] ^= k1;
		p[6] ^= k2;
		p[7] ^= k0;
		p += 8;
	}

	currentBlock_ = -1;
	_dbg_assert_(errorString_.empty());
}

NPDRMDemoBlockDevice::~NPDRMDemoBlockDevice() {
	std::lock_guard<std::mutex> guard(mutex_);
	delete [] table_;
	delete [] tempBuf_;
	delete [] blockBuf_;
}

int lzrc_decompress(void *out, int out_len, void *in, int in_len);

bool NPDRMDemoBlockDevice::ReadBlock(int blockNumber, u8 *outPtr, bool uncached) {
	FileLoader::Flags flags = uncached ? FileLoader::Flags::HINT_UNCACHED : FileLoader::Flags::NONE;
	std::lock_guard<std::mutex> guard(mutex_);

	if (blockSize_ == 0) {
		// Wasn't opened successfully.
		return false;
	}

	int lba = blockNumber - currentBlock_;
	if (lba >= 0 && lba < blockLBAs_){
		memcpy(outPtr, blockBuf_ + lba*2048, 2048);
		return true;
	}

	int block = blockNumber / blockLBAs_;
	lba = blockNumber % blockLBAs_;
	currentBlock_ = block * blockLBAs_;

	if (table_[block].unk_1c != 0) {
		if((u32)block == (numBlocks_ - 1))
			return true; // demos make by fake_np
		else
			return false;
	}

	u8 *readBuf;
	if (table_[block].size < blockSize_)
		readBuf = tempBuf_;
	else
		readBuf = blockBuf_;

	size_t readSize = fileLoader_->ReadAt(psarOffset+table_[block].offset, 1, table_[block].size, readBuf, flags);
	if (readSize != (size_t)table_[block].size){
		if((u32)block==(numBlocks_-1))
			return true;
		else
			return false;
	}

	if ((table_[block].flag & 1) == 0) {
		// skip mac check
	}

	if ((table_[block].flag & 4) == 0) {
		CIPHER_KEY ckey;
		sceDrmBBCipherInit(&kirk_, &ckey, 1, 2, hkey, vkey, table_[block].offset>>4);
		sceDrmBBCipherUpdate(&kirk_, &ckey, readBuf, table_[block].size);
		sceDrmBBCipherFinal(&ckey);
	}

	if (table_[block].size < blockSize_) {
		int lzsize = lzrc_decompress(blockBuf_, 0x00100000, readBuf, table_[block].size);
		if(lzsize != blockSize_){
			ERROR_LOG(Log::Loader, "LZRC decompress error! lzsize=%d\n", lzsize);
			NotifyReadError();
			return false;
		}
	}

	memcpy(outPtr, blockBuf_+lba*2048, 2048);
	return true;
}

// static const UINT8 nullsha1[CHD_SHA1_BYTES] = { 0 };

struct CHDImpl {
	chd_file *chd = nullptr;
	const chd_header *header = nullptr;
};

struct ExtendedCoreFile {
	core_file core;  // Must be the first struct member, for some tricky pointer casts.
	uint64_t seekPos;
};

CHDFileBlockDevice::CHDFileBlockDevice(FileLoader *fileLoader)
	: BlockDevice(fileLoader), impl_(new CHDImpl()) {
	Path paths[8];
	paths[0] = fileLoader->GetPath();
	int depth = 0;

	core_file_ = new ExtendedCoreFile();
	core_file_->core.argp = fileLoader;
	core_file_->core.fsize = [](core_file *file) -> uint64_t {
		FileLoader *loader = (FileLoader *)file->argp;
		return loader->FileSize();
	};
	core_file_->core.fseek = [](core_file *file, int64_t offset, int seekType) -> int {
		ExtendedCoreFile *coreFile = (ExtendedCoreFile *)file;
		switch (seekType) {
		case SEEK_SET:
			coreFile->seekPos = offset;
			break;
		case SEEK_CUR:
			coreFile->seekPos += offset;
			break;
		case SEEK_END:
		{
			FileLoader *loader = (FileLoader *)file->argp;
			coreFile->seekPos = loader->FileSize() + offset;
			break;
		}
		default:
			break;
		}
		return 0;
	};
	core_file_->core.fread = [](void *out_data, size_t size, size_t count, core_file *file) {
		ExtendedCoreFile *coreFile = (ExtendedCoreFile *)file;
		FileLoader *loader = (FileLoader *)file->argp;
		uint64_t totalSize = size * count;
		loader->ReadAt(coreFile->seekPos, totalSize, out_data);
		coreFile->seekPos += totalSize;
		return size * count;
	};
	core_file_->core.fclose = [](core_file *file) {
		ExtendedCoreFile *coreFile = (ExtendedCoreFile *)file;
		delete coreFile;
		return 0;
	};

	/*
	// TODO: Support parent/child CHD files.

	// Default, in case of failure
	numBlocks = 0;

	chd_header childHeader;

	chd_error err = chd_read_header(paths[0].c_str(), &childHeader);
	if (err != CHDERR_NONE) {
		ERROR_LOG(Log::Loader, "Error loading CHD header for '%s': %s", paths[0].c_str(), chd_error_string(err));
		NotifyReadError();
		return;
	}

	if (memcmp(nullsha1, childHeader.parentsha1, sizeof(childHeader.sha1)) != 0) {
		chd_header parentHeader;

		// Look for parent CHD in current directory
		Path chdDir = paths[0].NavigateUp();

		std::vector<File::FileInfo> files;
		if (File::GetFilesInDir(chdDir, &files)) {
			parentHeader.length = 0;

			for (const auto &file : files) {
				std::string extension = file.fullName.GetFileExtension();
				if (extension != ".chd") {
					continue;
				}

				if (chd_read_header(filepath.c_str(), &parentHeader) == CHDERR_NONE &&
					memcmp(parentHeader.sha1, childHeader.parentsha1, sizeof(parentHeader.sha1)) == 0) {
					// ERROR_LOG(Log::Loader, "Checking '%s'", filepath.c_str());
					paths[++depth] = filepath;
					break;
				}
			}

			// Check if parentHeader was opened
			if (parentHeader.length == 0) {
				ERROR_LOG(Log::Loader, "Error loading CHD '%s': parents not found", fileLoader->GetPath().c_str());
				NotifyReadError();
				return;
			}
			memcpy(childHeader.parentsha1, parentHeader.parentsha1, sizeof(childHeader.parentsha1));
		} while (memcmp(nullsha1, childHeader.parentsha1, sizeof(childHeader.sha1)) != 0);
	}
	*/

	chd_file *file = nullptr;
	chd_error err = chd_open_core_file(&core_file_->core, CHD_OPEN_READ, NULL, &file);
	if (err != CHDERR_NONE) {
		errorString_ = StringFromFormat("CHD error: %s", paths[depth].c_str(), chd_error_string(err));
		return;
	}

	impl_->chd = file;
	impl_->header = chd_get_header(impl_->chd);

	readBuffer = new u8[impl_->header->hunkbytes];
	currentHunk = -1;
	blocksPerHunk = impl_->header->hunkbytes / impl_->header->unitbytes;
	numBlocks = impl_->header->unitcount;

	_dbg_assert_(errorString_.empty());
}

CHDFileBlockDevice::~CHDFileBlockDevice() {
	if (impl_->chd) {
		chd_close(impl_->chd);
		delete[] readBuffer;
	}
}

bool CHDFileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr, bool uncached) {
	if (!impl_->chd) {
		ERROR_LOG(Log::Loader, "ReadBlock: CHD not open. %s", fileLoader_->GetPath().c_str());
		return false;
	}
	if ((u32)blockNumber >= numBlocks) {
		memset(outPtr, 0, GetBlockSize());
		return false;
	}
	u32 hunk = blockNumber / blocksPerHunk;
	u32 blockInHunk = blockNumber % blocksPerHunk;

	if (currentHunk != hunk) {
		chd_error err = chd_read(impl_->chd, hunk, readBuffer);
		if (err != CHDERR_NONE) {
			ERROR_LOG(Log::Loader, "CHD read failed: %d %d %s", blockNumber, hunk, chd_error_string(err));
			NotifyReadError();
		}
		currentHunk = hunk;
	}
	memcpy(outPtr, readBuffer + blockInHunk * impl_->header->unitbytes, GetBlockSize());
	return true;
}

bool CHDFileBlockDevice::ReadBlocks(u32 minBlock, int count, u8 *outPtr) {
	if (minBlock >= numBlocks) {
		memset(outPtr, 0, GetBlockSize() * count);
		return false;
	}

	for (int i = 0; i < count; i++) {
		if (!ReadBlock(minBlock + i, outPtr + i * GetBlockSize())) {
			return false;
		}
	}
	return true;
}
