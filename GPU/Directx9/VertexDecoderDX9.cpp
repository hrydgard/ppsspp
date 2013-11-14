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

#include "math/lin/matrix4x4.h"

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"

#include "GPU/Directx9/VertexDecoderDX9.h"
#include "GPU/Directx9/VertexShaderGeneratorDX9.h"

namespace DX9 {


// Always use float for decoding data
#define USE_WEIGHT_HACK
#define USE_TC_HACK
#define USE_PPC_VTX_JIT 0

static const u8 tcsize[4] = {0,2,4,8}, tcalign[4] = {0,1,2,4};
static const u8 colsize[8] = {0,0,0,0,2,2,2,4}, colalign[8] = {0,0,0,0,2,2,2,4};
static const u8 nrmsize[4] = {0,3,6,12}, nrmalign[4] = {0,1,2,4};
static const u8 possize[4] = {0,3,6,12}, posalign[4] = {0,1,2,4};
static const u8 wtsize[4] = {0,1,2,4}, wtalign[4] = {0,1,2,4};

inline int align(int n, int align) {
	return (n + (align - 1)) & ~(align - 1);
}

#if 0
// This is what the software transform spits out, and thus w
DecVtxFormat GetTransformedVtxFormat(const DecVtxFormat &fmt) {
	DecVtxFormat tfm = {0};
	int size = 0;
	int offset = 0;
	// Weights disappear during transform.
	if (fmt.uvfmt) {
		// UV always becomes float2.
		tfm.uvfmt = DEC_FLOAT_2;
		tfm.uvoff = offset;
		offset += DecFmtSize(tfm.uvfmt);
	}
	// We always (?) get two colors out, they're floats (although we'd probably be fine with less precision).
	tfm.c0fmt = DEC_FLOAT_4;
	tfm.c0off = offset;
	offset += DecFmtSize(tfm.c0fmt);
	tfm.c1fmt = DEC_FLOAT_3;  // color1 (specular) doesn't have alpha.
	tfm.c1off = offset;
	offset += DecFmtSize(tfm.c1fmt);
	// We never get a normal, it's gone.
	// But we do get a position, and it's always float3.
	tfm.posfmt = DEC_FLOAT_3;
	tfm.posoff = offset;
	offset += DecFmtSize(tfm.posfmt);
	// Update stride.
	tfm.stride = offset;
	return tfm;
}
#endif


VertexDecoderDX9::VertexDecoderDX9() : coloff(0), nrmoff(0), posoff(0), jitted_(0) {
	memset(stats_, 0, sizeof(stats_));
}

void VertexDecoderDX9::Step_WeightsU8() const
{
#ifdef USE_WEIGHT_HACK
	float *wt = (float *)(decoded_ + decFmt.w0off);
	const u8 *wdata = (const u8*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		wt[j] = wdata[j];
		wt[j] *= (1.0f/255.f);
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
#else
	u8 *wt = (u8 *)(decoded_ + decFmt.w0off);
	const u8 *wdata = (const u8*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++)
		wt[j] = wdata[j];
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
#endif
}

void VertexDecoderDX9::Step_WeightsU16() const
{
#ifdef USE_WEIGHT_HACK
	float *wt = (float *)(decoded_  + decFmt.w0off);
	const u16_le *wdata = (const u16_le*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		wt[j] =wdata[j];
		wt[j] *= (1.0f/65535.f);
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
#else
	u16 *wt = (u16 *)(decoded_  + decFmt.w0off);
	const u16_le *wdata = (const u16_le*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++)
		wt[j] =wdata[j];
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
#endif
}

// Float weights should be uncommon, we can live with having to multiply these by 2.0
// to avoid special checks in the vertex shader generator.
// (PSP uses 0.0-2.0 fixed point numbers for weights)
void VertexDecoderDX9::Step_WeightsFloat() const
{
#if 0
	float *wt = (float *)(decoded_ + decFmt.w0off);
	const float_le *wdata = (const float_le*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		wt[j] = wdata[j];
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0.0f;
#else
	float *wt = (float *)(decoded_ + decFmt.w0off);
	u32 *st = (u32 *)wt;
	const u32_le *wdata = (const u32_le*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		st[j] = wdata[j];
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0.0f;
#endif
}

void VertexDecoderDX9::Step_TcU8() const
{
#ifndef USE_TC_HACK
	u8 *uv = (u8 *)(decoded_ + decFmt.uvoff);
	const u8 *uvdata = (const u8*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
#else
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u8 *uvdata = (const u8*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
	uv[0] *= (1.0f/255.f);
	uv[1] *= (1.0f/255.f);
#endif
}

void VertexDecoderDX9::Step_TcU16() const
{
	u16 *uv = (u16 *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
}

void VertexDecoderDX9::Step_TcU16Double() const
{
	u16 *uv = (u16 *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le*)(ptr_ + tcoff);
	*uv = *uvdata;
	uv[0] = uvdata[0] * 2;
	uv[1] = uvdata[1] * 2;
}

void VertexDecoderDX9::Step_TcU16Through() const
{
	u16 *uv = (u16 *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
}

void VertexDecoderDX9::Step_TcU16ThroughDouble() const
{
	u16 *uv = (u16 *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0] * 2;
	uv[1] = uvdata[1] * 2;
}

void VertexDecoderDX9::Step_TcFloat() const
{
#if 0 // Swapping float is more heavy as swapping u32
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float_le *uvdata = (const float_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
#else
	u32 *uv = (u32 *)(decoded_ + decFmt.uvoff);
	const u32_le *uvdata = (const u32_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
#endif
}

void VertexDecoderDX9::Step_TcFloatThrough() const
{
#if 0 // Swapping float is more heavy as swapping u32
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float_le *uvdata = (const float_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
#else
	u32 *uv = (u32 *)(decoded_ + decFmt.uvoff);
	const u32_le *uvdata = (const u32_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
#endif
}

void VertexDecoderDX9::Step_TcU8Prescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u8 *uvdata = (const u8 *)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] * (1.f / 128.f) * gstate_c.uv.uScale + gstate_c.uv.uOff;
	uv[1] = (float)uvdata[1] * (1.f / 128.f) * gstate_c.uv.vScale + gstate_c.uv.vOff;
}

void VertexDecoderDX9::Step_TcU16Prescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le *)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] * (1.f / 32768.f) * gstate_c.uv.uScale + gstate_c.uv.uOff;
	uv[1] = (float)uvdata[1] * (1.f / 32768.f) * gstate_c.uv.vScale + gstate_c.uv.vOff;
}

void VertexDecoderDX9::Step_TcFloatPrescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float_le *uvdata = (const float_le*)(ptr_ + tcoff);
	uv[0] = uvdata[0] * gstate_c.uv.uScale + gstate_c.uv.uOff;
	uv[1] = uvdata[1] * gstate_c.uv.vScale + gstate_c.uv.vOff;
}

void VertexDecoderDX9::Step_Color565() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = (u16)(*(u16_le*)(ptr_ + coloff));

	c[0] = 255;
	c[1] = Convert5To8(cdata & 0x1f);
	c[2] = Convert6To8((cdata>>5) & 0x3f);
	c[3] = Convert5To8((cdata>>11) & 0x1f);
}

void VertexDecoderDX9::Step_Color5551() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = (u16)(*(u16_le*)(ptr_ + coloff));
	c[0] = (cdata >> 15) ? 255 : 0;
	c[1] = Convert5To8(cdata & 0x1f);
	c[2] = Convert5To8((cdata>>5) & 0x1f);
	c[3] = Convert5To8((cdata>>10) & 0x1f);
}

void VertexDecoderDX9::Step_Color4444() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = (u16)(*(u16_le*)(ptr_ + coloff));
	c[0] =  Convert4To8((cdata >> (12)) & 0xF);
	c[1] =  Convert4To8((cdata >> (0)) & 0xF);
	c[2] =  Convert4To8((cdata >> (4)) & 0xF);
	c[3] =  Convert4To8((cdata >> (8)) & 0xF);
}

void VertexDecoderDX9::Step_Color8888() const
{
 	// Directx want ARGB
	u8 *c = (u8*)(decoded_ + decFmt.c0off);
	const u8 *cdata = (const u8*)(ptr_ + coloff);
	c[0] = cdata[3];
	c[1] = cdata[0];
	c[2] = cdata[1];
	c[3] = cdata[2];
}

void VertexDecoderDX9::Step_Color565Morph() const
{
	float col[3] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];		
		u16 cdata = (u16)(*(u16_le*)(ptr_ + onesize_*n + coloff));

		col[0] += w * (cdata & 0x1f) * (255.0f / 31.0f);
		col[1] += w * ((cdata>>5) & 0x3f) * (255.0f / 63.0f);
		col[2] += w * ((cdata>>11) & 0x1f) * (255.0f / 31.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	// Dx want ARGB
	c[0] = 255;
	c[1] = (u8)col[0];
	c[2] = (u8)col[1];
	c[3] = (u8)col[2];
}

void VertexDecoderDX9::Step_Color5551Morph() const
{
	float col[4] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = (u16)(*(u16_le*)(ptr_ + onesize_*n + coloff));
		col[0] += w * (cdata & 0x1f) * (255.0f / 31.0f);
		col[1] += w * ((cdata>>5) & 0x1f) * (255.0f / 31.0f);
		col[2] += w * ((cdata>>10) & 0x1f) * (255.0f / 31.0f);
		col[3] += w * ((cdata>>15) ? 255.0f : 0.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	// Dx want ARGB
	c[0] = (u8)col[3];
	c[1] = (u8)col[0];
	c[2] = (u8)col[1];
	c[3] = (u8)col[2];
}

void VertexDecoderDX9::Step_Color4444Morph() const
{
	float col[4] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = (u16)(*(u16_le*)(ptr_ + onesize_*n + coloff));
		for (int j = 0; j < 4; j++)
			col[j] += w * ((cdata >> (j * 4)) & 0xF) * (255.0f / 15.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	// Dx want ARGB
	c[0] = (u8)col[3];
	c[1] = (u8)col[0];
	c[2] = (u8)col[1];
	c[3] = (u8)col[2];
}

void VertexDecoderDX9::Step_Color8888Morph() const
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
	
	// Dx want ARGB
	c[0] = (u8)col[3];
	c[1] = (u8)col[0];
	c[2] = (u8)col[1];
	c[3] = (u8)col[2];
}

void VertexDecoderDX9::Step_NormalS8() const
{
#if 0
	s8 *normal = (s8 *)(decoded_ + decFmt.nrmoff);
	u8 xorval = 0;
	if (gstate.reversenormals & 1)
		xorval = 0xFF;  // Using xor instead of - to handle -128
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = sv[j] ^ xorval;
	normal[3] = 0;
#else
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	u8 xorval = 0;
	if (gstate.reversenormals & 1)
		xorval = 0xFF;  // Using xor instead of - to handle -128
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = (float)(sv[j] ^ xorval) * (1.0f/127.f);
	normal[3] = 0;
#endif
}

void VertexDecoderDX9::Step_NormalS16() const
{
	s16 *normal = (s16 *)(decoded_ + decFmt.nrmoff);
	u16 xorval = 0;
	if (gstate.reversenormals & 1)
		xorval = 0xFFFF;
	const s16_le *sv = (const s16_le*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = sv[j] ^ xorval;
	normal[3] = 0;
}

void VertexDecoderDX9::Step_NormalFloat() const
{
#if 0 // Swapping float is more heavy as swapping u32
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	float multiplier = 1.0f;
	if (gstate.reversenormals & 1)
		multiplier = -multiplier;
	const float_le *fv = (const float_le*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = fv[j] * multiplier;
#else
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const float *fv = (const float*)(ptr_ + nrmoff);

	u32 *v = (u32 *)normal;
	const u32_le *sv = (const u32_le*)fv;

	for (int j = 0; j < 3; j++) 
		v[j] = sv[j];

	float multiplier = 1.0f;
	if (gstate.reversenormals & 1) {
		multiplier = -multiplier;
		for (int j = 0; j < 3; j++)
			normal[j] = normal[j] * multiplier;
	}
#endif
}

void VertexDecoderDX9::Step_NormalS8Morph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		if (gstate.reversenormals & 1) {
			multiplier = -multiplier;
		}
		const s8 *bv = (const s8*)(ptr_ + onesize_*n + nrmoff);
		multiplier *= (1.0f/127.0f);
		for (int j = 0; j < 3; j++)
			normal[j] += bv[j] * multiplier;
	}
}

void VertexDecoderDX9::Step_NormalS16Morph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		if (gstate.reversenormals & 1) {
			multiplier = -multiplier;
		}
		const s16_le *sv = (const s16_le *)(ptr_ + onesize_*n + nrmoff);
		multiplier *= (1.0f/32767.0f);
		for (int j = 0; j < 3; j++)
			normal[j] += sv[j] * multiplier;
	}
}

void VertexDecoderDX9::Step_NormalFloatMorph() const
{
#if 0 // Swapping float is more heavy as swapping u32
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		if (gstate.reversenormals & 1) {
			multiplier = -multiplier;
		}
		const float_le *fv = (const float_le*)(ptr_ + onesize_*n + nrmoff);
		for (int j = 0; j < 3; j++)
			normal[j] += fv[j] * multiplier;
	}
#else
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	u32 *v = (u32 *)normal;
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		
		const float *fv = (const float*)(ptr_ + onesize_*n + nrmoff);
		const u32_le *sv = (const u32_le*)fv;
		for (int j = 0; j < 3; j++) {
			v[j] = sv[j];
		}

		if (gstate.reversenormals & 1) {
			multiplier = -multiplier;
			for (int j = 0; j < 3; j++) {
				normal[j] += normal[j] * multiplier;
			}
		}		
	}

#endif
}

void VertexDecoderDX9::Step_PosS8() const
{
#if 0
	s8 *v = (s8 *)(decoded_ + decFmt.posoff);
	const s8 *sv = (const s8*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j];
	v[3] = 0;
#else
	float *v = (float *)(decoded_ + decFmt.posoff);
	const s8 *sv = (const s8*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = (float)sv[j] * 1.0f / 127.0f;
	v[3] = 0;
#endif
}

void VertexDecoderDX9::Step_PosS16() const
{
	s16 *v = (s16 *)(decoded_ + decFmt.posoff);
	const s16_le *sv = (const s16_le*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j];
	v[3] = 0;
}

void VertexDecoderDX9::Step_PosFloat() const
{
#if 0 // Swapping float is more heavy as swapping u32
	float *v = (float *)(decoded_ + decFmt.posoff);
	const float_le *sv = (const float_le*)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = sv[2];
#else
	u32 *v = (u32 *)(decoded_ + decFmt.posoff);
	const u32_le *sv = (const u32_le*)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = sv[2];
#endif
}

void VertexDecoderDX9::Step_PosS8Through() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	const s8 *sv = (const s8*)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = sv[2];
	v[3] = 0;
}

void VertexDecoderDX9::Step_PosS16Through() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	const s16_le *sv = (const s16_le*)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = sv[2];
	v[3] = 0;
}

void VertexDecoderDX9::Step_PosFloatThrough() const
{
#if 0// Swapping float is more heavy as swapping u32
	float *v = (float *)(decoded_ + decFmt.posoff);
	const float_le *fv = (const float_le*)(ptr_ + posoff);
	v[0] = fv[0];
	v[1] = fv[1];
	v[2] = fv[2];
	v[3] = 0;
#else
	u32 *v = (u32 *)(decoded_ + decFmt.posoff);
	const u32_le *fv = (const u32_le*)(ptr_ + posoff);
	v[0] = fv[0];
	v[1] = fv[1];
	v[2] = fv[2];
	v[3] = 0;
#endif
}

void VertexDecoderDX9::Step_PosS8Morph() const
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

void VertexDecoderDX9::Step_PosS16Morph() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		float multiplier = 1.0f / 32767.0f;
		const s16_le *sv = (const s16_le*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += (float)sv[j] * (multiplier * gstate_c.morphWeights[n]);
	}
}

void VertexDecoderDX9::Step_PosFloatMorph() const
{
#if 0 // Swapping float is more heavy as swapping u32
	float *v = (float *)(decoded_ + decFmt.posoff);
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const float_le *fv = (const float_le*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += fv[j] * gstate_c.morphWeights[n];
	}
#else
	float *pos = (float *)(decoded_ + decFmt.posoff);
	u32 tmp_[4];
	float * tmpf_ =(float*)tmp_;

	memset(pos, 0, sizeof(float) * 3);

	for (int n = 0; n < morphcount; n++) {
		const u32_le *spos = (const u32_le*)(ptr_ + onesize_*n + posoff);

		for (int j = 0; j < 3; j++) {
			tmp_[j] = spos[j];
			pos[j] += tmpf_[j] * gstate_c.morphWeights[n];
		}
	}
#endif
}

static const StepFunction wtstep[4] = {
	0,
	&VertexDecoderDX9::Step_WeightsU8,
	&VertexDecoderDX9::Step_WeightsU16,
	&VertexDecoderDX9::Step_WeightsFloat,
};

static const StepFunction tcstep[4] = {
	0,
	&VertexDecoderDX9::Step_TcU8,
	&VertexDecoderDX9::Step_TcU16,
	&VertexDecoderDX9::Step_TcFloat,
};

static const StepFunction tcstep_prescale[4] = {
	0,
	&VertexDecoderDX9::Step_TcU8Prescale,
	&VertexDecoderDX9::Step_TcU16Prescale,
	&VertexDecoderDX9::Step_TcFloatPrescale,
};

static const StepFunction tcstep_through[4] = {
	0,
	&VertexDecoderDX9::Step_TcU8,
	&VertexDecoderDX9::Step_TcU16Through,
	&VertexDecoderDX9::Step_TcFloatThrough,
};

// Some HD Remaster games double the u16 texture coordinates.
static const StepFunction tcstep_Remaster[4] = {
	0,
	&VertexDecoderDX9::Step_TcU8,
	&VertexDecoderDX9::Step_TcU16Double,
	&VertexDecoderDX9::Step_TcFloat,
};

static const StepFunction tcstep_through_Remaster[4] = {
	0,
	&VertexDecoderDX9::Step_TcU8,
	&VertexDecoderDX9::Step_TcU16ThroughDouble,
	&VertexDecoderDX9::Step_TcFloatThrough,
};

// TODO: Tc Morph

static const StepFunction colstep[8] = {
	0, 0, 0, 0,
	&VertexDecoderDX9::Step_Color565,
	&VertexDecoderDX9::Step_Color5551,
	&VertexDecoderDX9::Step_Color4444,
	&VertexDecoderDX9::Step_Color8888,
};

static const StepFunction colstep_morph[8] = {
	0, 0, 0, 0,
	&VertexDecoderDX9::Step_Color565Morph,
	&VertexDecoderDX9::Step_Color5551Morph,
	&VertexDecoderDX9::Step_Color4444Morph,
	&VertexDecoderDX9::Step_Color8888Morph,
};

static const StepFunction nrmstep[4] = {
	0,
	&VertexDecoderDX9::Step_NormalS8,
	&VertexDecoderDX9::Step_NormalS16,
	&VertexDecoderDX9::Step_NormalFloat,
};

static const StepFunction nrmstep_morph[4] = {
	0,
	&VertexDecoderDX9::Step_NormalS8Morph,
	&VertexDecoderDX9::Step_NormalS16Morph,
	&VertexDecoderDX9::Step_NormalFloatMorph,
};

static const StepFunction posstep[4] = {
	0,
	&VertexDecoderDX9::Step_PosS8,
	&VertexDecoderDX9::Step_PosS16,
	&VertexDecoderDX9::Step_PosFloat,
};

static const StepFunction posstep_morph[4] = {
	0,
	&VertexDecoderDX9::Step_PosS8Morph,
	&VertexDecoderDX9::Step_PosS16Morph,
	&VertexDecoderDX9::Step_PosFloatMorph,
};

static const StepFunction posstep_through[4] = {
	0,
	&VertexDecoderDX9::Step_PosS8Through,
	&VertexDecoderDX9::Step_PosS16Through,
	&VertexDecoderDX9::Step_PosFloatThrough,
};


void VertexDecoderDX9::SetVertexType(u32 fmt, VertexDecoderJitCache *jitCache) {
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

	DEBUG_LOG(G3D,"VTYPE: THRU=%i TC=%i COL=%i POS=%i NRM=%i WT=%i NW=%i IDX=%i MC=%i", (int)throughmode, tc,col,pos,nrm,weighttype,nweights,idx,morphcount);

	if (weighttype) { // && nweights?
		weightoff = size;
		//size = align(size, wtalign[weighttype]);	unnecessary
		size += wtsize[weighttype] * nweights;
		if (wtalign[weighttype] > biggest)
			biggest = wtalign[weighttype];

		steps_[numSteps_++] = wtstep[weighttype];

#ifndef USE_WEIGHT_HACK
		int fmtBase = DEC_FLOAT_1;
		if (weighttype == GE_VTYPE_WEIGHT_8BIT >> GE_VTYPE_WEIGHT_SHIFT) {
			fmtBase = DEC_U8_1;
		} else if (weighttype == GE_VTYPE_WEIGHT_16BIT >> GE_VTYPE_WEIGHT_SHIFT) {
			fmtBase = DEC_U16_1;
		} else if (weighttype == GE_VTYPE_WEIGHT_FLOAT >> GE_VTYPE_WEIGHT_SHIFT) {
			fmtBase = DEC_FLOAT_1;
		}
#else
		// Hack
		int fmtBase = DEC_FLOAT_1;
#endif

		int numWeights = TranslateNumBonesDX9(nweights);

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

	if (tc) {
		size = align(size, tcalign[tc]);
		tcoff = size;
		size += tcsize[tc];
		if (tcalign[tc] > biggest)
			biggest = tcalign[tc];

		if (g_Config.bPrescaleUV && !throughmode && gstate.getTextureFunction() == 0) {
			steps_[numSteps_++] = tcstep_prescale[tc];
			decFmt.uvfmt = DEC_FLOAT_2;
		} else {
			if (g_DoubleTextureCoordinates)
				steps_[numSteps_++] = throughmode ? tcstep_through_Remaster[tc] : tcstep_Remaster[tc];
			else
		steps_[numSteps_++] = throughmode ? tcstep_through[tc] : tcstep[tc];

		switch (tc) {
		case GE_VTYPE_TC_8BIT >> GE_VTYPE_TC_SHIFT:
#ifdef USE_TC_HACK			
			decFmt.uvfmt = DEC_FLOAT_2;
#else
			decFmt.uvfmt = throughmode ? DEC_U8A_2 : DEC_U8_2;
#endif
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

		steps_[numSteps_++] = morphcount == 1 ? nrmstep[nrm] : nrmstep_morph[nrm];

		if (morphcount == 1) {
			// The normal formats match the gl formats perfectly, let's use 'em.
			switch (nrm) {
			//case GE_VTYPE_NRM_8BIT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_S8_3; break;
				case GE_VTYPE_NRM_8BIT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_FLOAT_3; break;
			case GE_VTYPE_NRM_16BIT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_S16_3; break;
			case GE_VTYPE_NRM_FLOAT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_FLOAT_3; break;
			}
		} else {
			decFmt.nrmfmt = DEC_FLOAT_3;
		}

		// Actually, temporarily let's not.
		decFmt.nrmoff = decOff;
		decOff += DecFmtSize(decFmt.nrmfmt);
	}

	if (pos)  // there's always a position
	{
		size = align(size, posalign[pos]);
		posoff = size;
		size += possize[pos];
		if (posalign[pos] > biggest)
			biggest = posalign[pos];

		if (throughmode) {
			steps_[numSteps_++] = posstep_through[pos];
			decFmt.posfmt = DEC_FLOAT_3;
		} else {
			steps_[numSteps_++] = morphcount == 1 ? posstep[pos] : posstep_morph[pos];

			if (morphcount == 1) {
				// The non-through-mode position formats match the gl formats perfectly, let's use 'em.
				switch (pos) {
				//case GE_VTYPE_POS_8BIT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_S8_3; break;
				case GE_VTYPE_POS_8BIT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_FLOAT_3; break;
				case GE_VTYPE_POS_16BIT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_S16_3; break;
				case GE_VTYPE_POS_FLOAT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_FLOAT_3; break;
				}
			} else {
				// Actually, temporarily let's not.
				decFmt.posfmt = DEC_FLOAT_3;
			}
		}
		decFmt.posoff = decOff;
		decOff += DecFmtSize(decFmt.posfmt);
	} else
		ERROR_LOG_REPORT(G3D, "Vertices without position found") 
		
	decFmt.stride = decOff;

	size = align(size, biggest);
	onesize_ = size;
	size *= morphcount;
	DEBUG_LOG(G3D,"SVT : size = %i, aligned to biggest %i", size, biggest);
	
#if defined(PPC) && USE_PPC_VTX_JIT
	// Attempt to JIT as well
	if (jitCache) {
		jitted_ = jitCache->Compile(*this);
	}
#endif
}

void GetIndexBoundsDX9(void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound) {
	// Find index bounds. Could cache this in display lists.
	// Also, this could be greatly sped up with SSE2/NEON, although rarely a bottleneck.
	int lowerBound = 0x7FFFFFFF;
	int upperBound = 0;
	u32 idx = vertType & GE_VTYPE_IDX_MASK;
	if (idx == GE_VTYPE_IDX_8BIT) {
		const u8 *ind8 = (const u8 *)inds;
		for (int i = 0; i < count; i++) {
			if (ind8[i] > upperBound)
				upperBound = ind8[i];
			if (ind8[i] < lowerBound)
				lowerBound = ind8[i];
		}
	} else if (idx == GE_VTYPE_IDX_16BIT) {
		const u16 *ind16 = (const u16*)inds;
		for (int i = 0; i < count; i++) {
			if (ind16[i] > upperBound)
				upperBound = ind16[i];
			if (ind16[i] < lowerBound)
				lowerBound = ind16[i];
		}
	} else {
		lowerBound = 0;
		upperBound = count - 1;
	}
	*indexLowerBound = (u16)lowerBound;
	*indexUpperBound = (u16)upperBound;
}

void VertexDecoderDX9::DecodeVerts(u8 *decodedptr, const void *verts, int indexLowerBound, int indexUpperBound) const {
	// Decode the vertices within the found bounds, once each
	// decoded_ and ptr_ are used in the steps, so can't be turned into locals for speed.
	decoded_ = decodedptr;
	ptr_ = (const u8*)verts + indexLowerBound * size;

	int count = indexUpperBound - indexLowerBound + 1;
	int stride = decFmt.stride;
#if defined(PPC) && USE_PPC_VTX_JIT
	if (jitted_) {
		// We've compiled the steps into optimized machine code, so just jump!
		jitted_(ptr_, decoded_, count);
	} else 
#endif
	{
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

// TODO: Does not support morphs, skinning etc.
u32 VertexDecoderDX9::InjectUVs(u8 *decoded, const void *verts, float *customuv, int count) const {
	u32 customVertType = (gstate.vertType & ~GE_VTYPE_TC_MASK) | GE_VTYPE_TC_FLOAT;
	VertexDecoderDX9 decOut;
	decOut.SetVertexType(customVertType);
	
	const u8 *inp = (const u8 *)verts;
	u8 *out = decoded;
	for (int i = 0; i < count; i++) {
		if (pos) memcpy(out + decOut.posoff, inp + posoff, possize[pos]);
		if (nrm) memcpy(out + decOut.nrmoff, inp + nrmoff, nrmsize[nrm]);
		if (col) memcpy(out + decOut.coloff, inp + coloff, colsize[col]);
		// Ignore others for now, this is all we need for puzbob.
		// Inject!
		memcpy(out + decOut.tcoff, &customuv[i * 2], tcsize[decOut.tc]);
		inp += this->onesize_;
		out += decOut.onesize_;
	}
	return customVertType;
}

int VertexDecoderDX9::ToString(char *output) const {
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



#if defined(PPC) && USE_PPC_VTX_JIT

VertexDecoderJitCache::VertexDecoderJitCache() {
	// 64k should be enough.
	AllocCodeSpace(1024 * 64);

	// Add some random code to "help" MSVC's buggy disassembler :(
	/* really needed ? */
	Break();
	Break();
}

typedef void (VertexDecoderJitCache::*JitStepFunction)();

struct JitLookup {
	StepFunction func;
	JitStepFunction jitFunc;
};


using namespace PpcGen;

static const PPCReg srcReg = R20;
static const PPCReg dstReg = R21;
static const PPCReg counterReg = R22;

static const PPCReg tempReg1 = R23;
static const PPCReg tempReg2 = R24;
static const PPCReg tempReg3 = R25;
static const PPCReg scratchReg = R26;

static const PPCReg fpScratchReg = FPR26; 

static const PPCReg fprU8bitd = FPR18; // 1./255.f
static const PPCReg fprU16bitd = FPR19; // 1./65535.f
static const PPCReg fprS8bitd = FPR16; // 1./127.f
static const PPCReg fprS16bitd = FPR17; // 1./32767.f
static const PPCReg fprZero = FPR15; 

static const JitLookup jitLookup[] = {
	{&VertexDecoderDX9::Step_WeightsU8, &VertexDecoderJitCache::Jit_WeightsU8},
	{&VertexDecoderDX9::Step_WeightsU16, &VertexDecoderJitCache::Jit_WeightsU16},
	{&VertexDecoderDX9::Step_WeightsFloat, &VertexDecoderJitCache::Jit_WeightsFloat},

	{&VertexDecoderDX9::Step_TcU8, &VertexDecoderJitCache::Jit_TcU8},
	{&VertexDecoderDX9::Step_TcU16, &VertexDecoderJitCache::Jit_TcU16},
	{&VertexDecoderDX9::Step_TcFloat, &VertexDecoderJitCache::Jit_TcFloat},

	{&VertexDecoderDX9::Step_TcU16Through, &VertexDecoderJitCache::Jit_TcU16Through},
	{&VertexDecoderDX9::Step_TcFloatThrough, &VertexDecoderJitCache::Jit_TcFloatThrough},

	{&VertexDecoderDX9::Step_NormalS8, &VertexDecoderJitCache::Jit_NormalS8},
	{&VertexDecoderDX9::Step_NormalS16, &VertexDecoderJitCache::Jit_NormalS16},
	{&VertexDecoderDX9::Step_NormalFloat, &VertexDecoderJitCache::Jit_NormalFloat},

	{&VertexDecoderDX9::Step_Color8888, &VertexDecoderJitCache::Jit_Color8888},
	{&VertexDecoderDX9::Step_Color4444, &VertexDecoderJitCache::Jit_Color4444},
	{&VertexDecoderDX9::Step_Color565, &VertexDecoderJitCache::Jit_Color565},
	{&VertexDecoderDX9::Step_Color5551, &VertexDecoderJitCache::Jit_Color5551},

	{&VertexDecoderDX9::Step_PosS8Through, &VertexDecoderJitCache::Jit_PosS8Through},
	{&VertexDecoderDX9::Step_PosS16Through, &VertexDecoderJitCache::Jit_PosS16Through},
	{&VertexDecoderDX9::Step_PosFloatThrough, &VertexDecoderJitCache::Jit_PosFloat},

	{&VertexDecoderDX9::Step_PosS8, &VertexDecoderJitCache::Jit_PosS8},
	{&VertexDecoderDX9::Step_PosS16, &VertexDecoderJitCache::Jit_PosS16},
	{&VertexDecoderDX9::Step_PosFloat, &VertexDecoderJitCache::Jit_PosFloat},
};

/** Called like this jitted_(r3, r4, r5); **/
JittedVertexDecoder VertexDecoderJitCache::Compile(const VertexDecoderDX9 &dec) {
	//return 0;

	dec_ = &dec;
	const u8 *start = this->GetCodePtr();

	// TODO: Test and make work
	Prologue();

	// Copy some regs
	MR(srcReg, R3);
	MR(dstReg, R4);
	MR(counterReg, R5);

	// Store some values
	MOVI2F(fprU8bitd, (1.f/255.f));
	MOVI2F(fprU16bitd, (1.f/65535.f));	
	MOVI2F(fprS8bitd, (1.f/127.f));
	MOVI2F(fprS16bitd, (1.f/32767.f));	
	MOVI2F(fprZero, 0.f);	

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

	ADDI(srcReg, srcReg, dec.VertexSize());
	ADDI(dstReg, dstReg, dec.decFmt.stride);

	// counterReg --;
	ADDI(counterReg, counterReg, -1); // SUBI
	
	// if (counterReg!=0) => loopStart
	CMPI(counterReg, 0);
	BNE(loopStart);

	Epilogue();

	// Return
	BLR();

	FlushIcache();

	// DisassembleArm(start, GetCodePtr() - start);
	// char temp[1024] = {0};
	// dec.ToString(temp);
	// INFO_LOG(HLE, "%s", temp);

	return (JittedVertexDecoder)start;
}

void VertexDecoderJitCache::Jit_WeightsU8() {
	// Basic implementation - a byte at a time. TODO: Optimize
	/* TODO */
#if 0
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LDRB(tempReg1, srcReg, dec_->weightoff + j);
		STRB(tempReg1, dstReg, dec_->decFmt.w0off + j);
	}
	while (j & 3) {
		STf(scratchReg, dstReg, dec_->decFmt.w0off + j);
		j++;
	}
#endif
	
	volatile u64 tmp1, tmp2, tmp3, tmp4;

	int j;
	for (j = 0; j < dec_->nweights; j++) {
		LBZ(tempReg1, srcReg, dec_->weightoff + j);
		
		// Save it in tmp memory 
		MOVI2R(R7, (u32)&tmp1);
		STD(tempReg1, R7);

		// Load as float
		LFD(tempReg1, R7, 0);

		// Convert
		FCFID(tempReg1, tempReg1);
		
		// Double to simple
		FRSP(tempReg1, tempReg1);

		// Mult by (1.0f/255.f)
		FMULS(tempReg1, tempReg1, fprU8bitd);
		
		// Save
		SFS(tempReg1, dstReg, (dec_->decFmt.w0off + (j * 4)));
	}

	while (j & 3) {
		SFS(fprZero, dstReg, dec_->decFmt.w0off + (j*4));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsU16() {
	volatile u64 tmp1, tmp2, tmp3, tmp4;
	int j;
	for (j = 0; j < dec_->nweights; j++) {
		MOVI2R(scratchReg, dec_->weightoff + (j *2));
		LHBRX(tempReg1, srcReg, scratchReg);
		
		// Save it in tmp memory 
		MOVI2R(R7, (u32)&tmp1);
		STD(tempReg1, R7);

		// Load as float
		LFD(tempReg1, R7, 0);

		// Convert
		FCFID(tempReg1, tempReg1);
		
		// Double to simple
		FRSP(tempReg1, tempReg1);

		// Mult by (1.0f/65335.f)
		FMULS(tempReg1, tempReg1, fprU16bitd);
		
		// Save
		SFS(tempReg1, dstReg, (dec_->decFmt.w0off + (j * 4)));
	}

	while (j & 3) {
		SFS(fprZero, dstReg, dec_->decFmt.w0off + (j*4));
		j++;
	}
}

void VertexDecoderJitCache::Jit_WeightsFloat() {
	int j;
	MOVI2R(scratchReg, dec_->weightoff);
	for (j = 0; j < dec_->nweights; j++) {
		ADDI(scratchReg, scratchReg, 4);
		LWBRX(tempReg1, srcReg, scratchReg);
		STW(tempReg1, dstReg, dec_->decFmt.w0off + j * 4);
	}
	while (j & 3) {  // Zero additional weights rounding up to 4.
		SFS(fprZero, dstReg, dec_->decFmt.w0off + j * 4);
		j++;
	}
}

// Fill last two bytes with zeroes to align to 4 bytes. LDRH does it for us, handy.
void VertexDecoderJitCache::Jit_TcU8() {
	volatile u64 tmp1, tmp2, tmp3, tmp4;

	LBZ(tempReg1, srcReg, dec_->tcoff + 0);
	LBZ(tempReg2, srcReg, dec_->tcoff + 1);
	
	// Save it in tmp memory 
	MOVI2R(R7, (u32)&tmp1);
	MOVI2R(R8, (u32)&tmp2);

	STD(tempReg1, R7);
	STD(tempReg2, R8);

	// Load as float
	LFD(tempReg1, R7, 0);
	LFD(tempReg2, R8, 0);

	// Convert
	FCFID(tempReg1, tempReg1);
	FCFID(tempReg2, tempReg2);

	// Double to simple
	FRSP(tempReg1, tempReg1);
	FRSP(tempReg2, tempReg2);

	// Mult by (1.0f/255.f)
	FMULS(tempReg1, tempReg1, fprU8bitd);
	FMULS(tempReg2, tempReg2, fprU8bitd);

	// Save
	SFS(tempReg1, dstReg, (dec_->decFmt.uvoff + 0));
	SFS(tempReg2, dstReg, (dec_->decFmt.uvoff + 4));
}

void VertexDecoderJitCache::Jit_TcU16() {
	/* Todo */
	/** optimize ! **/
	MOVI2R(R7, dec_->tcoff + 0);
	MOVI2R(R8, dec_->tcoff + 2);
	LHBRX(tempReg1, srcReg, R7);
	LHBRX(tempReg2, srcReg, R8);
	STH(tempReg1, dstReg, dec_->decFmt.uvoff + 0);
	STH(tempReg2, dstReg, dec_->decFmt.uvoff + 2);
}

void VertexDecoderJitCache::Jit_TcFloat() {
	/** Todo vmx ... **/

	MOVI2R(scratchReg, dec_->tcoff);
	LWBRX(tempReg1, srcReg, scratchReg);

	// scratchReg = dec_->tcoff + 4
	ADDI(scratchReg, scratchReg, 4);
	LWBRX(tempReg2, srcReg, scratchReg);
	
	STW(tempReg1, dstReg, dec_->decFmt.uvoff);
	STW(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_TcU16Through() {
	/** optimize ! **/
	MOVI2R(R7, dec_->tcoff + 0);
	MOVI2R(R8, dec_->tcoff + 2);
	LHBRX(tempReg1, srcReg, R7);
	LHBRX(tempReg2, srcReg, R8);
	STH(tempReg1, dstReg, dec_->decFmt.uvoff + 0);
	STH(tempReg2, dstReg, dec_->decFmt.uvoff + 2);
}

void VertexDecoderJitCache::Jit_TcFloatThrough() {
	MOVI2R(scratchReg, dec_->tcoff + 0);
	LWBRX(tempReg1, srcReg, scratchReg);

	ADDI(scratchReg, scratchReg, 4);
	LWBRX(tempReg2, srcReg, scratchReg);

	STW(tempReg1, dstReg, dec_->decFmt.uvoff + 4);
	STW(tempReg2, dstReg, dec_->decFmt.uvoff + 4);
}

void VertexDecoderJitCache::Jit_Color8888() {
	/** Todo optimize ... **/
	LBZ(tempReg1, srcReg, dec_->coloff + 3);
	LBZ(tempReg2, srcReg, dec_->coloff + 0);
	STB(tempReg1, dstReg, dec_->decFmt.c0off + 0);
	STB(tempReg2, dstReg, dec_->decFmt.c0off + 1);

	LBZ(tempReg1, srcReg, dec_->coloff + 1);
	LBZ(tempReg2, srcReg, dec_->coloff + 2);
	STB(tempReg1, dstReg, dec_->decFmt.c0off + 2);
	STB(tempReg2, dstReg, dec_->decFmt.c0off + 3);
	/*
	// Wrong !!! should use rlwim
	
	MOVI2R(scratchReg, dec_->coloff);
	LWBRX(tempReg1, srcReg, scratchReg);	
	STW(tempReg1, dstReg, dec_->decFmt.c0off);
	*/
}

void VertexDecoderJitCache::Jit_Color4444() {
	/** todo **/
}

void VertexDecoderJitCache::Jit_Color565() {
	/** todo **/
}

void VertexDecoderJitCache::Jit_Color5551() {
	/** todo **/
}

void VertexDecoderJitCache::Jit_NormalS8() {
	volatile u64 tmp1, tmp2, tmp3, tmp4;

	LBZ(tempReg1, srcReg, dec_->nrmoff + 0);
	LBZ(tempReg2, srcReg, dec_->nrmoff + 1);
	LBZ(tempReg3, srcReg, dec_->nrmoff + 2);
	
	EXTSB(tempReg1,tempReg1);
	EXTSB(tempReg2,tempReg2);
	EXTSB(tempReg3,tempReg3);
	
	// Save it in tmp memory 
	MOVI2R(R7, (u32)&tmp1);
	MOVI2R(R8, (u32)&tmp2);
	MOVI2R(R9, (u32)&tmp3);

	STD(tempReg1, R7);
	STD(tempReg2, R8);
	STD(tempReg3, R9);

	// Load as float
	LFD(tempReg1, R7, 0);
	LFD(tempReg2, R8, 0);
	LFD(tempReg3, R9, 0);

	// Convert
	FCFID(tempReg1, tempReg1);
	FCFID(tempReg2, tempReg2);
	FCFID(tempReg3, tempReg3);

	// Double to simple
	FRSP(tempReg1, tempReg1);
	FRSP(tempReg2, tempReg2);
	FRSP(tempReg3, tempReg3);

	// Mult by (1.0f/255.f)
	FMULS(tempReg1, tempReg1, fprS8bitd);
	FMULS(tempReg2, tempReg2, fprS8bitd);
	FMULS(tempReg3, tempReg3, fprS8bitd);

	// Save
	SFS(tempReg1, dstReg, (dec_->decFmt.nrmoff + 0));
	SFS(tempReg2, dstReg, (dec_->decFmt.nrmoff + 4));
	SFS(tempReg3, dstReg, (dec_->decFmt.nrmoff + 8));
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_NormalS16() {
	/** todo **/
}

void VertexDecoderJitCache::Jit_NormalFloat() {
	MOVI2R(scratchReg, dec_->nrmoff);
	LWBRX(tempReg1, srcReg, scratchReg);

	ADDI(scratchReg, scratchReg, 4);
	LWBRX(tempReg2, srcReg, scratchReg);
	
	ADDI(scratchReg, scratchReg, 4);
	LWBRX(tempReg3, srcReg, scratchReg);

	STW(tempReg1, dstReg, dec_->decFmt.nrmoff);
	STW(tempReg2, dstReg, dec_->decFmt.nrmoff + 4);
	STW(tempReg3, dstReg, dec_->decFmt.nrmoff + 8);
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS8Through() {
	volatile u64 tmp1, tmp2, tmp3, tmp4;

	LBZ(tempReg1, srcReg, dec_->posoff + 0);
	LBZ(tempReg2, srcReg, dec_->posoff + 1);
	LBZ(tempReg3, srcReg, dec_->posoff + 2);
	
	EXTSB(tempReg1,tempReg1);
	EXTSB(tempReg2,tempReg2);
	EXTSB(tempReg3,tempReg3);
	
	// Save it in tmp memory 
	MOVI2R(R7, (u32)&tmp1);
	MOVI2R(R8, (u32)&tmp2);
	MOVI2R(R9, (u32)&tmp3);

	STD(tempReg1, R7);
	STD(tempReg2, R8);
	STD(tempReg3, R9);

	// Load as float
	LFD(tempReg1, R7, 0);
	LFD(tempReg2, R8, 0);
	LFD(tempReg3, R9, 0);

	// Convert
	FCFID(tempReg1, tempReg1);
	FCFID(tempReg2, tempReg2);
	FCFID(tempReg3, tempReg3);

	// Double to simple
	FRSP(tempReg1, tempReg1);
	FRSP(tempReg2, tempReg2);
	FRSP(tempReg3, tempReg3);

	// Mult by (1.0f/255.f)
	FMULS(tempReg1, tempReg1, fprS8bitd);
	FMULS(tempReg2, tempReg2, fprS8bitd);
	FMULS(tempReg3, tempReg3, fprS8bitd);

	// Save
	SFS(tempReg1, dstReg, (dec_->decFmt.posoff + 0));
	SFS(tempReg2, dstReg, (dec_->decFmt.posoff + 4));
	SFS(tempReg3, dstReg, (dec_->decFmt.posoff + 8));
}

// Through expands into floats, always. Might want to look at changing this.
void VertexDecoderJitCache::Jit_PosS16Through() {
	volatile u64 tmp1, tmp2, tmp3;

	MOVI2R(R7, dec_->posoff + 0);	
	MOVI2R(R8, dec_->posoff + 2);	
	MOVI2R(R9, dec_->posoff + 4);

	LHBRX(tempReg1, srcReg, R7);
	LHBRX(tempReg2, srcReg, R8);
	LHBRX(tempReg3, srcReg, R9);

	EXTSH(tempReg1, tempReg1);
	EXTSH(tempReg2, tempReg2);
	EXTSH(tempReg3, tempReg3);
	
	// Save it in tmp memory 
	MOVI2R(R7, (u32)&tmp1);
	MOVI2R(R8, (u32)&tmp2);
	MOVI2R(R9, (u32)&tmp3);

	STD(tempReg1, R7);
	STD(tempReg2, R8);
	STD(tempReg3, R9);

	// Load as float
	LFD(tempReg1, R7, 0);
	LFD(tempReg2, R8, 0);
	LFD(tempReg3, R9, 0);

	// Convert
	FCFID(tempReg1, tempReg1);
	FCFID(tempReg2, tempReg2);
	FCFID(tempReg3, tempReg3);

	// Double to simple
	FRSP(tempReg1, tempReg1);
	FRSP(tempReg2, tempReg2);
	FRSP(tempReg3, tempReg3);

	// Save
	SFS(tempReg1, dstReg, (dec_->decFmt.posoff + 0));
	SFS(tempReg2, dstReg, (dec_->decFmt.posoff + 4));
	SFS(tempReg3, dstReg, (dec_->decFmt.posoff + 8));
}

// Copy 3 bytes and then a zero. Might as well copy four.
void VertexDecoderJitCache::Jit_PosS8() {
	volatile u64 tmp1, tmp2, tmp3, tmp4;

	LBZ(tempReg1, srcReg, dec_->posoff + 0);
	LBZ(tempReg2, srcReg, dec_->posoff + 1);
	LBZ(tempReg3, srcReg, dec_->posoff + 2);
	
	EXTSB(tempReg1,tempReg1);
	EXTSB(tempReg2,tempReg2);
	EXTSB(tempReg3,tempReg3);
	
	// Save it in tmp memory 
	MOVI2R(R7, (u32)&tmp1);
	MOVI2R(R8, (u32)&tmp2);
	MOVI2R(R9, (u32)&tmp3);

	STD(tempReg1, R7);
	STD(tempReg2, R8);
	STD(tempReg3, R9);

	// Load as float
	LFD(tempReg1, R7, 0);
	LFD(tempReg2, R8, 0);
	LFD(tempReg3, R9, 0);

	// Convert
	FCFID(tempReg1, tempReg1);
	FCFID(tempReg2, tempReg2);
	FCFID(tempReg3, tempReg3);

	// Double to simple
	FRSP(tempReg1, tempReg1);
	FRSP(tempReg2, tempReg2);
	FRSP(tempReg3, tempReg3);

	// Mult by (1.0f/255.f)
	FMULS(tempReg1, tempReg1, fprS8bitd);
	FMULS(tempReg2, tempReg2, fprS8bitd);
	FMULS(tempReg3, tempReg3, fprS8bitd);

	// Save
	SFS(tempReg1, dstReg, (dec_->decFmt.posoff + 0));
	SFS(tempReg2, dstReg, (dec_->decFmt.posoff + 4));
	SFS(tempReg3, dstReg, (dec_->decFmt.posoff + 8));
}

// Copy 6 bytes and then 2 zeroes.
void VertexDecoderJitCache::Jit_PosS16() {	
	MOVI2R(R7, dec_->posoff + 0);	
	MOVI2R(R8, dec_->posoff + 2);	
	MOVI2R(R9, dec_->posoff + 4);

	LHBRX(tempReg1, srcReg, R7);
	LHBRX(tempReg2, srcReg, R8);
	LHBRX(tempReg3, srcReg, R9);
	
	STH(tempReg1, dstReg, dec_->decFmt.posoff + 0);
	STH(tempReg2, dstReg, dec_->decFmt.posoff + 2);
	STH(tempReg3, dstReg, dec_->decFmt.posoff + 4);

	LI(scratchReg, 0);
	STH(scratchReg, dstReg, dec_->decFmt.posoff + 6);
}

// Just copy 12 bytes.
void VertexDecoderJitCache::Jit_PosFloat() {
	/** Todo vmx ... **/
	LWZ(tempReg1, srcReg, dec_->posoff);
	LWZ(tempReg2, srcReg, dec_->posoff + 4);
	LWZ(tempReg3, srcReg, dec_->posoff + 8);
	
	MOVI2R(scratchReg, dec_->decFmt.posoff);

	STWBRX(tempReg1, dstReg, scratchReg);

	ADDI(scratchReg, scratchReg, 4);
	STWBRX(tempReg2, dstReg, scratchReg);

	ADDI(scratchReg, scratchReg, 4);
	STWBRX(tempReg3, dstReg, scratchReg);
}

bool VertexDecoderJitCache::CompileStep(const VertexDecoderDX9 &dec, int step) {
	// See if we find a matching JIT function
	for (size_t i = 0; i < ARRAY_SIZE(jitLookup); i++) {
		if (dec.steps_[step] == jitLookup[i].func) {
			((*this).*jitLookup[i].jitFunc)();
			return true;
		}
	}
	return false;
}
#else
VertexDecoderJitCache::VertexDecoderJitCache() {
	// link only !!!
}
#endif

};
