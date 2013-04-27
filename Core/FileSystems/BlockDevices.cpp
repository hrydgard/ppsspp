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

extern "C"
{
#include "zlib.h"
};

#include "BlockDevices.h"
#include <cstdio>
#include <cstring>

BlockDevice *constructBlockDevice(const char *filename) {
	// Check for CISO
	FILE *f = fopen(filename, "rb");
	if (!f)
		return 0;
	char buffer[4];
	auto size = fread(buffer, 1, 4, f); //size_t
	fclose(f);
	if (!memcmp(buffer, "CISO", 4) && size == 4)
		return new CISOFileBlockDevice(filename);
	else
		return new FileBlockDevice(filename);
}

FileBlockDevice::FileBlockDevice(std::string _filename)
: filename(_filename)
{
	f = fopen(_filename.c_str(), "rb");
	fseek(f,0,SEEK_END);
	filesize = ftell(f);
	fseek(f,0,SEEK_SET);
}

FileBlockDevice::~FileBlockDevice()
{
	fclose(f);
}

bool FileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr) 
{
	fseek(f, blockNumber * GetBlockSize(), SEEK_SET);
	if(fread(outPtr, 1, 2048, f) != 2048)
		DEBUG_LOG(LOADER, "Could not read 2048 bytes from block");

	return true;
}

// .CSO format

// complessed ISO(9660) header format
typedef struct ciso_header
{
	unsigned char magic[4];			// +00 : 'C','I','S','O'                 
	u32 header_size;		// +04 : header size (==0x18)            
	u64 total_bytes;	// +08 : number of original data size    
	u32 block_size;		// +10 : number of compressed block size 
	unsigned char ver;				// +14 : version 01                      
	unsigned char align;			// +15 : align of index value            
	unsigned char rsv_06[2];		// +16 : reserved                        
#if 0
	// INDEX BLOCK
	unsigned int index[0];			// +18 : block[0] index                 
	unsigned int index[1];			// +1C : block[1] index                  
	:
	:
	unsigned int index[last];		// +?? : block[last]                     
	unsigned int index[last+1];		// +?? : end of last data point          
	// DATA BLOCK
	unsigned char data[];			// +?? : compressed or plain sector data
#endif
} CISO_H;


// TODO: Need much better error handling.

CISOFileBlockDevice::CISOFileBlockDevice(std::string _filename)
: filename(_filename)
{
	// CISO format is EXTREMELY crappy and incomplete. All tools make broken CISO.

	f = fopen(_filename.c_str(), "rb");
	CISO_H hdr;
	size_t readSize = fread(&hdr, sizeof(CISO_H), 1, f);
	if (readSize != 1 || memcmp(hdr.magic, "CISO", 4) != 0)
	{
		WARN_LOG(LOADER, "Invalid CSO!");
	}
	else
	{
		DEBUG_LOG(LOADER, "Valid CSO!");
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
	DEBUG_LOG(LOADER, "hdrSize=%i numBlocks=%i align=%i", hdrSize, numBlocks, indexShift);

	u32 indexSize = numBlocks + 1;

	index = new u32[indexSize];
	if(fread(index, sizeof(u32), indexSize, f) != indexSize)
		memset(index, 0, indexSize * sizeof(u32));
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

	idx = (idx & 0x7FFFFFFF) << indexShift;
	idx2 = (idx2 & 0x7FFFFFFF) << indexShift;

	u32 compressedReadPos = idx;
	u32 compressedReadSize = idx2 - idx;

	fseek(f, compressedReadPos, SEEK_SET);
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


NPDRMDemoBlockDevice::NPDRMDemoBlockDevice(std::string _filename) 
	: filename_(_filename), pbpReader_(_filename.c_str()) {
	std::string paramSfo;
	pbpReader_.GetSubFileAsString(PBP_PARAM_SFO, &paramSfo);
}

NPDRMDemoBlockDevice::~NPDRMDemoBlockDevice() {

}

bool NPDRMDemoBlockDevice::ReadBlock(int blockNumber, u8 *outPtr) {
	// TODO: Fill in decryption code here. Use pbpReader to read the file - might need to 
	// extend its functionality to do it efficiently.

	return false;
}
