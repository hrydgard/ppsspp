#pragma once

#include <cstdint>
#include <cstdlib>

namespace Draw {

enum class DataFormat : uint8_t {
	UNDEFINED,

	R8_UNORM,
	R8G8_UNORM,
	R8G8B8_UNORM,

	R8G8B8A8_UNORM,
	R8G8B8A8_UNORM_SRGB,
	B8G8R8A8_UNORM,  // D3D style
	B8G8R8A8_UNORM_SRGB,  // D3D style

	R8G8B8A8_SNORM,
	R8G8B8A8_UINT,
	R8G8B8A8_SINT,

	R4G4_UNORM_PACK8,
	A4R4G4B4_UNORM_PACK16,  // A4 in the UPPER bit
	B4G4R4A4_UNORM_PACK16,
	R4G4B4A4_UNORM_PACK16,
	R5G6B5_UNORM_PACK16,
	B5G6R5_UNORM_PACK16,
	R5G5B5A1_UNORM_PACK16, // A1 in the LOWER bit
	B5G5R5A1_UNORM_PACK16, // A1 in the LOWER bit
	A1R5G5B5_UNORM_PACK16, // A1 in the UPPER bit.
	A1B5G5R5_UNORM_PACK16, // A1 in the UPPER bit. OpenGL-only.

	R16_UNORM,

	R16_FLOAT,
	R16G16_FLOAT,
	R16G16B16A16_FLOAT,

	R32_FLOAT,
	R32G32_FLOAT,
	R32G32B32_FLOAT,
	R32G32B32A32_FLOAT,

	// Block compression formats.
	// These are modern names for DXT and friends, now patent free.
	// https://msdn.microsoft.com/en-us/library/bb694531.aspx
	BC1_RGBA_UNORM_BLOCK,  // 64 bits per 4x4 block. Used by Basis, along with ETC2_R8G8B8_UNORM_BLOCK.
	BC2_UNORM_BLOCK,  // 4-bit straight alpha + DXT1 color. 128 bits per block. Usually not worth using
	BC3_UNORM_BLOCK,  // 3-bit alpha with 2 ref values (+ magic) + DXT1 color. 128 bits per block.
	BC4_UNORM_BLOCK,  // 1-channel, same storage as BC3 alpha. 64 bits per block.
	BC5_UNORM_BLOCK,  // 2-channel RG, each has same storage as BC3 alpha. 128 bits per block.
	BC7_UNORM_BLOCK,  // Highly advanced RGBA, very expensive to compress, very good quality. 128 bits per block.

	// Ericsson texture compression.
	ETC2_R8G8B8_UNORM_BLOCK,  // Color-only, 64 bits per 4x4 block.
	ETC2_R8G8B8A1_UNORM_BLOCK,  // Color + alpha, 128 bits per 4x4 block.
	ETC2_R8G8B8A8_UNORM_BLOCK,  // Color + alpha, 128 bits per 4x4 block.

	// This is the one ASTC format used by UASTC / basis Universal.
	ASTC_4x4_UNORM_BLOCK,

	S8,
	D16,
	D16_S8,
	D24_S8,
	D32F,
	D32F_S8,
};

size_t DataFormatSizeInBytes(DataFormat fmt);
bool DataFormatIsDepthStencil(DataFormat fmt);
inline bool DataFormatIsColor(DataFormat fmt) {
	return !DataFormatIsDepthStencil(fmt);
}
bool DataFormatIsBlockCompressed(DataFormat fmt, int *blockSize);

// Limited format support for now.
const char *DataFormatToString(DataFormat fmt);

void ConvertFromRGBA8888(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format);
void ConvertFromBGRA8888(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format);
void ConvertToD32F(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format);
void ConvertToD16(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format);

}  // namespace
