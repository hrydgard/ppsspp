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
#include "Core/HDRemaster.h"
#include "Core/Reporting.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Math3D.h"

#include "VertexDecoder.h"
#include "VertexShaderGenerator.h"

static const u8 tcsize[4] = {0,2,4,8}, tcalign[4] = {0,1,2,4};
static const u8 colsize[8] = {0,0,0,0,2,2,2,4}, colalign[8] = {0,0,0,0,2,2,2,4};
static const u8 nrmsize[4] = {0,3,6,12}, nrmalign[4] = {0,1,2,4};
static const u8 possize[4] = {3,3,6,12}, posalign[4] = {1,1,2,4};
static const u8 wtsize[4] = {0,1,2,4}, wtalign[4] = {0,1,2,4};

// When software skinning. This array is only used when non-jitted - when jitted, the matrix
// is kept in registers.
static float MEMORY_ALIGNED16(skinMatrix[12]);

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
	// Always full alpha.
}

void VertexDecoder::Step_Color5551() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(u16*)(ptr_ + coloff);
	c[0] = Convert5To8(cdata & 0x1f);
	c[1] = Convert5To8((cdata>>5) & 0x1f);
	c[2] = Convert5To8((cdata>>10) & 0x1f);
	c[3] = (cdata >> 15) ? 255 : 0;
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && c[3] != 0;
}

void VertexDecoder::Step_Color4444() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(u16*)(ptr_ + coloff);
	for (int j = 0; j < 4; j++)
		c[j] = Convert4To8((cdata >> (j * 4)) & 0xF);
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && c[3] == 255;
}

void VertexDecoder::Step_Color8888() const
{
	u8 *c = decoded_ + decFmt.c0off;
	const u8 *cdata = (const u8*)(ptr_ + coloff);
	memcpy(c, cdata, sizeof(u8) * 4);
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && c[3] == 255;
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
		c[i] = clamp_u8((int)col[i]);
	}
	c[3] = 255;
	// Always full alpha.
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
		c[i] = clamp_u8((int)col[i]);
	}
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && c[3] == 255;
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
		c[i] = clamp_u8((int)col[i]);
	}
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && c[3] == 255;
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
		c[i] = clamp_u8((int)col[i]);
	}
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && c[3] == 255;
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
	const u16 *uv = (const u16*)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = uv[2];
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
	&VertexDecoder::Step_PosS8,
	&VertexDecoder::Step_PosS8,
	&VertexDecoder::Step_PosS16,
	&VertexDecoder::Step_PosFloat,
};

static const StepFunction posstep_skin[4] = {
	&VertexDecoder::Step_PosS8Skin,
	&VertexDecoder::Step_PosS8Skin,
	&VertexDecoder::Step_PosS16Skin,
	&VertexDecoder::Step_PosFloatSkin,
};

static const StepFunction posstep_morph[4] = {
	&VertexDecoder::Step_PosS8Morph,
	&VertexDecoder::Step_PosS8Morph,
	&VertexDecoder::Step_PosS16Morph,
	&VertexDecoder::Step_PosFloatMorph,
};

static const StepFunction posstep_through[4] = {
	&VertexDecoder::Step_PosS8Through,
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
		DEBUG_LOG_REPORT_ONCE(vtypeM, G3D,"VTYPE with morph used: THRU=%i TC=%i COL=%i POS=%i NRM=%i WT=%i NW=%i IDX=%i MC=%i", (int)throughmode, tc,col,pos,nrm,weighttype,nweights,idx,morphcount);
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

		// NOTE: That we check getUVGenMode here means that we must include it in the decoder ID!
		if (g_Config.bPrescaleUV && !throughmode && (gstate.getUVGenMode() == 0 || gstate.getUVGenMode() == 3)) {
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

	if (!pos) {
		ERROR_LOG_REPORT(G3D, "Vertices without position found");
		pos = 1;
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
	}

	decFmt.stride = decOff;

	size = align(size, biggest);
	onesize_ = size;
	size *= morphcount;
	DEBUG_LOG(G3D,"SVT : size = %i, aligned to biggest %i", size, biggest);

	// Attempt to JIT as well
	if (jitCache && g_Config.bVertexDecoderJit) {
		jitted_ = jitCache->Compile(*this);
		if (!jitted_) {
			WARN_LOG(G3D, "Vertex decoder JIT failed! fmt = %08x", fmt_);
		}
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

#if defined(PPC)

#error This should not be built for PowerPC, at least not yet.

#endif
