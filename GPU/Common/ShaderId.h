#pragma once

#include <string>
#include <cstring>
#include <cstdint>
#include "base/basictypes.h"

// TODO: There will be additional bits, indicating that groups of these will be
// sent to the shader and processed there. This will cut down the number of shaders ("ubershader approach")
// This is probably only really worth doing for lighting and bones.
enum {
	VS_BIT_LMODE = 0,
	VS_BIT_IS_THROUGH = 1,
	VS_BIT_ENABLE_FOG = 2,
	VS_BIT_HAS_COLOR = 3,
	VS_BIT_DO_TEXTURE = 4,
	VS_BIT_DO_TEXTURE_TRANSFORM = 6,
	VS_BIT_USE_HW_TRANSFORM = 8,
	VS_BIT_HAS_NORMAL = 9,  // conditioned on hw transform
	VS_BIT_NORM_REVERSE = 10,
	VS_BIT_HAS_TEXCOORD = 11,
	VS_BIT_HAS_COLOR_TESS = 12,  // 1 bit
	VS_BIT_HAS_TEXCOORD_TESS = 13,  // 1 bit
	VS_BIT_NORM_REVERSE_TESS = 14, // 1 bit 1 free after
	VS_BIT_UVGEN_MODE = 16,
	VS_BIT_UVPROJ_MODE = 18,  // 2, can overlap with LS0
	VS_BIT_LS0 = 18,  // 2
	VS_BIT_LS1 = 20,  // 2
	VS_BIT_BONES = 22,  // 3 should be enough, not 8
	VS_BIT_ENABLE_BONES = 30,
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
	VS_BIT_WEIGHT_FMTSCALE = 57,  // only two bits, 1 free after
	VS_BIT_FLATSHADE = 62, // 1 bit
	VS_BIT_BEZIER = 63, // 1 bit
	// No more free
};


// Local
enum {
	FS_BIT_CLEARMODE = 0,
	FS_BIT_DO_TEXTURE = 1,
	FS_BIT_TEXFUNC = 2,  // 3 bits
	FS_BIT_TEXALPHA = 5,
	FS_BIT_SHADER_TEX_CLAMP = 7,
	FS_BIT_CLAMP_S = 8,
	FS_BIT_CLAMP_T = 9,
	FS_BIT_TEXTURE_AT_OFFSET = 10,
	FS_BIT_LMODE = 11,
	FS_BIT_ALPHA_TEST = 12,
	FS_BIT_ALPHA_TEST_FUNC = 13,  // 3 bits
	FS_BIT_ALPHA_AGAINST_ZERO = 16,
	FS_BIT_COLOR_TEST = 17,
	FS_BIT_COLOR_TEST_FUNC = 18,  // 2 bits
	FS_BIT_COLOR_AGAINST_ZERO = 20,
	FS_BIT_ENABLE_FOG = 21,
	FS_BIT_DO_TEXTURE_PROJ = 22,
	FS_BIT_COLOR_DOUBLE = 23,
	FS_BIT_STENCIL_TO_ALPHA = 24,  // 2 bits
	FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE = 26,  // 4 bits
	FS_BIT_REPLACE_LOGIC_OP_TYPE = 30,  // 2 bits
	FS_BIT_REPLACE_BLEND = 32,  // 3 bits
	FS_BIT_BLENDEQ = 35,  // 3 bits
	FS_BIT_BLENDFUNC_A = 38,  // 4 bits
	FS_BIT_BLENDFUNC_B = 42,
	FS_BIT_FLATSHADE = 46,
	FS_BIT_BGRA_TEXTURE = 47,
};

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

	bool Bit(int bit) const {
		return (d[bit >> 5] >> (bit & 31)) & 1;
	}
	// Does not handle crossing 32-bit boundaries
	int Bits(int bit, int count) const {
		const int mask = (1 << count) - 1;
		return (d[bit >> 5] >> (bit & 31)) & mask;
	}
	void SetBit(int bit, bool value = true) {
		if (value) {
			d[bit >> 5] |= 1 << (bit & 31);
		}
	}
	void SetBits(int bit, int count, int value) {
		if (value != 0) {
			const int mask = (1 << count) - 1;
			d[bit >> 5] |= (value & mask) << (bit & 31);
		}
	}

	void ToString(std::string *dest) const {
		dest->resize(sizeof(d));
		memcpy(&(*dest)[0], d, sizeof(d));
	}
	void FromString(std::string src) {
		memcpy(d, &(src)[0], sizeof(d));
	}
};


bool CanUseHardwareTransform(int prim);
void ComputeVertexShaderID(ShaderID *id, uint32_t vertexType, bool useHWTransform);
// Generates a compact string that describes the shader. Useful in a list to get an overview
// of the current flora of shaders.
std::string VertexShaderDesc(const ShaderID &id);

void ComputeFragmentShaderID(ShaderID *id);
std::string FragmentShaderDesc(const ShaderID &id);
