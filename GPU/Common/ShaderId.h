#pragma once

#include <string>
#include <cstring>
#include <cstdint>

#include "Common/CommonFuncs.h"

// VS_BIT_LIGHT_UBERSHADER indicates that some groups of these will be
// sent to the shader and processed there. This cuts down the number of shaders ("ubershader approach").
enum VShaderBit : uint8_t {
	VS_BIT_LMODE = 0,
	VS_BIT_IS_THROUGH = 1,
	// bit 2 is free.
	VS_BIT_HAS_COLOR = 3,
	// bit 4 is free.
	VS_BIT_VERTEX_RANGE_CULLING = 5,
	VS_BIT_SIMPLE_STEREO = 6,
	// 7 is free.
	VS_BIT_USE_HW_TRANSFORM = 8,
	VS_BIT_HAS_NORMAL = 9,  // conditioned on hw transform
	VS_BIT_NORM_REVERSE = 10,
	VS_BIT_HAS_TEXCOORD = 11,
	VS_BIT_HAS_COLOR_TESS = 12,  // 1 bit
	VS_BIT_HAS_TEXCOORD_TESS = 13,  // 1 bit
	VS_BIT_NORM_REVERSE_TESS = 14, // 1 bit
	VS_BIT_HAS_NORMAL_TESS = 15, // 1 bit
	VS_BIT_UVGEN_MODE = 16,
	VS_BIT_UVPROJ_MODE = 18,  // 2, can overlap with LS0
	VS_BIT_LS0 = 18,  // 2
	VS_BIT_LS1 = 20,  // 2
	VS_BIT_BONES = 22,  // 3 should be enough, not 8
	// 25 - 29 are free.
	VS_BIT_ENABLE_BONES = 30,

	// If this is set along with LIGHTING_ENABLE, all other lighting bits below
	// are passed to the shader directly instead.
	VS_BIT_LIGHT_UBERSHADER = 31,

	VS_BIT_LIGHT0_COMP = 32,  // 2 bits
	VS_BIT_LIGHT0_TYPE = 34,  // 2 bits
	VS_BIT_LIGHT1_COMP = 36,  // 2 bits
	VS_BIT_LIGHT1_TYPE = 38,  // 2 bits
	VS_BIT_LIGHT2_COMP = 40,  // 2 bits
	VS_BIT_LIGHT2_TYPE = 42,  // 2 bits
	VS_BIT_LIGHT3_COMP = 44,  // 2 bits
	VS_BIT_LIGHT3_TYPE = 46,  // 2 bits
	VS_BIT_MATERIAL_UPDATE = 48,  // 3 bits
	VS_BIT_SPLINE = 51, // 1 bit
	VS_BIT_LIGHT0_ENABLE = 52,
	VS_BIT_LIGHT1_ENABLE = 53,
	VS_BIT_LIGHT2_ENABLE = 54,
	VS_BIT_LIGHT3_ENABLE = 55,
	VS_BIT_LIGHTING_ENABLE = 56,
	VS_BIT_WEIGHT_FMTSCALE = 57,  // only two bits
	// 59 - 61 are free.
	VS_BIT_FLATSHADE = 62, // 1 bit
	VS_BIT_BEZIER = 63, // 1 bit
	// No more free
};

static inline VShaderBit operator +(VShaderBit bit, int i) {
	return VShaderBit((int)bit + i);
}

// Local
enum FShaderBit : uint8_t {
	FS_BIT_CLEARMODE = 0,
	FS_BIT_DO_TEXTURE = 1,
	FS_BIT_TEXFUNC = 2,  // 3 bits
	FS_BIT_DOUBLE_COLOR = 5,  // Not used with FS_BIT_UBERSHADER
	FS_BIT_3D_TEXTURE = 6,
	FS_BIT_SHADER_TEX_CLAMP = 7,
	FS_BIT_CLAMP_S = 8,
	FS_BIT_CLAMP_T = 9,
	FS_BIT_TEXALPHA = 10,  // Not used with FS_BIT_UBERSHADER
	FS_BIT_LMODE = 11,
	FS_BIT_ALPHA_TEST = 12,
	FS_BIT_ALPHA_TEST_FUNC = 13,  // 3 bits
	FS_BIT_ALPHA_AGAINST_ZERO = 16,
	FS_BIT_COLOR_TEST = 17,
	FS_BIT_COLOR_TEST_FUNC = 18,  // 2 bits
	FS_BIT_COLOR_AGAINST_ZERO = 20,
	FS_BIT_ENABLE_FOG = 21,  // Not used with FS_BIT_UBERSHADER
	FS_BIT_DO_TEXTURE_PROJ = 22,
	// 1 free bit
	FS_BIT_STENCIL_TO_ALPHA = 24,  // 2 bits
	FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE = 26,  // 4 bits    (ReplaceAlphaType)
	FS_BIT_SIMULATE_LOGIC_OP_TYPE = 30,  // 2 bits
	FS_BIT_REPLACE_BLEND = 32,  // 3 bits  (ReplaceBlendType)
	FS_BIT_BLENDEQ = 35,  // 3 bits
	FS_BIT_BLENDFUNC_A = 38,  // 4 bits
	FS_BIT_BLENDFUNC_B = 42,  // 4 bits
	FS_BIT_FLATSHADE = 46,
	FS_BIT_BGRA_TEXTURE = 47,
	FS_BIT_TEST_DISCARD_TO_ZERO = 48,
	FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL = 49,
	FS_BIT_COLOR_WRITEMASK = 50,
	FS_BIT_REPLACE_LOGIC_OP = 51,  // 4 bits. GE_LOGIC_COPY means no-op/off.
	FS_BIT_SHADER_DEPAL_MODE = 55,  // 2 bits (ShaderDepalMode)
	FS_BIT_SAMPLE_ARRAY_TEXTURE = 57,  // For multiview, framebuffers are array textures and we need to sample the two layers correctly.
	FS_BIT_STEREO = 58,
	FS_BIT_USE_FRAMEBUFFER_FETCH = 59,
	FS_BIT_UBERSHADER = 60,
	FS_BIT_DEPTH_TEST_NEVER = 61,  // Only used on Mali. Set when depth == NEVER. We forcibly avoid writing to depth in this case, since it crashes the driver.
};

static inline FShaderBit operator +(FShaderBit bit, int i) {
	return FShaderBit((int)bit + i);
}

// Some of these bits are straight from FShaderBit, since they essentially enable attributes directly.
enum GShaderBit : uint8_t {
	GS_BIT_ENABLED = 0,     // If not set, we don't use a geo shader.
	GS_BIT_DO_TEXTURE = 1,  // presence of texcoords
	GS_BIT_LMODE = 2,
	GS_BIT_CURVE = 3,       // curve, which means don't do range culling.
};

static inline GShaderBit operator +(GShaderBit bit, int i) {
	return GShaderBit((int)bit + i);
}

struct ShaderID {
	ShaderID() {
		clear();
	}
	void clear() {
		for (size_t i = 0; i < ARRAY_SIZE(d); i++) {
			d[i] = 0;
		}
	}
	void set_invalid() {
		for (size_t i = 0; i < ARRAY_SIZE(d); i++) {
			d[i] = 0xFFFFFFFF;
		}
	}
	bool is_invalid() const {
		for (size_t i = 0; i < ARRAY_SIZE(d); i++) {
			if (d[i] != 0xFFFFFFFF)
				return false;
		}
		return true;
	}

	uint32_t d[2];
	bool operator < (const ShaderID &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(uint32_t); i++) {
			if (d[i] < other.d[i])
				return true;
			if (d[i] > other.d[i])
				return false;
		}
		return false;
	}
	bool operator == (const ShaderID &other) const {
		for (size_t i = 0; i < sizeof(d) / sizeof(uint32_t); i++) {
			if (d[i] != other.d[i])
				return false;
		}
		return true;
	}
	bool operator != (const ShaderID &other) const {
		return !(*this == other);
	}

	uint32_t Word(int word) const {
		return d[word];
	}

	// Note: This is a binary copy to string-as-bytes, not a human-readable representation.
	void ToString(std::string *dest) const {
		dest->resize(sizeof(d));
		memcpy(&(*dest)[0], d, sizeof(d));
	}
	// Note: This is a binary copy from string-as-bytes, not a human-readable representation.
	void FromString(std::string src) {
		memcpy(d, &(src)[0], sizeof(d));
	}

protected:
	bool Bit(int bit) const {
		return (d[bit >> 5] >> (bit & 31)) & 1;
	}
	// Does not handle crossing 32-bit boundaries. count must be 30 or smaller.
	int Bits(int bit, int count) const {
		const int mask = (1 << count) - 1;
		return (d[bit >> 5] >> (bit & 31)) & mask;
	}
	void SetBit(int bit, bool value = true) {
		if (value) {
			d[bit >> 5] |= 1 << (bit & 31);
		} else {
			d[bit >> 5] &= ~(1 << (bit & 31));
		}
	}
	void SetBits(int bit, int count, int value) {
		const int mask = (1 << count) - 1;
		const int shifted_mask = mask << (bit & 31);
		d[bit >> 5] = (d[bit >> 5] & ~shifted_mask) | ((value & mask) << (bit & 31));
	}
};

struct VShaderID : ShaderID {
	VShaderID() : ShaderID() {
	}

	explicit VShaderID(ShaderID &src) {
		memcpy(d, src.d, sizeof(d));
	}

	bool Bit(VShaderBit bit) const {
		return ShaderID::Bit((int)bit);
	}

	int Bits(VShaderBit bit, int count) const {
		return ShaderID::Bits((int)bit, count);
	}

	void SetBit(VShaderBit bit, bool value = true) {
		ShaderID::SetBit((int)bit, value);
	}

	void SetBits(VShaderBit bit, int count, int value) {
		ShaderID::SetBits((int)bit, count, value);
	}
};

struct FShaderID : ShaderID {
	FShaderID() : ShaderID() {
	}

	explicit FShaderID(ShaderID &src) {
		memcpy(d, src.d, sizeof(d));
	}

	bool Bit(FShaderBit bit) const {
		return ShaderID::Bit((int)bit);
	}

	int Bits(FShaderBit bit, int count) const {
		return ShaderID::Bits((int)bit, count);
	}

	void SetBit(FShaderBit bit, bool value = true) {
		ShaderID::SetBit((int)bit, value);
	}

	void SetBits(FShaderBit bit, int count, int value) {
		ShaderID::SetBits((int)bit, count, value);
	}
};

struct GShaderID : ShaderID {
	GShaderID() : ShaderID() {
	}

	explicit GShaderID(ShaderID &src) {
		memcpy(d, src.d, sizeof(d));
	}

	bool Bit(GShaderBit bit) const {
		return ShaderID::Bit((int)bit);
	}

	int Bits(GShaderBit bit, int count) const {
		return ShaderID::Bits((int)bit, count);
	}

	void SetBit(GShaderBit bit, bool value = true) {
		ShaderID::SetBit((int)bit, value);
	}

	void SetBits(GShaderBit bit, int count, int value) {
		ShaderID::SetBits((int)bit, count, value);
	}
};

namespace Draw {
class Bugs;
}

class VertexDecoder;

void ComputeVertexShaderID(VShaderID *id, VertexDecoder *vertexDecoder, bool useHWTransform, bool useHWTessellation, bool weightsAsFloat, bool useSkinInDecode);
// Generates a compact string that describes the shader. Useful in a list to get an overview
// of the current flora of shaders.
std::string VertexShaderDesc(const VShaderID &id);

struct ComputedPipelineState;
void ComputeFragmentShaderID(FShaderID *id, const ComputedPipelineState &pipelineState, const Draw::Bugs &bugs);
std::string FragmentShaderDesc(const FShaderID &id);

void ComputeGeometryShaderID(GShaderID *id, const Draw::Bugs &bugs, int prim);
std::string GeometryShaderDesc(const GShaderID &id);

// For sanity checking.
bool FragmentIdNeedsFramebufferRead(const FShaderID &id);
