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


#include "Common/FileUtil.h"
#include "Core/FileSystems/BlockDevices.h"
#include <cstdio>
#include <cstring>

extern "C"
{
#include "zlib.h"
#include "ext/libkirk/amctrl.h"
#include "ext/libkirk/kirk_engine.h"
};

BlockDevice *constructBlockDevice(const char *filename) {
	// Check for CISO
	FILE *f = File::OpenCFile(filename, "rb");
	if (!f)
		return 0;
	char buffer[4];
	auto size = fread(buffer, 1, 4, f); //size_t
	fseek(f, 0, SEEK_SET);
	if (!memcmp(buffer, "CISO", 4) && size == 4)
		return new CISOFileBlockDevice(f);
	else if (!memcmp(buffer, "\x00PBP", 4) && size == 4)
		return new NPDRMDemoBlockDevice(f);
	else
		return new FileBlockDevice(f);
}


// Android NDK does not support 64-bit file I/O using C streams
// so we fall back onto syscalls

#ifdef ANDROID

FileBlockDevice::FileBlockDevice(FILE *file)
: f(file)
{
	fd = fileno(file);

	off64_t off = lseek64(fd, 0, SEEK_END);
	filesize = off;
	lseek64(fd, 0, SEEK_SET);
}

FileBlockDevice::~FileBlockDevice()
{
	fclose(f);
}

bool FileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr)
{
	lseek64(fd, (u64)blockNumber * (u64)GetBlockSize(), SEEK_SET);
	if (read(fd, outPtr, 2048) != 2048) {
		ERROR_LOG(FILESYS, "Could not read() 2048 bytes from block");
	}
	return true;
}

#else

FileBlockDevice::FileBlockDevice(FILE *file)
	: f(file)
{
	fseek(f, 0, SEEK_END);
	filesize = ftello(f);
	fseek(f, 0, SEEK_SET);
}

FileBlockDevice::~FileBlockDevice()
{
	fclose(f);
}

bool FileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr) 
{
	fseeko(f, (u64)blockNumber * (u64)GetBlockSize(), SEEK_SET);
	if (fread(outPtr, 1, 2048, f) != 2048)
		DEBUG_LOG(FILESYS, "Could not read 2048 bytes from block");

	return true;
}

#endif

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

CISOFileBlockDevice::CISOFileBlockDevice(FILE *file)
	: f(file)
{
	// CISO format is EXTREMELY crappy and incomplete. All tools make broken CISO.

	f = file;
	CISO_H hdr;
	size_t readSize = fread(&hdr, sizeof(CISO_H), 1, f);
	if (readSize != 1 || memcmp(hdr.magic, "CISO", 4) != 0)
	{
		WARN_LOG(LOADER, "Invalid CSO!");
	}
	else
	{
		VERBOSE_LOG(LOADER, "Valid CSO!");
	}
	if (hdr.ver > 1)
	{
		ERROR_LOG(LOADER, "CSO version too high!");
		//ARGH!
	}

	int hdrSize = hdr.header_size;
	blockSize = hdr.block_size;
	if (blockSize != 0x800)
	{
		ERROR_LOG(LOADER, "CSO Unsupported Block Size");
	}
	indexShift = hdr.align;
	u64 totalSize = hdr.total_bytes;
	numBlocks = (u32)(totalSize / blockSize);
	VERBOSE_LOG(LOADER, "CSO hdrSize=%i numBlocks=%i align=%i", hdrSize, numBlocks, indexShift);

	u32 indexSize = numBlocks + 1;

#if COMMON_LITTLE_ENDIAN
	index = new u32[indexSize];
	if (fread(index, sizeof(u32), indexSize, f) != indexSize)
		memset(index, 0, indexSize * sizeof(u32));
#else
	index = new u32[indexSize];
	u32_le *indexTemp = new u32_le[indexSize];

	if (fread(indexTemp, sizeof(u32), indexSize, f) != indexSize)
	{
		memset(indexTemp, 0, indexSize * sizeof(u32_le));
	}

	for (u32 i = 0; i < indexSize; i++)
	{
		index[i] = indexTemp[i];
	}

	delete[] indexTemp;
#endif
}

CISOFileBlockDevice::~CISOFileBlockDevice()
{
	fclose(f);
	delete [] index;
}

bool CISOFileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr) 
{
	if ((u32)blockNumber >= numBlocks)
	{
		memset(outPtr, 0, 2048);
		return false;
	}

	u32 idx = index[blockNumber];
	u32 idx2 = index[blockNumber+1];
	u8 inbuffer[4096]; //too big
	z_stream z;

	int plain = idx & 0x80000000;

	u64 compressedReadPos = (u64)(idx & 0x7FFFFFFF) << indexShift;
	size_t compressedReadSize = (size_t)(((u64)(idx2 & 0x7FFFFFFF) << indexShift) - compressedReadPos);

	fseeko(f, compressedReadPos, SEEK_SET);
	u32 readSize = (u32)fread(inbuffer, 1, compressedReadSize, f);

	if (plain)
	{
		memset(outPtr, 0, 2048);
		memcpy(outPtr, inbuffer, readSize);
	}
	else
	{
		memset(outPtr, 0, 2048);
		z.zalloc = Z_NULL;
		z.zfree = Z_NULL;
		z.opaque = Z_NULL;
		if(inflateInit2(&z, -15) != Z_OK)
		{
			ERROR_LOG(LOADER, "deflateInit ERROR : %s\n", (z.msg) ? z.msg : "???");
			return false;
		}
		z.avail_in = readSize;
		z.next_out = outPtr;
		z.avail_out = blockSize;
		z.next_in = inbuffer;

		int status = inflate(&z, Z_FULL_FLUSH);
		if(status != Z_STREAM_END)
			//if (status != Z_OK)
		{
			ERROR_LOG(LOADER, "block %d:inflate : %s[%d]\n", blockNumber, (z.msg) ? z.msg : "error", status);
			inflateEnd(&z);
			return 1;
		}
		int cmp_size = blockSize - z.avail_out;
		if (cmp_size != (int)blockSize)
		{
			ERROR_LOG(LOADER, "block %d : block size error %d != %d\n", blockNumber, cmp_size, blockSize);
			inflateEnd(&z);
			return false;
		}
		inflateEnd(&z);
	}
	return true;
}


NPDRMDemoBlockDevice::NPDRMDemoBlockDevice(FILE *file)
	: f(file)
{
	MAC_KEY mkey;
	CIPHER_KEY ckey;
	u8 np_header[256];
	u32 tableOffset, tableSize;
	u32 lbaStart, lbaEnd;

	fseek(f, 0x24, SEEK_SET);
	fread(&psarOffset, 1, 4, f);
	fseek(f, psarOffset, SEEK_SET);
	size_t readSize = fread(&np_header, 1, 256, f);
	if(readSize!=256){
		ERROR_LOG(LOADER, "Invalid NPUMDIMG header!");
	}

	kirk_init();

	// getkey
	sceDrmBBMacInit(&mkey, 3);
	sceDrmBBMacUpdate(&mkey, np_header, 0xc0);
	bbmac_getkey(&mkey, np_header+0xc0, vkey);

	// decrypt NP header
	memcpy(hkey, np_header+0xa0, 0x10);
	sceDrmBBCipherInit(&ckey, 1, 2, hkey, vkey, 0);
	sceDrmBBCipherUpdate(&ckey, np_header+0x40, 0x60);
	sceDrmBBCipherFinal(&ckey);

	lbaStart = *(u32*)(np_header+0x54); // LBA start
	lbaEnd   = *(u32*)(np_header+0x64); // LBA end
	lbaSize  = (lbaEnd-lbaStart+1);     // LBA size of ISO
	blockLBAs = *(u32*)(np_header+0x0c); // block size in LBA
	blockSize = blockLBAs*2048;
	numBlocks = (lbaSize+blockLBAs-1)/blockLBAs; // total blocks;

	blockBuf = new u8[blockSize];
	tempBuf  = new u8[blockSize];

	tableOffset = *(u32*)(np_header+0x6c); // table offset
	fseek(f, psarOffset+tableOffset, SEEK_SET);

	tableSize = numBlocks*32;
	table = new table_info[numBlocks];

	readSize = fread(table, 1, tableSize, f);
	if(readSize!=tableSize){
		ERROR_LOG(LOADER, "Invalid NPUMDIMG table!");
	}

	u32 *p = (u32*)table;
	u32 i, k0, k1, k2, k3;
	for(i=0; i<numBlocks; i++){
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

	currentBlock = -1;

}

NPDRMDemoBlockDevice::~NPDRMDemoBlockDevice()
{
	fclose(f);
	delete [] table;
	delete [] tempBuf;
	delete [] blockBuf;
}

int lzrc_decompress(void *out, int out_len, void *in, int in_len);

bool NPDRMDemoBlockDevice::ReadBlock(int blockNumber, u8 *outPtr)
{
	CIPHER_KEY ckey;
	int block, lba, lzsize;
	size_t readSize;
	u8 *readBuf;

	lba = blockNumber-currentBlock;
	if(lba>=0 && lba<blockLBAs){
		memcpy(outPtr, blockBuf+lba*2048, 2048);
		return true;
	}

	block = blockNumber/blockLBAs;
	lba = blockNumber%blockLBAs;
	currentBlock = block*blockLBAs;

	if(table[block].unk_1c!=0){
		if(block==(numBlocks-1))
			return true; // demos make by fake_np
		else
			return false;
	}

	fseek(f, psarOffset+table[block].offset, SEEK_SET);

	if(table[block].size<blockSize)
		readBuf = tempBuf;
	else
		readBuf = blockBuf;

	readSize = fread(readBuf, 1, table[block].size, f);
	if(readSize != (size_t)table[block].size){
		if(block==(numBlocks-1))
			return true;
		else
			return false;
	}

	if((table[block].flag&1)==0){
		// skip mac check
	}

	if((table[block].flag&4)==0){
		sceDrmBBCipherInit(&ckey, 1, 2, hkey, vkey, table[block].offset>>4);
		sceDrmBBCipherUpdate(&ckey, readBuf, table[block].size);
		sceDrmBBCipherFinal(&ckey);
	}

	if(table[block].size<blockSize){
		lzsize = lzrc_decompress(blockBuf, 0x00100000, readBuf, table[block].size);
		if(lzsize!=blockSize){
			ERROR_LOG(LOADER, "LZRC decompress error! lzsize=%d\n", lzsize);
			return false;
		}
	}

	memcpy(outPtr, blockBuf+lba*2048, 2048);

	return true;
}
