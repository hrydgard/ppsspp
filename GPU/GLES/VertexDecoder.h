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

#include "../GPUState.h"
#include "../Globals.h"
#include "base/basictypes.h"

// DecVtxFormat - vertex formats for PC
// Kind of like a D3D VertexDeclaration.
// Can write code to easily bind these using OpenGL, or read these manually.
// No morph support, that is taken care of by the VertexDecoder.

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
	DEC_U8A_2,
	DEC_U16A_2,
};

int DecFmtSize(u8 fmt);

struct DecVtxFormat {
	u8 w0fmt; u8 w0off;  // first 4 weights
	u8 w1fmt; u8 w1off;  // second 4 weights
	u8 uvfmt; u8 uvoff;
	u8 c0fmt; u8 c0off;  // First color
	u8 c1fmt; u8 c1off;
	u8 nrmfmt; u8 nrmoff;
	u8 posfmt; u8 posoff;
	short stride;
};

// This struct too.
struct TransformedVertex
{
	float x, y, z, fog;     // in case of morph, preblend during decode
	float u; float v; float w;   // scaled by uscale, vscale, if there
	u8 color0[4];   // prelit
	u8 color1[3];   // prelit
};

DecVtxFormat GetTransformedVtxFormat(const DecVtxFormat &fmt);

class VertexDecoder;

typedef void (VertexDecoder::*StepFunction)() const;

void GetIndexBounds(void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound);

enum {
	STAT_VERTSSUBMITTED = 0,
	NUM_VERTEX_DECODER_STATS = 1
};

// Right now
//   - only contains computed information
//   - does decoding in nasty branchfilled loops
// Future TODO
//   - should be cached, not recreated every time
//   - will compile into list of called functions
//   - will compile into lighting fast specialized x86 and ARM
//   - will not bother translating components that can be read directly
//     by OpenGL ES. Will still have to translate 565 colors and things
//     like that. DecodedVertex will not be a fixed struct. Will have to
//     do morphing here.
class VertexDecoder
{
public:
	VertexDecoder() : coloff(0), nrmoff(0), posoff(0) {}
	~VertexDecoder() {}

	void SetVertexType(u32 vtype);
	u32 VertexType() const { return fmt_; }
	const DecVtxFormat &GetDecVtxFmt() { return decFmt; }

	void DecodeVerts(u8 *decoded, const void *verts, int indexLowerBound, int indexUpperBound) const;

	// This could be easily generalized to inject any one component. Don't know another use for it though.
	u32 InjectUVs(u8 *decoded, const void *verts, float *customuv, int count) const;

	bool hasColor() const { return col != 0; }
	int VertexSize() const { return size; }

	void Step_WeightsU8() const;
	void Step_WeightsU16() const;
	void Step_WeightsFloat() const;

	void Step_TcU8() const;
	void Step_TcU16() const;
	void Step_TcFloat() const;
	void Step_TcU16Through() const;
	void Step_TcFloatThrough() const;

	// TODO: tcmorph

	void Step_Color4444() const;
	void Step_Color565() const;
	void Step_Color5551() const;
	void Step_Color8888() const;

	void Step_Color4444Morph() const;
	void Step_Color565Morph() const;
	void Step_Color5551Morph() const;
	void Step_Color8888Morph() const;

	void Step_NormalS8() const;
	void Step_NormalS16() const;
	void Step_NormalFloat() const;

	void Step_NormalS8Morph() const;
	void Step_NormalS16Morph() const;
	void Step_NormalFloatMorph() const;

	void Step_PosS8() const;
	void Step_PosS16() const;
	void Step_PosFloat() const;

	void Step_PosS8Morph() const;
	void Step_PosS16Morph() const;
	void Step_PosFloatMorph() const;

	void Step_PosS8Through() const;
	void Step_PosS16Through() const;
	void Step_PosFloatThrough() const;

	void ResetStats() {
		memset(stats_, 0, sizeof(stats_));
	}

	void IncrementStat(int stat, int amount) {
		stats_[stat] += amount;
	}

	// output must be big for safety.
	// Returns number of chars written.
	// Ugly for speed.
	int ToString(char *output) const;

	// Mutable decoder state
	mutable u8 *decoded_;
	mutable const u8 *ptr_;

	// "Immutable" state, set at startup

	// The decoding steps
	StepFunction steps_[5];
	int numSteps_;

	u32 fmt_;
	DecVtxFormat decFmt;

	bool throughmode;
	int biggest;
	int size;
	int onesize_;

	int weightoff;
	int tcoff;
	int coloff;
	int nrmoff;
	int posoff;

	int tc;
	int col;
	int nrm;
	int pos;
	int weighttype;
	int idx;
	int morphcount;
	int nweights;

	int stats_[NUM_VERTEX_DECODER_STATS];
};

// Reads decoded vertex formats in a convenient way. For software transform and debugging.
class VertexReader
{
public:
	VertexReader(u8 *base, const DecVtxFormat &decFmt, int vtype) : base_(base), data_(base), decFmt_(decFmt), vtype_(vtype) {}

	void ReadPos(float pos[3]) {
		switch (decFmt_.posfmt) {
		case DEC_FLOAT_3:
			{
				const float *f = (const float *)(data_ + decFmt_.posoff);
				memcpy(pos, f, 12);
				if (isThrough()) {
					// Integer value passed in a float. Wraps and all, required for Monster Hunter.
					pos[2] = (float)((u16)(s32)pos[2]) * (1.0f / 65535.0f);
				}
			}
			break;
		case DEC_S16_3:
			{
				// X and Y are signed 16 bit, Z is unsigned 16 bit
				const s16 *s = (const s16 *)(data_ + decFmt_.posoff);
				const u16 *u = (const u16 *)(data_ + decFmt_.posoff);
				if (isThrough()) {
					for (int i = 0; i < 2; i++)
						pos[i] = s[i];
					pos[2] = u[2] * (1.0f / 65535.0f);
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = s[i] * (1.f / 32767.f);
				}
			}
			break;
		case DEC_S8_3:
			{
				// X and Y are signed 8 bit, Z is unsigned 8 bit
				const s8 *b = (const s8 *)(data_ + decFmt_.posoff);
				const u8 *u = (const u8 *)(data_ + decFmt_.posoff);
				if (isThrough()) {
					for (int i = 0; i < 2; i++)
						pos[i] = b[i];
					pos[2] = u[2] / 255.0f;
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = b[i] * (1.f / 127.f);
				}
			}
			break;
		default:
			ERROR_LOG(G3D, "Reader: Unsupported Pos Format");
			break;
		}
	}

	void ReadNrm(float nrm[3]) {
		switch (decFmt_.nrmfmt) {
		case DEC_FLOAT_3:
			//memcpy(nrm, data_ + decFmt_.nrmoff, 12);
			{
				const float *f = (const float *)(data_ + decFmt_.nrmoff);
				for (int i = 0; i < 3; i++)
					nrm[i] = f[i] ;
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
			ERROR_LOG(G3D, "Reader: Unsupported Nrm Format");
			break;
		}
	}

	void ReadUV(float uv[2]) {
		switch (decFmt_.uvfmt) {
		case DEC_U8_2:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.uvoff);
				uv[0] = b[0] * (1.f / 128.f);
				uv[1] = b[1] * (1.f / 128.f);
			}
			break;

		case DEC_U16_2:
			{
				const u16 *s = (const u16 *)(data_ + decFmt_.uvoff);
				uv[0] = s[0] * (1.f / 32768.f);
				uv[1] = s[1] * (1.f / 32768.f);
			}
			break;

		case DEC_FLOAT_2:
			{
				const float *f = (const float *)(data_ + decFmt_.uvoff);
				uv[0] = f[0] * 2.f;
				uv[1] = f[1] * 2.f;
			}
			break;

		case DEC_U16A_2:
			{
				const u16 *p = (const u16 *)(data_ + decFmt_.uvoff);
				uv[0] = (float)p[0];
				uv[1] = (float)p[1];
			}
			break;
		default:
			ERROR_LOG(G3D, "Reader: Unsupported UV Format");
			break;
		}
	}

	void ReadColor0(float color[4]) {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.c0off);
				for (int i = 0; i < 4; i++)
					color[i] = b[i] * (1.f / 255.f);
			}
			break;
		case DEC_FLOAT_4:
			memcpy(color, data_ + decFmt_.c0off, 16); 
			break;
		default:
			ERROR_LOG(G3D, "Reader: Unsupported C0 Format");
			break;
		}
	}

	void ReadColor1(float color[3]) {
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
			ERROR_LOG(G3D, "Reader: Unsupported C1 Format");
			break;
		}
	}

	void ReadWeights(float weights[8]) {
		const float *f = (const float *)(data_ + decFmt_.w0off);
		const u8 *b = (const u8 *)(data_ + decFmt_.w0off);
		const u16 *s = (const u16 *)(data_ + decFmt_.w0off);
		switch (decFmt_.w0fmt) {
		case DEC_FLOAT_1:
		case DEC_FLOAT_2:
		case DEC_FLOAT_3:
		case DEC_FLOAT_4:
			for (int i = 0; i <= decFmt_.w0fmt - DEC_FLOAT_1; i++)
				weights[i] = f[i] * 2.f;
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
			ERROR_LOG(G3D, "Reader: Unsupported W0 Format");
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
				weights[i+4] = f[i] * 2.f;
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
			ERROR_LOG(G3D, "Reader: Unsupported W1 Format");
			break;
		}
	}

	bool hasColor0() const { return decFmt_.c0fmt != 0; }
	bool hasNormal() const { return decFmt_.nrmfmt != 0; }
	bool hasUV() const { return decFmt_.uvfmt != 0; }
	bool isThrough() const { return (vtype_ & GE_VTYPE_THROUGH) != 0; }
	void Goto(int index) {
		data_ = base_ + index * decFmt_.stride;
	}

private:
	u8 *base_;
	u8 *data_;
	DecVtxFormat decFmt_;
	int vtype_;
};

// Debugging utilities
void PrintDecodedVertex(VertexReader &vtx);


