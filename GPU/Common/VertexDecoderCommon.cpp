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

#include <algorithm>
#include <cstdio>

#include "ppsspp_config.h"

#include "Common/CommonTypes.h"
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/MemMap.h"
#include "Core/HDRemaster.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/Util/AudioFormat.h"  // for clamp_u8
#include "GPU/Common/ShaderCommon.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Math3D.h"
#include "GPU/Common/VertexDecoderCommon.h"

static const u8 tcsize[4] = { 0, 2, 4, 8 }, tcalign[4] = { 0, 1, 2, 4 };
static const u8 colsize[8] = { 0, 0, 0, 0, 2, 2, 2, 4 }, colalign[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
static const u8 nrmsize[4] = { 0, 3, 6, 12 }, nrmalign[4] = { 0, 1, 2, 4 };
static const u8 possize[4] = { 3, 3, 6, 12 }, posalign[4] = { 1, 1, 2, 4 };
static const u8 wtsize[4] = { 0, 1, 2, 4 }, wtalign[4] = { 0, 1, 2, 4 };

static constexpr bool validateJit = false;

// When software skinning. This array is only used when non-jitted - when jitted, the matrix
// is kept in registers.
alignas(16) static float skinMatrix[12];

inline int align(int n, int align) {
	return (n + (align - 1)) & ~(align - 1);
}

int TranslateNumBones(int bones) {
	if (!bones) return 0;
	if (bones < 4) return 4;
	// if (bones < 8) return 8;   I get drawing problems in FF:CC with this!
	return bones;
}

static int DecFmtSize(u8 fmt) {
	switch (fmt) {
	case DEC_NONE: return 0;
	case DEC_FLOAT_1: return 4;
	case DEC_FLOAT_2: return 8;
	case DEC_FLOAT_3: return 12;
	case DEC_FLOAT_4: return 16;
	case DEC_S8_3: return 4;
	case DEC_S16_3: return 8;
	case DEC_U8_1: return 4;
	case DEC_U8_2: return 4;
	case DEC_U8_3: return 4;
	case DEC_U8_4: return 4;
	case DEC_U16_1: return 4;
	case DEC_U16_2: return 4;
	case DEC_U16_3: return 8;
	case DEC_U16_4: return 8;
	default:
		return 0;
	}
}

void DecVtxFormat::ComputeID() {
	id = w0fmt | (w1fmt << 4) | (uvfmt << 8) | (c0fmt << 12) | (c1fmt << 16) | (nrmfmt << 20);
}

void DecVtxFormat::InitializeFromID(uint32_t id) {
	this->id = id;
	w0fmt = ((id) & 0xF);
	w1fmt = ((id >> 4) & 0xF);
	uvfmt = ((id >> 8) & 0xF);
	c0fmt = ((id >> 12) & 0xF);
	c1fmt = ((id >> 16) & 0xF);
	nrmfmt = ((id >> 20) & 0xF);
	w0off = 0;
	w1off = w0off + DecFmtSize(w0fmt);
	uvoff = w1off + DecFmtSize(w1fmt);
	c0off = uvoff + DecFmtSize(uvfmt);
	c1off = c0off + DecFmtSize(c0fmt);
	nrmoff = c1off + DecFmtSize(c1fmt);
	posoff = nrmoff + DecFmtSize(nrmfmt);
	stride = posoff + DecFmtSize(PosFmt());
}

void GetIndexBounds(const void *inds, int count, u32 vertType, u16 *indexLowerBound, u16 *indexUpperBound) {
	// Find index bounds. Could cache this in display lists.
	// Also, this could be greatly sped up with SSE2/NEON, although rarely a bottleneck.
	u32 idx = vertType & GE_VTYPE_IDX_MASK;
	if (idx == GE_VTYPE_IDX_16BIT) {
		uint16_t upperBound = 0;
		uint16_t lowerBound = 0xFFFF;
		const u16_le *ind16 = (const u16_le *)inds;
		for (int i = 0; i < count; i++) {
			u16 value = ind16[i];
			if (value > upperBound)
				upperBound = value;
			if (value < lowerBound)
				lowerBound = value;
		}
		*indexLowerBound = lowerBound;
		*indexUpperBound = upperBound;
	} else if (idx == GE_VTYPE_IDX_8BIT) {
		uint8_t upperBound = 0;
		uint8_t lowerBound = 0xFF;
		const u8 *ind8 = (const u8 *)inds;
		for (int i = 0; i < count; i++) {
			u8 value = ind8[i];
			if (value > upperBound)
				upperBound = value;
			if (value < lowerBound)
				lowerBound = value;
		}
		*indexLowerBound = lowerBound;
		*indexUpperBound = upperBound;
	} else if (idx == GE_VTYPE_IDX_32BIT) {
		int lowerBound = 0x7FFFFFFF;
		int upperBound = 0;
		WARN_LOG_REPORT_ONCE(indexBounds32, G3D, "GetIndexBounds: Decoding 32-bit indexes");
		const u32_le *ind32 = (const u32_le *)inds;
		for (int i = 0; i < count; i++) {
			u16 value = (u16)ind32[i];
			// These aren't documented and should be rare.  Let's bounds check each one.
			if (ind32[i] != value) {
				ERROR_LOG_REPORT_ONCE(indexBounds32Bounds, G3D, "GetIndexBounds: Index outside 16-bit range");
			}
			if (value > upperBound)
				upperBound = value;
			if (value < lowerBound)
				lowerBound = value;
		}
		*indexLowerBound = (u16)lowerBound;
		*indexUpperBound = (u16)upperBound;
	} else {
		*indexLowerBound = 0;
		if (count > 0) {
			*indexUpperBound = count - 1;
		} else {
			*indexUpperBound = 0;
		}
	}
}

void PrintDecodedVertex(const VertexReader &vtx) {
	if (vtx.hasNormal()) {
		float nrm[3];
		vtx.ReadNrm(nrm);
		printf("N: %f %f %f\n", nrm[0], nrm[1], nrm[2]);
	}
	if (vtx.hasUV()) {
		float uv[2];
		vtx.ReadUV(uv);
		printf("TC: %f %f\n", uv[0], uv[1]);
	}
	if (vtx.hasColor0()) {
		float col0[4];
		vtx.ReadColor0(col0);
		printf("C0: %f %f %f %f\n", col0[0], col0[1], col0[2], col0[3]);
	}
	if (vtx.hasColor1()) {
		float col1[3];
		vtx.ReadColor1(col1);
		printf("C1: %f %f %f\n", col1[0], col1[1], col1[2]);
	}
	// Etc..
	float pos[3];
	vtx.ReadPos(pos);
	printf("P: %f %f %f\n", pos[0], pos[1], pos[2]);
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
	const u16_le *wdata = (const u16_le *)(ptr_);
	int j;
	for (j = 0; j < nweights; j++)
		wt[j] = wdata[j];
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
}

void VertexDecoder::Step_WeightsU8ToFloat() const
{
	float *wt = (float *)(decoded_ + decFmt.w0off);
	const u8 *wdata = (const u8*)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		wt[j] = (float)wdata[j] * (1.0f / 128.0f);
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
}

void VertexDecoder::Step_WeightsU16ToFloat() const
{
	float *wt = (float *)(decoded_ + decFmt.w0off);
	const u16_le *wdata = (const u16_le *)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		wt[j] = (float)wdata[j] * (1.0f / 32768.0f);
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0;
}

// Float weights should be uncommon, we can live with having to multiply these by 2.0
// to avoid special checks in the vertex shader generator.
// (PSP uses 0.0-2.0 fixed point numbers for weights)
void VertexDecoder::Step_WeightsFloat() const
{
	float *wt = (float *)(decoded_ + decFmt.w0off);
	const float_le *wdata = (const float_le *)(ptr_);
	int j;
	for (j = 0; j < nweights; j++) {
		wt[j] = wdata[j];
	}
	while (j & 3)   // Zero additional weights rounding up to 4.
		wt[j++] = 0.0f;
}

void VertexDecoder::ComputeSkinMatrix(const float weights[8]) const {
	memset(skinMatrix, 0, sizeof(skinMatrix));
	for (int j = 0; j < nweights; j++) {
		const float *bone = &gstate.boneMatrix[j * 12];
		if (weights[j] != 0.0f) {
			for (int i = 0; i < 12; i++) {
				skinMatrix[i] += weights[j] * bone[i];
			}
		}
	}
}

void VertexDecoder::Step_WeightsU8Skin() const {
	const u8 *wdata = (const u8*)(ptr_);
	float weights[8];
	for (int j = 0; j < nweights; j++)
		weights[j] = wdata[j] * (1.0f / 128.0f);
	ComputeSkinMatrix(weights);
}

void VertexDecoder::Step_WeightsU16Skin() const {
	const u16_le *wdata = (const u16_le *)(ptr_);
	float weights[8];
	for (int j = 0; j < nweights; j++)
		weights[j] = wdata[j] * (1.0f / 32768.0f);
	ComputeSkinMatrix(weights);
}

void VertexDecoder::Step_WeightsFloatSkin() const {
	const float_le *wdata = (const float_le *)(ptr_);
	ComputeSkinMatrix(wdata);
}

void VertexDecoder::Step_TcU8ToFloat() const
{
	// u32 to write two bytes of zeroes for free.
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u8 *uvdata = (const u8*)(ptr_ + tcoff);
	uv[0] = uvdata[0] * (1.0f / 128.0f);
	uv[1] = uvdata[1] * (1.0f / 128.0f);
}

void VertexDecoder::Step_TcU16ToFloat() const
{
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le *)(ptr_ + tcoff);
	uv[0] = uvdata[0] * (1.0f / 32768.0f);
	uv[1] = uvdata[1] * (1.0f / 32768.0f);
}

void VertexDecoder::Step_TcU16DoubleToFloat() const
{
	float *uv = (float*)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le *)(ptr_ + tcoff);
	uv[0] = uvdata[0] * (1.0f / 16384.0f);
	uv[1] = uvdata[1] * (1.0f / 16384.0f);
}

void VertexDecoder::Step_TcU16ThroughToFloat() const
{
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le *)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];

	gstate_c.vertBounds.minU = std::min(gstate_c.vertBounds.minU, (u16)uvdata[0]);
	gstate_c.vertBounds.maxU = std::max(gstate_c.vertBounds.maxU, (u16)uvdata[0]);
	gstate_c.vertBounds.minV = std::min(gstate_c.vertBounds.minV, (u16)uvdata[1]);
	gstate_c.vertBounds.maxV = std::max(gstate_c.vertBounds.maxV, (u16)uvdata[1]);
}

void VertexDecoder::Step_TcU16ThroughDoubleToFloat() const
{
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le *)(ptr_ + tcoff);
	uv[0] = uvdata[0] * 2;
	uv[1] = uvdata[1] * 2;
}

void VertexDecoder::Step_TcFloat() const
{
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float_le *uvdata = (const float_le *)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
}

void VertexDecoder::Step_TcFloatThrough() const
{
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float_le *uvdata = (const float_le *)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];

	gstate_c.vertBounds.minU = std::min(gstate_c.vertBounds.minU, (u16)uvdata[0]);
	gstate_c.vertBounds.maxU = std::max(gstate_c.vertBounds.maxU, (u16)uvdata[0]);
	gstate_c.vertBounds.minV = std::min(gstate_c.vertBounds.minV, (u16)uvdata[1]);
	gstate_c.vertBounds.maxV = std::max(gstate_c.vertBounds.maxV, (u16)uvdata[1]);
}

void VertexDecoder::Step_TcU8Prescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u8 *uvdata = (const u8 *)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] * (1.f / 128.f) * prescaleUV_->uScale + prescaleUV_->uOff;
	uv[1] = (float)uvdata[1] * (1.f / 128.f) * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_TcU16Prescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le *)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] * (1.f / 32768.f) * prescaleUV_->uScale + prescaleUV_->uOff;
	uv[1] = (float)uvdata[1] * (1.f / 32768.f) * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_TcU16DoublePrescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const u16_le *uvdata = (const u16_le *)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] * (1.f / 16384.f) * prescaleUV_->uScale + prescaleUV_->uOff;
	uv[1] = (float)uvdata[1] * (1.f / 16384.f) * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_TcFloatPrescale() const {
	float *uv = (float *)(decoded_ + decFmt.uvoff);
	const float_le *uvdata = (const float_le *)(ptr_ + tcoff);
	uv[0] = uvdata[0] * prescaleUV_->uScale + prescaleUV_->uOff;
	uv[1] = uvdata[1] * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_TcU8MorphToFloat() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const u8 *uvdata = (const u8 *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * (1.f / 128.f) * w;
		uv[1] += (float)uvdata[1] * (1.f / 128.f) * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0];
	out[1] = uv[1];
}

void VertexDecoder::Step_TcU16MorphToFloat() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const u16_le *uvdata = (const u16_le *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * (1.f / 32768.f) * w;
		uv[1] += (float)uvdata[1] * (1.f / 32768.f) * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0];
	out[1] = uv[1];
}

void VertexDecoder::Step_TcU16DoubleMorphToFloat() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const u16_le *uvdata = (const u16_le *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * (1.f / 16384.f) * w;
		uv[1] += (float)uvdata[1] * (1.f / 16384.f) * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0];
	out[1] = uv[1];
}

void VertexDecoder::Step_TcFloatMorph() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const float_le *uvdata = (const float_le *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * w;
		uv[1] += (float)uvdata[1] * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0];
	out[1] = uv[1];
}

void VertexDecoder::Step_TcU8PrescaleMorph() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const u8 *uvdata = (const u8 *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * (1.f / 128.f) * w;
		uv[1] += (float)uvdata[1] * (1.f / 128.f) * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0] * prescaleUV_->uScale + prescaleUV_->uOff;
	out[1] = uv[1] * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_TcU16PrescaleMorph() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const u16_le *uvdata = (const u16_le *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * (1.f / 32768.f) * w;
		uv[1] += (float)uvdata[1] * (1.f / 32768.f) * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0] * prescaleUV_->uScale + prescaleUV_->uOff;
	out[1] = uv[1] * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_TcU16DoublePrescaleMorph() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const u16_le *uvdata = (const u16_le *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * (1.f / 16384.f) * w;
		uv[1] += (float)uvdata[1] * (1.f / 16384.f) * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0] * prescaleUV_->uScale + prescaleUV_->uOff;
	out[1] = uv[1] * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_TcFloatPrescaleMorph() const {
	float uv[2] = { 0, 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const float_le *uvdata = (const float_le *)(ptr_ + onesize_*n + tcoff);

		uv[0] += (float)uvdata[0] * w;
		uv[1] += (float)uvdata[1] * w;
	}

	float *out = (float *)(decoded_ + decFmt.uvoff);
	out[0] = uv[0] * prescaleUV_->uScale + prescaleUV_->uOff;
	out[1] = uv[1] * prescaleUV_->vScale + prescaleUV_->vOff;
}

void VertexDecoder::Step_ColorInvalid() const
{
	// Do nothing.  This is only here to prevent crashes.
}

void VertexDecoder::Step_Color565() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(const u16_le *)(ptr_ + coloff);
	c[0] = Convert5To8(cdata & 0x1f);
	c[1] = Convert6To8((cdata >> 5) & 0x3f);
	c[2] = Convert5To8((cdata >> 11) & 0x1f);
	c[3] = 255;
	// Always full alpha.
}

void VertexDecoder::Step_Color5551() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(const u16_le *)(ptr_ + coloff);
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (cdata >> 15) != 0;
	c[0] = Convert5To8(cdata & 0x1f);
	c[1] = Convert5To8((cdata >> 5) & 0x1f);
	c[2] = Convert5To8((cdata >> 10) & 0x1f);
	c[3] = (cdata >> 15) ? 255 : 0;
}

void VertexDecoder::Step_Color4444() const
{
	u8 *c = decoded_ + decFmt.c0off;
	u16 cdata = *(const u16_le *)(ptr_ + coloff);
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (cdata >> 12) == 0xF;
	for (int j = 0; j < 4; j++)
		c[j] = Convert4To8((cdata >> (j * 4)) & 0xF);
}

void VertexDecoder::Step_Color8888() const
{
	u8 *c = decoded_ + decFmt.c0off;
	const u8 *cdata = (const u8*)(ptr_ + coloff);
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && cdata[3] == 255;
	memcpy(c, cdata, sizeof(u8) * 4);
}

void VertexDecoder::Step_Color565Morph() const
{
	float col[3] = { 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(const u16_le *)(ptr_ + onesize_*n + coloff);
		col[0] += w * (cdata & 0x1f) * (255.0f / 31.0f);
		col[1] += w * ((cdata >> 5) & 0x3f) * (255.0f / 63.0f);
		col[2] += w * ((cdata >> 11) & 0x1f) * (255.0f / 31.0f);
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
	float col[4] = { 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(const u16_le *)(ptr_ + onesize_*n + coloff);
		col[0] += w * (cdata & 0x1f) * (255.0f / 31.0f);
		col[1] += w * ((cdata >> 5) & 0x1f) * (255.0f / 31.0f);
		col[2] += w * ((cdata >> 10) & 0x1f) * (255.0f / 31.0f);
		col[3] += w * ((cdata >> 15) ? 255.0f : 0.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	for (int i = 0; i < 4; i++) {
		c[i] = clamp_u8((int)col[i]);
	}
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (int)col[3] >= 255;
}

void VertexDecoder::Step_Color4444Morph() const
{
	float col[4] = { 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(const u16_le *)(ptr_ + onesize_*n + coloff);
		for (int j = 0; j < 4; j++)
			col[j] += w * ((cdata >> (j * 4)) & 0xF) * (255.0f / 15.0f);
	}
	u8 *c = decoded_ + decFmt.c0off;
	for (int i = 0; i < 4; i++) {
		c[i] = clamp_u8((int)col[i]);
	}
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (int)col[3] >= 255;
}

void VertexDecoder::Step_Color8888Morph() const
{
	float col[4] = { 0 };
	for (int n = 0; n < morphcount; n++) {
		float w = gstate_c.morphWeights[n];
		const u8 *cdata = (const u8*)(ptr_ + onesize_*n + coloff);
		for (int j = 0; j < 4; j++)
			col[j] += w * cdata[j];
	}
	u8 *c = decoded_ + decFmt.c0off;
	for (int i = 0; i < 4; i++) {
		c[i] = clamp_u8((int)col[i]);
	}
	gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (int)col[3] >= 255;
}

void VertexDecoder::Step_NormalS8() const
{
	s8 *normal = (s8 *)(decoded_ + decFmt.nrmoff);
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = sv[j];
	normal[3] = 0;
}

void VertexDecoder::Step_NormalS8ToFloat() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	normal[0] = sv[0] * (1.0f / 128.0f);
	normal[1] = sv[1] * (1.0f / 128.0f);
	normal[2] = sv[2] * (1.0f / 128.0f);
}

void VertexDecoder::Step_NormalS16() const
{
	s16 *normal = (s16 *)(decoded_ + decFmt.nrmoff);
	const s16_le *sv = (const s16_le *)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = sv[j];
	normal[3] = 0;
}

void VertexDecoder::Step_NormalFloat() const
{
	u32 *normal = (u32 *)(decoded_ + decFmt.nrmoff);
	const u32_le *fv = (const u32_le *)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = fv[j];
}

void VertexDecoder::Step_NormalS8Skin() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	const float fn[3] = { sv[0] * (1.0f / 128.0f), sv[1] * (1.0f / 128.0f), sv[2] * (1.0f / 128.0f) };
	Norm3ByMatrix43(normal, fn, skinMatrix);
}

void VertexDecoder::Step_NormalS16Skin() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const s16_le *sv = (const s16_le *)(ptr_ + nrmoff);
	const float fn[3] = { sv[0] * (1.0f / 32768.0f), sv[1] * (1.0f / 32768.0f), sv[2] * (1.0f / 32768.0f) };
	Norm3ByMatrix43(normal, fn, skinMatrix);
}

void VertexDecoder::Step_NormalFloatSkin() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	const float_le *fn = (const float_le *)(ptr_ + nrmoff);
	Norm3ByMatrix43(normal, fn, skinMatrix);
}

void VertexDecoder::Step_NormalS8Morph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const s8 *bv = (const s8*)(ptr_ + onesize_*n + nrmoff);
		const float multiplier = gstate_c.morphWeights[n] * (1.0f / 128.0f);
		for (int j = 0; j < 3; j++)
			normal[j] += bv[j] * multiplier;
	}
}

void VertexDecoder::Step_NormalS16Morph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const s16_le *sv = (const s16_le *)(ptr_ + onesize_*n + nrmoff);
		const float multiplier = gstate_c.morphWeights[n] * (1.0f / 32768.0f);
		for (int j = 0; j < 3; j++)
			normal[j] += sv[j] * multiplier;
	}
}

void VertexDecoder::Step_NormalFloatMorph() const
{
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	memset(normal, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		float multiplier = gstate_c.morphWeights[n];
		const float_le *fv = (const float_le *)(ptr_ + onesize_*n + nrmoff);
		for (int j = 0; j < 3; j++)
			normal[j] += fv[j] * multiplier;
	}
}

void VertexDecoder::Step_NormalS8MorphSkin() const {
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	float nrm[3]{};
	for (int n = 0; n < morphcount; n++) {
		const s8 *bv = (const s8*)(ptr_ + onesize_ * n + nrmoff);
		const float multiplier = gstate_c.morphWeights[n] * (1.0f / 128.0f);
		for (int j = 0; j < 3; j++)
			nrm[j] += bv[j] * multiplier;
	}
	Norm3ByMatrix43(normal, nrm, skinMatrix);
}

void VertexDecoder::Step_NormalS16MorphSkin() const {
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	float nrm[3]{};
	for (int n = 0; n < morphcount; n++) {
		const s16_le *sv = (const s16_le *)(ptr_ + onesize_ * n + nrmoff);
		const float multiplier = gstate_c.morphWeights[n] * (1.0f / 32768.0f);
		for (int j = 0; j < 3; j++)
			nrm[j] += sv[j] * multiplier;
	}
	Norm3ByMatrix43(normal, nrm, skinMatrix);
}

void VertexDecoder::Step_NormalFloatMorphSkin() const {
	float *normal = (float *)(decoded_ + decFmt.nrmoff);
	float nrm[3]{};
	for (int n = 0; n < morphcount; n++) {
		float multiplier = gstate_c.morphWeights[n];
		const float_le *fv = (const float_le *)(ptr_ + onesize_ * n + nrmoff);
		for (int j = 0; j < 3; j++)
			nrm[j] += fv[j] * multiplier;
	}
	Norm3ByMatrix43(normal, nrm, skinMatrix);
}

void VertexDecoder::Step_PosS8() const
{
	float *pos = (float *)(decoded_ + decFmt.posoff);
	const s8 *sv = (const s8*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		pos[j] = sv[j] * (1.0f / 128.0f);
}

void VertexDecoder::Step_PosS16() const
{
	float *pos = (float *)(decoded_ + decFmt.posoff);
	const s16_le *sv = (const s16_le *)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		pos[j] = sv[j] * (1.0f / 32768.0f);
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
	const float fn[3] = { sv[0] * (1.0f / 128.0f), sv[1] * (1.0f / 128.0f), sv[2] * (1.0f / 128.0f) };
	Vec3ByMatrix43(pos, fn, skinMatrix);
}

void VertexDecoder::Step_PosS16Skin() const
{
	float *pos = (float *)(decoded_ + decFmt.posoff);
	const s16_le *sv = (const s16_le *)(ptr_ + posoff);
	const float fn[3] = { sv[0] * (1.0f / 32768.0f), sv[1] * (1.0f / 32768.0f), sv[2] * (1.0f / 32768.0f) };
	Vec3ByMatrix43(pos, fn, skinMatrix);
}

void VertexDecoder::Step_PosFloatSkin() const
{
	float *pos = (float *)(decoded_ + decFmt.posoff);
	const float_le *fn = (const float_le *)(ptr_ + posoff);
	Vec3ByMatrix43(pos, fn, skinMatrix);
}

void VertexDecoder::Step_PosInvalid() const {
	// Invalid positions are just culled.  Simulate by forcing invalid values.
	float *v = (float *)(decoded_ + decFmt.posoff);
	v[0] = std::numeric_limits<float>::infinity();
	v[1] = std::numeric_limits<float>::infinity();
	v[2] = std::numeric_limits<float>::infinity();
}

void VertexDecoder::Step_PosS8Through() const {
	// 8-bit positions in throughmode always decode to 0, depth included.
	float *v = (float *)(decoded_ + decFmt.posoff);
	v[0] = 0;
	v[1] = 0;
	v[2] = 0;
}

void VertexDecoder::Step_PosS16Through() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	const s16_le *sv = (const s16_le *)(ptr_ + posoff);
	const u16_le *uv = (const u16_le *)(ptr_ + posoff);
	v[0] = sv[0];
	v[1] = sv[1];
	v[2] = uv[2];
}

void VertexDecoder::Step_PosFloatThrough() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	const float *fv = (const float *)(ptr_ + posoff);
	memcpy(v, fv, 8);
	v[2] = fv[2] > 65535.0f ? 65535.0f : (fv[2] < 0.0f ? 0.0f : fv[2]);
}

void VertexDecoder::Step_PosS8Morph() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const float multiplier = 1.0f / 128.0f;
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
		const float multiplier = 1.0f / 32768.0f;
		const s16_le *sv = (const s16_le *)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += (float)sv[j] * (multiplier * gstate_c.morphWeights[n]);
	}
}

void VertexDecoder::Step_PosFloatMorph() const
{
	float *v = (float *)(decoded_ + decFmt.posoff);
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const float_le *fv = (const float_le *)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += fv[j] * gstate_c.morphWeights[n];
	}
}

void VertexDecoder::Step_PosS8MorphSkin() const {
	float *v = (float *)(decoded_ + decFmt.posoff);
	float pos[3]{};
	for (int n = 0; n < morphcount; n++) {
		const float multiplier = 1.0f / 128.0f;
		const s8 *sv = (const s8*)(ptr_ + onesize_ * n + posoff);
		for (int j = 0; j < 3; j++)
			pos[j] += (float)sv[j] * (multiplier * gstate_c.morphWeights[n]);
	}
	Vec3ByMatrix43(v, pos, skinMatrix);
}

void VertexDecoder::Step_PosS16MorphSkin() const {
	float *v = (float *)(decoded_ + decFmt.posoff);
	float pos[3]{};
	for (int n = 0; n < morphcount; n++) {
		const float multiplier = 1.0f / 32768.0f;
		const s16_le *sv = (const s16_le *)(ptr_ + onesize_ * n + posoff);
		for (int j = 0; j < 3; j++)
			pos[j] += (float)sv[j] * (multiplier * gstate_c.morphWeights[n]);
	}
	Vec3ByMatrix43(v, pos, skinMatrix);
}

void VertexDecoder::Step_PosFloatMorphSkin() const {
	float *v = (float *)(decoded_ + decFmt.posoff);
	float pos[3]{};
	for (int n = 0; n < morphcount; n++) {
		const float_le *fv = (const float_le *)(ptr_ + onesize_ * n + posoff);
		for (int j = 0; j < 3; j++)
			pos[j] += fv[j] * gstate_c.morphWeights[n];
	}
	Vec3ByMatrix43(v, pos, skinMatrix);
}

static const StepFunction wtstep[4] = {
	0,
	&VertexDecoder::Step_WeightsU8,
	&VertexDecoder::Step_WeightsU16,
	&VertexDecoder::Step_WeightsFloat,
};

static const StepFunction wtstepToFloat[4] = {
	0,
	&VertexDecoder::Step_WeightsU8ToFloat,
	&VertexDecoder::Step_WeightsU16ToFloat,
	&VertexDecoder::Step_WeightsFloat,
};

// TODO: Morph weights correctly! This is missing. Not sure if any game actually
// use this functionality at all.

static const StepFunction wtstep_skin[4] = {
	0,
	&VertexDecoder::Step_WeightsU8Skin,
	&VertexDecoder::Step_WeightsU16Skin,
	&VertexDecoder::Step_WeightsFloatSkin,
};

static const StepFunction tcstepToFloat[4] = {
	0,
	&VertexDecoder::Step_TcU8ToFloat,
	&VertexDecoder::Step_TcU16ToFloat,
	&VertexDecoder::Step_TcFloat,
};

static const StepFunction tcstep_prescale[4] = {
	0,
	&VertexDecoder::Step_TcU8Prescale,
	&VertexDecoder::Step_TcU16Prescale,
	&VertexDecoder::Step_TcFloatPrescale,
};

static const StepFunction tcstep_prescale_remaster[4] = {
	0,
	&VertexDecoder::Step_TcU8Prescale,
	&VertexDecoder::Step_TcU16DoublePrescale,
	&VertexDecoder::Step_TcFloatPrescale,
};

static const StepFunction tcstep_prescale_morph[4] = {
	0,
	&VertexDecoder::Step_TcU8PrescaleMorph,
	&VertexDecoder::Step_TcU16PrescaleMorph,
	&VertexDecoder::Step_TcFloatPrescaleMorph,
};

static const StepFunction tcstep_prescale_morph_remaster[4] = {
	0,
	&VertexDecoder::Step_TcU8PrescaleMorph,
	&VertexDecoder::Step_TcU16DoublePrescaleMorph,
	&VertexDecoder::Step_TcFloatPrescaleMorph,
};

static const StepFunction tcstep_morphToFloat[4] = {
	0,
	&VertexDecoder::Step_TcU8MorphToFloat,
	&VertexDecoder::Step_TcU16MorphToFloat,
	&VertexDecoder::Step_TcFloatMorph,
};

static const StepFunction tcstep_morph_remasterToFloat[4] = {
	0,
	&VertexDecoder::Step_TcU8MorphToFloat,
	&VertexDecoder::Step_TcU16DoubleMorphToFloat,
	&VertexDecoder::Step_TcFloatMorph,
};

static const StepFunction tcstep_throughToFloat[4] = {
	0,
	&VertexDecoder::Step_TcU8ToFloat,
	&VertexDecoder::Step_TcU16ThroughToFloat,
	&VertexDecoder::Step_TcFloatThrough,
};

static const StepFunction tcstep_remasterToFloat[4] = {
	0,
	&VertexDecoder::Step_TcU8ToFloat,
	&VertexDecoder::Step_TcU16DoubleToFloat,
	&VertexDecoder::Step_TcFloat,
};

static const StepFunction tcstep_through_remasterToFloat[4] = {
	0,
	&VertexDecoder::Step_TcU8ToFloat,
	&VertexDecoder::Step_TcU16ThroughDoubleToFloat,
	&VertexDecoder::Step_TcFloatThrough,
};

static const StepFunction colstep[8] = {
	0,
	&VertexDecoder::Step_ColorInvalid,
	&VertexDecoder::Step_ColorInvalid,
	&VertexDecoder::Step_ColorInvalid,
	&VertexDecoder::Step_Color565,
	&VertexDecoder::Step_Color5551,
	&VertexDecoder::Step_Color4444,
	&VertexDecoder::Step_Color8888,
};

static const StepFunction colstep_morph[8] = {
	0,
	&VertexDecoder::Step_ColorInvalid,
	&VertexDecoder::Step_ColorInvalid,
	&VertexDecoder::Step_ColorInvalid,
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

static const StepFunction nrmstep8BitToFloat[4] = {
	0,
	&VertexDecoder::Step_NormalS8ToFloat,
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

static const StepFunction nrmstep_morphskin[4] = {
	0,
	&VertexDecoder::Step_NormalS8MorphSkin,
	&VertexDecoder::Step_NormalS16MorphSkin,
	&VertexDecoder::Step_NormalFloatMorphSkin,
};

static const StepFunction posstep[4] = {
	&VertexDecoder::Step_PosInvalid,
	&VertexDecoder::Step_PosS8,
	&VertexDecoder::Step_PosS16,
	&VertexDecoder::Step_PosFloat,
};

static const StepFunction posstep_skin[4] = {
	&VertexDecoder::Step_PosInvalid,
	&VertexDecoder::Step_PosS8Skin,
	&VertexDecoder::Step_PosS16Skin,
	&VertexDecoder::Step_PosFloatSkin,
};

static const StepFunction posstep_morph[4] = {
	&VertexDecoder::Step_PosInvalid,
	&VertexDecoder::Step_PosS8Morph,
	&VertexDecoder::Step_PosS16Morph,
	&VertexDecoder::Step_PosFloatMorph,
};

static const StepFunction posstep_morph_skin[4] = {
	&VertexDecoder::Step_PosInvalid,
	&VertexDecoder::Step_PosS8MorphSkin,
	&VertexDecoder::Step_PosS16MorphSkin,
	&VertexDecoder::Step_PosFloatMorphSkin,
};

static const StepFunction posstep_through[4] = {
	&VertexDecoder::Step_PosInvalid,
	&VertexDecoder::Step_PosS8Through,
	&VertexDecoder::Step_PosS16Through,
	&VertexDecoder::Step_PosFloatThrough,
};

void VertexDecoder::SetVertexType(u32 fmt, const VertexDecoderOptions &options, VertexDecoderJitCache *jitCache) {
	fmt_ = fmt;
	throughmode = (fmt & GE_VTYPE_THROUGH) != 0;
	numSteps_ = 0;

	biggest = 0;
	size = 0;

	tc = fmt & 0x3;
	col = (fmt >> 2) & 0x7;
	nrm = (fmt >> 5) & 0x3;
	pos = (fmt >> 7) & 0x3;
	weighttype = (fmt >> 9) & 0x3;
	idx = (fmt >> 11) & 0x3;
	morphcount = ((fmt >> 18) & 0x7) + 1;
	nweights = ((fmt >> 14) & 0x7) + 1;

	int decOff = 0;
	memset(&decFmt, 0, sizeof(decFmt));

	if (morphcount > 1) {
		DEBUG_LOG_REPORT_ONCE(vtypeM, G3D, "VTYPE with morph used: THRU=%i TC=%i COL=%i POS=%i NRM=%i WT=%i NW=%i IDX=%i MC=%i", (int)throughmode, tc, col, pos, nrm, weighttype, nweights, idx, morphcount);
	} else {
		DEBUG_LOG(G3D, "VTYPE: THRU=%i TC=%i COL=%i POS=%i NRM=%i WT=%i NW=%i IDX=%i MC=%i", (int)throughmode, tc, col, pos, nrm, weighttype, nweights, idx, morphcount);
	}

	skinInDecode = weighttype != 0 && options.applySkinInDecode;

	if (weighttype) { // && nweights?
		weightoff = size;
		//size = align(size, wtalign[weighttype]);	unnecessary
		size += wtsize[weighttype] * nweights;
		if (wtalign[weighttype] > biggest)
			biggest = wtalign[weighttype];

		if (skinInDecode) {
			// No visible output, computes a matrix that is passed through the skinMatrix variable
			// to the "nrm" and "pos" steps.
			// Technically we should support morphing the weights too, but I have a hard time
			// imagining that any game would use that.. but you never know.
			steps_[numSteps_++] = wtstep_skin[weighttype];
		} else {
			int fmtBase = DEC_FLOAT_1;
			if (options.expandAllWeightsToFloat) {
				steps_[numSteps_++] = wtstepToFloat[weighttype];
				fmtBase = DEC_FLOAT_1;
			} else {
				steps_[numSteps_++] = wtstep[weighttype];
				if (weighttype == GE_VTYPE_WEIGHT_8BIT >> GE_VTYPE_WEIGHT_SHIFT) {
					fmtBase = DEC_U8_1;
				} else if (weighttype == GE_VTYPE_WEIGHT_16BIT >> GE_VTYPE_WEIGHT_SHIFT) {
					fmtBase = DEC_U16_1;
				} else if (weighttype == GE_VTYPE_WEIGHT_FLOAT >> GE_VTYPE_WEIGHT_SHIFT) {
					fmtBase = DEC_FLOAT_1;
				}
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
		// throughmode is automatically included though, because it's part of the vertType.
		if (!throughmode && (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_COORDS || gstate.getUVGenMode() == GE_TEXMAP_UNKNOWN)) {
			if (g_DoubleTextureCoordinates)
				steps_[numSteps_++] = morphcount == 1 ? tcstep_prescale_remaster[tc] : tcstep_prescale_morph_remaster[tc];
			else
				steps_[numSteps_++] = morphcount == 1 ? tcstep_prescale[tc] : tcstep_prescale_morph[tc];
			decFmt.uvfmt = DEC_FLOAT_2;
		} else {
			// We now always expand UV to float.
			if (morphcount != 1 && !throughmode)
				steps_[numSteps_++] = g_DoubleTextureCoordinates ? tcstep_morph_remasterToFloat[tc] : tcstep_morphToFloat[tc];
			else if (g_DoubleTextureCoordinates)
				steps_[numSteps_++] = throughmode ? tcstep_through_remasterToFloat[tc] : tcstep_remasterToFloat[tc];
			else
				steps_[numSteps_++] = throughmode ? tcstep_throughToFloat[tc] : tcstepToFloat[tc];
			decFmt.uvfmt = DEC_FLOAT_2;
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

		// All color formats decode to DEC_U8_4.
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
			steps_[numSteps_++] = morphcount == 1 ? nrmstep_skin[nrm] : nrmstep_morphskin[nrm];
			// After skinning, we always have three floats.
			decFmt.nrmfmt = DEC_FLOAT_3;
		} else {
			if (morphcount == 1) {
				// The 8-bit and 16-bit normal formats match GL formats nicely, and the 16-bit normal format matches a D3D format so let's use them where possible.
				switch (nrm) {
				case GE_VTYPE_NRM_8BIT >> GE_VTYPE_NRM_SHIFT:
					if (options.expand8BitNormalsToFloat) {
						decFmt.nrmfmt = DEC_FLOAT_3;
						steps_[numSteps_++] = nrmstep8BitToFloat[nrm];
					} else {
						decFmt.nrmfmt = DEC_S8_3;
						steps_[numSteps_++] = nrmstep[nrm];
					}
					break;
				case GE_VTYPE_NRM_16BIT >> GE_VTYPE_NRM_SHIFT:
					decFmt.nrmfmt = DEC_S16_3;
					steps_[numSteps_++] = nrmstep[nrm];
					break;
				case GE_VTYPE_NRM_FLOAT >> GE_VTYPE_NRM_SHIFT:
					decFmt.nrmfmt = DEC_FLOAT_3;
					steps_[numSteps_++] = nrmstep[nrm];
					break;
				}
			} else {
				decFmt.nrmfmt = DEC_FLOAT_3;
				steps_[numSteps_++] = nrmstep_morph[nrm];
			}
		}
		decFmt.nrmoff = decOff;
		decOff += DecFmtSize(decFmt.nrmfmt);
	}

	bool reportNoPos = false;
	if (!pos) {
		reportNoPos = true;
	}
	if (pos >= 0) { // there's always a position
		size = align(size, posalign[pos]);
		posoff = size;
		size += possize[pos];
		if (posalign[pos] > biggest)
			biggest = posalign[pos];

		// We don't set posfmt because it's always DEC_FLOAT_3.
		if (throughmode) {
			steps_[numSteps_++] = posstep_through[pos];
		} else {
			if (skinInDecode) {
				steps_[numSteps_++] = morphcount == 1 ? posstep_skin[pos] : posstep_morph_skin[pos];
			} else {
				steps_[numSteps_++] = morphcount == 1 ? posstep[pos] : posstep_morph[pos];
			}
		}
		decFmt.posoff = decOff;
		decOff += DecFmtSize(DecVtxFormat::PosFmt());
	}

	decFmt.stride = options.alignOutputToWord ? align(decOff, 4) : decOff;

	decFmt.ComputeID();

	size = align(size, biggest);
	onesize_ = size;
	size *= morphcount;
	DEBUG_LOG(G3D, "SVT : size = %i, aligned to biggest %i", size, biggest);

	if (reportNoPos) {
		char temp[256]{};
		ToString(temp, true);
		ERROR_LOG_REPORT(G3D, "Vertices without position found: (%08x) %s", fmt_, temp);
	}

	_assert_msg_(decFmt.uvfmt == DEC_FLOAT_2 || decFmt.uvfmt == DEC_NONE, "Reader only supports float UV");

	// Attempt to JIT as well. But only do that if the main CPU JIT is enabled, in order to aid
	// debugging attempts - if the main JIT doesn't work, this one won't do any better, probably.
	if (jitCache) {
		jitted_ = jitCache->Compile(*this, &jittedSize_);
		if (!jitted_) {
			WARN_LOG(G3D, "Vertex decoder JIT failed! fmt = %08x (%s)", fmt_, GetString(SHADER_STRING_SHORT_DESC).c_str());
		}
	}
}

void VertexDecoder::DecodeVerts(u8 *decodedptr, const void *verts, const UVScale *uvScaleOffset, int indexLowerBound, int indexUpperBound) const {
	// A single 0 is acceptable for point lists.
	_dbg_assert_(indexLowerBound <= indexUpperBound);

	// Decode the vertices within the found bounds, once each
	// decoded_ and ptr_ are used in the steps, so can't be turned into locals for speed.
	const u8 *startPtr = (const u8*)verts + indexLowerBound * size;

	int count = indexUpperBound - indexLowerBound + 1;
	int stride = decFmt.stride;

	// Check alignment before running the decoder, as we may crash if it's bad (as should the real PSP but doesn't always)
	if (((uintptr_t)verts & (biggest - 1)) != 0) {
		// Bad alignment. Not really sure what to do here... zero the verts to be safe?
		memset(decodedptr, 0, count * stride);
		return;
	}

	if (jitted_ && !validateJit) {
		// We've compiled the steps into optimized machine code, so just jump!
		jitted_(startPtr, decodedptr, count, uvScaleOffset);
	} else {
		ptr_ = startPtr;
		decoded_ = decodedptr;
		prescaleUV_ = uvScaleOffset;
		// Interpret the decode steps
		for (; count; count--) {
			for (int i = 0; i < numSteps_; i++) {
				((*this).*steps_[i])();
			}
			ptr_ += size;
			decoded_ += stride;
		}

		if (jitted_ && validateJit) {
			CompareToJit(startPtr, decodedptr, indexUpperBound - indexLowerBound + 1, uvScaleOffset);
		}
	}
}

static float LargestAbsDiff(Vec4f a, Vec4f b, int n) {
	Vec4f delta = a - b;
	float largest = 0;
	for (int i = 0; i < n; ++ i) {
		largest = std::max(largest, fabsf(delta[i]));
	}
	return largest;
}

static bool DecodedVertsAreSimilar(const VertexReader &vtx1, const VertexReader &vtx2) {
	Vec4f vec1{}, vec2{};
	if (vtx1.hasNormal()) {
		vtx1.ReadNrm(vec1.AsArray());
		vtx2.ReadNrm(vec2.AsArray());
		float diff = LargestAbsDiff(vec1, vec2, 3);
		if (diff >= 1.0 / 512.0f) {
			WARN_LOG(G3D, "Normal diff %f", diff);
			return false;
		}
	}
	if (vtx1.hasUV()) {
		vtx1.ReadUV(vec1.AsArray());
		vtx2.ReadUV(vec2.AsArray());
		float diff = LargestAbsDiff(vec1, vec2, 2);
		if (diff >= 1.0 / 512.0f) {
			WARN_LOG(G3D, "UV diff %f", diff);
			return false;
		}
	}
	if (vtx1.hasColor0()) {
		vtx1.ReadColor0(vec1.AsArray());
		vtx2.ReadColor0(vec2.AsArray());
		float diff = LargestAbsDiff(vec1, vec2, 4);
		if (diff >= 1.0 / 255.0f) {
			WARN_LOG(G3D, "Color0 diff %f", diff);
			return false;
		}
	}
	if (vtx1.hasColor1()) {
		vtx1.ReadColor1(vec1.AsArray());
		vtx2.ReadColor1(vec2.AsArray());
		float diff = LargestAbsDiff(vec1, vec2, 4);
		if (diff >= 1.0 / 255.0f) {
			WARN_LOG(G3D, "Color1 diff %f", diff);
			return false;
		}
	}
	vtx1.ReadPos(vec1.AsArray());
	vtx2.ReadPos(vec2.AsArray());
	float diff = LargestAbsDiff(vec1, vec2, 3);
	if (diff >= 1.0 / 512.0f) {
		WARN_LOG(G3D, "Pos diff %f", diff);
		return false;
	}

	return true;
}

void VertexDecoder::CompareToJit(const u8 *startPtr, u8 *decodedptr, int count, const UVScale *uvScaleOffset) const {
	std::vector<uint8_t> jittedBuffer(decFmt.stride * count);
	jitted_(startPtr, &jittedBuffer[0], count, uvScaleOffset);

	VertexReader controlReader(decodedptr, GetDecVtxFmt(), fmt_);
	VertexReader jittedReader(&jittedBuffer[0], GetDecVtxFmt(), fmt_);
	for (int i = 0; i < count; ++i) {
		int off = decFmt.stride * i;
		controlReader.Goto(i);
		jittedReader.Goto(i);
		if (!DecodedVertsAreSimilar(controlReader, jittedReader)) {
			char name[512]{};
			ToString(name, true);
			ERROR_LOG(G3D, "Encountered vertexjit mismatch at %d/%d for %s", i, count, name);
			if (morphcount > 1) {
				printf("Morph:\n");
				for (int j = 0; j < morphcount; ++j) {
					printf("  %f\n", gstate_c.morphWeights[j]);
				}
			}
			if (weighttype) {
				printf("Bones:\n");
				for (int j = 0; j < nweights; ++j) {
					for (int k = 0; k < 4; ++k) {
						if (k == 0)
							printf(" *");
						else
							printf("  ");
						printf(" %f,%f,%f\n", gstate.boneMatrix[j * 12 + k * 3 + 0], gstate.boneMatrix[j * 12 + k * 3 + 1], gstate.boneMatrix[j * 12 + k * 3 + 2]);
					}
				}
			}
			printf("Src:\n");
			const u8 *s = startPtr + i * size;
			for (int j = 0; j < size; ++j) {
				int oneoffset = j % onesize_;
				if (oneoffset == weightoff && weighttype)
					printf(" W:");
				else if (oneoffset == tcoff && tc)
					printf(" T:");
				else if (oneoffset == coloff && col)
					printf(" C:");
				else if (oneoffset == nrmoff && nrm)
					printf(" N:");
				else if (oneoffset == posoff)
					printf(" P:");
				printf("%02x ", s[j]);
				if (oneoffset == onesize_ - 1)
					printf("\n");
			}
			printf("Interpreted vertex:\n");
			PrintDecodedVertex(controlReader);
			printf("Jit vertex:\n");
			PrintDecodedVertex(jittedReader);
			Crash();
		}
	}
}

static const char * const posnames[4] = { "?", "s8", "s16", "f" };
static const char * const nrmnames[4] = { "", "s8", "s16", "f" };
static const char * const tcnames[4] = { "", "u8", "u16", "f" };
static const char * const idxnames[4] = { "-", "u8", "u16", "?" };
static const char * const weightnames[4] = { "-", "u8", "u16", "f" };
static const char * const colnames[8] = { "", "?", "?", "?", "565", "5551", "4444", "8888" };

int VertexDecoder::ToString(char *output, bool spaces) const {
	char *start = output;
	
	output += sprintf(output, "P: %s ", posnames[pos]);
	if (nrm)
		output += sprintf(output, "N: %s ", nrmnames[nrm]);
	if (col)
		output += sprintf(output, "C: %s ", colnames[col]);
	if (tc)
		output += sprintf(output, "T: %s ", tcnames[tc]);
	if (weighttype)
		output += sprintf(output, "W: %s (%ix) ", weightnames[weighttype], nweights);
	if (idx)
		output += sprintf(output, "I: %s ", idxnames[idx]);
	if (morphcount > 1)
		output += sprintf(output, "Morph: %i ", morphcount);
	if (throughmode)
		output += sprintf(output, " (through)");

	output += sprintf(output, " (%ib)", VertexSize());

	if (!spaces) {
		size_t len = strlen(start);
		for (int i = 0; i < len; i++) {
			if (start[i] == ' ')
				start[i] = '_';
		}
	}

	return output - start;
}

std::string VertexDecoder::GetString(DebugShaderStringType stringType) {
	char buffer[256];
	switch (stringType) {
	case SHADER_STRING_SHORT_DESC:
		ToString(buffer, true);
		return std::string(buffer);
	case SHADER_STRING_SOURCE_CODE:
		{
			if (!jitted_)
				return "Not compiled";
			std::vector<std::string> lines;
#if PPSSPP_ARCH(ARM64)
			lines = DisassembleArm64((const u8 *)jitted_, jittedSize_);
#elif PPSSPP_ARCH(ARM)
			lines = DisassembleArm2((const u8 *)jitted_, jittedSize_);
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
			lines = DisassembleX86((const u8 *)jitted_, jittedSize_);
#elif PPSSPP_ARCH(RISCV64)
			lines = DisassembleRV64((const u8 *)jitted_, jittedSize_);
#else
			// No disassembler defined
#endif
			std::string buffer;
			for (const auto &line : lines) {
				buffer += line;
				buffer += "\n";
			}
			return buffer;
		}

	default:
		return "N/A";
	}
}

VertexDecoderJitCache::VertexDecoderJitCache()
#if PPSSPP_ARCH(ARM64)
 : fp(this)
#endif
{
	// 256k should be enough.
	AllocCodeSpace(1024 * 64 * 4);

	// Add some random code to "help" MSVC's buggy disassembler :(
#if defined(_WIN32) && (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64))
	using namespace Gen;
	for (int i = 0; i < 100; i++) {
		MOV(32, R(EAX), R(EBX));
		RET();
	}
#elif PPSSPP_ARCH(ARM)
	BKPT(0);
	BKPT(0);
#endif
}

void VertexDecoderJitCache::Clear() {
	if (g_Config.iCpuCore == (int)CPUCore::JIT || g_Config.iCpuCore == (int)CPUCore::JIT_IR) {
		ClearCodeSpace(0);
	}
}
