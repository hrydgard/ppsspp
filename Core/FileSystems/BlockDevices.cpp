// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

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
	fread(outPtr, 2048, 1, f);
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
	fread(&hdr, 1, sizeof(CISO_H), f);
	if (memcmp(hdr.magic, "CISO", 4) != 0)
	{
		//ARGH!
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
	numBlocks = (int)(totalSize / blockSize);
	DEBUG_LOG(LOADER, "hdrSize=%i numBlocks=%i align=%i", hdrSize, numBlocks, indexShift);

	int indexSize = numBlocks + 1;

	index = new u32[indexSize];
	fread(index, 4, indexSize, f);
}

CISOFileBlockDevice::~CISOFileBlockDevice()
{
	fclose(f);
	delete [] index;
}

bool CISOFileBlockDevice::ReadBlock(int blockNumber, u8 *outPtr) 
{
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
	fread(inbuffer, compressedReadSize, 1, f);

	if (plain)
	{
		memset(outPtr, 0, 2048);
		memcpy(outPtr, inbuffer, compressedReadSize);
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
			return 1;
		}
		z.avail_in = compressedReadSize;
		z.next_out = outPtr;
		z.avail_out = blockSize;
		z.next_in = inbuffer;

		int status = inflate(&z, Z_FULL_FLUSH);
		if(status != Z_STREAM_END)
			//if (status != Z_OK)
		{
			ERROR_LOG(LOADER, "block %d:inflate : %s[%d]\n", blockNumber, (z.msg) ? z.msg : "error", status);
			return 1;
		}
		int cmp_size = blockSize - z.avail_out;
		if (cmp_size != (int)blockSize)
		{
			ERROR_LOG(LOADER, "block %d : block size error %d != %d\n", blockNumber, cmp_size, blockSize);
			return 1;
		}
	}
	return true;
}
