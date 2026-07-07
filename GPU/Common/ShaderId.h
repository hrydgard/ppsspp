#pragma once

#include <string>
#include <cstring>
#include <cstdint>

#include "Common/CommonFuncs.h"
#include "GPU/GPUState.h"

enum class ClipInfoFlags;

// Shared ID checks for when the vertex and fragment shaders (and host code) need to coordinate.

// NOTE: Both of these assume non-through-mode. Don't check these if in through mode.
inline bool needFragmentMinMaxClipping() {
	return gstate.getDepthRangeMin() != 0 && gstate.getDepthRangeMax() != 0xFFFF;
}

inline bool needFragmentDepthClamp() {
	// If gstate.isDepthClipEnabled is false, clamping does not happen, instead fragments are culled as normal.
	return (gstate.getDepthRangeMin() == 0 || gstate.getDepthRangeMax() == 0xFFFF) && gstate.isDepthClipEnabled();
}

// VS_BIT_LIGHT_UBERSHADER indicates that some groups of these will be
// sent to the shader and processed there. This cuts down the number of shaders ("ubershader approach").
enum VShaderBit : uint8_t {
	VS_BIT_IS_THROUGH = 0,
	VS_BIT_USE_HW_TRANSFORM = 1,
	VS_BIT_HAS_NORMAL = 2,  // conditioned on hw transform
	VS_BIT_HAS_TEXCOORD = 3,
	VS_BIT_HAS_COLOR = 4,
	VS_BIT_LMODE = 5,
	VS_BIT_NORM_REVERSE = 6,
	VS_BIT_FLATSHADE = 7,
	VS_BIT_MATERIAL_UPDATE = 8,  // 3 bits
	// Free bit: 11
	VS_BIT_UVGEN_MODE = 12,  // 2 bits
	VS_BIT_UVPROJ_MODE = 14,  // 2 bits
	VS_BIT_ENABLE_BONES = 16,
	VS_BIT_WEIGHT_FMTSCALE = 17,  // only two bits
	VS_BIT_BONES = 19,  // 3 should be enough to represent 1-8 bones.
	VS_BIT_FS_MINMAX_DISCARD = 22, // Do min/max and/or depth clamp in the fragment shader. It just means we need to forward Z and W to the fragment shader.
	VS_BIT_FS_DEPTH_CLAMP = 23, // Do depth clamp in the fragment shader.
	VS_BIT_LIGHTING_ENABLE = 24,
	VS_BIT_LS0 = 25,  // 2 bits
	VS_BIT_LS1 = 27,  // 2 bits

	// If this is set along with LIGHTING_ENABLE, all other lighting bits below
	// are passed to the shader directly instead.
	VS_BIT_LIGHT_UBERSHADER = 29,

	VS_BIT_LIGHT0_COMP = 30,  // 2 bits
	VS_BIT_LIGHT0_TYPE = 32,  // 2 bits
	VS_BIT_LIGHT1_COMP = 34,  // 2 bits
	VS_BIT_LIGHT1_TYPE = 36,  // 2 bits
	VS_BIT_LIGHT2_COMP = 38,  // 2 bits
	VS_BIT_LIGHT2_TYPE = 40,  // 2 bits
	VS_BIT_LIGHT3_COMP = 42,  // 2 bits
	VS_BIT_LIGHT3_TYPE = 44,  // 2 bits
	VS_BIT_LIGHT0_ENABLE = 46,
	VS_BIT_LIGHT1_ENABLE = 47,
	VS_BIT_LIGHT2_ENABLE = 48,
	VS_BIT_LIGHT3_ENABLE = 49,

	VS_BIT_VERTEX_RANGE_CULLING = 50,
	VS_BIT_SIMPLE_STEREO = 51,
	// bits 52-63 are free.
};

static inline VShaderBit operator +(VShaderBit bit, int i) {
	return VShaderBit((int)bit + i);
}

// TODO: See what we can free up. We're out of bits!
enum FShaderBit : uint8_t {
	FS_BIT_CLEARMODE = 0,
	FS_BIT_DO_TEXTURE = 1,
	FS_BIT_TEXFUNC = 2,  // 3 bits
	FS_BIT_SHADER_TEX_CLAMP = 5,
	FS_BIT_CLAMP_S = 6,
	FS_BIT_CLAMP_T = 7,
	FS_BIT_SHADER_DEPAL_FORMAT = 8,  // 3 bits (GEBufferFormat), connected to FS_BIT_SHADER_DEPAL_MODE
	FS_BIT_ALPHA_TEST = 12,
	FS_BIT_ALPHA_TEST_FUNC = 13,  // 3 bits
	FS_BIT_ALPHA_AGAINST_ZERO = 16,
	FS_BIT_COLOR_TEST = 17,
	FS_BIT_COLOR_TEST_FUNC = 18,  // 2 bits
	FS_BIT_COLOR_AGAINST_ZERO = 20,
	FS_BIT_ENABLE_FOG = 21,
	FS_BIT_DO_TEXTURE_PROJ = 22,
	FS_BIT_MINMAX_DISCARD = 23,
	FS_BIT_STENCIL_TO_ALPHA = 24,  // 2 bits
	FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE = 26,  // 4 bits    (ReplaceAlphaType)
	FS_BIT_SIMULATE_LOGIC_OP_TYPE = 30,  // 2 bits
	FS_BIT_REPLACE_BLEND = 32,  // 3 bits  (ReplaceBlendType)
	FS_BIT_BLENDEQ = 35,  // 3 bits
	FS_BIT_BLENDFUNC_A = 38,  // 4 bits
	FS_BIT_BLENDFUNC_B = 42,  // 4 bits
	FS_BIT_FLATSHADE = 46,
	// Free bit 47
	FS_BIT_TEST_DISCARD_TO_ZERO = 48,
	FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL = 49,
	FS_BIT_COLOR_WRITEMASK = 50,
	FS_BIT_REPLACE_LOGIC_OP = 51,  // 4 bits. GE_LOGIC_COPY means no-op/off.
	FS_BIT_SHADER_DEPAL_MODE = 55,  // 2 bits (ShaderDepalMode)
	FS_BIT_SAMPLE_ARRAY_TEXTURE = 57,  // For multiview, framebuffers are array textures and we need to sample the two layers correctly.
	FS_BIT_STEREO = 58,
	FS_BIT_USE_FRAMEBUFFER_FETCH = 59,
	FS_BIT_LMODE = 60,
	FS_BIT_DEPTH_TEST_NEVER = 61,  // Only used on Mali. Set when depth == NEVER. We forcibly avoid writing to depth in this case, since it crashes the driver.
	FS_BIT_DEPTH_CLAMP = 62,  // These both are connected to VS_BIT_MINMAX_DISCARD_OR_DEPTH_CLAMP in the vertex shader.
	FS_BIT_3D_TEXTURE = 63,
};

static inline FShaderBit operator +(FShaderBit bit, int i) {
	return FShaderBit((int)bit + i);
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

namespace Draw {
class Bugs;
}

void ComputeVertexShaderID(VShaderID *id, u32 vertType, bool useHWTransform, bool weightsAsFloat, bool useSkinInDecode, ClipInfoFlags clipInfoFlags);
// Generates a compact string that describes the shader. Useful in a list to get an overview
// of the current flora of shaders.
std::string VertexShaderDesc(const VShaderID &id);

struct ComputedPipelineState;
void ComputeFragmentShaderID(FShaderID *id, const ComputedPipelineState &pipelineState, const Draw::Bugs &bugs, ClipInfoFlags clipInfoFlags);
std::string FragmentShaderDesc(const FShaderID &id);

// For sanity checking.
bool FragmentIdNeedsFramebufferRead(const FShaderID &id);
