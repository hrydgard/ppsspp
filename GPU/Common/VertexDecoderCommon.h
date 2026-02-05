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

#include <cstring>

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"
#include "Common/Data/Collections/Hashmaps.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/GPUState.h"

#if PPSSPP_ARCH(ARM)
#include "Common/ArmEmitter.h"
#elif PPSSPP_ARCH(ARM64)
#include "Common/Arm64Emitter.h"
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include "Common/x64Emitter.h"
#elif PPSSPP_ARCH(RISCV64)
#include "Common/RiscVEmitter.h"
#elif PPSSPP_ARCH(LOONGARCH64)
#include "Common/LoongArch64Emitter.h"
#else
#include "Common/FakeEmitter.h"
#endif

// DecVtxFormat - vertex formats for PC
// Kind of like a D3D VertexDeclaration.
// Can write code to easily bind these using OpenGL, or read these manually.
// No morph support, that is taken care of by the VertexDecoder.

// Keep this in 4 bits.
enum {
	DEC_NONE,
	DEC_FLOAT_1,
	DEC_FLOAT_2,
	DEC_FLOAT_3,
	DEC_FLOAT_4,
	DEC_S8_3,
	DEC_S16_3,
	DEC_U8_1,
	DEC_U8_2,
	DEC_U8_3,
	DEC_U8_4,
	DEC_U16_1,
	DEC_U16_2,
	DEC_U16_3,
	DEC_U16_4,
};

struct DecVtxFormat {
	u8 w0fmt; u8 w0off;  // first 4 weights
	u8 w1fmt; u8 w1off;  // second 4 weights
	u8 uvfmt; u8 uvoff;
	u8 c0fmt; u8 c0off;  // First color
	u8 c1fmt; u8 c1off;
	u8 nrmfmt; u8 nrmoff;
	u8 posoff;  // Output position format is always DEC_FLOAT_3.
	u8 stride;

	uint32_t id;
	void ComputeID();
	void InitializeFromID(uint32_t id);

	static u8 PosFmt() { return DEC_FLOAT_3; }
};

void GetIndexBounds(const void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound);

inline constexpr int RoundUp4(int x) {
	return (x + 3) & ~3;
}

class IndexConverter {
private:
	union {
		const void *indices;
		const u8 *indices8;
		const u16_le *indices16;
		const u32_le *indices32;
	};
	u32 indexType;

public:
	IndexConverter(u32 vertType, const void *indices)
		: indices(indices), indexType(vertType & GE_VTYPE_IDX_MASK) {
	}

	u32 operator() (u32 index) const {
		switch (indexType) {
		case GE_VTYPE_IDX_8BIT:
			return indices8[index];
		case GE_VTYPE_IDX_16BIT:
			return indices16[index];
		case GE_VTYPE_IDX_32BIT:
			return indices32[index];
		default:
			return index;
		}
	}
};

// Reads decoded vertex formats in a convenient way. For software transform and debugging.
class VertexReader {
public:
	VertexReader(const u8 *base, const DecVtxFormat &decFmt, int vtype) : base_(base), data_(base), decFmt_(decFmt), vtype_(vtype) {}

	void ReadPosAuto(float pos[3]) const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		pos[0] = f[0];
		pos[1] = f[1];
		if (!isThrough()) {
			pos[2] = f[2];
		} else {
			// Integer value passed in a float. Clamped to 0, 65535.
			pos[2] = (int)f[2] * (1.0f / 65535.0f);
		}
	}

	void ReadPosThrough(float pos[3]) const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		pos[0] = f[0];
		pos[1] = f[1];
		// Integer value passed in a float. Clamped to 0, 65535.
		pos[2] = (int)f[2] * (1.0f / 65535.0f);
	}

	void ReadPosNonThrough(float pos[3]) const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		memcpy(pos, f, 12);
	}

	void ReadPosThroughZ16(float pos[3]) const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		memcpy(pos, f, 12);
	}

	void ReadNrm(float nrm[3]) const {
		switch (decFmt_.nrmfmt) {
		case DEC_FLOAT_3:
			//memcpy(nrm, data_ + decFmt_.nrmoff, 12);
			{
				const float *f = (const float *)(data_ + decFmt_.nrmoff);
				for (int i = 0; i < 3; i++)
					nrm[i] = f[i];
			}
			break;
		case DEC_S16_3:
			{
				const s16 *s = (const s16 *)(data_ + decFmt_.nrmoff);
				for (int i = 0; i < 3; i++)
					nrm[i] = s[i] * (1.f / 32767.f);
			}
			break;
		case DEC_S8_3:
			{
				const s8 *b = (const s8 *)(data_ + decFmt_.nrmoff);
				for (int i = 0; i < 3; i++)
					nrm[i] = b[i] * (1.f / 127.f);
			}
			break;
		default:
			memset(nrm, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadUV(float uv[2]) const {
		// Only DEC_FLOAT_2 is supported.
		const float *f = (const float *)(data_ + decFmt_.uvoff);
		uv[0] = f[0];
		uv[1] = f[1];
	}

	void ReadColor0(float color[4]) const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			Uint8x4ToFloat4(color, *(const u32 *)(data_ + decFmt_.c0off));
			break;
		case DEC_FLOAT_4:
			memcpy(color, data_ + decFmt_.c0off, 16);
			break;
		default:
			memset(color, 0, sizeof(float) * 4);
			break;
		}
	}

	u32 ReadColor0_8888() const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.c0off);
				u32 value;
				memcpy(&value, b, 4);
				return value;
			}
			break;
		case DEC_FLOAT_4:
			{
				const float *f = (const float *)(data_ + decFmt_.c0off);
				return Float4ToUint8x4_NoClamp(f);
			}
			break;
		default:
			return 0;
		}
	}

	void ReadColor1(float color[3]) const {
		switch (decFmt_.c1fmt) {
		case DEC_U8_4:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.c1off);
				for (int i = 0; i < 3; i++)
					color[i] = b[i] * (1.f / 255.f);
			}
			break;
		case DEC_FLOAT_4:
			memcpy(color, data_ + decFmt_.c1off, 12);
			break;
		default:
			memset(color, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadWeights(float weights[8]) const {
		const float *f = (const float *)(data_ + decFmt_.w0off);
		const u8 *b = (const u8 *)(data_ + decFmt_.w0off);
		const u16 *s = (const u16 *)(data_ + decFmt_.w0off);
		switch (decFmt_.w0fmt) {
		case DEC_FLOAT_1:
		case DEC_FLOAT_2:
		case DEC_FLOAT_3:
		case DEC_FLOAT_4:
			for (int i = 0; i <= decFmt_.w0fmt - DEC_FLOAT_1; i++)
				weights[i] = f[i];
			break;
		case DEC_U8_1: weights[0] = b[0] * (1.f / 128.f); break;
		case DEC_U8_2: for (int i = 0; i < 2; i++) weights[i] = b[i] * (1.f / 128.f); break;
		case DEC_U8_3: for (int i = 0; i < 3; i++) weights[i] = b[i] * (1.f / 128.f); break;
		case DEC_U8_4: for (int i = 0; i < 4; i++) weights[i] = b[i] * (1.f / 128.f); break;
		case DEC_U16_1: weights[0] = s[0] * (1.f / 32768.f); break;
		case DEC_U16_2: for (int i = 0; i < 2; i++) weights[i] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_3: for (int i = 0; i < 3; i++) weights[i] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_4: for (int i = 0; i < 4; i++) weights[i] = s[i] * (1.f / 32768.f); break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtw0, Log::G3D, "Reader: Unsupported W0 Format %d", decFmt_.w0fmt);
			memset(weights, 0, sizeof(float) * 8);
			break;
		}

		f = (const float *)(data_ + decFmt_.w1off);
		b = (const u8 *)(data_ + decFmt_.w1off);
		s = (const u16 *)(data_ + decFmt_.w1off);
		switch (decFmt_.w1fmt) {
		case 0:
			// It's fine for there to be w0 weights but not w1.
			break;
		case DEC_FLOAT_1:
		case DEC_FLOAT_2:
		case DEC_FLOAT_3:
		case DEC_FLOAT_4:
			for (int i = 0; i <= decFmt_.w1fmt - DEC_FLOAT_1; i++)
				weights[i+4] = f[i];
			break;
		case DEC_U8_1: weights[4] = b[0] * (1.f / 128.f); break;
		case DEC_U8_2: for (int i = 0; i < 2; i++) weights[i+4] = b[i] * (1.f / 128.f); break;
		case DEC_U8_3: for (int i = 0; i < 3; i++) weights[i+4] = b[i] * (1.f / 128.f); break;
		case DEC_U8_4: for (int i = 0; i < 4; i++) weights[i+4] = b[i] * (1.f / 128.f); break;
		case DEC_U16_1: weights[4] = s[0] * (1.f / 32768.f); break;
		case DEC_U16_2: for (int i = 0; i < 2; i++) weights[i+4] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_3: for (int i = 0; i < 3; i++) weights[i+4] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_4: for (int i = 0; i < 4; i++) weights[i+4] = s[i]  * (1.f / 32768.f); break;
		default:
			memset(weights + 4, 0, sizeof(float) * 4);
			break;
		}
	}

	bool hasColor0() const { return decFmt_.c0fmt != 0; }
	bool hasColor1() const { return decFmt_.c1fmt != 0; }
	bool hasNormal() const { return decFmt_.nrmfmt != 0; }
	bool hasUV() const { return decFmt_.uvfmt != 0; }
	bool isThrough() const { return (vtype_ & GE_VTYPE_THROUGH) != 0; }
	void Goto(int index) {
		data_ = base_ + index * decFmt_.stride;
	}

private:
	const u8 *base_;
	const u8 *data_;
	DecVtxFormat decFmt_;
	int vtype_;
};
// Debugging utilities
void PrintDecodedVertex(const VertexReader &vtx);


class VertexDecoder;
class VertexDecoderJitCache;

typedef void (*StepFunction)(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
typedef void (VertexDecoderJitCache::*JitStepFunction)();

struct JitLookup {
	StepFunction func;
	JitStepFunction jitFunc;
};

// Collapse to less skinning shaders to reduce shader switching, which is expensive.
int TranslateNumBones(int bones);

typedef void (*JittedVertexDecoder)(const u8 *src, u8 *dst, int count, const UVScale *uvScaleOffset);

struct VertexDecoderOptions {
	bool expandAllWeightsToFloat;
	bool expand8BitNormalsToFloat;
};

inline uint32_t GetVertTypeID(uint32_t vertType, int uvGenMode, bool skinInDecode) {
	// As the decoder depends on the UVGenMode when we use UV prescale, we simply mash it
	// into the top of the verttype where there are unused bits.
	return (vertType & 0xFFFFFF) | (uvGenMode << 24) | (skinInDecode << 26);
}

inline bool VertTypeIDSkinInDecode(uint32_t vertType) {
	return ((vertType >> 26) & 1) != 0;
}

inline GETexMapMode VertTypeIDUVGenMode(uint32_t vertType) {
	return (GETexMapMode)((vertType >> 24) & 3);
}

class VertexDecoder {
public:
	// A jit cache is not mandatory.
	void SetVertexType(u32 vtype, const VertexDecoderOptions &options, VertexDecoderJitCache *jitCache = nullptr);

	u32 VertexType() const { return fmt_; }

	const DecVtxFormat &GetDecVtxFmt() const { return decFmt; }

	void DecodeVerts(u8 *decoded, const u8 *startPtr, const UVScale *uvScaleOffset, int count) const;

	int VertexSize() const { return size; }  // PSP format size

	std::string GetString(DebugShaderStringType stringType) const;

	void ComputeSkinMatrix(const float weights[8]) const;

	static void Step_WeightsU8(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_WeightsU16(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_WeightsU8ToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_WeightsU16ToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_WeightsFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_WeightsU8Skin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_WeightsU16Skin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_WeightsFloatSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_TcU8ToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16ToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_TcU8Prescale(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16Prescale(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16DoublePrescale(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcFloatPrescale(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_TcU16DoubleToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16ThroughToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16ThroughDoubleToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcFloatThrough(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_TcU8MorphToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16MorphToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16DoubleMorphToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcFloatMorph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU8PrescaleMorph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16PrescaleMorph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcU16DoublePrescaleMorph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_TcFloatPrescaleMorph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_ColorInvalid(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_Color4444(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_Color565(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_Color5551(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_Color8888(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_Color4444Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_Color565Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_Color5551Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_Color8888Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_NormalS8(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalS8ToFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalS16(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_NormalS8Skin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalS16Skin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalFloatSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_NormalS8Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalS16Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalFloatMorph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_NormalS8MorphSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalS16MorphSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_NormalFloatMorphSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_PosS8(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosS16(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosFloat(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_PosS8Skin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosS16Skin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosFloatSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_PosS8Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosS16Morph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosFloatMorph(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_PosS8MorphSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosS16MorphSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosFloatMorphSkin(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	static void Step_PosInvalid(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosS8Through(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosS16Through(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);
	static void Step_PosFloatThrough(const VertexDecoder *dec, const u8 *ptr, u8 *decoded);

	// output must be big for safety.
	// Returns number of chars written.
	// Ugly for speed.
	int ToString(char *output, bool spaces) const;

	bool IsInSpace(const uint8_t *ptr) const {
		return ptr >= (const uint8_t *)jitted_ && ptr < ((const uint8_t *)jitted_ + jittedSize_);
	}

	// Mutable decoder state
	mutable const UVScale *prescaleUV_ = nullptr;
	JittedVertexDecoder jitted_ = 0;
	int32_t jittedSize_ = 0;

	// "Immutable" state, set at startup

	// The decoding steps. Never more than 5 (weight, texcoord, color, normal, pos)
	StepFunction steps_[5]{};
	int numSteps_;

	u32 fmt_;
	DecVtxFormat decFmt;

	bool throughmode;
	bool skinInDecode;
	// With morph and weights, this can be more than 256 bytes.
	u16 size;
	u8 onesize_;

	u8 weightoff;
	u8 tcoff;
	u8 coloff;
	u8 nrmoff;
	u8 posoff;

	u8 tc;
	u8 col;
	u8 nrm;
	u8 pos;
	u8 weighttype;
	u8 idx;
	u8 morphcount;
	u8 nweights;

	u8 biggest;  // in practice, alignment.

#ifdef _DEBUG
	mutable u64 decodedCount = 0;
#endif

	friend class VertexDecoderJitCache;

private:
	void CompareToJit(const u8 *startPtr, u8 *decodedptr, int count, const UVScale *uvScaleOffset) const;
};

const char *GetStepFunctionName(StepFunction func);

// A compiled vertex decoder takes the following arguments (C calling convention):
// u8 *src, u8 *dst, int count
//
// x86:
//   src is placed in esi and dst in edi
//   for every vertex, we step esi and edi forwards by the two vertex sizes
//   all movs are done relative to esi and edi
//
// that's it!

#if PPSSPP_ARCH(ARM)
#define VERTEXDECODER_JIT_BACKEND ArmGen::ARMXCodeBlock
#elif PPSSPP_ARCH(ARM64)
#define VERTEXDECODER_JIT_BACKEND Arm64Gen::ARM64CodeBlock
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#define VERTEXDECODER_JIT_BACKEND Gen::XCodeBlock
#elif PPSSPP_ARCH(RISCV64)
#define VERTEXDECODER_JIT_BACKEND RiscVGen::RiscVCodeBlock
#elif PPSSPP_ARCH(LOONGARCH64)
#define VERTEXDECODER_JIT_BACKEND LoongArch64Gen::LoongArch64CodeBlock
#endif


#ifdef VERTEXDECODER_JIT_BACKEND
class VertexDecoderJitCache : public VERTEXDECODER_JIT_BACKEND {
public:
	VertexDecoderJitCache();

	// Returns a pointer to the code to run.
	JittedVertexDecoder Compile(const VertexDecoder &dec, int32_t *jittedSize);
	void Clear();

	void Jit_WeightsU8();
	void Jit_WeightsU16();
	void Jit_WeightsU8ToFloat();
	void Jit_WeightsU16ToFloat();
	void Jit_WeightsFloat();

	void Jit_WeightsU8Skin();
	void Jit_WeightsU16Skin();
	void Jit_WeightsFloatSkin();

	void Jit_TcU8ToFloat();
	void Jit_TcU16ToFloat();
	void Jit_TcFloat();

	void Jit_TcU8Prescale();
	void Jit_TcU16Prescale();
	void Jit_TcFloatPrescale();

	void Jit_TcAnyMorph(int bits);
	void Jit_TcU8MorphToFloat();
	void Jit_TcU16MorphToFloat();
	void Jit_TcFloatMorph();
	void Jit_TcU8PrescaleMorph();
	void Jit_TcU16PrescaleMorph();
	void Jit_TcFloatPrescaleMorph();

	void Jit_TcU16ThroughToFloat();
	void Jit_TcFloatThrough();

	void Jit_Color8888();
	void Jit_Color4444();
	void Jit_Color565();
	void Jit_Color5551();

	void Jit_NormalS8();
	void Jit_NormalS8ToFloat();
	void Jit_NormalS16();
	void Jit_NormalFloat();

	void Jit_NormalS8Skin();
	void Jit_NormalS16Skin();
	void Jit_NormalFloatSkin();

	void Jit_PosS8();
	void Jit_PosS16();
	void Jit_PosFloat();
	void Jit_PosS8Through();
	void Jit_PosS16Through();
	void Jit_PosFloatThrough();

	void Jit_PosS8Skin();
	void Jit_PosS16Skin();
	void Jit_PosFloatSkin();

	void Jit_NormalS8Morph();
	void Jit_NormalS16Morph();
	void Jit_NormalFloatMorph();

	void Jit_NormalS8MorphSkin();
	void Jit_NormalS16MorphSkin();
	void Jit_NormalFloatMorphSkin();

	void Jit_PosS8Morph();
	void Jit_PosS16Morph();
	void Jit_PosFloatMorph();

	void Jit_PosS8MorphSkin();
	void Jit_PosS16MorphSkin();
	void Jit_PosFloatMorphSkin();

	void Jit_Color8888Morph();
	void Jit_Color4444Morph();
	void Jit_Color565Morph();
	void Jit_Color5551Morph();

private:
	bool CompileStep(const VertexDecoder &dec, int i);
	void Jit_ApplyWeights();
	void Jit_WriteMatrixMul(int outOff, bool pos);
	void Jit_WriteMorphColor(int outOff, bool checkAlpha = true);
	void Jit_AnyS8ToFloat(int srcoff);
	void Jit_AnyS16ToFloat(int srcoff);
	void Jit_AnyU8ToFloat(int srcoff, u32 bits = 32);
	void Jit_AnyU16ToFloat(int srcoff, u32 bits = 64);
	void Jit_AnyS8Morph(int srcoff, int dstoff);
	void Jit_AnyS16Morph(int srcoff, int dstoff);
	void Jit_AnyFloatMorph(int srcoff, int dstoff);

	const VertexDecoder *dec_ = nullptr;
#if PPSSPP_ARCH(ARM64)
	Arm64Gen::ARM64FloatEmitter fp;
#endif
};
#else
class VertexDecoderJitCache : public FakeGen::FakeXCodeBlock {
public:
	VertexDecoderJitCache();

	JittedVertexDecoder Compile(const VertexDecoder &dec, int32_t *jittedSize) {
		return nullptr;
	}
	void Clear();
};
#endif
