// Copyright (c) 2013- PPSSPP Project.

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
#include "base/basictypes.h"
#include "Common/Log.h"
#include "Common/CommonTypes.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"

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
	union {
		u8 color0[4];   // prelit
		u32 color0_32;
	};
	union {
		u8 color1[4];   // prelit
		u32 color1_32;
	};
};

void GetIndexBounds(const void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound);

enum {
	STAT_VERTSSUBMITTED = 0,
	NUM_VERTEX_DECODER_STATS = 1
};

inline int RoundUp4(int x) {
	return (x + 3) & ~3;
}

// Reads decoded vertex formats in a convenient way. For software transform and debugging.
class VertexReader
{
public:
	VertexReader(u8 *base, const DecVtxFormat &decFmt, int vtype) : base_(base), data_(base), decFmt_(decFmt), vtype_(vtype) {}

	void ReadPos(float pos[3]) const {
		switch (decFmt_.posfmt) {
		case DEC_FLOAT_3:
			{
				const float *f = (const float *)(data_ + decFmt_.posoff);
				memcpy(pos, f, 12);
				if (isThrough()) {
					// Integer value passed in a float. Clamped to 0, 65535.
					pos[2] = pos[2] > 65535.0f ? 1.0f : (pos[2] < 0.0f ? 0.0f : pos[2] * (1.0f / 65535.0f));
				}
				// See https://github.com/hrydgard/ppsspp/pull/3419, something is weird.
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
			ERROR_LOG_REPORT_ONCE(fmtpos, G3D, "Reader: Unsupported Pos Format %d", decFmt_.posfmt);
			memset(pos, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadPosZ16(float pos[3]) const {
		switch (decFmt_.posfmt) {
		case DEC_FLOAT_3:
			{
				const float *f = (const float *)(data_ + decFmt_.posoff);
				memcpy(pos, f, 12);
				if (isThrough()) {
					// Integer value passed in a float. Clamped to 0, 65535.
					pos[2] = pos[2] > 65535.0f ? 65535.0f : (pos[2] < 0.0f ? 0.0f : pos[2]);
				}
				// TODO: Does non-through need conversion?
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
					pos[2] = u[2];
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = s[i] * (1.f / 32767.f);
					// TODO: Does depth need conversion?
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
					pos[2] = u[2];
				} else {
					for (int i = 0; i < 3; i++)
						pos[i] = b[i] * (1.f / 127.f);
					// TODO: Does depth need conversion?
				}
			}
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtz16, G3D, "Reader: Unsupported Pos Format %d", decFmt_.posfmt);
			memset(pos, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadNrm(float nrm[3]) const {
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
			ERROR_LOG_REPORT_ONCE(fmtnrm, G3D, "Reader: Unsupported Nrm Format %d", decFmt_.nrmfmt);
			memset(nrm, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadUV(float uv[2]) const {
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
				uv[0] = f[0];
				uv[1] = f[1];
			}
			break;

		case DEC_U8A_2:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.uvoff);
				uv[0] = (float)b[0];
				uv[1] = (float)b[1];
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
			ERROR_LOG_REPORT_ONCE(fmtuv, G3D, "Reader: Unsupported UV Format %d", decFmt_.uvfmt);
			memset(uv, 0, sizeof(float) * 2);
			break;
		}
	}

	void ReadColor0(float color[4]) const {
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
			ERROR_LOG_REPORT_ONCE(fmtc0, G3D, "Reader: Unsupported C0 Format %d", decFmt_.c0fmt);
			memset(color, 0, sizeof(float) * 4);
			break;
		}
	}

	void ReadColor0_8888(u8 color[4]) const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			{
				const u8 *b = (const u8 *)(data_ + decFmt_.c0off);
				for (int i = 0; i < 4; i++)
					color[i] = b[i];
			}
			break;
		case DEC_FLOAT_4:
			{
				const float *f = (const float *)(data_ + decFmt_.c0off);
				for (int i = 0; i < 4; i++)
					color[i] = f[i] * 255.0f;
			}
			break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtc0_8888, G3D, "Reader: Unsupported C0 Format %d", decFmt_.c0fmt);
			memset(color, 0, sizeof(u8) * 4);
			break;
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
			ERROR_LOG_REPORT_ONCE(fmtc1, G3D, "Reader: Unsupported C1 Format %d", decFmt_.c1fmt);
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
			ERROR_LOG_REPORT_ONCE(fmtw0, G3D, "Reader: Unsupported W0 Format %d", decFmt_.w0fmt);
			memset(weights, 0, sizeof(float) * 4);
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
			ERROR_LOG_REPORT_ONCE(fmtw1, G3D, "Reader: Unsupported W1 Format %d", decFmt_.w1fmt);
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
	u8 *base_;
	u8 *data_;
	DecVtxFormat decFmt_;
	int vtype_;
};
// Debugging utilities
void PrintDecodedVertex(VertexReader &vtx);

