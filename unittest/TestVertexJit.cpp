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

#include "base/timeutil.h"
#include "Common/Common.h"
#include "Core/Config.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/ge_constants.h"
#include "unittest/TestVertexJit.h"
#include "unittest/UnitTest.h"

class VertexDecoderTestHarness {
	static const int BUFFER_SIZE = 64 * 65536;
	static const int ROUNDS = 200;

public:
	VertexDecoderTestHarness()
		: dec_(nullptr), needsReset_(true), dstPos_(0) {
		src_ = new u8[BUFFER_SIZE];
		dst_ = new u8[BUFFER_SIZE];
		cache_ = new VertexDecoderJitCache();

		g_Config.bVertexDecoderJit = true;
	}
	~VertexDecoderTestHarness() {
		delete src_;
		delete dst_;
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

		dec_->DecodeVerts(dst_, src_, indexLowerBound_, indexUpperBound);
	}

	double ExecuteTimed(int vtype, int indexUpperBound, bool useJit) {
		SetupExecute(vtype, useJit);

		int total = 0;
		double st = real_time_now();
		do {
			for (int j = 0; j < ROUNDS; ++j) {
				dec_->DecodeVerts(dst_, src_, indexLowerBound_, indexUpperBound);
				++total;
			}
		} while (real_time_now() - st < 0.5);
		double elapsed = real_time_now() - st;

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
		u16 result;
		memcpy(&result, dst_ + dstPos_, sizeof(result));
		dstPos_ += sizeof(result);
		return result;
	}

	float GetFloat() {
		float result;
		memcpy(&result, dst_ + dstPos_, sizeof(result));
		dstPos_ += sizeof(result);
		return result;
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

private:
	void SetupExecute(int vtype, bool useJit) {
		if (dec_ != nullptr) {
			delete dec_;
		}
		dec_ = new VertexDecoder();
		dec_->SetVertexType(vtype, options_, useJit ? cache_ : nullptr);

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
};

bool TestVertexJit() {
	VertexDecoderTestHarness dec;
	for (int i = 0; i < 100; ++i) {
		dec.AddFloat(0.5f, 1.0f, -1.0f);
	}
	int vtype = GE_VTYPE_POS_FLOAT;
	double yesJit = dec.ExecuteTimed(vtype, 100, true);
	double noJit = dec.ExecuteTimed(vtype, 100, false);

	printf("Result: %f, %f, %f\n", dec.GetFloat(), dec.GetFloat(), dec.GetFloat());
	printf("Jit was %fx faster than steps.\n\n", yesJit / noJit);

	return yesJit > noJit;
}
