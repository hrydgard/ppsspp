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

#include "../../Core/MemMap.h"
#include "../ge_constants.h"

#include "VertexDecoder.h"

void PrintDecodedVertex(VertexReader &vtx) {
	if (vtx.hasNormal())
	{
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
	if (vtx.hasColor0()) {
		float col1[3];
		vtx.ReadColor1(col1);
		printf("C1: %f %f %f\n", col1[0], col1[1], col1[2]);
	}
	// Etc..
	float pos[3];
	vtx.ReadPos(pos);
	printf("P: %f %f %f\n", pos[0], pos[1], pos[2]);
}

const int tcsize[4] = {0,2,4,8}, tcalign[4] = {0,1,2,4};
const int colsize[8] = {0,0,0,0,2,2,2,4}, colalign[8] = {0,0,0,0,2,2,2,4};
const int nrmsize[4] = {0,3,6,12}, nrmalign[4] = {0,1,2,4};
const int possize[4] = {0,3,6,12}, posalign[4] = {0,1,2,4};
const int wtsize[4] = {0,1,2,4}, wtalign[4] = {0,1,2,4};

inline int align(int n, int align) {
	return (n + (align - 1)) & ~(align - 1);
}

int DecFmtSize(u8 fmt) {
	switch (fmt) {
	case DEC_NONE: return 0;
	case DEC_FLOAT_1: return 4;
	case DEC_FLOAT_2: return 8;
	case DEC_FLOAT_3: return 12;
	case DEC_FLOAT_4: return 16;
	case DEC_S8_3: return 4;
	case DEC_S16_3: return 8;
	case DEC_U8_4: return 4;
	default:
		return 0;
	}
}

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

void VertexDecoder::Step_WeightsU8() const
{
	float *wt = (float *)decoded_;
	const u8 *wdata = (const u8*)(ptr_);
	for (int j = 0; j < nweights; j++)
		wt[j] = (float)wdata[j] / 128.0f;
	decoded_ += nweights * sizeof(float);
}

void VertexDecoder::Step_WeightsU16() const
{
	float *wt = (float *)decoded_;
	const u16 *wdata = (const u16*)(ptr_);
	for (int j = 0; j < nweights; j++)
		wt[j] = (float)wdata[j] / 32768.0f;
	decoded_ += nweights * sizeof(float);
}

void VertexDecoder::Step_WeightsFloat() const
{
	float *wt = (float *)decoded_;
	const float *wdata = (const float*)(ptr_);
	for (int j = 0; j < nweights; j++)
		wt[j] = wdata[j];
	decoded_ += nweights * sizeof(float);
}

void VertexDecoder::Step_TcU8() const
{
	float *uv = (float *)decoded_;
	const u8 *uvdata = (const u8*)(ptr_ + tcoff);
	for (int j = 0; j < 2; j++)
		uv[j] = (float)uvdata[j] / 128.0f;
	decoded_ += 2 * sizeof(float);
}

void VertexDecoder::Step_TcU16() const
{
	float *uv = (float *)decoded_;
	const u16 *uvdata = (const u16*)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] / 32768.0f;
	uv[1] = (float)uvdata[1] / 32768.0f;
	decoded_ += 2 * sizeof(float);
}

void VertexDecoder::Step_TcU16Through() const
{
	float *uv = (float *)decoded_;
	const u16 *uvdata = (const u16*)(ptr_ + tcoff);
	uv[0] = (float)uvdata[0] / (float)(gstate_c.curTextureWidth);
	uv[1] = (float)uvdata[1] / (float)(gstate_c.curTextureHeight);
	decoded_ += 2 * sizeof(float);
}

void VertexDecoder::Step_TcFloat() const
{
	float *uv = (float *)decoded_;
	const float *uvdata = (const float*)(ptr_ + tcoff);
	uv[0] = uvdata[0];
	uv[1] = uvdata[1];
	decoded_ += 2 * sizeof(float);
}

void VertexDecoder::Step_TcFloatThrough() const
{
	float *uv = (float *)decoded_;
	const float *uvdata = (const float*)(ptr_ + tcoff);
	uv[0] = uvdata[0] / (float)(gstate_c.curTextureWidth);
	uv[1] = uvdata[1] / (float)(gstate_c.curTextureHeight);
	decoded_ += 2 * sizeof(float);
}

void VertexDecoder::Step_Color565() const
{
	u8 *c = decoded_;
	u16 cdata = *(u16*)(ptr_ + coloff);
	c[0] = Convert5To8(cdata & 0x1f);
	c[1] = Convert6To8((cdata>>5) & 0x3f);
	c[2] = Convert5To8((cdata>>11) & 0x1f);
	c[3] = 1.0f;
	decoded_ += 4;
}

void VertexDecoder::Step_Color5551() const
{
	u8 *c = decoded_;
	u16 cdata = *(u16*)(ptr_ + coloff);
	c[0] = Convert5To8(cdata & 0x1f);
	c[1] = Convert5To8((cdata>>5) & 0x1f);
	c[2] = Convert5To8((cdata>>10) & 0x1f);
	c[3] = (cdata>>15) ? 255 : 0;
	decoded_ += 4;
}

void VertexDecoder::Step_Color4444() const
{
	u8 *c = decoded_;
	u16 cdata = *(u16*)(ptr_ + coloff);
	for (int j = 0; j < 4; j++)
		c[j] = Convert4To8((cdata >> (j * 4)) & 0xF);
	decoded_ += 4;
}

void VertexDecoder::Step_Color8888() const
{
	u8 *c = decoded_;
	// TODO: speedup
	u8 *cdata = (u8*)(ptr_ + coloff);
	for (int j = 0; j < 4; j++)
		c[j] = cdata[j];
	decoded_ += 4;
}

void VertexDecoder::Step_Color565Morph() const
{
	float col[3] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(u16*)(ptr_ + onesize_*n + coloff);
		col[0] += w * (cdata & 0x1f) / 31.f;
		col[1] += w * ((cdata>>5) & 0x3f) / 63.f;
		col[2] += w * ((cdata>>11) & 0x1f) / 31.f;
	}
	for (int i = 0; i < 3; i++) {
		decoded_[i] = (u8)(col[i] * 255.0f);
	}
	decoded_[3] = 255;
	decoded_ += 4;
}

void VertexDecoder::Step_Color5551Morph() const
{
	float col[4] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(u16*)(ptr_ + onesize_*n + coloff);
		col[0] += w * (cdata & 0x1f) / 31.f;
		col[1] += w * ((cdata>>5) & 0x1f) / 31.f;
		col[2] += w * ((cdata>>10) & 0x1f) / 31.f;
		col[3] += w * (cdata>>15) ? 1.0f : 0.0f;
	}
	for (int i = 0; i < 4; i++) {
		decoded_[i] = (u8)(col[i] * 255.0f);
	}
	decoded_ += 4;
}

void VertexDecoder::Step_Color4444Morph() const
{
	float col[4] = {0};
	for (int n = 0; n < morphcount; n++)
	{
		float w = gstate_c.morphWeights[n];
		u16 cdata = *(u16*)(ptr_ + onesize_*n + coloff);
		for (int j = 0; j < 4; j++)
			col[j] += w * ((cdata >> (j * 4)) & 0xF) / 15.f;
	}
	for (int i = 0; i < 4; i++) {
		decoded_[i] = (u8)(col[i] * 255.0f);
	}
	decoded_ += 4;
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
	for (int i = 0; i < 4; i++) {
		decoded_[i] = (u8)(col[i]);
	}
	decoded_ += 4;
}

void VertexDecoder::Step_NormalS8() const
{
	float *normal = (float *)decoded_;
	float multiplier = 1.0f;
	if (gstate.reversenormals & 0xFFFFFF)
		multiplier = -multiplier;
	const s8 *sv = (const s8*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = (sv[j]/127.0f) * multiplier;
	decoded_ += 12;
}

void VertexDecoder::Step_NormalS16() const
{
	float *normal = (float *)decoded_;
	float multiplier = 1.0f;
	if (gstate.reversenormals & 0xFFFFFF)
		multiplier = -multiplier;
	const short *sv = (const short*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = (sv[j]/32767.0f) * multiplier;
	decoded_ += 12;
}

void VertexDecoder::Step_NormalFloat() const
{
	float *normal = (float *)decoded_;
	float multiplier = 1.0f;
	if (gstate.reversenormals & 0xFFFFFF)
		multiplier = -multiplier;
	const float *fv = (const float*)(ptr_ + nrmoff);
	for (int j = 0; j < 3; j++)
		normal[j] = fv[j] * multiplier;
	decoded_ += 12;
}

void VertexDecoder::Step_NormalS8Morph() const
{
	float *normal = (float *)decoded_;
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		if (gstate.reversenormals & 0xFFFFFF) {
			multiplier = -multiplier;
		}
		const s8 *sv = (const s8*)(ptr_ + onesize_*n + nrmoff);
		for (int j = 0; j < 3; j++)
			normal[j] += (sv[j]/32767.0f) * multiplier;
	}
	decoded_ += 12;
}

void VertexDecoder::Step_NormalS16Morph() const
{
	float *normal = (float *)decoded_;
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		if (gstate.reversenormals & 0xFFFFFF) {
			multiplier = -multiplier;
		}
		const float *fv = (const float*)(ptr_ + onesize_*n + nrmoff);
		for (int j = 0; j < 3; j++)
			normal[j] += fv[j] * multiplier;
	}
	decoded_ += 12;
}

void VertexDecoder::Step_NormalFloatMorph() const
{
	float *normal = (float *)decoded_;
	memset(normal, 0, sizeof(float)*3);
	for (int n = 0; n < morphcount; n++)
	{
		float multiplier = gstate_c.morphWeights[n];
		if (gstate.reversenormals & 0xFFFFFF) {
			multiplier = -multiplier;
		}
		const float *fv = (const float*)(ptr_ + onesize_*n + nrmoff);
		for (int j = 0; j < 3; j++)
			normal[j] += fv[j] * multiplier;
	}
	decoded_ += 12;
}

void VertexDecoder::Step_PosS8() const
{
	float *v = (float *)decoded_;
	float multiplier = 1.0f / 127.0f;
	const s8 *sv = (const s8*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j] * multiplier;
	decoded_ += 12;
}

void VertexDecoder::Step_PosS16() const
{
	float *v = (float *)decoded_;
	float multiplier = 1.0f / 32767.0f;
	const short *sv = (const short*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j] * multiplier;
	decoded_ += 12;
}

void VertexDecoder::Step_PosFloat() const
{
	float *v = (float *)decoded_;
	const float *fv = (const float*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = fv[j];
	decoded_ += 12;
}

void VertexDecoder::Step_PosS8Through() const
{
	float *v = (float *)decoded_;
	const s8 *sv = (const s8*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j];
	decoded_ += 12;
}

void VertexDecoder::Step_PosS16Through() const
{
	float *v = (float *)decoded_;
	const short *sv = (const short*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = sv[j];
	decoded_ += 12;
}

void VertexDecoder::Step_PosFloatThrough() const
{
	float *v = (float *)decoded_;
	const float *fv = (const float*)(ptr_ + posoff);
	for (int j = 0; j < 3; j++)
		v[j] = fv[j];
	decoded_ += 12;
}

void VertexDecoder::Step_PosS8Morph() const
{
	float *v = (float *)decoded_;
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const s8 *sv = (const s8*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += (sv[j] / 127.f) * gstate_c.morphWeights[n];
	}
	decoded_ += 12;
}

void VertexDecoder::Step_PosS16Morph() const
{
	float *v = (float *)decoded_;
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		float multiplier = 1.0f / 32767.0f;
		const short *sv = (const short*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += (sv[j] * multiplier) * gstate_c.morphWeights[n];
	}
	decoded_ += 12;
}

void VertexDecoder::Step_PosFloatMorph() const
{
	float *v = (float *)decoded_;
	memset(v, 0, sizeof(float) * 3);
	for (int n = 0; n < morphcount; n++) {
		const float *fv = (const float*)(ptr_ + onesize_*n + posoff);
		for (int j = 0; j < 3; j++)
			v[j] += fv[j] * gstate_c.morphWeights[n];
	}
	decoded_ += 12;
}

const StepFunction wtstep[4] = {
	0,
	&VertexDecoder::Step_WeightsU8,
	&VertexDecoder::Step_WeightsU16,
	&VertexDecoder::Step_WeightsFloat,
};

const StepFunction tcstep[4] = {
	0,
	&VertexDecoder::Step_TcU8,
	&VertexDecoder::Step_TcU16,
	&VertexDecoder::Step_TcFloat,
};

const StepFunction tcstep_through[4] = {
	0,
	&VertexDecoder::Step_TcU8,
	&VertexDecoder::Step_TcU16Through,
	&VertexDecoder::Step_TcFloatThrough,
};

// TODO: Tc Morph

const StepFunction colstep[8] = {
	0, 0, 0, 0,
	&VertexDecoder::Step_Color565,
	&VertexDecoder::Step_Color5551,
	&VertexDecoder::Step_Color4444,
	&VertexDecoder::Step_Color8888,
};

const StepFunction colstep_morph[8] = {
	0, 0, 0, 0,
	&VertexDecoder::Step_Color565Morph,
	&VertexDecoder::Step_Color5551Morph,
	&VertexDecoder::Step_Color4444Morph,
	&VertexDecoder::Step_Color8888Morph,
};

const StepFunction nrmstep[4] = {
	0,
	&VertexDecoder::Step_NormalS8,
	&VertexDecoder::Step_NormalS16,
	&VertexDecoder::Step_NormalFloat,
};

const StepFunction nrmstep_morph[4] = {
	0,
	&VertexDecoder::Step_NormalS8Morph,
	&VertexDecoder::Step_NormalS16Morph,
	&VertexDecoder::Step_NormalFloatMorph,
};

const StepFunction posstep[4] = {
	0,
	&VertexDecoder::Step_PosS8,
	&VertexDecoder::Step_PosS16,
	&VertexDecoder::Step_PosFloat,
};

const StepFunction posstep_morph[4] = {
	0,
	&VertexDecoder::Step_PosS8Morph,
	&VertexDecoder::Step_PosS16Morph,
	&VertexDecoder::Step_PosFloatMorph,
};

const StepFunction posstep_through[4] = {
	0,
	&VertexDecoder::Step_PosS8Through,
	&VertexDecoder::Step_PosS16Through,
	&VertexDecoder::Step_PosFloatThrough,
};


void VertexDecoder::SetVertexType(u32 fmt) {
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
		//size = align(size, wtalign[weighttype]);	unnecessary
		size += wtsize[weighttype] * nweights;
		if (wtalign[weighttype] > biggest)
			biggest = wtalign[weighttype];

		steps_[numSteps_++] = wtstep[weighttype];

		if (nweights < 5) {
			decFmt.w0off = decOff;
			decFmt.w0fmt = DEC_FLOAT_1 + nweights - 1;
		} else {
			decFmt.w0off = decOff;
			decFmt.w0fmt = DEC_FLOAT_4;
			decFmt.w1off = decOff + 4 * 4;
			decFmt.w1fmt = DEC_FLOAT_1 + nweights - 5;
		}
		decOff += nweights * 4;
	}

	if (tc) {
		size = align(size, tcalign[tc]);
		tcoff = size;
		size += tcsize[tc];
		if (tcalign[tc] > biggest)
			biggest = tcalign[tc];

		steps_[numSteps_++] = throughmode ? tcstep_through[tc] : tcstep[tc];

		// All UV decode to DEC_FLOAT2 currently.
		decFmt.uvfmt = DEC_FLOAT_2;
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

		// The normal formats match the gl formats perfectly, let's use 'em.
		switch (nrm) {
		case GE_VTYPE_NRM_8BIT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_S8_3; break;
		case GE_VTYPE_NRM_16BIT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_S16_3; break;
		case GE_VTYPE_NRM_FLOAT >> GE_VTYPE_NRM_SHIFT: decFmt.nrmfmt = DEC_FLOAT_3; break;
		}

		// Actually, temporarily let's not.
		decFmt.nrmfmt = DEC_FLOAT_3;
		decFmt.nrmoff = decOff;
		decOff += DecFmtSize(decFmt.nrmfmt);
	}

	//if (pos)  - there's always a position
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

			// The non-through-mode position formats match the gl formats perfectly, let's use 'em.
			switch (pos) {
			case GE_VTYPE_POS_8BIT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_S8_3; break;
			case GE_VTYPE_POS_16BIT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_S16_3; break;
			case GE_VTYPE_POS_FLOAT >> GE_VTYPE_POS_SHIFT: decFmt.posfmt = DEC_FLOAT_3; break;
			}
			// Actually, temporarily let's not.
			decFmt.posfmt = DEC_FLOAT_3;
		}
		decFmt.posoff = decOff;
		decOff += DecFmtSize(decFmt.posfmt);
	}
	decFmt.stride = decOff;

	size = align(size, biggest);
	onesize_ = size;
	size *= morphcount;
	DEBUG_LOG(G3D,"SVT : size = %i, aligned to biggest %i", size, biggest);
}

void VertexDecoder::DecodeVerts(u8 *decodedptr, const void *verts, const void *inds, int prim, int count, int *indexLowerBound, int *indexUpperBound) const
{
	// Find index bounds. Could cache this in display lists.
	int lowerBound = 0x7FFFFFFF;
	int upperBound = 0;
	if (idx == (GE_VTYPE_IDX_8BIT >> GE_VTYPE_IDX_SHIFT)) {
		const u8 *ind8 = (const u8 *)inds;
		for (int i = 0; i < count; i++) {
			if (ind8[i] < lowerBound)
				lowerBound = ind8[i];
			if (ind8[i] > upperBound)
				upperBound = ind8[i];
		}
	} else if (idx == (GE_VTYPE_IDX_16BIT >> GE_VTYPE_IDX_SHIFT)) {
		const u16 *ind16 = (const u16*)inds;
		for (int i = 0; i < count; i++) {
			if (ind16[i] < lowerBound)
				lowerBound = ind16[i];
			if (ind16[i] > upperBound)
				upperBound = ind16[i];
		}
	} else {
		lowerBound = 0;
		upperBound = count - 1;
	}
	*indexLowerBound = lowerBound;
	*indexUpperBound = upperBound;

	// Decode the vertices within the found bounds, once each
	decoded_ = decodedptr;
	ptr_ = (const u8*)verts + lowerBound * size;
	for (int index = lowerBound; index <= upperBound; index++)
	{
		for (int i = 0; i < numSteps_; i++) {
			((*this).*steps_[i])();
		}
		ptr_ += size;
	}
}

