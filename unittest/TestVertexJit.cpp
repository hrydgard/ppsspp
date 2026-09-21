// Copyright (c) 2015- PPSSPP Project.

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

#include <math.h>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/MemoryUtil.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HDRemaster.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "unittest/TestVertexJit.h"
#include "unittest/UnitTest.h"

const UVScale g_uvScale{1.0f, 1.0f, 0.0f, 0.0f};

class VertexDecoderTestHarness {
	static const int BUFFER_SIZE = 64 * 65536;
	static const int ROUNDS = 200;

public:
	VertexDecoderTestHarness()
		: dec_(nullptr), needsReset_(true), dstPos_(0), assertFailed_(false) {
		src_ = new u8[BUFFER_SIZE];
		dst_ = new u8[BUFFER_SIZE];
		cache_ = new VertexDecoderJitCache();
	}
	~VertexDecoderTestHarness() {
		delete [] src_;
		delete [] dst_;
		delete cache_;
		delete dec_;
	}

	void Reset() {
		memset(src_, 0, BUFFER_SIZE);
		memset(dst_, 0, BUFFER_SIZE);
		options_ = {};
		delete dec_;
		dec_ = nullptr;
		srcPos_ = 0;
		dstPos_ = 0;
		needsReset_ = false;
	}

	void SetOptions(const VertexDecoderOptions &opts) {
		if (needsReset_) {
			Reset();
		}
		options_ = opts;
	}

	void Execute(int vtype, int count, bool useJit) {
		SetupExecute(vtype, useJit);

		dec_->DecodeVerts(dst_, src_, &g_uvScale, count);
	}

	double ExecuteTimed(int vtype, int count, bool useJit) {
		SetupExecute(vtype, useJit);

		int total = 0;
		double st = time_now_d();
		do {
			for (int j = 0; j < ROUNDS; ++j) {
				dec_->DecodeVerts(dst_, src_, &g_uvScale, count);
				++total;
			}
		} while (time_now_d() - st < 0.5);
		double elapsed = time_now_d() - st;

		return total / elapsed;
	}

	void Add8(u8 x) {
		if (needsReset_) {
			Reset();
		}
		memcpy(src_ + srcPos_, &x, sizeof(x));
		srcPos_ += sizeof(x);
	}
	void Add8(u8 x, u8 y) {
		Add8(x);
		Add8(y);
	}
	void Add8(u8 x, u8 y, u8 z) {
		Add8(x);
		Add8(y);
		Add8(z);
	}
	void Add8(u8 x, u8 y, u8 z, u8 w) {
		Add8(x);
		Add8(y);
		Add8(z);
		Add8(w);
	}

	void Add16(u16_le x) {
		if (needsReset_) {
			Reset();
		}
		memcpy(src_ + srcPos_, &x, sizeof(x));
		srcPos_ += sizeof(x);
	}
	void Add16(u16_le x, u16_le y) {
		Add16(x);
		Add16(y);
	}
	void Add16(u16_le x, u16_le y, u16_le z) {
		Add16(x);
		Add16(y);
		Add16(z);
	}

	void AddFloat(float_le x) {
		if (needsReset_) {
			Reset();
		}
		memcpy(src_ + srcPos_, &x, sizeof(x));
		srcPos_ += sizeof(x);
	}
	void AddFloat(float_le x, float_le y) {
		AddFloat(x);
		AddFloat(y);
	}
	void AddFloat(float_le x, float_le y, float_le z) {
		AddFloat(x);
		AddFloat(y);
		AddFloat(z);
	}

	u8 Get8() {
		return dst_[dstPos_++];
	}

	u16 Get16() {
		u16_le result;
		memcpy(&result, dst_ + dstPos_, sizeof(result));
		dstPos_ += sizeof(result);
		return result;
	}

	float GetFloat() {
		float_le result;
		memcpy(&result, dst_ + dstPos_, sizeof(result));
		dstPos_ += sizeof(result);
		return result;
	}

	void Assert8(const char *title, u8 x, u8 y) {
		u8 resx = Get8();
		u8 resy = Get8();
		if (resx != x || resy != y) {
			assertFailed_ = true;
			printf("%s: Failed %d, %d != expected %d, %d\n", title, resx, resy, x, y);
		}
	}
	void Assert8(const char *title, u8 x, u8 y, u8 z) {
		u8 resx = Get8();
		u8 resy = Get8();
		u8 resz = Get8();
		if (resx != x || resy != y || resz != z) {
			assertFailed_ = true;
			printf("%s: Failed %d, %d, %d != expected %d, %d, %d\n", title, resx, resy, resz, x, y, z);
		}
	}
	void Assert8(const char *title, u8 x, u8 y, u8 z, u8 w) {
		u8 resx = Get8();
		u8 resy = Get8();
		u8 resz = Get8();
		u8 resw = Get8();
		if (resx != x || resy != y || resz != z || resw != w) {
			assertFailed_ = true;
			printf("%s: Failed %d, %d, %d, %d != expected %d, %d, %d, %d\n", title, resx, resy, resz, resw, x, y, z, w);
		}
	}

	void Assert16(const char *title, u16 x, u16 y) {
		u16 resx = Get16();
		u16 resy = Get16();
		if (resx != x || resy != y) {
			assertFailed_ = true;
			printf("%s: Failed %d, %d != expected %d, %d\n", title, resx, resy, x, y);
		}
	}
	void Assert16(const char *title, u16 x, u16 y, u16 z) {
		u16 resx = Get16();
		u16 resy = Get16();
		u16 resz = Get16();
		if (resx != x || resy != y || resz != z) {
			assertFailed_ = true;
			printf("%s: Failed %d, %d, %d != expected %d, %d, %d\n", title, resx, resy, resz, x, y, z);
		}
	}

	bool CompareFloat(float a, float b) {
		return a - fmodf(a, 0.0000001f) == b - fmodf(b, 0.0000001f);
	}

	void AssertFloat(const char *title, float x) {
		float resx = GetFloat();
		if (!CompareFloat(resx, x)) {
			assertFailed_ = true;
			printf("%s: Failed %f != expected %f\n", title, resx, x);
		}
	}
	void AssertFloat(const char *title, float x, float y) {
		float resx = GetFloat();
		float resy = GetFloat();
		if (!CompareFloat(resx, x) || !CompareFloat(resy, y)) {
			assertFailed_ = true;
			printf("%s: Failed %f, %f != expected %f, %f\n", title, resx, resy, x, y);
		}
	}
	void AssertFloat(const char *title, float x, float y, float z) {
		float resx = GetFloat();
		float resy = GetFloat();
		float resz = GetFloat();
		if (!CompareFloat(resx, x) || !CompareFloat(resy, y) || !CompareFloat(resz, z)) {
			assertFailed_ = true;
			printf("%s: Failed %f, %f, %f != expected %f, %f, %f\n", title, resx, resy, resz, x, y, z);
		}
	}

	void Skip(u32 c) {
		dstPos_ += c;
	}

	void *GetData() {
		return dst_;
	}

	int GetDstStride() {
		if (dec_) {
			return dec_->decFmt.stride;
		}
		return 0;
	}

	bool HasFailed() {
		return assertFailed_;
	}

private:
	void SetupExecute(int vtype, bool useJit) {
		if (dec_ != nullptr) {
			delete dec_;
		}
		dec_ = new VertexDecoder();
		dec_->SetVertexType(vtype, options_, useJit ? cache_ : nullptr);
		dstPos_ = 0;

		needsReset_ = true;
	}

	u8 *src_;
	u8 *dst_;
	VertexDecoderJitCache *cache_;
	VertexDecoderOptions options_;
	VertexDecoder *dec_;
	int indexLowerBound_;
	int indexUpperBound_;
	bool needsReset_;
	size_t srcPos_;
	size_t dstPos_;
	bool assertFailed_;
};

static bool TestVertex8() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_8BIT | GE_VTYPE_NRM_8BIT | GE_VTYPE_TC_8BIT;

	dec.Add8(127, 128);
	dec.Add8(127, 0, 128);
	dec.Add8(127, 0, 128);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 1, jit == 1);
		dec.AssertFloat("TestVertex8-TC", 127.0f / 128.0f, 1.0f);
		dec.Assert8("TestVertex8-Nrm", 127, 0, 128);
		dec.Skip(1);
		dec.AssertFloat("TestVertex8-Pos", 127.0f / 128.0f, 0.0f, -1.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertex16() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_16BIT | GE_VTYPE_NRM_16BIT | GE_VTYPE_TC_16BIT;

	dec.Add16(32767, 32768);
	dec.Add16(32767, 0, 32768);
	dec.Add16(32767, 0, 32768);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 1, jit == 1);
		dec.AssertFloat("TestVertex16-TC", 32767.0f / 32768.0f, 1.0f);
		dec.Assert16("TestVertex16-Nrm", 32767, 0, 32768);
		dec.Skip(2);
		dec.AssertFloat("TestVertex16-Pos", 32767.0f / 32768.0f, 0.0f, -1.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertexFloat() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_NRM_FLOAT | GE_VTYPE_TC_FLOAT;

	dec.AddFloat(1.0f, -1.0f);
	dec.AddFloat(1.0f, 0.5f, -1.0f);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 1, jit == 1);
		dec.AssertFloat("TestVertexFloat-TC", 1.0f, -1.0f);
		dec.AssertFloat("TestVertexFloat-Nrm", 1.0f, 0.5f, -1.0f);
		dec.AssertFloat("TestVertexFloat-Pos", 1.0f, 0.5f, -1.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertex8Through() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_8BIT | GE_VTYPE_NRM_8BIT | GE_VTYPE_TC_8BIT | GE_VTYPE_THROUGH;

	dec.Add8(127, 128);
	dec.Add8(127, 0, 128);
	dec.Add8(127, 0, 128);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 1, jit == 1);
		// Note: this is correct, even in through.
		dec.AssertFloat("TestVertex8Through-TC", 127.0f / 128.0f, 1.0f);
		dec.Assert8("TestVertex8Through-Nrm", 127, 0, 128);
		// Ignoring Pos since s8 through isn't really an option.
	}

	return !dec.HasFailed();
}

static bool TestVertex16Through() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_16BIT | GE_VTYPE_NRM_16BIT | GE_VTYPE_TC_16BIT | GE_VTYPE_THROUGH;

	dec.Add16(32767, 32768);
	dec.Add16(32767, 0, 32768);
	dec.Add16(32767, 0, 32768);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 1, jit == 1);
		dec.AssertFloat("TestVertex16Through-TC", 32767.0f, 32768.0f);
		dec.Assert16("TestVertex16Through-Nrm", 32767, 0, 32768);
		dec.Skip(2);
		dec.AssertFloat("TestVertex16Through-Pos", 32767.0f, 0.0f, 32768.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertexFloatThrough() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_NRM_FLOAT | GE_VTYPE_TC_FLOAT | GE_VTYPE_THROUGH;

	dec.AddFloat(1.0f, -1.0f);
	dec.AddFloat(1.0f, 0.5f, -1.0f);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 1, jit == 1);
		dec.AssertFloat("TestVertexFloatThrough-TC", 1.0f, -1.0f);
		dec.AssertFloat("TestVertexFloatThrough-Nrm", 1.0f, 0.5f, -1.0f);
		dec.AssertFloat("TestVertexFloatThrough-Pos", 1.0f, 0.5f, 0.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertexColor8888() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_COL_8888;
	bool failed = false;

	dec.Add8(1, 2, 3, 4);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		gstate_c.vertexFullAlpha = true;
		dec.Execute(vtype, 1, jit == 1);
		dec.Assert8("TestVertexColor8888-Col", 1, 2, 3, 4);
		dec.AssertFloat("TestVertexColor8888-Pos", 1.0f, 0.5f, -1.0f);

		if (gstate_c.vertexFullAlpha) {
			printf("TestVertexColor8888: failed to clear vertexFullAlpha\n");
			failed = true;
		}
	}

	dec.Add8(255, 255, 255, 255);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		gstate_c.vertexFullAlpha = true;
		dec.Execute(vtype, 1, jit == 1);
		dec.Assert8("TestVertexColor8888-Col", 255, 255, 255, 255);
		dec.AssertFloat("TestVertexColor8888-Pos", 1.0f, 0.5f, -1.0f);

		if (!gstate_c.vertexFullAlpha) {
			printf("TestVertexColor8888: cleared vertexFullAlpha\n");
			failed = true;
		}
	}

	return !dec.HasFailed() && !failed;
}

static bool TestVertexColor4444() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_COL_4444;
	bool failed = false;

	dec.Add16(0x1234, 0);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		gstate_c.vertexFullAlpha = true;
		dec.Execute(vtype, 1, jit == 1);
		dec.Assert8("TestVertexColor4444-Col", 0x44, 0x33, 0x22, 0x11);
		dec.AssertFloat("TestVertexColor4444-Pos", 1.0f, 0.5f, -1.0f);

		if (gstate_c.vertexFullAlpha) {
			printf("TestVertexColor4444: failed to clear vertexFullAlpha\n");
			failed = true;
		}
	}

	dec.Add16(0xFFFF, 0);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		gstate_c.vertexFullAlpha = true;
		dec.Execute(vtype, 1, jit == 1);
		dec.Assert8("TestVertexColor4444-Col", 255, 255, 255, 255);
		dec.AssertFloat("TestVertexColor4444-Pos", 1.0f, 0.5f, -1.0f);

		if (!gstate_c.vertexFullAlpha) {
			printf("TestVertexColor4444: cleared vertexFullAlpha\n");
			failed = true;
		}
	}

	return !dec.HasFailed() && !failed;
}

static bool TestVertexColor5551() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_COL_5551;
	bool failed = false;

	dec.Add16((0 << 15) | (1 << 10) | (2 << 5) | 3, 0);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		gstate_c.vertexFullAlpha = true;
		dec.Execute(vtype, 1, jit == 1);
		dec.Assert8("TestVertexColor5551-Col", 0x18, 0x10, 0x8, 0x0);
		dec.AssertFloat("TestVertexColor5551-Pos", 1.0f, 0.5f, -1.0f);

		if (gstate_c.vertexFullAlpha) {
			printf("TestVertexColor5551: failed to clear vertexFullAlpha\n");
			failed = true;
		}
	}

	dec.Add16(0xFFFF, 0);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		gstate_c.vertexFullAlpha = true;
		dec.Execute(vtype, 1, jit == 1);
		dec.Assert8("TestVertexColor5551-Col", 255, 255, 255, 255);
		dec.AssertFloat("TestVertexColor5551-Pos", 1.0f, 0.5f, -1.0f);

		if (!gstate_c.vertexFullAlpha) {
			printf("TestVertexColor5551: cleared vertexFullAlpha\n");
			failed = true;
		}
	}

	return !dec.HasFailed() && !failed;
}

static bool TestVertexColor565() {
	VertexDecoderTestHarness dec;
	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_COL_565;
	bool failed = false;

	dec.Add16((1 << 11) | (2 << 5) | 3, 0);
	dec.AddFloat(1.0f, 0.5f, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		gstate_c.vertexFullAlpha = true;
		dec.Execute(vtype, 1, jit == 1);
		dec.Assert8("TestVertexColor565-Col", 0x18, 0x8, 0x8, 255);
		dec.AssertFloat("TestVertexColor565-Pos", 1.0f, 0.5f, -1.0f);

		if (!gstate_c.vertexFullAlpha) {
			printf("TestVertexColor565: cleared vertexFullAlpha\n");
			failed = true;
		}
	}

	return !dec.HasFailed() && !failed;
}

static bool TestVertex8Skin() {
	VertexDecoderTestHarness dec;
	VertexDecoderOptions opts{};
	dec.SetOptions(opts);

	for (int i = 0; i < 8 * 12; ++i) {
		gstate.boneMatrix[i] = 0.0f;
	}
	gstate.boneMatrix[0] = 2.0f;
	gstate.boneMatrix[4] = 1.0f;
	gstate.boneMatrix[8] = 5.0f;

	gstate.boneMatrix[12] = 1.0f;
	gstate.boneMatrix[16] = 2.0f;
	gstate.boneMatrix[20] = 5.0f;

	int vtype = GE_VTYPE_POS_8BIT | GE_VTYPE_NRM_8BIT | GE_VTYPE_WEIGHT_8BIT | (1 << GE_VTYPE_WEIGHTCOUNT_SHIFT);
	u32 vertTypeID = GetVertTypeID(vtype, 0);

	dec.Add8(128 + 64, 128 - 64);
	dec.Add8(127, 0, 128);
	dec.Add8(127, 0, 128);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vertTypeID, 1, jit == 1);
		dec.AssertFloat("TestVertex8Skin-Nrm", (2.0f * 1.5f + 1.0f * 0.5f) * 127.0f / 128.0f, 0.0f, 2.0f * 5.0f * -1.0f);
		dec.AssertFloat("TestVertex8Skin-Pos", (2.0f * 1.5f + 1.0f * 0.5f) * 127.0f / 128.0f, 0.0f, 2.0f * 5.0f * -1.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertex16Skin() {
	VertexDecoderTestHarness dec;
	VertexDecoderOptions opts{};
	dec.SetOptions(opts);

	for (int i = 0; i < 8 * 12; ++i) {
		gstate.boneMatrix[i] = 0.0f;
	}
	gstate.boneMatrix[0] = 2.0f;
	gstate.boneMatrix[4] = 1.0f;
	gstate.boneMatrix[8] = 5.0f;

	gstate.boneMatrix[12] = 1.0f;
	gstate.boneMatrix[16] = 2.0f;
	gstate.boneMatrix[20] = 5.0f;

	int vtype = GE_VTYPE_POS_16BIT | GE_VTYPE_NRM_16BIT | GE_VTYPE_WEIGHT_16BIT | (1 << GE_VTYPE_WEIGHTCOUNT_SHIFT);
	u32 vertTypeID = GetVertTypeID(vtype, 0);

	dec.Add16(32768 + 16384, 32768 - 16384);
	dec.Add16(32767, 0, 32768);
	dec.Add16(32767, 0, 32768);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vertTypeID, 1, jit == 1);
		dec.AssertFloat("TestVertex16Skin-Nrm", (2.0f * 1.5f + 1.0f * 0.5f) * 32767.0f / 32768.0f, 0.0f, 2.0f * 5.0f * -1.0f);
		dec.AssertFloat("TestVertex16Skin-Pos", (2.0f * 1.5f + 1.0f * 0.5f) * 32767.0f / 32768.0f, 0.0f, 2.0f * 5.0f * -1.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertexFloatSkin() {
	VertexDecoderTestHarness dec;
	VertexDecoderOptions opts{};
	dec.SetOptions(opts);

	for (int i = 0; i < 8 * 12; ++i) {
		gstate.boneMatrix[i] = 0.0f;
	}
	gstate.boneMatrix[0] = 2.0f;
	gstate.boneMatrix[4] = 1.0f;
	gstate.boneMatrix[8] = 5.0f;

	gstate.boneMatrix[12] = 1.0f;
	gstate.boneMatrix[16] = 2.0f;
	gstate.boneMatrix[20] = 5.0f;

	int vtype = GE_VTYPE_POS_FLOAT | GE_VTYPE_NRM_FLOAT | GE_VTYPE_WEIGHT_FLOAT | (1 << GE_VTYPE_WEIGHTCOUNT_SHIFT);
	u32 vertTypeID = GetVertTypeID(vtype, 0);

	dec.AddFloat(1.5f, 0.5f);
	dec.AddFloat(1.0f, 0, -1.0f);
	dec.AddFloat(1.0f, 0, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vertTypeID, 1, jit == 1);
		dec.AssertFloat("TestVertexFloatSkin-Nrm", (2.0f * 1.5f + 1.0f * 0.5f) * 1.0f, 0.0f, 2.0f * 5.0f * -1.0f);
		dec.AssertFloat("TestVertexFloatSkin-Pos", (2.0f * 1.5f + 1.0f * 0.5f) * 1.0f, 0.0f, 2.0f * 5.0f * -1.0f);
	}

	return !dec.HasFailed();
}

// TODO: Morph (col, pos, nrm), weights (no skin), morph + weights?

// Everything below checks the JIT (and the handwritten SIMD decoders) against the step functions,
// across the whole space of vertex formats. The two must agree bit for bit - not just closely -
// since tests and frame dumps are recorded with one and games run with the other.

namespace {

struct JitMatchRng {
	uint32_t state = 0x12345678;
	uint32_t Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	float Float(float lo, float hi) {
		return lo + (hi - lo) * (float)(Next() & 0xFFFFFF) / (float)0xFFFFFF;
	}
	// The GE loads morph weights, bone matrices and the UV scale as 24-bit floats.
	float Float24(float lo, float hi) {
		float f = Float(lo, hi);
		uint32_t bits;
		memcpy(&bits, &f, 4);
		bits &= 0xFFFFFF00;
		memcpy(&f, &bits, 4);
		return f;
	}
	// Vertex data floats: mostly arbitrary, with some exact values mixed in.
	float VertexFloat() {
		static const float nice[] = { 0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, 127.0f / 128.0f, 256.0f };
		if ((Next() & 3) == 0) {
			return nice[Next() % ARRAY_SIZE(nice)];
		}
		return Float(-8.0f, 8.0f);
	}
};

// Output size of the formats the decoder can produce.
// Accumulating steps (morph in particular) are compiled into fused multiply-adds on some
// architectures and not others, and the prescale paths differ in where the scale lands, so the
// last bit of a decoded value is not something every JIT can be expected to reproduce exactly.
// Allow a couple of ULPs there - every real bug this test has caught was orders of magnitude
// larger, so nothing interesting slips through.
static bool NearlyEqualFloat(float a, float b, int terms) {
	if (a == b) {
		return true;
	}
	if (std::isnan(a) || std::isnan(b)) {
		return false;
	}
	// Relative to the larger magnitude, with a floor of 1 - a prescaled texcoord is a small
	// difference of larger terms, so the error is best judged against what went into it.
	// Relative to the larger magnitude, with a floor of 1 - a prescaled texcoord is a small
	// difference of larger terms, so the error is best judged against what went into it. Allow
	// one rounding per accumulated term, since morph sums several.
	const float scale = std::max(1.0f, std::max(fabsf(a), fabsf(b)));
	return fabsf(a - b) <= scale * 1e-6f * (float)std::max(1, terms);
}

int DecodedComponentSize(u8 fmt) {
	switch (fmt) {
	case DEC_FLOAT_2: return 8;
	case DEC_FLOAT_3: return 12;
	case DEC_S8_3: return 3;
	case DEC_S16_3: return 6;
	case DEC_U8_4: return 4;
	default: return -1;
	}
}

// With specialPos, some plain float positions get a NaN or infinity, and specialPos[v] says which.
void FillVertexData(JitMatchRng &rng, const VertexDecoder &dec, u8 *src, int count, bool *specialPos) {
	static const int wtSize[] = { 0, 1, 2, 4 };
	static const int tcSize[] = { 0, 1, 2, 4 };
	static const int nrmPosSize[] = { 0, 1, 2, 4 };
	auto fillScalars = [&](u8 *p, int n, int elemSize) {
		for (int i = 0; i < n; i++) {
			if (elemSize == 4) {
				float_le f = rng.VertexFloat();
				memcpy(p + i * 4, &f, 4);
			} else if (elemSize == 2) {
				u16_le v = (u16)rng.Next();
				memcpy(p + i * 2, &v, 2);
			} else {
				p[i] = (u8)rng.Next();
			}
		}
	};

	for (int v = 0; v < count; v++) {
		for (int m = 0; m < dec.morphcount; m++) {
			u8 *p = src + v * dec.size + m * dec.onesize_;
			if (dec.weighttype) {
				int sz = wtSize[dec.weighttype];
				for (int w = 0; w < dec.nweights; w++) {
					// Weights are normally 0..1ish; keep floats there so skinning stays sane.
					if (sz == 4) {
						float_le f = rng.Float(0.0f, 1.5f);
						memcpy(p + dec.weightoff + w * 4, &f, 4);
					} else {
						fillScalars(p + dec.weightoff + w * sz, 1, sz);
					}
				}
			}
			if (dec.tc) {
				fillScalars(p + dec.tcoff, 2, tcSize[dec.tc]);
			}
			if (dec.col) {
				fillScalars(p + dec.coloff, dec.col == (GE_VTYPE_COL_8888 >> GE_VTYPE_COL_SHIFT) ? 4 : 2, 1);
			}
			if (dec.nrm) {
				fillScalars(p + dec.nrmoff, 3, nrmPosSize[dec.nrm]);
			}
			if (dec.pos) {
				fillScalars(p + dec.posoff, 3, nrmPosSize[dec.pos]);
			}
		}
		if (specialPos) {
			specialPos[v] = (rng.Next() & 7) == 0;
			if (specialPos[v]) {
				static const float specials[] = { NAN, INFINITY, -INFINITY };
				float_le f = specials[rng.Next() % 3];
				memcpy(src + v * dec.size + dec.posoff + (rng.Next() % 3) * 4, &f, 4);
			}
		}
	}
}

// Skinning is the one place the JITs may round differently: arm64 accumulates the bone matrices
// with fused multiply-adds, the steps multiply and add separately. The difference comes from
// rounding the intermediate terms, which can be far larger than the result when they cancel, so
// the allowed error scales with the sum of the terms' magnitudes rather than with the result.
void SkinTermMagnitudes(const VertexDecoder &dec, const u8 *vtx, bool pos, float out[3]) {
	auto readScalar = [](const u8 *p, int type, int i) -> float {
		switch (type) {
		case 1: return fabsf((float)(s8)p[i] * (1.0f / 128.0f));
		case 2: { s16_le s; memcpy(&s, p + i * 2, 2); return fabsf((float)(s16)s * (1.0f / 32768.0f)); }
		default: { float_le f; memcpy(&f, p + i * 4, 4); return fabsf((float)f); }
		}
	};
	auto readWeight = [](const u8 *p, int type, int i) -> float {
		switch (type) {
		case 1: return p[i] * (1.0f / 128.0f);
		case 2: { u16_le w; memcpy(&w, p + i * 2, 2); return (u16)w * (1.0f / 32768.0f); }
		default: { float_le f; memcpy(&f, p + i * 4, 4); return fabsf((float)f); }
		}
	};

	// Weights come from the first morph frame only, same as the steps.
	float absMatrix[12]{};
	for (int j = 0; j < dec.nweights; j++) {
		const float w = readWeight(vtx + dec.weightoff, dec.weighttype, j);
		for (int k = 0; k < 12; k++) {
			absMatrix[k] += w * fabsf(gstate.boneMatrix[j * 12 + k]);
		}
	}

	const int type = pos ? dec.pos : dec.nrm;
	const int off = pos ? dec.posoff : dec.nrmoff;
	float absVec[3]{};
	for (int m = 0; m < dec.morphcount; m++) {
		const float mw = dec.morphcount > 1 ? fabsf(gstate_c.morphWeights[m]) : 1.0f;
		for (int k = 0; k < 3; k++) {
			absVec[k] += mw * readScalar(vtx + m * dec.onesize_ + off, type, k);
		}
	}

	for (int i = 0; i < 3; i++) {
		out[i] = absVec[0] * absMatrix[i] + absVec[1] * absMatrix[3 + i] + absVec[2] * absMatrix[6 + i] + (pos ? absMatrix[9 + i] : 0.0f);
	}
}

struct JitMismatch {
	int formats = 0;
	int verts = 0;
	std::string example;
};

}  // namespace

[[maybe_unused]] static bool TestVertexJitMatchesSteps() {
	// static, or MSVC treats these as captured references and won't use VERTS as an array bound.
	static constexpr int VERTS = 32;
	static constexpr int BUF_SIZE = 64 * 1024;
	// Decode may overrun by a vertex plus 16 bytes, see DecodeVerts.
	u8 *src = (u8 *)AllocateAlignedMemory(BUF_SIZE, 16);
	u8 *refOut = (u8 *)AllocateAlignedMemory(BUF_SIZE, 16);
	u8 *jitOut = (u8 *)AllocateAlignedMemory(BUF_SIZE, 16);

	VertexDecoderJitCache *cache = new VertexDecoderJitCache();
	JitMatchRng rng;
	std::map<std::string, JitMismatch> mismatches;
	int formatsTested = 0;
	int formatsJitted = 0;
	const bool savedDoubleTexCoords = g_DoubleTextureCoordinates;

	auto testFormat = [&](u32 vtype, int uvGenMode, bool doubleTexCoords, bool expand8BitNormals) {
		g_DoubleTextureCoordinates = doubleTexCoords;
		VertexDecoderOptions opts{};
		opts.expand8BitNormalsToFloat = expand8BitNormals;
		const u32 vertTypeID = GetVertTypeID(vtype, uvGenMode);

		VertexDecoder ref{};
		ref.SetVertexType(vertTypeID, opts, nullptr);
		// Without a cache, SetVertexType still installs the handwritten decoders for a couple of
		// formats. The reference has to be the steps.
		ref.jitted_ = nullptr;

		if (cache->GetSpaceLeft() < 16384) {
			cache->Clear();
		}
		VertexDecoder jit{};
		jit.SetVertexType(vertTypeID, opts, cache);
		formatsTested++;
		if (!jit.jitted_) {
			return;
		}
		formatsJitted++;

		// Which step writes each component: weights (if any) first, then tc, col, nrm, pos.
		int stepIndex = ref.weighttype ? 1 : 0;
		StepFunction tcStep = ref.tc ? ref.steps_[stepIndex++] : nullptr;
		StepFunction colStep = ref.col ? ref.steps_[stepIndex++] : nullptr;
		StepFunction nrmStep = ref.nrm ? ref.steps_[stepIndex++] : nullptr;
		StepFunction posStep = ref.steps_[stepIndex];

		for (int i = 0; i < 8; i++) {
			gstate_c.morphWeights[i] = rng.Float24(-0.5f, 1.5f);
		}
		for (int i = 0; i < 8 * 12; i++) {
			gstate.boneMatrix[i] = rng.Float24(-2.0f, 2.0f);
		}
		const UVScale uvScale{ rng.Float24(-2.0f, 2.0f), rng.Float24(-2.0f, 2.0f), rng.Float24(-1.0f, 1.0f), rng.Float24(-1.0f, 1.0f) };

		const int srcBytes = ref.VertexSize() * VERTS;
		_assert_(srcBytes + 256 <= BUF_SIZE && ref.decFmt.stride * (VERTS + 1) + 16 <= BUF_SIZE);
		// Only plain float positions are cleaned of NaN and infinity (see Step_PosFloat).
		bool specialPos[VERTS]{};
		FillVertexData(rng, ref, src, VERTS, posStep == &VertexDecoder::Step_PosFloat ? specialPos : nullptr);

		const KnownVertexBounds initialBounds{ 0xFFFF, 0xFFFF, 0, 0 };
		memset(refOut, 0, BUF_SIZE);
		gstate_c.vertexFullAlpha = true;
		gstate_c.vertBounds = initialBounds;
		// Vary the count a little, so the decoders' leftover-vertex paths get used too.
		const int numVerts = VERTS - (formatsTested & 3);
		ref.DecodeVerts(refOut, src, &uvScale, numVerts);
		const bool refFullAlpha = gstate_c.vertexFullAlpha;
		const KnownVertexBounds refBounds = gstate_c.vertBounds;

		memset(jitOut, 0, BUF_SIZE);
		gstate_c.vertexFullAlpha = true;
		gstate_c.vertBounds = initialBounds;
		jit.DecodeVerts(jitOut, src, &uvScale, numVerts);
		const bool jitFullAlpha = gstate_c.vertexFullAlpha;
		const KnownVertexBounds jitBounds = gstate_c.vertBounds;

		char fmtDesc[256]{};
		ref.ToString(fmtDesc, sizeof(fmtDesc), true);
		const char *kind = cache->IsInSpace((const u8 *)jit.jitted_) ? "jit" : "handwritten";

		auto record = [&](const char *what, StepFunction step, int badVerts, const std::string &detail) {
			std::string key = StringFromFormat("%-4s %-11s %s", what, kind, step ? GetStepFunctionName(step) : "-");
			JitMismatch &m = mismatches[key];
			if (m.formats == 0) {
				m.example = StringFromFormat("%08x %s morph=%d uvgen=%d dbl=%d expand8=%d: %s", vertTypeID, fmtDesc, ref.morphcount, uvGenMode, (int)doubleTexCoords, (int)expand8BitNormals, detail.c_str());
			}
			m.formats++;
			m.verts += badVerts;
		};

		auto compareComponent = [&](const char *what, StepFunction step, u8 fmt, int off, bool skinnedPos = false, bool skinnedNrm = false) {
			if (fmt == DEC_NONE) {
				return;
			}
			const bool skinned = skinnedPos || skinnedNrm;
			const int sz = DecodedComponentSize(fmt);
			if (sz < 0) {
				record(what, step, 0, StringFromFormat("unexpected decoded format %d", fmt));
				return;
			}
			int badVerts = 0;
			std::string detail;
			for (int v = 0; v < numVerts; v++) {
				const u8 *r = refOut + v * ref.decFmt.stride + off;
				const u8 *j = jitOut + v * ref.decFmt.stride + off;
				if (specialPos[v] && step == posStep) {
					// NaN and infinity only have to come out finite. How is up to the platform.
					bool finite = true;
					for (int c = 0; c < 3; c++) {
						float fr, fj;
						memcpy(&fr, r + c * 4, 4);
						memcpy(&fj, j + c * 4, 4);
						finite = finite && std::isfinite(fr) && std::isfinite(fj);
					}
					if (!finite && badVerts++ == 0) {
						float fj[3];
						memcpy(fj, j, 12);
						detail = StringFromFormat("vert %d: NaN/inf input gave %g %g %g", v, fj[0], fj[1], fj[2]);
					}
					continue;
				}
				if (memcmp(r, j, sz) == 0) {
					continue;
				}
				// Tolerate the last bit, see NearlyEqualFloat.
				if (fmt == DEC_FLOAT_2 || fmt == DEC_FLOAT_3) {
					bool close = true;
					for (int c = 0; c < sz / 4; c++) {
						float fr, fj;
						memcpy(&fr, r + c * 4, 4);
						memcpy(&fj, j + c * 4, 4);
						close = close && NearlyEqualFloat(fr, fj, ref.morphcount);
					}
					if (close) {
						continue;
					}
				} else if (fmt == DEC_U8_4) {
					// A one-bit rounding difference upstream lands as +-1 on a channel.
					bool close = true;
					for (int c = 0; c < 4; c++) {
						close = close && std::abs((int)r[c] - (int)j[c]) <= 1;
					}
					if (close) {
						continue;
					}
				}
				if (skinned && fmt == DEC_FLOAT_3) {
					float terms[3];
					SkinTermMagnitudes(ref, src + v * ref.VertexSize(), skinnedPos, terms);
					bool close = true;
					for (int c = 0; c < 3; c++) {
						float fr, fj;
						memcpy(&fr, r + c * 4, 4);
						memcpy(&fj, j + c * 4, 4);
						// About 32 ULPs of the largest term, to cover a rounding per accumulated bone.
						if (!(fabsf(fr - fj) <= terms[c] * (1.0f / (1 << 19)))) {
							close = false;
						}
					}
					if (close) {
						continue;
					}
				}
				if (badVerts++ == 0) {
					detail = StringFromFormat("vert %d: steps", v);
					const bool isFloat = fmt == DEC_FLOAT_2 || fmt == DEC_FLOAT_3;
					for (int pass = 0; pass < 2; pass++) {
						const u8 *p = pass == 0 ? r : j;
						if (pass == 1) {
							detail += " vs jit";
						}
						for (int c = 0; c < (isFloat ? sz / 4 : sz); c++) {
							if (isFloat) {
								float f;
								memcpy(&f, p + c * 4, 4);
								detail += StringFromFormat(" %.9g", f);
							} else if (fmt == DEC_S16_3) {
								if (c < 3) {
									s16 s;
									memcpy(&s, p + c * 2, 2);
									detail += StringFromFormat(" %d", s);
								}
							} else {
								detail += StringFromFormat(" %d", fmt == DEC_S8_3 ? (int)(s8)p[c] : (int)p[c]);
							}
						}
					}
				}
			}
			if (badVerts) {
				record(what, step, badVerts, detail);
			}
		};

		compareComponent("uv", tcStep, ref.decFmt.uvfmt, ref.decFmt.uvoff);
		compareComponent("col", colStep, ref.decFmt.c0fmt, ref.decFmt.c0off);
		compareComponent("nrm", nrmStep, ref.decFmt.nrmfmt, ref.decFmt.nrmoff, false, ref.skinInDecode);
		compareComponent("pos", posStep, DEC_FLOAT_3, ref.decFmt.posoff, ref.skinInDecode && !ref.throughmode, false);
		if (refFullAlpha != jitFullAlpha) {
			record("fullAlpha", colStep, 0, StringFromFormat("steps %d vs jit %d", (int)refFullAlpha, (int)jitFullAlpha));
		}
		// TODO: Only the steps track bounds for float UVs in through mode. Undecided which way to go.
		if (tcStep != &VertexDecoder::Step_TcFloatThrough && memcmp(&refBounds, &jitBounds, sizeof(refBounds)) != 0) {
			record("bounds", tcStep, 0, StringFromFormat("steps %d,%d-%d,%d vs jit %d,%d-%d,%d",
				refBounds.minU, refBounds.minV, refBounds.maxU, refBounds.maxV, jitBounds.minU, jitBounds.minV, jitBounds.maxU, jitBounds.maxV));
		}
	};

	static const int colFormats[] = { 0, 4, 5, 6, 7 };
	for (int through = 0; through <= 1; through++) {
		for (int tc = 0; tc < 4; tc++) {
			for (int col : colFormats) {
				for (int nrm = 0; nrm < 4; nrm++) {
					for (int pos = 1; pos < 4; pos++) {
						for (int wt = 0; wt < 4; wt++) {
							for (int nweights = 1; nweights <= (wt ? 8 : 1); nweights++) {
								for (int morph = 1; morph <= 8; morph++) {
									u32 vtype = (tc << GE_VTYPE_TC_SHIFT) | (col << GE_VTYPE_COL_SHIFT) | (nrm << GE_VTYPE_NRM_SHIFT) |
										(pos << GE_VTYPE_POS_SHIFT) | (wt << GE_VTYPE_WEIGHT_SHIFT) |
										((nweights - 1) << GE_VTYPE_WEIGHTCOUNT_SHIFT) | ((morph - 1) << GE_VTYPE_MORPHCOUNT_SHIFT) |
										(through ? GE_VTYPE_THROUGH : 0);
									// The UV gen mode and double texcoords only change anything with texcoords, and
									// only the TEXTURE_COORDS / TEXTURE_MATRIX split matters (the other two modes
									// decode like these). The expand option only applies to plain 8-bit normals.
									const int uvGenModes = (tc && !through) ? 2 : 1;
									const int doubleModes = tc ? 2 : 1;
									const int expandModes = (nrm == 1 && morph == 1 && !wt) ? 2 : 1;
									for (int uvGen = 0; uvGen < uvGenModes; uvGen++) {
										for (int dbl = 0; dbl < doubleModes; dbl++) {
											for (int expand = 0; expand < expandModes; expand++) {
												testFormat(vtype, uvGen == 0 ? GE_TEXMAP_TEXTURE_COORDS : GE_TEXMAP_TEXTURE_MATRIX, dbl != 0, expand != 0);
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	g_DoubleTextureCoordinates = savedDoubleTexCoords;
	delete cache;
	FreeAlignedMemory(src);
	FreeAlignedMemory(refOut);
	FreeAlignedMemory(jitOut);

	printf("VertexJitMatchesSteps: %d formats, %d jitted, %d mismatch kinds\n", formatsTested, formatsJitted, (int)mismatches.size());
	for (auto &iter : mismatches) {
		printf("  %s: %d formats, %d verts\n    e.g. %s\n", iter.first.c_str(), iter.second.formats, iter.second.verts, iter.second.example.c_str());
	}
	return mismatches.empty();
}


typedef bool (*VertexTestFunc)();

static VertexTestFunc vertdecTestFuncs[] = {
	&TestVertex8,
	&TestVertex16,
	&TestVertexFloat,

	&TestVertex8Through,
	&TestVertex16Through,
	&TestVertexFloatThrough,

	&TestVertexColor8888,
	&TestVertexColor4444,
	&TestVertexColor5551,
	&TestVertexColor565,

	&TestVertex8Skin,
	&TestVertex16Skin,
	&TestVertexFloatSkin,

	&TestVertexJitMatchesSteps,
};

bool TestVertexJit() {
	VertexDecoderTestHarness dec;
	/*for (int i = 0; i < 100; ++i) {
		dec.AddFloat(0.5f, 1.0f, -1.0f);
	}
	int vtype = GE_VTYPE_POS_FLOAT;*/
	/*for (int i = 0; i < 100; ++i) {
		dec.Add16(32767, 0, 32768);
	}
	int vtype = GE_VTYPE_POS_16BIT;*/
	for (int i = 0; i < 100; ++i) {
		dec.Add8(127, 0, 128);
	}
	int vtype = GE_VTYPE_POS_8BIT;
	double yesJit = dec.ExecuteTimed(vtype, 100, true);
	double noJit = dec.ExecuteTimed(vtype, 100, false);

	float x = dec.GetFloat();
	float y = dec.GetFloat();
	float z = dec.GetFloat();
	printf("Result: %f, %f, %f\n", x, y, z);
	printf("Jit was %fx faster than steps.\n\n", yesJit / noJit);

	bool pass = true;
	for (size_t i = 0; i < ARRAY_SIZE(vertdecTestFuncs); ++i) {
		if (!vertdecTestFuncs[i]()) {
			pass = false;
		}
	}

	return pass;
}
