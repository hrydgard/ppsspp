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

#include "base/basictypes.h"
#include "base/logging.h"

#include "Common/CPUDetect.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/Math3D.h"

#include "VertexDecoder.h"
#include "VertexShaderGenerator.h"

#if defined(_M_IX86) || defined(_M_X64)
#include <emmintrin.h>
#endif

extern void DisassembleArm(const u8 *data, int size);

static const u8 tcsize[4] = {0,2,4,8}, tcalign[4] = {0,1,2,4};
static const u8 colsize[8] = {0,0,0,0,2,2,2,4}, colalign[8] = {0,0,0,0,2,2,2,4};
static const u8 nrmsize[4] = {0,3,6,12}, nrmalign[4] = {0,1,2,4};
static const u8 possize[4] = {0,3,6,12}, posalign[4] = {0,1,2,4};
static const u8 wtsize[4] = {0,1,2,4}, wtalign[4] = {0,1,2,4};

// When software skinning. This array is only used when non-jitted - when jitted, the matrix
// is kept in registers.
static float MEMORY_ALIGNED16(skinMatrix[12]);

// We start out by converting the active matrices into 4x4 which are easier to multiply with
// using SSE / NEON and store them here.
static float MEMORY_ALIGNED16(bones[16 * 8]);

// The rest will be dumped to bones as on x86.

// NEON register allocation:
// Q0: Texture scaling parameters
// Q1: Temp storage
// Q2: Vector-by-matrix accumulator
// Q3: Unused
//
// We'll use Q4-Q7 as the "matrix accumulator".
// First two matrices will be preloaded into Q8-Q11 and Q12-Q15 to reduce
// memory bandwidth requirements.


inline int align(int n, int align) {
	return (n + (align - 1)) & ~(align - 1);
}

VertexDecoder::VertexDecoder() : coloff(0), nrmoff(0), posoff(0), jitted_(0) {
	memset(stats_, 0, sizeof(stats_));
}

void VertexDecoder::Step_WeightsU8() const
{
	u8 *wt = (u8 *)(decoded_ + decFmt.w0off);
	const u8 *wdata = (const u8*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++)
		wt[j] = wdata[j];
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
}

void VertexDecoder::Step_WeightsU16() const
{
	u16 *wt = (u16 *)(decoded_ + decFmt.w0off);
	const u16 *wdata = (const u16*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++)
		wt[j] = wdata[j];
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
}

// Float weights should be uncommon, we can live with having to multiply these by 2.0
// to avoid special checks in the vertex shader generator.
// (PSP uses 0.0-2.0 fixed point numbers for weights)
void VertexDecoder::Step_WeightsFloat() const
{
	float *wt = (float *)(decoded_ + decFmt.w0off);
	const float *wdata = (const float*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		wt[j] = wdata[j];
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0.0f;
}

void VertexDecoder::Step_WeightsU8Skin() const
{
	memset(skinMatrix, 0, sizeof(skinMatrix));
	u8 *wt = (u8 *)(decoded_ + decFmt.w0off);
	const u8 *wdata = (const u8*)(ptr_);
	for (int j = 0; j < nweights; j++) {
		const float *bone = &gstate.boneMatrix[j * 12];
		if (wdata[j] != 0) {
			float weight = wdata[j] / 128.0f;
			for (int i = 0; i < 12; i++) {
				skinMatrix[i] += weight * bone[i];
			}
		}
	}
}

void VertexDecoder::Step_WeightsU16Skin() const
{
	memset(skinMatrix, 0, sizeof(skinMatrix));
	u16 *wt = (u16 *)(decoded_ + decFmt.w0off);
	const u16 *wdata = (const u16*)(ptr_);
	for (int j = 0; j < nweights; j++) {
		const float *bone = &gstate.boneMatrix[j * 12];
		if (wdata[j] != 0) {
			float weight = wdata[j] / 32768.0f;
			for (int i = 0; i < 12; i++) {
				skinMatrix[i] += weight * bone[i];
			}
		}
	}
}

// Float weights should be uncommon, we can live with having to multiply these by 2.0
// to avoid special checks in the vertex shader generator.
// (PSP uses 0.0-2.0 fixed point numbers for weights)
void VertexDecoder::Step_WeightsFloatSkin() const
{
	memset(skinMatrix, 0, sizeof(skinMatrix));
	float *wt = (float *)(decoded_ + decFmt.w0off);
	const float *wdata = (const float*)(ptr_);
	for (int j = 0; j < nweights; j++) {
		const float *bone = &gstate.boneMatrix[j * 12];
		float weight = wdata[j];
		if (weight > 0.0) {
			for (int i = 0; i < 12; i++) {
				skinMatrix[i] += weight * bone[i];
			}
		}
	}
}

void VertexDecoder::Step_TcU8() const
{
	// u32 to write two bytes of zeroes for free.
	u32 *uv = (u32*)(decoded_ + decFmt.uvoff);
	const u16 *uvdata = (const u16*)(ptr_ + tcoff);
	*uv = *uvdata;
}

void VertexDecoder::Step_TcU16() const
{
	u32 *uv = (u32 *)(decoded_ + decFmt.uvoff);
	const u32 *uvdata = (const u32*)(ptr_ + tcoff);
	*uv = *uvdata;
}

void VertexDecoder::Step_TcU16Double() const
{
	u16 *uv = (u16*)(decoded_ + decFmt.uvoff);
	const u16 *uvdata = (const u16*)(ptr_ + tcoff);
	*uv = *uvdata;
	uv[0] = uvdata[0] * 2;
	uv[1] = uvdata[1] * 2;
}

void VertexDecoder::Step_TcU16Through() const
{
	u16 *uv = (u16 *)(decoded_ + decFmt.uvoff);
	const u16 *uvdata = (const u16*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
}

void VertexDecoder::Step_TcU16ThroughDouble() const
{
	u16 *uv = (u16 *)(decoded_ + decFmt.uvoff);
	const u16 *uvdata = (const u16*)(ptr_ + tcoff);
	uv[0] = uvdata[0] * 2;
	uv[1] = uvdata[1] * 2;
}

void VertexDecoder::Step_TcFloat() const
{
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float *uvdata = (const float*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
}

void VertexDecoder::Step_TcFloatThrough() const
{
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float *uvdata = (const float*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
}

void VertexDecoder::Step_TcU8Prescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u8 *uvdata = (const u8 *)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] * (1.f / 128.f) * gstate_c.uv.uScale + gstate_c.uv.uOff;
	uv[1] = (float)uvdata[1] * (1.f / 128.f) * gstate_c.uv.vScale + gstate_c.uv.vOff;
}

void VertexDecoder::Step_TcU16Prescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u16 *uvdata = (const u16 *)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] * (1.f / 32768.f) * gstate_c.uv.uScale + gstate_c.uv.uOff;
	uv[1] = (float)uvdata[1] * (1.f / 32768.f) * gstate_c.uv.vScale + gstate_c.uv.vOff;
}

void VertexDecoder::Step_TcFloatPrescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float *uvdata = (const float*)(ptr_ + tcoff);
	uv[0] = uvdata[0] * gstate_c.uv.uScale + gstate_c.uv.uOff;
	uv[1] = uvdata[1] * gstate_c.uv.vScale + gstate_c.uv.vOff;
}

void VertexDecoder::Step_Color565() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(u16*)(ptr_ + coloff);
	c[0] = Convert5To8(cdata & 0x1f);
	c[1] = Convert6To8((cdata>>5) & 0x3f);
	c[2] = Convert5To8((cdata>>11) & 0x1f);
	c[3] = 255;
}

void VertexDecoder::Step_Color5551() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(u16*)(ptr_ + coloff);
	c[0] = Convert5To8(cdata & 0x1f);
	c[1] = Convert5To8((cdata>>5) & 0x1f);
	c[2] = Convert5To8((cdata>>10) & 0x1f);
	c[3] = (cdata >> 15) ? 255 : 0;
}

void VertexDecoder::Step_Color4444() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(u16*)(ptr_ + coloff);
	for (int j = 0; j < 4; j++)
		c[j] = Convert4To8((cdata >> (j * 4)) & 0xF);
}

void VertexDecoder::Step_Color8888() const
{
	u8 *c = decoded_ + decFmt.c0off;
	const u8 *cdata = (const u8*)(ptr_ + coloff);
	memcpy(c, cdata, sizeof(u8) * 4);
}

void VertexDecoder::Step_Color565Morph() const
{
	float col[3] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(u16*)(ptr_ + onesize_*n + coloff);
		col[0] += w * (cdata & 0x1f) * (255.0f / 31.0f);
		col[1] += w * ((cdata>>5) & 0x3f) * (255.0f / 63.0f);
		col[2] += w * ((cdata>>11) & 0x1f) * (255.0f / 31.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	for (int i = 0; i < 3; i++) {
		c[i] = (u8)col[i];
	}
	c[3] = 255;
}

void VertexDecoder::Step_Color5551Morph() const
{
	float col[4] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(u16*)(ptr_ + onesize_*n + coloff);
		col[0] += w * (cdata & 0x1f) * (255.0f / 31.0f);
		col[1] += w * ((cdata>>5) & 0x1f) * (255.0f / 31.0f);
		col[2] += w * ((cdata>>10) & 0x1f) * (255.0f / 31.0f);
		col[3] += w * ((cdata>>15) ? 255.0f : 0.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	for (int i = 0; i < 4; i++) {
		c[i] = (u8)col[i];
	}
}

void VertexDecoder::Step_Color4444Morph() const
{
	float col[4] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(u16*)(ptr_ + onesize_*n + coloff);
		for (int j = 0; j < 4; j++)
			col[j] += w * ((cdata >> (j * 4)) & 0xF) * (255.0f / 15.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	for (int i = 0; i < 4; i++) {
		c[i] = (u8)col[i];
	}
}

void VertexDecoder::Step_Color8888Morph() const
{
	float col[4] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		const u8 *cdata = (const u8*)(ptr_ + onesize_*n + coloff);
		for (int j = 0; j < 4; j++)
			col[j] += w * cdata[j];
	}
	u8 *c = decoded_ + decFmt.c0off;
	for (int i = 0; i < 4; i++) {
		c[i] = (u8)(col[i]);
	}
}

void VertexDecoder::Step_NormalS8() const
{
	s8 *normal = (s8 *)(decoded_ + decFmt.nrmoff);
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = sv[j];
	normal[3] = 0;
}

void VertexDecoder::Step_NormalS16() const
{
	s16 *normal = (s16 *)(decoded_ + decFmt.nrmoff);
	const s16 *sv = (const s16*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = sv[j];
	normal[3] = 0;
}

void VertexDecoder::Step_NormalFloat() const
{
	u32 *normal = (u32 *)(decoded_ + decFmt.nrmoff);
	const u32 *fv = (const u32*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = fv[j];
}

void VertexDecoder::Step_NormalS8Skin() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	const float fn[3] = { sv[0] / 128.0f, sv[1] / 128.0f, sv[2] / 128.0f };
	Norm3ByMatrix43(normal, fn, skinMatrix);
}

void VertexDecoder::Step_NormalS16Skin() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const s16 *sv = (const s16*)(ptr_ + nrmoff);
	const float fn[3] = { sv[0] / 32768.0f, sv[1] / 32768.0f, sv[2] / 32768.0f };
	Norm3ByMatrix43(normal, fn, skinMatrix);
}

void VertexDecoder::Step_NormalFloatSkin() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const float *fn = (const float *)(ptr_ + nrmoff);
	Norm3ByMatrix43(normal, fn, skinMatrix);
}

void VertexDecoder::Step_NormalS8Morph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		const s8 *bv = (const s8*)(ptr_ + onesize_*n + nrmoff);
		float multiplier = gstate_c.morphWeights[n] * (1.0f/127.0f);
		for (int j = 0; j < 3; j++)
			normal[j] += bv[j] * multiplier;
	}
}

void VertexDecoder::Step_NormalS16Morph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n] * (1.0f/32767.0f);
		const s16 *sv = (const s16 *)(ptr_ + onesize_*n + nrmoff);
		for (int j = 0; j < 3; j++)
			normal[j] += sv[j] * multiplier;
	}
}

void VertexDecoder::Step_NormalFloatMorph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		const float *fv = (const float*)(ptr_ + onesize_*n + nrmoff);
		for (int j = 0; j < 3; j++)
			normal[j] += fv[j] * multiplier;
	}
}

void VertexDecoder::Step_PosS8() const
{
	s8 *v = (s8 *)(decoded_ + decFmt.posoff);
	const s8 *sv = (const s8*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j];
	v[3] = 0;
}

void VertexDecoder::Step_PosS16() const
{
	s16 *v = (s16 *)(decoded_ + decFmt.posoff);
	const s16 *sv = (const s16*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j];
	v[3] = 0;
}

void VertexDecoder::Step_PosFloat() const
{
	u8 *v = (u8 *)(decoded_ + decFmt.posoff);
	const u8 *fv = (const u8*)(ptr_ + posoff);
	memcpy(v, fv, 12);
}

void VertexDecoder::Step_PosS8Skin() const
{
	float *pos = (float *)(decoded_ + decFmt.posoff);
	const s8 *sv = (const s8*)(ptr_ + posoff);
	const float fn[3] = { sv[0] / 128.0f, sv[1] / 128.0f, sv[2] / 128.0f };
	Vec3ByMatrix43(pos, fn, skinMatrix);
}

void VertexDecoder::Step_PosS16Skin() const
{
	float *pos = (float *)(decoded_ + decFmt.posoff);
	const s16 *sv = (const s16*)(ptr_ + posoff);
	const float fn[3] = { sv[0] / 32768.0f, sv[1] / 32768.0f, sv[2] / 32768.0f };
	Vec3ByMatrix43(pos, fn, skinMatrix);
}

void VertexDecoder::Step_PosFloatSkin() const
{
	float *pos = (float *)(decoded_ + decFmt.posoff);
	const float *fn = (const float *)(ptr_ + posoff);
	Vec3ByMatrix43(pos, fn, skinMatrix);
}

void VertexDecoder::Step_PosS8Through() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	const s8 *sv = (const s8*)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = sv[2];
}

void VertexDecoder::Step_PosS16Through() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	const s16 *sv = (const s16*)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = sv[2];
}

void VertexDecoder::Step_PosFloatThrough() const
{
	u8 *v = (u8 *)(decoded_ + decFmt.posoff);
	const u8 *fv = (const u8*)(ptr_ + posoff);
	memcpy(v, fv, 12);
}

void VertexDecoder::Step_PosS8Morph() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		float multiplier = 1.0f / 127.0f;
		const s8 *sv = (const s8*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += (float)sv[j] * (multiplier * gstate_c.morphWeights[n]);
	}
}

void VertexDecoder::Step_PosS16Morph() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		float multiplier = 1.0f / 32767.0f;
		const s16 *sv = (const s16*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += (float)sv[j] * (multiplier * gstate_c.morphWeights[n]);
	}
}

void VertexDecoder::Step_PosFloatMorph() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const float *fv = (const float*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += fv[j] * gstate_c.morphWeights[n];
	}
}

static const StepFunction wtstep[4] = {
	0,
	&VertexDecoder::Step_WeightsU8,
	&VertexDecoder::Step_WeightsU16,
	&VertexDecoder::Step_WeightsFloat,
};

static const StepFunction wtstep_skin[4] = {
	0,
	&VertexDecoder::Step_WeightsU8Skin,
	&VertexDecoder::Step_WeightsU16Skin,
	&VertexDecoder::Step_WeightsFloatSkin,
};

static const StepFunction tcstep[4] = {
	0,
	&VertexDecoder::Step_TcU8,
	&VertexDecoder::Step_TcU16,
	&VertexDecoder::Step_TcFloat,
};

static const StepFunction tcstep_prescale[4] = {
	0,
	&VertexDecoder::Step_TcU8Prescale,
	&VertexDecoder::Step_TcU16Prescale,
	&VertexDecoder::Step_TcFloatPrescale,
};

static const StepFunction tcstep_through[4] = {
	0,
	&VertexDecoder::Step_TcU8,
	&VertexDecoder::Step_TcU16Through,
	&VertexDecoder::Step_TcFloatThrough,
};

// Some HD Remaster games double the u16 texture coordinates.
static const StepFunction tcstep_Remaster[4] = {
	0,
	&VertexDecoder::Step_TcU8,
	&VertexDecoder::Step_TcU16Double,
	&VertexDecoder::Step_TcFloat,
};

static const StepFunction tcstep_through_Remaster[4] = {
	0,
	&VertexDecoder::Step_TcU8,
	&VertexDecoder::Step_TcU16ThroughDouble,
	&VertexDecoder::Step_TcFloatThrough,
};

// TODO: Tc Morph

static const StepFunction colstep[8] = {
	0, 0, 0, 0,
	&VertexDecoder::Step_Color565,
	&VertexDecoder::Step_Color5551,
	&VertexDecoder::Step_Color4444,
	&VertexDecoder::Step_Color8888,
};

static const StepFunction colstep_morph[8] = {
	0, 0, 0, 0,
	&VertexDecoder::Step_Color565Morph,
	&VertexDecoder::Step_Color5551Morph,
	&VertexDecoder::Step_Color4444Morph,
	&VertexDecoder::Step_Color8888Morph,
};

static const StepFunction nrmstep[4] = {
	0,
	&VertexDecoder::Step_NormalS8,
	&VertexDecoder::Step_NormalS16,
	&VertexDecoder::Step_NormalFloat,
};

static const StepFunction nrmstep_skin[4] = {
	0,
	&VertexDecoder::Step_NormalS8Skin,
	&VertexDecoder::Step_NormalS16Skin,
	&VertexDecoder::Step_NormalFloatSkin,
};

static const StepFunction nrmstep_morph[4] = {
	0,
	&VertexDecoder::Step_NormalS8Morph,
	&VertexDecoder::Step_NormalS16Morph,
	&VertexDecoder::Step_NormalFloatMorph,
};

static const StepFunction posstep[4] = {
	0,
	&VertexDecoder::Step_PosS8,
	&VertexDecoder::Step_PosS16,
	&VertexDecoder::Step_PosFloat,
};

static const StepFunction posstep_skin[4] = {
	0,
	&VertexDecoder::Step_PosS8Skin,
	&VertexDecoder::Step_PosS16Skin,
	&VertexDecoder::Step_PosFloatSkin,
};

static const StepFunction posstep_morph[4] = {
	0,
	&VertexDecoder::Step_PosS8Morph,
	&VertexDecoder::Step_PosS16Morph,
	&VertexDecoder::Step_PosFloatMorph,
};

static const StepFunction posstep_through[4] = {
	0,
	&VertexDecoder::Step_PosS8Through,
	&VertexDecoder::Step_PosS16Through,
	&VertexDecoder::Step_PosFloatThrough,
};

void VertexDecoder::SetVertexType(u32 fmt, VertexDecoderJitCache *jitCache) {
	fmt_ = fmt;
	throughmode = (fmt & GE_VTYPE_THROUGH) != 0;
	numSteps_ = 0;

	int biggest = 0;
	size = 0;

	tc = fmt & 0x3;
	col = (fmt >> 2) & 0x7;
	nrm = (fmt >> 5) & 0x3;
	pos = (fmt >> 7) & 0x3;
	weighttype = (fmt >> 9) & 0x3;
	idx = (fmt >> 11) & 0x3;
	morphcount = ((fmt >> 18) & 0x7)+1;
	nweights = ((fmt >> 14) & 0x7)+1;

	int decOff = 0;
	memset(&decFmt, 0, sizeof(decFmt));

	if (morphcount > 1) {
		DEBUG_LOG_REPORT_ONCE(m, G3D,"VTYPE with morph used: THRU=%i TC=%i COL=%i POS=%i NRM=%i WT=%i NW=%i IDX=%i MC=%i", (int)throughmode, tc,col,pos,nrm,weighttype,nweights,idx,morphcount);
	} else {
		DEBUG_LOG(G3D,"VTYPE: THRU=%i TC=%i COL=%i POS=%i NRM=%i WT=%i NW=%i IDX=%i MC=%i", (int)throughmode, tc,col,pos,nrm,weighttype,nweights,idx,morphcount);
	}

	bool skinInDecode = weighttype != 0 && g_Config.bSoftwareSkinning && morphcount == 1;

	if (weighttype) { // && nweights?
		weightoff = size;
		//size = align(size, wtalign[weighttype]);	unnecessary
		size += wtsize[weighttype] * nweights;
		if (wtalign[weighttype] > biggest)
			biggest = wtalign[weighttype];

		if (skinInDecode) {
			steps_[numSteps_++] = wtstep_skin[weighttype];
			// No visible output
		} else {
			steps_[numSteps_++] = wtstep[weighttype];

			int fmtBase = DEC_FLOAT_1;
			if (weighttype == GE_VTYPE_WEIGHT_8BIT >> GE_VTYPE_WEIGHT_SHIFT) {
				fmtBase = DEC_U8_1;
			} else if (weighttype == GE_VTYPE_WEIGHT_16BIT >> GE_VTYPE_WEIGHT_SHIFT) {
				fmtBase = DEC_U16_1;
			} else if (weighttype == GE_VTYPE_WEIGHT_FLOAT >> GE_VTYPE_WEIGHT_SHIFT) {
				fmtBase = DEC_FLOAT_1;
			}

			int numWeights = TranslateNumBones(nweights);

			if (numWeights <= 4) {
				decFmt.w0off = decOff;
				decFmt.w0fmt = fmtBase + numWeights - 1;
				decOff += DecFmtSize(decFmt.w0fmt);
			} else {
				decFmt.w0off = decOff;
				decFmt.w0fmt = fmtBase + 3;
				decOff += DecFmtSize(decFmt.w0fmt);
				decFmt.w1off = decOff;
				decFmt.w1fmt = fmtBase + numWeights - 5;
				decOff += DecFmtSize(decFmt.w1fmt);
			}
		}
	}

	if (tc) {
		size = align(size, tcalign[tc]);
		tcoff = size;
		size += tcsize[tc];
		if (tcalign[tc] > biggest)
			biggest = tcalign[tc];

		// NOTE: That we check getTextureFunction here means that we must include it in the decoder ID!
		if (g_Config.bPrescaleUV && !throughmode && (gstate.getTextureFunction() == 0 || gstate.getTextureFunction() == 3)) {
			steps_[numSteps_++] = tcstep_prescale[tc];
			decFmt.uvfmt = DEC_FLOAT_2;
		} else {
			if (g_DoubleTextureCoordinates)
				steps_[numSteps_++] = throughmode ? tcstep_through_Remaster[tc] : tcstep_Remaster[tc];
			else
				steps_[numSteps_++] = throughmode ? tcstep_through[tc] : tcstep[tc];

			switch (tc) {
			case GE_VTYPE_TC_8BIT >> GE_VTYPE_TC_SHIFT:
				decFmt.uvfmt = throughmode ? DEC_U8A_2 : DEC_U8_2;
				break;
			case GE_VTYPE_TC_16BIT >> GE_VTYPE_TC_SHIFT:
				decFmt.uvfmt = throughmode ? DEC_U16A_2 : DEC_U16_2;
				break;
			case GE_VTYPE_TC_FLOAT >> GE_VTYPE_TC_SHIFT:
				decFmt.uvfmt = DEC_FLOAT_2;
				break;
			}
		}

		decFmt.uvoff = decOff;
		decOff += DecFmtSize(decFmt.uvfmt);
	}

	if (col) {
		size = align(size, colalign[col]);
		coloff = size;
		size += colsize[col];
		if (colalign[col] > biggest)
			biggest = colalign[col];

		steps_[numSteps_++] = morphcount == 1 ? colstep[col] : colstep_morph[col];

		// All color formats decode to DEC_U8_4 currently.
		// They can become floats later during transform though.
		decFmt.c0fmt = DEC_U8_4;
		decFmt.c0off = decOff;
		decOff += DecFmtSize(decFmt.c0fmt);
	} else {
		coloff = 0;
	}

	if (nrm) {
		size = align(size, nrmalign[nrm]);
		nrmoff = size;
		size += nrmsize[nrm];
		if (nrmalign[nrm] > biggest)
			biggest = nrmalign[nrm]; 

		if (skinInDecode) {
			steps_[numSteps_++] = nrmstep_skin[nrm];
			// After skinning, we always have three floats.
			decFmt.nrmfmt = DEC_FLOAT_3;
		} else {
			steps_[numSteps_++] = morphcount == 1 ? nrmstep[nrm] : nrmstep_morph[nrm];

			if (morphcount == 1) {
				// The normal formats match the gl formats perfectly, let's use 'em.
				switch (nrm) {
				case GE_VTYPE_NRM_8BIT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_S8_3; break;
				case GE_VTYPE_NRM_16BIT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_S16_3; break;
				case GE_VTYPE_NRM_FLOAT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_FLOAT_3; break;
				}
			} else {
				decFmt.nrmfmt = DEC_FLOAT_3;
			}
		}
		decFmt.nrmoff = decOff;
		decOff += DecFmtSize(decFmt.nrmfmt);
	}

	if (pos) { // there's always a position
		size = align(size, posalign[pos]);
		posoff = size;
		size += possize[pos];
		if (posalign[pos] > biggest)
			biggest = posalign[pos];

		if (throughmode) {
			steps_[numSteps_++] = posstep_through[pos];
			decFmt.posfmt = DEC_FLOAT_3;
		} else {
			if (skinInDecode) {
				steps_[numSteps_++] = posstep_skin[pos];
				decFmt.posfmt = DEC_FLOAT_3;
			} else {
				steps_[numSteps_++] = morphcount == 1 ? posstep[pos] : posstep_morph[pos];

				if (morphcount == 1) {
					// The non-through-mode position formats match the gl formats perfectly, let's use 'em.
					switch (pos) {
					case GE_VTYPE_POS_8BIT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_S8_3; break;
					case GE_VTYPE_POS_16BIT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_S16_3; break;
					case GE_VTYPE_POS_FLOAT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_FLOAT_3; break;
					}
				} else {
					// Actually, temporarily let's not.
					decFmt.posfmt = DEC_FLOAT_3;
				}
			}
		}
		decFmt.posoff = decOff;
		decOff += DecFmtSize(decFmt.posfmt);
	} else {
		ERROR_LOG_REPORT(G3D, "Vertices without position found");
	}

	decFmt.stride = decOff;

	size = align(size, biggest);
	onesize_ = size;
	size *= morphcount;
	DEBUG_LOG(G3D,"SVT : size = %i, aligned to biggest %i", size, biggest);

	// Attempt to JIT as well
	if (jitCache && g_Config.bVertexDecoderJit) {
		jitted_ = jitCache->Compile(*this);
	}
}

void VertexDecoder::DecodeVerts(u8 *decodedptr, const void *verts, int indexLowerBound, int indexUpperBound) const {
	// Decode the vertices within the found bounds, once each
	// decoded_ and ptr_ are used in the steps, so can't be turned into locals for speed.
	decoded_ = decodedptr;
	ptr_ = (const u8*)verts + indexLowerBound * size;

	int count = indexUpperBound - indexLowerBound + 1;
	int stride = decFmt.stride;
	if (jitted_) {
		// We've compiled the steps into optimized machine code, so just jump!
		jitted_(ptr_, decoded_, count);
	} else {
		// Interpret the decode steps
		for (; count; count--) {
			for (int i = 0; i < numSteps_; i++) {
				((*this).*steps_[i])();
			}
			ptr_ += size;
			decoded_ += stride;
		}
	}
}

int VertexDecoder::ToString(char *output) const {
	char * start = output;
	output += sprintf(output, "P: %i ", pos);
	if (nrm)
		output += sprintf(output, "N: %i ", nrm);
	if (col)
		output += sprintf(output, "C: %i ", col);
	if (tc)
		output += sprintf(output, "T: %i ", tc);
	if (weighttype)
		output += sprintf(output, "W: %i ", weighttype);
	if (idx)
		output += sprintf(output, "I: %i ", idx);
	if (morphcount > 1)
		output += sprintf(output, "Morph: %i ", morphcount);
	output += sprintf(output, "Verts: %i ", stats_[STAT_VERTSSUBMITTED]);
	if (throughmode)
		output += sprintf(output, " (through)");

	output += sprintf(output, " (size: %i)", VertexSize());
	return output - start;
}

VertexDecoderJitCache::VertexDecoderJitCache() {
	// 256k should be enough.
	AllocCodeSpace(1024 * 64 * 4);

	// Add some random code to "help" MSVC's buggy disassembler :(
#if defined(_WIN32)
	using namespace Gen;
	for (int i = 0; i < 100; i++) {
		MOV(32, R(EAX), R(EBX));
		RET();
	}
#else
#ifdef ARM
	BKPT(0);
	BKPT(0);
#endif
#endif
}

typedef void (VertexDecoderJitCache::*JitStepFunction)();

struct JitLookup {
	StepFunction func;
	JitStepFunction jitFunc;
};

#ifdef ARM

static const float by128 = 1.0f / 128.0f;
static const float by256 = 1.0f / 256.0f;
static const float by32768 = 1.0f / 32768.0f;

using namespace ArmGen;

static const ARMReg tempReg1 = R3;
static const ARMReg tempReg2 = R4;
static const ARMReg tempReg3 = R5;
static const ARMReg scratchReg = R6;
static const ARMReg scratchReg2 = R7;
static const ARMReg scratchReg3 = R12;
static const ARMReg srcReg = R0;
static const ARMReg dstReg = R1;
static const ARMReg counterReg = R2;
static const ARMReg fpScratchReg = S4;
static const ARMReg fpScratchReg2 = S5;
static const ARMReg fpScratchReg3 = S6;
static const ARMReg fpScratchReg4 = S7;
static const ARMReg fpUscaleReg = S0;
static const ARMReg fpVscaleReg = S1;
static const ARMReg fpUoffsetReg = S2;
static const ARMReg fpVoffsetReg = S3;

// Simpler aliases for NEON. Overlaps with corresponding VFP regs.
static const ARMReg neonUVScaleReg = D0;
static const ARMReg neonUVOffsetReg = D1;
static const ARMReg neonScratchReg = D2;
static const ARMReg neonScratchRegQ = Q1;  // Overlaps with all the scratch regs

// Everything above S6 is fair game for skinning

// S8-S15 are used during matrix generation

// These only live through the matrix multiplication
static const ARMReg src[3] = {S8, S9, S10};  // skin source
static const ARMReg acc[3] = {S11, S12, S13};  // skin accumulator

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoder::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoder::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},

	{&VertexDecoder::Step_WeightsU8Skin, &VertexDecoderJitCache::Jit_WeightsU8Skin},
	{&VertexDecoder::Step_WeightsU16Skin, &VertexDecoderJitCache::Jit_WeightsU16Skin},
	{&VertexDecoder::Step_WeightsFloatSkin, &VertexDecoderJitCache::Jit_WeightsFloatSkin},

	{&VertexDecoder::Step_TcU8, &VertexDecoderJitCache::Jit_TcU8},
	{&VertexDecoder::Step_TcU16, &VertexDecoderJitCache::Jit_TcU16},
	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
	{&VertexDecoder::Step_TcU16Double, &VertexDecoderJitCache::Jit_TcU16Double},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},

	{&VertexDecoder::Step_TcU16Through, &VertexDecoderJitCache::Jit_TcU16Through},
	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},
	{&VertexDecoder::Step_TcU16ThroughDouble, &VertexDecoderJitCache::Jit_TcU16ThroughDouble},

	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoder::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoder::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},

	{&VertexDecoder::Step_NormalS8Skin, &VertexDecoderJitCache::Jit_NormalS8Skin},
	{&VertexDecoder::Step_NormalS16Skin, &VertexDecoderJitCache::Jit_NormalS16Skin},
	{&VertexDecoder::Step_NormalFloatSkin, &VertexDecoderJitCache::Jit_NormalFloatSkin},

	{&VertexDecoder::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	{&VertexDecoder::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoder::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoder::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},

	{&VertexDecoder::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	{&VertexDecoder::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoder::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoder::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8Skin, &VertexDecoderJitCache::Jit_PosS8Skin},
	{&VertexDecoder::Step_PosS16Skin, &VertexDecoderJitCache::Jit_PosS16Skin},
	{&VertexDecoder::Step_PosFloatSkin, &VertexDecoderJitCache::Jit_PosFloatSkin},
};

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec) {
	dec_ = &dec;
	const u8 *start = AlignCode16();

	bool prescaleStep = false;
	bool skinning = false;

	// Look for prescaled texcoord steps
	for (int i = 0; i < dec.numSteps_; i++) {
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8Prescale ||
				dec.steps_[i] == &VertexDecoder::Step_TcU16Prescale ||
				dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescale) {
			prescaleStep = true;
		}
		if (dec.steps_[i] == &VertexDecoder::Step_WeightsU8Skin ||
				dec.steps_[i] == &VertexDecoder::Step_WeightsU16Skin ||
				dec.steps_[i] == &VertexDecoder::Step_WeightsFloatSkin) {
			skinning = true;
		}
	}

	SetCC(CC_AL);

	PUSH(6, R4, R5, R6, R7, R8, _LR);

	// Keep the scale/offset in a few fp registers if we need it.
	if (prescaleStep) {
		MOVI2R(R3, (u32)(&gstate_c.uv), scratchReg);
		VLDR(fpUscaleReg, R3, 0);
		VLDR(fpVscaleReg, R3, 4);
		VLDR(fpUoffsetReg, R3, 8);
		VLDR(fpVoffsetReg, R3, 12);
		if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
			MOVI2F(fpScratchReg, by128, scratchReg);
			VMUL(fpUscaleReg, fpUscaleReg, fpScratchReg);
			VMUL(fpVscaleReg, fpVscaleReg, fpScratchReg);
		} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
			MOVI2F(fpScratchReg, by32768, scratchReg);
			VMUL(fpUscaleReg, fpUscaleReg, fpScratchReg);
			VMUL(fpVscaleReg, fpVscaleReg, fpScratchReg);
		}
	}

	// TODO: NEON skinning register mapping
	// The matrix will be built in Q12-Q15.
	// The temporary matrix to be added to the built matrix will be in Q8-Q11.

	if (skinning) {
		// TODO: Preload scale factors
	}

	JumpTarget loopStart = GetCodePtr();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			// Reset the code ptr and return zero to indicate that we failed.
			SetCodePtr(const_cast<u8 *>(start));
			char temp[1024] = {0};
			dec.ToString(temp);
			INFO_LOG(HLE, "Could not compile vertex decoder: %s", temp);
			return 0;
		}
	}

	ADDI2R(srcReg, srcReg, dec.VertexSize(), scratchReg);
	ADDI2R(dstReg, dstReg, dec.decFmt.stride, scratchReg);
	SUBS(counterReg, counterReg, 1);
	B_CC(CC_NEQ, loopStart);

	POP(6, R4, R5, R6, R7, R8, _PC);

	FlushLitPool();
	FlushIcache();
	// DisassembleArm(start, GetCodePtr() - start);
	// char temp[1024] = {0};
	// dec.ToString(temp);
	// INFO_LOG(HLE, "%s", temp);

	return (JittedVertexDecoder)start;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	// Basic implementation - a byte at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDRB(tempReg1, srcReg, dec_->weightoff + j);
		STRB(tempReg1, dstReg, dec_->decFmt.w0off + j);
	}
	if (j & 3) {
		// Create a zero register. Might want to make a fixed one.
		EOR(scratchReg, scratchReg, scratchReg);
	}
	while (j & 3) {
		STRB(scratchReg, dstReg, dec_->decFmt.w0off + j);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	// Basic implementation - a short at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDRH(tempReg1, srcReg, dec_->weightoff + j * 2);
		STRH(tempReg1, dstReg, dec_->decFmt.w0off + j * 2);
	}
	if (j & 3) {
		// Create a zero register. Might want to make a fixed one.
		EOR(scratchReg, scratchReg, scratchReg);
	}
	while (j & 3) {
		STRH(scratchReg, dstReg, dec_->decFmt.w0off + j * 2);
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDR(tempReg1, srcReg, dec_->weightoff + j * 4);
		STR(tempReg1, dstReg, dec_->decFmt.w0off + j * 4);
	}
	if (j & 3) {
		// Create a zero register. Might want to make a fixed one.
		EOR(scratchReg, scratchReg, scratchReg);
	}
}

static const ARMReg weightRegs[8] = { S8, S9, S10, S11, S12, S13, S14, S15 };

void VertexDecoderJitCache::Jit_ApplyWeights() {
	MOVI2R(tempReg2, (u32)skinMatrix, scratchReg);
#if 1
	// This approach saves a few stores but accesses the matrices in a more
	// sparse order.
	const float *bone = &gstate.boneMatrix[0];
	MOVI2R(tempReg1, (u32)bone, scratchReg);
	for (int i = 0; i < 12; i++) {
		VLDR(fpScratchReg3, tempReg1, i * 4);
		VMUL(fpScratchReg3, fpScratchReg3, weightRegs[0]);
		for (int j = 1; j < dec_->nweights; j++) {
			VLDR(fpScratchReg2, tempReg1, i * 4 + j * 4 * 12);
			VMLA(fpScratchReg3, fpScratchReg2, weightRegs[j]);
		}
		VSTR(fpScratchReg3, tempReg2, i * 4);
	}
#else
	// This one does accesses in linear order but wastes time storing, loading, storing.
	for (int j = 0; j < dec_->nweights; j++) {
		const float *bone = &gstate.boneMatrix[j * 12];
		MOVI2R(tempReg1, (u32)bone, scratchReg);
		// Okay, we have the weight.
		if (j == 0) {
			for (int i = 0; i < 12; i++) {
				VLDR(fpScratchReg2, tempReg1, i * 4);
				VMUL(fpScratchReg2, fpScratchReg2, weightRegs[j]);
				VSTR(fpScratchReg2, tempReg2, i * 4);
			}
		} else {
			for (int i = 0; i < 12; i++) {
				VLDR(fpScratchReg2, tempReg1, i * 4);
				VLDR(fpScratchReg3, tempReg2, i * 4);
				VMLA(fpScratchReg3, fpScratchReg2, weightRegs[j]);
				VSTR(fpScratchReg3, tempReg2, i * 4);
			}
		}
	}
#endif
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
	// No need to zero skinMatrix, we'll just STR to it in the first lap,
	// then VLDR/VADD/VSTR in subsequent laps.
	for (int j = 0; j < dec_->nweights; j++) {
		LDRB(tempReg1, srcReg, dec_->weightoff + j);
		VMOV(fpScratchReg, tempReg1);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
		MOVI2F(fpScratchReg2, by128, scratchReg);
		VMUL(weightRegs[j], fpScratchReg, fpScratchReg2);
	}

	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
	// No need to zero skinMatrix, we'll just STR to it in the first lap,
	// then VLDR/VADD/VSTR in subsequent laps.
	for (int j = 0; j < dec_->nweights; j++) {
		LDRH(tempReg1, srcReg, dec_->weightoff + j * 2);
		VMOV(fpScratchReg, tempReg1);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
		MOVI2F(fpScratchReg2, 1.0f / 32768.0f, scratchReg);
		VMUL(weightRegs[j], fpScratchReg, fpScratchReg2);
	}

	Jit_ApplyWeights();
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
	// No need to zero skinMatrix, we'll just STR to it in the first lap,
	// then VLDR/VADD/VSTR in subsequent laps.
	for (int j = 0; j < dec_->nweights; j++) {
		VLDR(weightRegs[j], srcReg, dec_->weightoff + j * 4);
	}

	Jit_ApplyWeights();
}

// Fill last two bytes with zeroes to align to 4 bytes. LDRH does it for us, handy.
void VertexDecoderJitCache::Jit_TcU8() {
	LDRB(tempReg1, srcReg, dec_->tcoff);
	LDRB(tempReg2, srcReg, dec_->tcoff + 1);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 8));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloat() {
	LDR(tempReg1, srcReg, dec_->tcoff);
	LDR(tempReg2, srcReg, dec_->tcoff + 4);
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
	STR(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16Through() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
	LDR(tempReg1, srcReg, dec_->tcoff);
	LDR(tempReg2, srcReg, dec_->tcoff + 4);
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
	STR(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16Double() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	LSL(tempReg1, tempReg1, 1);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 17));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU16ThroughDouble() {
	LDRH(tempReg1, srcReg, dec_->tcoff);
	LDRH(tempReg2, srcReg, dec_->tcoff + 2);
	LSL(tempReg1, tempReg1, 1);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 17));
	STR(tempReg1, dstReg, dec_->decFmt.uvoff);
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
	if (false && cpu_info.bNEON) {
		// TODO: Needs testing
		ADD(scratchReg, srcReg, dec_->tcoff);
		VLD1_lane(I_16, neonScratchReg, scratchReg, 0, false);
		VMOVL(I_8 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, neonScratchReg, neonScratchReg, neonUVScaleReg);
		VADD(F_32, neonScratchReg, neonScratchReg, neonUVOffsetReg);
		VST1(F_32, neonScratchReg, scratchReg2, 1, ALIGN_NONE);
	} else {
		// TODO: SIMD
		LDRB(tempReg1, srcReg, dec_->tcoff);
		LDRB(tempReg2, srcReg, dec_->tcoff + 1);
		VMOV(fpScratchReg, tempReg1);
		VMOV(fpScratchReg2, tempReg2);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
		VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
		// Could replace VMUL + VADD with VMLA but would require 2 more regs as we don't want to destroy fp*offsetReg. Later.
		VMUL(fpScratchReg, fpScratchReg, fpUscaleReg);
		VMUL(fpScratchReg2, fpScratchReg2, fpVscaleReg);
		VADD(fpScratchReg, fpScratchReg, fpUoffsetReg);
		VADD(fpScratchReg2, fpScratchReg2, fpVoffsetReg);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.uvoff);
		VSTR(fpScratchReg2, dstReg, dec_->decFmt.uvoff + 4);
	}
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
	if (false && cpu_info.bNEON) {
		// TODO: Needs testing
		ADD(scratchReg, srcReg, dec_->tcoff);
		VLD1_lane(I_32, neonScratchReg, scratchReg, 0, false);
		VMOVL(I_16 | I_UNSIGNED, neonScratchRegQ, neonScratchReg);  // Widen to 32-bit
		VCVT(F_32 | I_UNSIGNED, neonScratchRegQ, neonScratchRegQ);
		VMUL(F_32, neonScratchReg, neonScratchReg, neonUVScaleReg);
		VADD(F_32, neonScratchReg, neonScratchReg, neonUVOffsetReg);
		VST1(F_32, neonScratchReg, scratchReg2, 1, ALIGN_NONE);
	} else {
		LDRH(tempReg1, srcReg, dec_->tcoff);
		LDRH(tempReg2, srcReg, dec_->tcoff + 2);
		VMOV(fpScratchReg, tempReg1);
		VMOV(fpScratchReg2, tempReg2);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT);
		VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT);
		VMUL(fpScratchReg, fpScratchReg, fpUscaleReg);
		VMUL(fpScratchReg2, fpScratchReg2, fpVscaleReg);
		VADD(fpScratchReg, fpScratchReg, fpUoffsetReg);
		VADD(fpScratchReg2, fpScratchReg2, fpVoffsetReg);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.uvoff);
		VSTR(fpScratchReg2, dstReg, dec_->decFmt.uvoff + 4);
	}
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
	if (cpu_info.bNEON) {
		ADD(scratchReg, srcReg, dec_->tcoff);
		VLD1(F_32, neonScratchReg, scratchReg, 1, ALIGN_NONE);
		ADD(scratchReg2, dstReg, dec_->decFmt.uvoff);
		VMUL(F_32, neonScratchReg, neonScratchReg, neonUVScaleReg);
		VADD(F_32, neonScratchReg, neonScratchReg, neonUVOffsetReg);
		VST1(F_32, neonScratchReg, scratchReg2, 1, ALIGN_NONE);
	} else {
		// TODO: SIMD
		VLDR(fpScratchReg, srcReg, dec_->tcoff);
		VLDR(fpScratchReg2, srcReg, dec_->tcoff + 4);
		VMUL(fpScratchReg, fpScratchReg, fpUscaleReg);
		VMUL(fpScratchReg2, fpScratchReg2, fpVscaleReg);
		VADD(fpScratchReg, fpScratchReg, fpUoffsetReg);
		VADD(fpScratchReg2, fpScratchReg2, fpVoffsetReg);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.uvoff);
		VSTR(fpScratchReg2, dstReg, dec_->decFmt.uvoff + 4);
	}
}

void VertexDecoderJitCache::Jit_Color8888() {
	LDR(tempReg1, srcReg, dec_->coloff);
	STR(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color4444() {
	LDRH(tempReg1, srcReg, dec_->coloff);

	// Spread out the components.
	ANDI2R(tempReg2, tempReg1, 0x000F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0x00F0, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 4));
	ANDI2R(tempReg3, tempReg1, 0x0F00, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 8));
	ANDI2R(tempReg3, tempReg1, 0xF000, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 12));

	// And saturate.
	ORR(tempReg1, tempReg2, Operand2(tempReg2, ST_LSL, 4));

	STR(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color565() {
	LDRH(tempReg1, srcReg, dec_->coloff);

	// Spread out R and B first.  This puts them in 0x001F001F.
	ANDI2R(tempReg2, tempReg1, 0x001F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0xF800, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 5));

	// Expand 5 -> 8.
	LSL(tempReg3, tempReg2, 3);
	ORR(tempReg2, tempReg3, Operand2(tempReg2, ST_LSR, 2));
	ANDI2R(tempReg2, tempReg2, 0xFFFF00FF, scratchReg);

	// Now finally G.  We start by shoving it into a wall.
	LSR(tempReg1, tempReg1, 5);
	ANDI2R(tempReg1, tempReg1, 0x003F, scratchReg);
	LSL(tempReg3, tempReg1, 2);
	// Don't worry, shifts into a wall.
	ORR(tempReg3, tempReg3, Operand2(tempReg1, ST_LSR, 4));
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 8));

	// Add in full alpha.
	ORI2R(tempReg1, tempReg2, 0xFF000000, scratchReg);

	STR(tempReg1, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_Color5551() {
	LDRH(tempReg1, srcReg, dec_->coloff);

	ANDI2R(tempReg2, tempReg1, 0x001F, scratchReg);
	ANDI2R(tempReg3, tempReg1, 0x07E0, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 3));
	ANDI2R(tempReg3, tempReg1, 0xF800, scratchReg);
	ORR(tempReg2, tempReg2, Operand2(tempReg3, ST_LSL, 6));

	// Expand 5 -> 8.
	LSR(tempReg3, tempReg2, 2);
	// Clean up the bits that were shifted right.
	BIC(tempReg3, tempReg1, AssumeMakeOperand2(0x000000F8));
	BIC(tempReg3, tempReg3, AssumeMakeOperand2(0x0000F800));
	ORR(tempReg2, tempReg3, Operand2(tempReg2, ST_LSL, 3));

	// Now we just need alpha.
	TSTI2R(tempReg1, 0x8000, scratchReg);
	SetCC(CC_NEQ);
	ORI2R(tempReg2, tempReg2, 0xFF000000, scratchReg);
	SetCC(CC_AL);

	STR(tempReg2, dstReg, dec_->decFmt.c0off);
}

void VertexDecoderJitCache::Jit_NormalS8() {
	LDRB(tempReg1, srcReg, dec_->nrmoff);
	LDRB(tempReg2, srcReg, dec_->nrmoff + 1);
	LDRB(tempReg3, srcReg, dec_->nrmoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 8));
	ORR(tempReg1, tempReg1, Operand2(tempReg3, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.nrmoff);

	// Copy 3 bytes and then a zero. Might as well copy four.
	// LDR(tempReg1, srcReg, dec_->nrmoff);
	// ANDI2R(tempReg1, tempReg1, 0x00FFFFFF, scratchReg);
	// STR(tempReg1, dstReg, dec_->decFmt.nrmoff);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	LDRH(tempReg1, srcReg, dec_->nrmoff);
	LDRH(tempReg2, srcReg, dec_->nrmoff + 2);
	LDRH(tempReg3, srcReg, dec_->nrmoff + 4);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.nrmoff);
	STR(tempReg3, dstReg, dec_->decFmt.nrmoff + 4);
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	// Might not be aligned to 4, so we can't use LDMIA.
	// Actually - not true: This will always be aligned. TODO
	LDR(tempReg1, srcReg, dec_->nrmoff);
	LDR(tempReg2, srcReg, dec_->nrmoff + 4);
	LDR(tempReg3, srcReg, dec_->nrmoff + 8);
	// But this is always aligned to 4 so we're safe.
	ADD(scratchReg, dstReg, dec_->decFmt.nrmoff);
	STMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS8Through() {
	// TODO: SIMD
	LDRSB(tempReg1, srcReg, dec_->posoff);
	LDRSB(tempReg2, srcReg, dec_->posoff + 1);
	LDRSB(tempReg3, srcReg, dec_->posoff + 2);
	static const ARMReg tr[3] = { tempReg1, tempReg2, tempReg3 };
	for (int i = 0; i < 3; i++) {
		VMOV(fpScratchReg, tr[i]);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.posoff + i * 4);
	}
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS16Through() {
	// TODO: SIMD
	LDRSH(tempReg1, srcReg, dec_->posoff);
	LDRSH(tempReg2, srcReg, dec_->posoff + 2);
	LDRSH(tempReg3, srcReg, dec_->posoff + 4);
	static const ARMReg tr[3] = { tempReg1, tempReg2, tempReg3 };
	for (int i = 0; i < 3; i++) {
		VMOV(fpScratchReg, tr[i]);
		VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
		VSTR(fpScratchReg, dstReg, dec_->decFmt.posoff + i * 4);
	}
}

// Copy 3 bytes and then a zero. Might as well copy four.
void VertexDecoderJitCache::Jit_PosS8() {
	LDRB(tempReg1, srcReg, dec_->posoff);
	LDRB(tempReg2, srcReg, dec_->posoff + 1);
	LDRB(tempReg3, srcReg, dec_->posoff + 2);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 8));
	ORR(tempReg1, tempReg1, Operand2(tempReg3, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.posoff);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_PosS16() {
	LDRH(tempReg1, srcReg, dec_->posoff);
	LDRH(tempReg2, srcReg, dec_->posoff + 2);
	LDRH(tempReg3, srcReg, dec_->posoff + 4);
	ORR(tempReg1, tempReg1, Operand2(tempReg2, ST_LSL, 16));
	STR(tempReg1, dstReg, dec_->decFmt.posoff);
	STR(tempReg3, dstReg, dec_->decFmt.posoff + 4);
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
	LDR(tempReg1, srcReg, dec_->posoff);
	LDR(tempReg2, srcReg, dec_->posoff + 4);
	LDR(tempReg3, srcReg, dec_->posoff + 8);
	// But this is always aligned to 4 so we're safe.
	ADD(scratchReg, dstReg, dec_->decFmt.posoff);
	STMIA(scratchReg, false, 3, tempReg1, tempReg2, tempReg3);
}

void VertexDecoderJitCache::Jit_NormalS8Skin() {
	LDRSB(tempReg1, srcReg, dec_->nrmoff);
	LDRSB(tempReg2, srcReg, dec_->nrmoff + 1);
	LDRSB(tempReg3, srcReg, dec_->nrmoff + 2);
	VMOV(src[0], tempReg1);
	VMOV(src[1], tempReg2);
	VMOV(src[2], tempReg3);
	MOVI2F(S15, 1.0f/128.0f, scratchReg);
	VCVT(src[0], src[0], TO_FLOAT | IS_SIGNED);
	VCVT(src[1], src[1], TO_FLOAT | IS_SIGNED);
	VCVT(src[2], src[2], TO_FLOAT | IS_SIGNED);
	VMUL(src[0], src[0], S15);
	VMUL(src[1], src[1], S15);
	VMUL(src[2], src[2], S15);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalS16Skin() {
	LDRSH(tempReg1,  srcReg, dec_->nrmoff);
	LDRSH(tempReg2, srcReg, dec_->nrmoff + 2);
	LDRSH(tempReg3, srcReg, dec_->nrmoff + 4);
	VMOV(fpScratchReg, tempReg1);
	VMOV(fpScratchReg2, tempReg2);
	VMOV(fpScratchReg3, tempReg3);
	MOVI2F(S15, 1.0f/32768.0f, scratchReg);
	VCVT(fpScratchReg, fpScratchReg, TO_FLOAT | IS_SIGNED);
	VCVT(fpScratchReg2, fpScratchReg2, TO_FLOAT | IS_SIGNED);
	VCVT(fpScratchReg3, fpScratchReg3, TO_FLOAT | IS_SIGNED);
	VMUL(src[0], fpScratchReg, S15);
	VMUL(src[1], fpScratchReg2, S15);
	VMUL(src[2], fpScratchReg3, S15);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalFloatSkin() {
	VLDR(src[0], srcReg, dec_->nrmoff);
	VLDR(src[1], srcReg, dec_->nrmoff + 4);
	VLDR(src[2], srcReg, dec_->nrmoff + 8);
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_WriteMatrixMul(int outOff, bool pos) {
	MOVI2R(tempReg1, (u32)skinMatrix, scratchReg);
	for (int i = 0; i < 3; i++) {
		VLDR(fpScratchReg, tempReg1, 4 * i);
		VMUL(acc[i], fpScratchReg, src[0]);
	}
	for (int i = 0; i < 3; i++) {
		VLDR(fpScratchReg, tempReg1, 12 + 4 * i);
		VMLA(acc[i], fpScratchReg, src[1]);
	}
	for (int i = 0; i < 3; i++) {
		VLDR(fpScratchReg, tempReg1, 24 + 4 * i);
		VMLA(acc[i], fpScratchReg, src[2]);
	}
	if (pos) {
		for (int i = 0; i < 3; i++) {
			VLDR(fpScratchReg, tempReg1, 36 + 4 * i);
			VADD(acc[i], acc[i], fpScratchReg);
		}
	}
	for (int i = 0; i < 3; i++) {
		VSTR(acc[i], dstReg, outOff + i * 4);
	}
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
	LDRSB(tempReg1, srcReg, dec_->posoff);
	LDRSB(tempReg2, srcReg, dec_->posoff + 1);
	LDRSB(tempReg3, srcReg, dec_->posoff + 2);
	VMOV(src[0], tempReg1);
	VMOV(src[1], tempReg2);
	VMOV(src[2], tempReg3);
	MOVI2F(S15, 1.0f/128.0f, scratchReg);
	VCVT(src[0], src[0], TO_FLOAT | IS_SIGNED);
	VCVT(src[1], src[1], TO_FLOAT | IS_SIGNED);
	VCVT(src[2], src[2], TO_FLOAT | IS_SIGNED);
	VMUL(src[0], src[0], S15);
	VMUL(src[1], src[1], S15);
	VMUL(src[2], src[2], S15);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS16Skin() {
	LDRSH(tempReg1, srcReg, dec_->posoff);
	LDRSH(tempReg2, srcReg, dec_->posoff + 2);
	LDRSH(tempReg3, srcReg, dec_->posoff + 4);
	VMOV(src[0], tempReg1);
	VMOV(src[1], tempReg2);
	VMOV(src[2], tempReg3);
	MOVI2F(S15, 1.0f/32768.0f, scratchReg);
	VCVT(src[0], src[0], TO_FLOAT | IS_SIGNED);
	VCVT(src[1], src[1], TO_FLOAT | IS_SIGNED);
	VCVT(src[2], src[2], TO_FLOAT | IS_SIGNED);
	VMUL(src[0], src[0], S15);
	VMUL(src[1], src[1], S15);
	VMUL(src[2], src[2], S15);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosFloatSkin() {
	VLDR(src[0], srcReg, dec_->posoff);
	VLDR(src[1], srcReg, dec_->posoff + 4);
	VLDR(src[2], srcReg, dec_->posoff + 8);
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

#elif defined(_M_X64) || defined(_M_IX86)

using namespace Gen;

static const float MEMORY_ALIGNED16( by128[4] ) = {
	1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 128.0f
};
static const float MEMORY_ALIGNED16( by256[4] ) = {
	1.0f / 256, 1.0f / 256, 1.0f / 256, 1.0f / 256
};
static const float MEMORY_ALIGNED16( by32768[4] ) = {
	1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f, 1.0f / 32768.0f,
};

static const u32 MEMORY_ALIGNED16( threeMasks[4] ) = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0};
static const u32 MEMORY_ALIGNED16( aOne[4] ) = {0, 0, 0, 0x3F800000};

#ifdef _M_X64
#ifdef _WIN32
static const X64Reg tempReg1 = RAX;
static const X64Reg tempReg2 = R9;
static const X64Reg tempReg3 = R10;
static const X64Reg srcReg = RCX;
static const X64Reg dstReg = RDX;
static const X64Reg counterReg = R8;
#else
static const X64Reg tempReg1 = RAX;
static const X64Reg tempReg2 = R9;
static const X64Reg tempReg3 = R10;
static const X64Reg srcReg = RDI;
static const X64Reg dstReg = RSI;
static const X64Reg counterReg = RDX;
#endif
#else
static const X64Reg tempReg1 = EAX;
static const X64Reg tempReg2 = EBX;
static const X64Reg tempReg3 = EDX;
static const X64Reg srcReg = ESI;
static const X64Reg dstReg = EDI;
static const X64Reg counterReg = ECX;
#endif

// XMM0-XMM5 are volatile on Windows X64
// XMM0-XMM7 are arguments (and thus volatile) on System V ABI (other x64 platforms)
static const X64Reg fpScaleOffsetReg = XMM0;

static const X64Reg fpScratchReg = XMM1;
static const X64Reg fpScratchReg2 = XMM2;
static const X64Reg fpScratchReg3 = XMM3;

// We're gonna keep the current skinning matrix in 4 XMM regs. Fortunately we easily
// have space for that now.

// To debug, just comment them out one at a time until it works. We fall back
// on the interpreter if the compiler fails.

static const JitLookup jitLookup[] = {
	{&VertexDecoder::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoder::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoder::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},

	{&VertexDecoder::Step_WeightsU8Skin, &VertexDecoderJitCache::Jit_WeightsU8Skin},
	{&VertexDecoder::Step_WeightsU16Skin, &VertexDecoderJitCache::Jit_WeightsU16Skin},
	{&VertexDecoder::Step_WeightsFloatSkin, &VertexDecoderJitCache::Jit_WeightsFloatSkin},

	{&VertexDecoder::Step_TcU8, &VertexDecoderJitCache::Jit_TcU8},
	{&VertexDecoder::Step_TcU16, &VertexDecoderJitCache::Jit_TcU16},
	{&VertexDecoder::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},
	{&VertexDecoder::Step_TcU16Double, &VertexDecoderJitCache::Jit_TcU16Double},

	{&VertexDecoder::Step_TcU8Prescale, &VertexDecoderJitCache::Jit_TcU8Prescale},
	{&VertexDecoder::Step_TcU16Prescale, &VertexDecoderJitCache::Jit_TcU16Prescale},
	{&VertexDecoder::Step_TcFloatPrescale, &VertexDecoderJitCache::Jit_TcFloatPrescale},

	{&VertexDecoder::Step_TcU16Through, &VertexDecoderJitCache::Jit_TcU16Through},
	{&VertexDecoder::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},
	{&VertexDecoder::Step_TcU16ThroughDouble, &VertexDecoderJitCache::Jit_TcU16ThroughDouble},

	{&VertexDecoder::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoder::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoder::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},

	{&VertexDecoder::Step_NormalS8Skin, &VertexDecoderJitCache::Jit_NormalS8Skin},
	{&VertexDecoder::Step_NormalS16Skin, &VertexDecoderJitCache::Jit_NormalS16Skin},
	{&VertexDecoder::Step_NormalFloatSkin, &VertexDecoderJitCache::Jit_NormalFloatSkin},

	{&VertexDecoder::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	{&VertexDecoder::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoder::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoder::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},

	{&VertexDecoder::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	{&VertexDecoder::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	{&VertexDecoder::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoder::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoder::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoder::Step_PosS8Skin, &VertexDecoderJitCache::Jit_PosS8Skin},
	{&VertexDecoder::Step_PosS16Skin, &VertexDecoderJitCache::Jit_PosS16Skin},
	{&VertexDecoder::Step_PosFloatSkin, &VertexDecoderJitCache::Jit_PosFloatSkin},
};

// TODO: This should probably be global...
#ifdef _M_X64
#define PTRBITS 64
#else
#define PTRBITS 32
#endif

JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoder &dec) {
	dec_ = &dec;
	const u8 *start = this->GetCodePtr();

#ifdef _M_IX86
	// Store register values
	PUSH(ESI);
	PUSH(EDI);
	PUSH(EBX);
	PUSH(EBP);

	// Read parameters
	int offset = 4;
	MOV(32, R(srcReg), MDisp(ESP, 16 + offset + 0));
	MOV(32, R(dstReg), MDisp(ESP, 16 + offset + 4));
	MOV(32, R(counterReg), MDisp(ESP, 16 + offset + 8));
#endif

	// Save XMM4/XMM5 which apparently can be problematic?
	// Actually, if they are, it must be a compiler bug because they SHOULD be ok.
	// So I won't bother.
	SUB(PTRBITS, R(ESP), Imm8(64));
	MOVUPS(MDisp(ESP, 0), XMM4);
	MOVUPS(MDisp(ESP, 16), XMM5);
	MOVUPS(MDisp(ESP, 32), XMM6);
	MOVUPS(MDisp(ESP, 48), XMM7);

	bool prescaleStep = false;
	// Look for prescaled texcoord steps
	for (int i = 0; i < dec.numSteps_; i++) {
		if (dec.steps_[i] == &VertexDecoder::Step_TcU8Prescale ||
				dec.steps_[i] == &VertexDecoder::Step_TcU16Prescale ||
				dec.steps_[i] == &VertexDecoder::Step_TcFloatPrescale) {
			prescaleStep = true;
		}
	}

	// Add code to convert matrices to 4x4.
	// Later we might want to do this when the matrices are loaded instead.
	// This is mostly proof of concept.
	int boneCount = 0;
	if (dec.weighttype && g_Config.bSoftwareSkinning) {
		for (int i = 0; i < 8; i++) {
			MOVUPS(XMM0, M((void *)(gstate.boneMatrix + 12 * i)));
			MOVUPS(XMM1, M((void *)(gstate.boneMatrix + 12 * i + 3)));
			MOVUPS(XMM2, M((void *)(gstate.boneMatrix + 12 * i + 3 * 2)));
			MOVUPS(XMM3, M((void *)(gstate.boneMatrix + 12 * i + 3 * 3)));
			ANDPS(XMM0, M((void *)&threeMasks));
			ANDPS(XMM1, M((void *)&threeMasks));
			ANDPS(XMM2, M((void *)&threeMasks));
			ANDPS(XMM3, M((void *)&threeMasks));
			ORPS(XMM3, M((void *)&aOne));
			MOVAPS(M((void *)(bones + 16 * i)), XMM0);
			MOVAPS(M((void *)(bones + 16 * i + 4)), XMM1);
			MOVAPS(M((void *)(bones + 16 * i + 8)), XMM2);
			MOVAPS(M((void *)(bones + 16 * i + 12)), XMM3);
		}
	}
	
	// Keep the scale/offset in a few fp registers if we need it.
	if (prescaleStep) {
#ifdef _M_X64
		MOV(64, R(tempReg1), Imm64((u64)(&gstate_c.uv)));
#else
		MOV(32, R(tempReg1), Imm32((u32)(&gstate_c.uv)));
#endif
		MOVSS(fpScaleOffsetReg, MDisp(tempReg1, 0));
		MOVSS(fpScratchReg, MDisp(tempReg1, 4));
		UNPCKLPS(fpScaleOffsetReg, R(fpScratchReg));
		if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_8BIT) {
			MULPS(fpScaleOffsetReg, M((void *)&by128));
		} else if ((dec.VertexType() & GE_VTYPE_TC_MASK) == GE_VTYPE_TC_16BIT) {
			MULPS(fpScaleOffsetReg, M((void *)&by32768));
		}
		MOVSS(fpScratchReg, MDisp(tempReg1, 8));
		MOVSS(fpScratchReg2, MDisp(tempReg1, 12));
		UNPCKLPS(fpScratchReg, R(fpScratchReg2));
		UNPCKLPD(fpScaleOffsetReg, R(fpScratchReg));
	}

	// Let's not bother with a proper stack frame. We just grab the arguments and go.
	JumpTarget loopStart = GetCodePtr();
	for (int i = 0; i < dec.numSteps_; i++) {
		if (!CompileStep(dec, i)) {
			// Reset the code ptr and return zero to indicate that we failed.
			SetCodePtr(const_cast<u8 *>(start));
			return 0;
		}
	}

	ADD(PTRBITS, R(srcReg), Imm32(dec.VertexSize()));
	ADD(PTRBITS, R(dstReg), Imm32(dec.decFmt.stride));
	SUB(32, R(counterReg), Imm8(1));
	J_CC(CC_NZ, loopStart, true);

	MOVUPS(XMM4, MDisp(ESP, 0));
	MOVUPS(XMM5, MDisp(ESP, 16));
	MOVUPS(XMM6, MDisp(ESP, 32));
	MOVUPS(XMM7, MDisp(ESP, 48));
	ADD(PTRBITS, R(ESP), Imm8(64));

#ifdef _M_IX86
	// Restore register values
	POP(EBP);
	POP(EBX);
	POP(EDI);
	POP(ESI);
#endif

	RET();

	return (JittedVertexDecoder)start;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	switch (dec_->nweights) {
	case 1:
		MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 2:
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 3:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		AND(32, R(tempReg1), Imm32(0x00FFFFFF));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 4:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		return;
	case 8:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->weightoff + 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w1off), R(tempReg2));
		return;
	}

	// Basic implementation - a byte at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		MOV(8, R(tempReg1), MDisp(srcReg, dec_->weightoff + j));
		MOV(8, MDisp(dstReg, dec_->decFmt.w0off + j), R(tempReg1));
	}
	while (j & 3) {
		MOV(8, MDisp(dstReg, dec_->decFmt.w0off + j), Imm8(0));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	switch (dec_->nweights) {
	case 1:
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), Imm32(0));
		return;

	case 2:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), Imm32(0));
		return;

	case 3:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->weightoff + 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), R(tempReg2));
		return;
	
	case 4:
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff));
		MOV(32, R(tempReg2), MDisp(srcReg, dec_->weightoff + 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off), R(tempReg1));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + 4), R(tempReg2));
		return;
	}

	// Basic implementation - a short at a time. TODO: Optimize
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		MOV(16, R(tempReg1), MDisp(srcReg, dec_->weightoff + j * 2));
		MOV(16, MDisp(dstReg, dec_->decFmt.w0off + j * 2), R(tempReg1));
	}
	while (j & 3) {
		MOV(16, MDisp(dstReg, dec_->decFmt.w0off + j * 2), Imm16(0));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		MOV(32, R(tempReg1), MDisp(srcReg, dec_->weightoff + j * 4));
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + j * 4), R(tempReg1));
	}
	while (j & 3) {  // Zero additional weights rounding up to 4.
		MOV(32, MDisp(dstReg, dec_->decFmt.w0off + j * 4), Imm32(0));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU8Skin() {
#ifdef _M_X64
	MOV(PTRBITS, R(tempReg2), Imm64((uintptr_t)&bones));
#else
	MOV(PTRBITS, R(tempReg2), Imm32((uintptr_t)&bones));
#endif
	for (int j = 0; j < dec_->nweights; j++) {
		MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->weightoff + j));
		CVTSI2SS(XMM1, R(tempReg1));
		MULSS(XMM1, M((void *)&by128));
		SHUFPS(XMM1, R(XMM1), _MM_SHUFFLE(0, 0, 0, 0));
		if (j == 0) {
			MOVAPS(XMM4, MDisp(tempReg2, 0));
			MOVAPS(XMM5, MDisp(tempReg2, 16));
			MULPS(XMM4, R(XMM1));
			MULPS(XMM5, R(XMM1));
			MOVAPS(XMM6, MDisp(tempReg2, 32));
			MOVAPS(XMM7, MDisp(tempReg2, 48));
			MULPS(XMM6, R(XMM1));
			MULPS(XMM7, R(XMM1));
		} else {
			MOVAPS(XMM2, MDisp(tempReg2, 0));
			MOVAPS(XMM3, MDisp(tempReg2, 16));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM4, R(XMM2));
			ADDPS(XMM5, R(XMM3));
			MOVAPS(XMM2, MDisp(tempReg2, 32));
			MOVAPS(XMM3, MDisp(tempReg2, 48));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM6, R(XMM2));
			ADDPS(XMM7, R(XMM3));
		}
		ADD(PTRBITS, R(tempReg2), Imm8(4 * 16));
	}
}

void VertexDecoderJitCache::Jit_WeightsU16Skin() {
#ifdef _M_X64
	MOV(PTRBITS, R(tempReg2), Imm64((uintptr_t)&bones));
#else
	MOV(PTRBITS, R(tempReg2), Imm32((uintptr_t)&bones));
#endif
	for (int j = 0; j < dec_->nweights; j++) {
		MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->weightoff + j * 2));
		CVTSI2SS(XMM1, R(tempReg1));
		MULSS(XMM1, M((void *)&by32768));
		SHUFPS(XMM1, R(XMM1), _MM_SHUFFLE(0, 0, 0, 0));
		if (j == 0) {
			MOVAPS(XMM4, MDisp(tempReg2, 0));
			MOVAPS(XMM5, MDisp(tempReg2, 16));
			MULPS(XMM4, R(XMM1));
			MULPS(XMM5, R(XMM1));
			MOVAPS(XMM6, MDisp(tempReg2, 32));
			MOVAPS(XMM7, MDisp(tempReg2, 48));
			MULPS(XMM6, R(XMM1));
			MULPS(XMM7, R(XMM1));
		} else {
			MOVAPS(XMM2, MDisp(tempReg2, 0));
			MOVAPS(XMM3, MDisp(tempReg2, 16));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM4, R(XMM2));
			ADDPS(XMM5, R(XMM3));
			MOVAPS(XMM2, MDisp(tempReg2, 32));
			MOVAPS(XMM3, MDisp(tempReg2, 48));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM6, R(XMM2));
			ADDPS(XMM7, R(XMM3));
		}
		ADD(PTRBITS, R(tempReg2), Imm8(4 * 16));
	}
}

void VertexDecoderJitCache::Jit_WeightsFloatSkin() {
#ifdef _M_X64
	MOV(PTRBITS, R(tempReg2), Imm64((uintptr_t)&bones));
#else
	MOV(PTRBITS, R(tempReg2), Imm32((uintptr_t)&bones));
#endif
	for (int j = 0; j < dec_->nweights; j++) {
		MOVSS(XMM1, MDisp(srcReg, dec_->weightoff + j * 4));
		SHUFPS(XMM1, R(XMM1), _MM_SHUFFLE(0, 0, 0, 0));
		if (j == 0) {
			MOVAPS(XMM4, MDisp(tempReg2, 0));
			MOVAPS(XMM5, MDisp(tempReg2, 16));
			MULPS(XMM4, R(XMM1));
			MULPS(XMM5, R(XMM1));
			MOVAPS(XMM6, MDisp(tempReg2, 32));
			MOVAPS(XMM7, MDisp(tempReg2, 48));
			MULPS(XMM6, R(XMM1));
			MULPS(XMM7, R(XMM1));
		} else {
			MOVAPS(XMM2, MDisp(tempReg2, 0));
			MOVAPS(XMM3, MDisp(tempReg2, 16));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM4, R(XMM2));
			ADDPS(XMM5, R(XMM3));
			MOVAPS(XMM2, MDisp(tempReg2, 32));
			MOVAPS(XMM3, MDisp(tempReg2, 48));
			MULPS(XMM2, R(XMM1));
			MULPS(XMM3, R(XMM1));
			ADDPS(XMM6, R(XMM2));
			ADDPS(XMM7, R(XMM3));
		}
		ADD(PTRBITS, R(tempReg2), Imm8(4 * 16));
	}
}

// Fill last two bytes with zeroes to align to 4 bytes. MOVZX does it for us, handy.
void VertexDecoderJitCache::Jit_TcU8() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcU16() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcU16Double() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->tcoff + 2));
	SHL(16, R(tempReg1), Imm8(1));  // 16 to get a wall to shift into
	SHL(32, R(tempReg2), Imm8(17));
	OR(32, R(tempReg1), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcFloat() {
#ifdef _M_X64
	MOV(64, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(64, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
#else
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->tcoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff + 4), R(tempReg2));
#endif
}

void VertexDecoderJitCache::Jit_TcU8Prescale() {
	// TODO: The first five instructions could be done in 1 or 2 in SSE4
	MOVZX(32, 8, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOVZX(32, 8, tempReg2, MDisp(srcReg, dec_->tcoff + 1));
	CVTSI2SS(fpScratchReg, R(tempReg1));
	CVTSI2SS(fpScratchReg2, R(tempReg2));
	UNPCKLPS(fpScratchReg, R(fpScratchReg2));
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcU16Prescale() {
	PXOR(fpScratchReg2, R(fpScratchReg2));
	MOVD_xmm(fpScratchReg, MDisp(srcReg, dec_->tcoff));
	PUNPCKLWD(fpScratchReg, R(fpScratchReg2));
	CVTDQ2PS(fpScratchReg, R(fpScratchReg));
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcFloatPrescale() {
	MOVQ_xmm(fpScratchReg, MDisp(srcReg, dec_->tcoff));
	MULPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	ADDPS(fpScratchReg, R(fpScaleOffsetReg));
	SHUFPS(fpScaleOffsetReg, R(fpScaleOffsetReg), _MM_SHUFFLE(1, 0, 3, 2));
	MOVQ_xmm(MDisp(dstReg, dec_->decFmt.uvoff), fpScratchReg);
}

void VertexDecoderJitCache::Jit_TcU16Through() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcU16ThroughDouble() {
	MOVZX(32, 16, tempReg1, MDisp(srcReg, dec_->tcoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->tcoff + 2));
	SHL(16, R(tempReg1), Imm8(1));  // 16 to get a wall to shift into
	SHL(32, R(tempReg2), Imm8(17));
	OR(32, R(tempReg1), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
#ifdef _M_X64
	MOV(64, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(64, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
#else
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->tcoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->tcoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.uvoff + 4), R(tempReg2));
#endif
}

void VertexDecoderJitCache::Jit_Color8888() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->coloff));
	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg1));
}

static const u32 MEMORY_ALIGNED16(nibbles[4]) = { 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, 0x0f0f0f0f, };


void VertexDecoderJitCache::Jit_Color4444() {
	// Needs benchmarking. A bit wasteful by only using 1 SSE lane.
#if 0
	// Alternate approach
	MOVD_xmm(XMM3, MDisp(srcReg, dec_->coloff));
	MOVAPS(XMM2, R(XMM3));
	MOVAPS(XMM1, M((void *)nibbles));
	PSLLD(XMM2, 4);
	PAND(XMM3, R(XMM1));
	PAND(XMM2, R(XMM1));
	PSRLD(XMM2, 4);
	PXOR(XMM1, R(XMM1));
	PUNPCKLBW(XMM2, R(XMM1));
	PUNPCKLBW(XMM3, R(XMM1));
	PSLLD(XMM2, 4);
	POR(XMM3, R(XMM2));
	MOVAPS(XMM2, R(XMM3));
	PSLLD(XMM2, 4);
	POR(XMM3, R(XMM2));
	MOVD_xmm(MDisp(dstReg, dec_->decFmt.c0off), XMM3);
	return;
#endif

	MOV(32, R(tempReg1), MDisp(srcReg, dec_->coloff));

	// 0000ABGR, copy R and double forwards.
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x0000000F));
	MOV(32, R(tempReg2), R(tempReg3));
	SHL(32, R(tempReg3), Imm8(4));
	OR(32, R(tempReg2), R(tempReg3));

	// tempReg1 -> 00ABGR00, then double G backwards.
	SHL(32, R(tempReg1), Imm8(8));
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x0000F000));
	OR(32, R(tempReg2), R(tempReg3));
	SHR(32, R(tempReg3), Imm8(4));
	OR(32, R(tempReg2), R(tempReg3));

	// Now do B forwards again (still 00ABGR00.)
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x000F0000));
	OR(32, R(tempReg2), R(tempReg3));
	SHL(32, R(tempReg3), Imm8(4));
	OR(32, R(tempReg2), R(tempReg3));

	// tempReg1 -> ABGR0000, then double A backwards.
	SHL(32, R(tempReg1), Imm8(8));
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0xF0000000));
	OR(32, R(tempReg2), R(tempReg3));
	SHR(32, R(tempReg3), Imm8(4));
	OR(32, R(tempReg2), R(tempReg3));

	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg2));
}

void VertexDecoderJitCache::Jit_Color565() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->coloff));

	MOV(32, R(tempReg2), R(tempReg1));
	AND(32, R(tempReg2), Imm32(0x0000001F));

	// B (we do R and B at the same time, they're both 5.)
	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x0000F800));
	SHL(32, R(tempReg3), Imm8(5));
	OR(32, R(tempReg2), R(tempReg3));

	// Expand 5 -> 8.  At this point we have 00BB00RR.
	MOV(32, R(tempReg3), R(tempReg2));
	SHL(32, R(tempReg2), Imm8(3));
	SHR(32, R(tempReg3), Imm8(2));
	OR(32, R(tempReg2), R(tempReg3));
	AND(32, R(tempReg2), Imm32(0x00FF00FF));

	// Now's as good a time to put in A as any.
	OR(32, R(tempReg2), Imm32(0xFF000000));

	// Last, we need to align, extract, and expand G.
	// 3 to align to G, and then 2 to expand to 8.
	SHL(32, R(tempReg1), Imm8(3 + 2));
	AND(32, R(tempReg1), Imm32(0x0000FC00));
	MOV(32, R(tempReg3), R(tempReg1));
	// 2 to account for tempReg1 being preshifted, 4 for expansion.
	SHR(32, R(tempReg3), Imm8(2 + 4));
	OR(32, R(tempReg1), R(tempReg3));
	AND(32, R(tempReg1), Imm32(0x0000FF00));
	OR(32, R(tempReg2), R(tempReg1));

	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg2));
}

void VertexDecoderJitCache::Jit_Color5551() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->coloff));

	MOV(32, R(tempReg2), R(tempReg1));
	AND(32, R(tempReg2), Imm32(0x0000001F));

	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x000003E0));
	SHL(32, R(tempReg3), Imm8(3));
	OR(32, R(tempReg2), R(tempReg3));

	MOV(32, R(tempReg3), R(tempReg1));
	AND(32, R(tempReg3), Imm32(0x00007C00));
	SHL(32, R(tempReg3), Imm8(6));
	OR(32, R(tempReg2), R(tempReg3));

	// Expand 5 -> 8.  After this is just A.
	MOV(32, R(tempReg3), R(tempReg2));
	SHL(32, R(tempReg2), Imm8(3));
	SHR(32, R(tempReg3), Imm8(2));
	// Chop off the bits that were shifted out.
	AND(32, R(tempReg3), Imm32(0x00070707));
	OR(32, R(tempReg2), R(tempReg3));

	// For A, we shift it to a single bit, and then subtract and XOR.
	// That's probably the simplest way to expand it...
	SHR(32, R(tempReg1), Imm8(15));
	// If it was 0, it's now -1, otherwise it's 0.  Easy.
	SUB(32, R(tempReg1), Imm8(1));
	XOR(32, R(tempReg1), Imm32(0xFF000000));
	AND(32, R(tempReg1), Imm32(0xFF000000));
	OR(32, R(tempReg2), R(tempReg1));

	MOV(32, MDisp(dstReg, dec_->decFmt.c0off), R(tempReg2));
}

// Copy 3 bytes and then a zero. Might as well copy four.
void VertexDecoderJitCache::Jit_NormalS8() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
	AND(32, R(tempReg1), Imm32(0x00FFFFFF));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->nrmoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 4), R(tempReg2));
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->nrmoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->nrmoff + 4));
	MOV(32, R(tempReg3), MDisp(srcReg, dec_->nrmoff + 8));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 4), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.nrmoff + 8), R(tempReg3));
}

// This could be a bit shorter with AVX 3-operand instructions and FMA.
void VertexDecoderJitCache::Jit_WriteMatrixMul(int outOff, bool pos) {
	MOVAPS(XMM1, R(XMM3));
	SHUFPS(XMM1, R(XMM1), _MM_SHUFFLE(0, 0, 0, 0));
	MULPS(XMM1, R(XMM4));
	MOVAPS(XMM2, R(XMM3));
	SHUFPS(XMM2, R(XMM2), _MM_SHUFFLE(1, 1, 1, 1));
	MULPS(XMM2, R(XMM5));
	ADDPS(XMM1, R(XMM2));
	MOVAPS(XMM2, R(XMM3));
	SHUFPS(XMM2, R(XMM2), _MM_SHUFFLE(2, 2, 2, 2));
	MULPS(XMM2, R(XMM6));
	ADDPS(XMM1, R(XMM2));
	if (pos) {
		ADDPS(XMM1, R(XMM7));
	}
	MOVUPS(MDisp(dstReg, outOff), XMM1);
}

void VertexDecoderJitCache::Jit_NormalS8Skin() {
	XORPS(XMM3, R(XMM3));
	MOVD_xmm(XMM1, MDisp(srcReg, dec_->nrmoff));
	PUNPCKLBW(XMM1, R(XMM3));
	PUNPCKLWD(XMM1, R(XMM3));
	PSLLD(XMM1, 24);
	PSRAD(XMM1, 24); // Ugly sign extension, can be done faster in SSE4
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M((void *)&by128));
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16Skin() {
	XORPS(XMM3, R(XMM3));
	MOVQ_xmm(XMM1, MDisp(srcReg, dec_->nrmoff));
	PUNPCKLWD(XMM1, R(XMM3));
	PSLLD(XMM1, 16);
	PSRAD(XMM1, 16); // Ugly sign extension, can be done faster in SSE4
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M((void *)&by32768));
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

void VertexDecoderJitCache::Jit_NormalFloatSkin() {
	MOVUPS(XMM3, MDisp(srcReg, dec_->nrmoff));
	Jit_WriteMatrixMul(dec_->decFmt.nrmoff, false);
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS8Through() {
	// TODO: SIMD
	for (int i = 0; i < 3; i++) {
		MOVSX(32, 8, tempReg1, MDisp(srcReg, dec_->posoff + i));
		CVTSI2SS(fpScratchReg, R(tempReg1));
		MOVSS(MDisp(dstReg, dec_->decFmt.posoff + i * 4), fpScratchReg);
	}
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS16Through() {
	XORPS(XMM3, R(XMM3));
	MOVQ_xmm(XMM1, MDisp(srcReg, dec_->posoff));
	PUNPCKLWD(XMM1, R(XMM3));
	PSLLD(XMM1, 16);
	PSRAD(XMM1, 16); // Ugly sign extension, can be done faster in SSE4
	CVTDQ2PS(XMM3, R(XMM1));
	MOVUPS(MDisp(dstReg, dec_->decFmt.posoff), XMM3);
}

// Copy 3 bytes and then a zero. Might as well copy four.
void VertexDecoderJitCache::Jit_PosS8() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
	AND(32, R(tempReg1), Imm32(0x00FFFFFF));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_PosS16() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
	MOVZX(32, 16, tempReg2, MDisp(srcReg, dec_->posoff + 4));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 4), R(tempReg2));
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
	MOV(32, R(tempReg1), MDisp(srcReg, dec_->posoff));
	MOV(32, R(tempReg2), MDisp(srcReg, dec_->posoff + 4));
	MOV(32, R(tempReg3), MDisp(srcReg, dec_->posoff + 8));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff), R(tempReg1));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 4), R(tempReg2));
	MOV(32, MDisp(dstReg, dec_->decFmt.posoff + 8), R(tempReg3));
}

void VertexDecoderJitCache::Jit_PosS8Skin() {
	XORPS(XMM3, R(XMM3));
	MOVD_xmm(XMM1, MDisp(srcReg, dec_->posoff));
	PUNPCKLBW(XMM1, R(XMM3));
	PUNPCKLWD(XMM1, R(XMM3));
	PSLLD(XMM1, 24);
	PSRAD(XMM1, 24); // Ugly sign extension, can be done faster in SSE4
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M((void *)&by128));
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

void VertexDecoderJitCache::Jit_PosS16Skin() {
	XORPS(XMM3, R(XMM3));
	MOVQ_xmm(XMM1, MDisp(srcReg, dec_->posoff));
	PUNPCKLWD(XMM1, R(XMM3));
	PSLLD(XMM1, 16);
	PSRAD(XMM1, 16); // Ugly sign extension, can be done faster in SSE4
	CVTDQ2PS(XMM3, R(XMM1));
	MULPS(XMM3, M((void *)&by32768));
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloatSkin() {
	MOVUPS(XMM3, MDisp(srcReg, dec_->posoff));
	Jit_WriteMatrixMul(dec_->decFmt.posoff, true);
}

#elif defined(PPC)

#error This should not be built for PowerPC, at least not yet.

#endif

bool VertexDecoderJitCache::CompileStep(const VertexDecoder &dec, int step) {
	// See if we find a matching JIT function
	for (size_t i = 0; i < ARRAY_SIZE(jitLookup); i++) {
		if (dec.steps_[step] == jitLookup[i].func) {
			((*this).*jitLookup[i].jitFunc)();
			return true;
		}
	}
	return false;
}
