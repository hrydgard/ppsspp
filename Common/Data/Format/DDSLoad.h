#pragma once

#include <cstdint>

// DDSPixelFormat.dwFlags bits
enum {
	DDPF_ALPHAPIXELS = 1,   // Texture contains alpha data; dwRGBAlphaBitMask contains valid data.
	DDPF_ALPHA       = 2,   // Used in some older DDS files for alpha channel only uncompressed data(dwRGBBitCount contains the alpha channel bitcount; dwABitMask contains valid data)
	DDPF_FOURCC      = 4,   // Texture contains compressed RGB data; dwFourCC contains valid data.	0x4
	DDPF_RGB         = 8,   // Texture contains uncompressed RGB data; dwRGBBitCount and the RGB masks(dwRBitMask, dwGBitMask, dwBBitMask) contain valid data.	0x40
	DDPF_YUV         = 16,  //Used in some older DDS files for YUV uncompressed data(dwRGBBitCount contains the YUV bit count; dwRBitMask contains the Y mask, dwGBitMask contains the U mask, dwBBitMask contains the V mask)
	DDPF_LUMINANCE   = 32,  // Used in some older DDS files for single channel color uncompressed data (dwRGBBitCount contains the luminance channel bit count; dwRBitMask contains the channel mask). Can be combined with DDPF_ALPHAPIXELS for a two channel DDS file.
};

// dwCaps members
enum {
	DDSCAPS_COMPLEX = 8,
	DDSCAPS_MIPMAP = 0x400000,
	DDSCAPS_TEXTURE = 0x1000,  // Required
};

// Not using any D3D headers here, this is cross platform and minimal.
// Boiled down from the public documentation.
struct DDSPixelFormat {
	uint32_t dwSize;  // must be 32
	uint32_t dwFlags;
	uint32_t dwFourCC;
	uint32_t dwRGBBitCount;
	uint32_t dwRBitMask;
	uint32_t dwGBitMask;
	uint32_t dwBBitMask;
	uint32_t dwABitMask;
};

struct DDSHeader {
	uint32_t dwMagic;  // Magic is not technically part of the header struct but convenient to have here when reading the files.
	uint32_t dwSize;  // must be 124
	uint32_t dwFlags;
	uint32_t dwHeight;
	uint32_t dwWidth;
	uint32_t dwPitchOrLinearSize;  // The pitch or number of bytes per scan line in an uncompressed texture; the total number of bytes in the top level texture for a compressed texture
	uint32_t dwDepth; // we'll always use 1 here
	uint32_t dwMipMapCount;
	uint32_t dwReserved1[11];
	DDSPixelFormat ddspf;
	uint32_t dwCaps;
	uint32_t dwCaps2; // nothing we care about
	uint32_t dwCaps3; // unused
	uint32_t dwCaps4; // unused
	uint32_t dwReserved2;
};

// DDS header extension to handle resource arrays, DXGI pixel formats that don't map to the legacy Microsoft DirectDraw pixel format structures, and additional metadata.
struct DDSHeaderDXT10 {
	uint32_t dxgiFormat;
	uint32_t resourceDimension;  // 1d = 2, 2d = 3, 3d = 4. very intuitive
	uint32_t miscFlag;
	uint32_t arraySize;  // we only support 1 here
	uint32_t miscFlags2;  // sets alpha interpretation, let's not bother
};

// Simple DDS parser, suitable for texture replacement packs.
// Doesn't actually load, only does some logic to fill out DDSLoadInfo so the caller can then
// do the actual load with a simple series of memcpys or whatever is appropriate.
struct DDSLoadInfo {
	uint32_t bytesToCopy;
};

bool DetectDDSParams(const DDSHeader *header, DDSLoadInfo *info);

// We use the Basis library for the actual reading, but before we do, we pre-scan using this, similarly to the png trick.
struct KTXHeader {
	uint8_t identifier[12];
	uint32_t vkFormat;
	uint32_t typeSize;
	uint32_t pixelWidth;
	uint32_t pixelHeight;
	uint32_t pixelDepth;
	uint32_t layerCount;
	uint32_t faceCount;
	uint32_t levelCount;
	uint32_t supercompressionScheme;
};
