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

#include "Common/CommonTypes.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "unittest/TestVertexJit.h"
#include "unittest/UnitTest.h"

class VertexDecoderTestHarness {
	static const int BUFFER_SIZE = 64 * 65536;
	static const int ROUNDS = 200;

public:
	VertexDecoderTestHarness()
		: dec_(nullptr), needsReset_(true), dstPos_(0), assertFailed_(false) {
		src_ = new u8[BUFFER_SIZE];
		dst_ = new u8[BUFFER_SIZE];
		cache_ = new VertexDecoderJitCache();

		gstate_c.uv.uScale = 1.0f;
		gstate_c.uv.vScale = 1.0f;
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
		memset(&options_, 0, sizeof(options_));
		delete dec_;
		dec_ = nullptr;
		indexLowerBound_ = 0;
		indexUpperBound_ = 0;
		srcPos_ = 0;
		dstPos_ = 0;
		needsReset_ = false;
	}

	void SetOptions(const VertexDecoderOptions &opts) {
		if (needsReset_) {
			Reset();
		}
		memcpy(&options_, &opts, sizeof(options_));
	}

	void SetIndexLowerBound(const int lower) {
		if (needsReset_) {
			Reset();
		}
		indexLowerBound_ = lower;
	}

	void Execute(int vtype, int indexUpperBound, bool useJit) {
		SetupExecute(vtype, useJit);

		dec_->DecodeVerts(dst_, src_, &gstate_c.uv, indexLowerBound_, indexUpperBound);
	}

	double ExecuteTimed(int vtype, int indexUpperBound, bool useJit) {
		SetupExecute(vtype, useJit);

		int total = 0;
		double st = time_now_d();
		do {
			for (int j = 0; j < ROUNDS; ++j) {
				dec_->DecodeVerts(dst_, src_, &gstate_c.uv, indexLowerBound_, indexUpperBound);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
		dec.Execute(vtype, 0, jit == 1);
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
	opts.applySkinInDecode = true;
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

	dec.Add8(128 + 64, 128 - 64);
	dec.Add8(127, 0, 128);
	dec.Add8(127, 0, 128);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 0, jit == 1);
		dec.AssertFloat("TestVertex8Skin-Nrm", (2.0f * 1.5f + 1.0f * 0.5f) * 127.0f / 128.0f, 0.0f, 2.0f * 5.0f * -1.0f);
		dec.AssertFloat("TestVertex8Skin-Pos", (2.0f * 1.5f + 1.0f * 0.5f) * 127.0f / 128.0f, 0.0f, 2.0f * 5.0f * -1.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertex16Skin() {
	VertexDecoderTestHarness dec;
	VertexDecoderOptions opts{};
	opts.applySkinInDecode = true;
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

	dec.Add16(32768 + 16384, 32768 - 16384);
	dec.Add16(32767, 0, 32768);
	dec.Add16(32767, 0, 32768);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 0, jit == 1);
		dec.AssertFloat("TestVertex16Skin-Nrm", (2.0f * 1.5f + 1.0f * 0.5f) * 32767.0f / 32768.0f, 0.0f, 2.0f * 5.0f * -1.0f);
		dec.AssertFloat("TestVertex16Skin-Pos", (2.0f * 1.5f + 1.0f * 0.5f) * 32767.0f / 32768.0f, 0.0f, 2.0f * 5.0f * -1.0f);
	}

	return !dec.HasFailed();
}

static bool TestVertexFloatSkin() {
	VertexDecoderTestHarness dec;
	VertexDecoderOptions opts{};
	opts.applySkinInDecode = true;
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

	dec.AddFloat(1.5f, 0.5f);
	dec.AddFloat(1.0f, 0, -1.0f);
	dec.AddFloat(1.0f, 0, -1.0f);

	for (int jit = 0; jit <= 1; ++jit) {
		dec.Execute(vtype, 0, jit == 1);
		dec.AssertFloat("TestVertexFloatSkin-Nrm", (2.0f * 1.5f + 1.0f * 0.5f) * 1.0f, 0.0f, 2.0f * 5.0f * -1.0f);
		dec.AssertFloat("TestVertexFloatSkin-Pos", (2.0f * 1.5f + 1.0f * 0.5f) * 1.0f, 0.0f, 2.0f * 5.0f * -1.0f);
	}

	return !dec.HasFailed();
}

// TODO: Morph (col, pos, nrm), weights (no skin), morph + weights?

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
