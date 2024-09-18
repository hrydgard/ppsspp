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

#include "ppsspp_config.h"
#include "Common/Common.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreParameter.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

// This must be aligned so that the matrices within are aligned.
alignas(16) GPUgstate gstate;
// Let's align this one too for good measure.
alignas(16) GPUStateCache gstate_c;

// For save state compatibility.
static int savedContextVersion = 1;

struct CmdRange {
	u8 start;
	u8 end;
};

static const CmdRange contextCmdRanges[] = {
	{0x00, 0x02},
	// Skip: {0x03, 0x0F},
	{0x10, 0x10},
	// Skip: {0x11, 0x11},
	{0x12, 0x28},
	// Skip: {0x29, 0x2B},
	{0x2c, 0x33},
	// Skip: {0x34, 0x35},
	{0x36, 0x38},
	// Skip: {0x39, 0x41},
	{0x42, 0x4D},
	// Skip: {0x4E, 0x4F},
	{0x50, 0x51},
	// Skip: {0x52, 0x52},
	{0x53, 0x58},
	// Skip: {0x59, 0x5A},
	{0x5B, 0xB5},
	// Skip: {0xB6, 0xB7},
	{0xB8, 0xC3},
	// Skip: {0xC4, 0xC4},
	{0xC5, 0xD0},
	// Skip: {0xD1, 0xD1}
	{0xD2, 0xE9},
	// Skip: {0xEA, 0xEA},
	{0xEB, 0xEC},
	// Skip: {0xED, 0xED},
	{0xEE, 0xEE},
	// Skip: {0xEF, 0xEF},
	{0xF0, 0xF6},
	// Skip: {0xF7, 0xF7},
	{0xF8, 0xF9},
	// Skip: {0xFA, 0xFF},
};

static u32_le *SaveMatrix(u32_le *cmds, GEMatrixType type, int sz, int numcmd, int datacmd) {
	if (!gpu)
		return cmds;

	*cmds++ = numcmd << 24;
	// This saves the CPU-visible values, not the actual used ones, which may differ.
	// Note that Restore overwrites both values.
	if (type == GE_MTX_BONE0) {
		for (int i = 0; i < 8; ++i)
			gpu->GetMatrix24(GEMatrixType(GE_MTX_BONE0 + i), cmds + i * 12, datacmd << 24);
	} else {
		gpu->GetMatrix24(type, cmds, datacmd << 24);
	}
	cmds += sz;

	return cmds;
}

static const u32_le *LoadMatrix(const u32_le *cmds, float *mtx, int sz) {
	// Skip the reset.
	cmds++;
	for (int i = 0; i < sz; ++i) {
		mtx[i] = getFloat24(*cmds++);
	}

	return cmds;
}

void GPUgstate::Reset() {
	memset(gstate.cmdmem, 0, sizeof(gstate.cmdmem));
	for (int i = 0; i < 256; i++) {
		gstate.cmdmem[i] = i << 24;
	}

	// Lighting is not enabled by default, matrices are zero initialized.
	memset(gstate.worldMatrix, 0, sizeof(gstate.worldMatrix));
	memset(gstate.viewMatrix, 0, sizeof(gstate.viewMatrix));
	memset(gstate.projMatrix, 0, sizeof(gstate.projMatrix));
	memset(gstate.tgenMatrix, 0, sizeof(gstate.tgenMatrix));
	memset(gstate.boneMatrix, 0, sizeof(gstate.boneMatrix));

	savedContextVersion = 1;

	gstate_c.Dirty(DIRTY_CULL_PLANES);
}

void GPUgstate::Save(u32_le *ptr) {
	// Not sure what the first 10 values are, exactly, but these seem right.
	ptr[5] = gstate_c.vertexAddr;
	ptr[6] = gstate_c.indexAddr;
	ptr[7] = gstate_c.offsetAddr;

	// Command values start 17 ints in.
	u32_le *cmds = ptr + 17;
	for (size_t i = 0; i < ARRAY_SIZE(contextCmdRanges); ++i) {
		for (int n = contextCmdRanges[i].start; n <= contextCmdRanges[i].end; ++n) {
			// We'll run ReapplyGfxState after this to process dirtying.
			*cmds++ = cmdmem[n];
		}
	}

	if (savedContextVersion == 0) {
		if (Memory::IsValidAddress(getClutAddress()))
			*cmds++ = loadclut;

		// Seems like it actually writes commands to load the matrices and then reset the counts.
		*cmds++ = boneMatrixNumber;
		*cmds++ = worldmtxnum;
		*cmds++ = viewmtxnum;
		*cmds++ = projmtxnum;
		*cmds++ = texmtxnum;

		u8 *matrices = (u8 *)cmds;
		memcpy(matrices, boneMatrix, sizeof(boneMatrix)); matrices += sizeof(boneMatrix);
		memcpy(matrices, worldMatrix, sizeof(worldMatrix)); matrices += sizeof(worldMatrix);
		memcpy(matrices, viewMatrix, sizeof(viewMatrix)); matrices += sizeof(viewMatrix);
		memcpy(matrices, projMatrix, sizeof(projMatrix)); matrices += sizeof(projMatrix);
		memcpy(matrices, tgenMatrix, sizeof(tgenMatrix)); matrices += sizeof(tgenMatrix);
	} else {
		cmds = SaveMatrix(cmds, GE_MTX_BONE0, ARRAY_SIZE(boneMatrix), GE_CMD_BONEMATRIXNUMBER, GE_CMD_BONEMATRIXDATA);
		cmds = SaveMatrix(cmds, GE_MTX_WORLD, ARRAY_SIZE(worldMatrix), GE_CMD_WORLDMATRIXNUMBER, GE_CMD_WORLDMATRIXDATA);
		cmds = SaveMatrix(cmds, GE_MTX_VIEW, ARRAY_SIZE(viewMatrix), GE_CMD_VIEWMATRIXNUMBER, GE_CMD_VIEWMATRIXDATA);
		cmds = SaveMatrix(cmds, GE_MTX_PROJECTION, ARRAY_SIZE(projMatrix), GE_CMD_PROJMATRIXNUMBER, GE_CMD_PROJMATRIXDATA);
		cmds = SaveMatrix(cmds, GE_MTX_TEXGEN, ARRAY_SIZE(tgenMatrix), GE_CMD_TGENMATRIXNUMBER, GE_CMD_TGENMATRIXDATA);

		*cmds++ = boneMatrixNumber & 0xFF00007F;
		*cmds++ = worldmtxnum & 0xFF00000F;
		*cmds++ = viewmtxnum & 0xFF00000F;
		*cmds++ = projmtxnum & 0xFF00000F;
		*cmds++ = texmtxnum & 0xFF00000F;
		*cmds++ = GE_CMD_END << 24;
	}
}

void GPUgstate::FastLoadBoneMatrix(u32 addr) {
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(addr);
	u32 num = boneMatrixNumber;
	u32 *dst = (u32 *)(boneMatrix + (num & 0x7F));

#ifdef _M_SSE
	__m128i row1 = _mm_slli_epi32(_mm_loadu_si128((const __m128i *)src), 8);
	__m128i row2 = _mm_slli_epi32(_mm_loadu_si128((const __m128i *)(src + 4)), 8);
	__m128i row3 = _mm_slli_epi32(_mm_loadu_si128((const __m128i *)(src + 8)), 8);
	if ((num & 0x3) == 0) {
		_mm_store_si128((__m128i *)dst, row1);
		_mm_store_si128((__m128i *)(dst + 4), row2);
		_mm_store_si128((__m128i *)(dst + 8), row3);
	} else {
		_mm_storeu_si128((__m128i *)dst, row1);
		_mm_storeu_si128((__m128i *)(dst + 4), row2);
		_mm_storeu_si128((__m128i *)(dst + 8), row3);
	}
#elif PPSSPP_ARCH(ARM_NEON)
	const uint32x4_t row1 = vshlq_n_u32(vld1q_u32(src), 8);
	const uint32x4_t row2 = vshlq_n_u32(vld1q_u32(src + 4), 8);
	const uint32x4_t row3 = vshlq_n_u32(vld1q_u32(src + 8), 8);
	vst1q_u32(dst, row1);
	vst1q_u32(dst + 4, row2);
	vst1q_u32(dst + 8, row3);
#else
	for (int i = 0; i < 12; i++) {
		dst[i] = src[i] << 8;
	}
#endif

	num += 12;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x00FFFFFF);
}

void GPUgstate::Restore(const u32_le *ptr) {
	// Not sure what the first 10 values are, exactly, but these seem right.
	gstate_c.vertexAddr = ptr[5];
	gstate_c.indexAddr = ptr[6];
	gstate_c.offsetAddr = ptr[7];

	// Command values start 17 ints in.
	const u32_le *cmds = ptr + 17;
	for (size_t i = 0; i < ARRAY_SIZE(contextCmdRanges); ++i) {
		for (int n = contextCmdRanges[i].start; n <= contextCmdRanges[i].end; ++n) {
			cmdmem[n] = *cmds++;
		}
	}

	if (savedContextVersion == 0) {
		if (Memory::IsValidAddress(getClutAddress()))
			loadclut = *cmds++;
		boneMatrixNumber = *cmds++;
		worldmtxnum = *cmds++;
		viewmtxnum = *cmds++;
		projmtxnum = *cmds++;
		texmtxnum = *cmds++;

		u8 *matrices = (u8 *)cmds;
		memcpy(boneMatrix, matrices, sizeof(boneMatrix)); matrices += sizeof(boneMatrix);
		memcpy(worldMatrix, matrices, sizeof(worldMatrix)); matrices += sizeof(worldMatrix);
		memcpy(viewMatrix, matrices, sizeof(viewMatrix)); matrices += sizeof(viewMatrix);
		memcpy(projMatrix, matrices, sizeof(projMatrix)); matrices += sizeof(projMatrix);
		memcpy(tgenMatrix, matrices, sizeof(tgenMatrix)); matrices += sizeof(tgenMatrix);
	} else {
		cmds = LoadMatrix(cmds, boneMatrix, ARRAY_SIZE(boneMatrix));
		cmds = LoadMatrix(cmds, worldMatrix, ARRAY_SIZE(worldMatrix));
		cmds = LoadMatrix(cmds, viewMatrix, ARRAY_SIZE(viewMatrix));
		cmds = LoadMatrix(cmds, projMatrix, ARRAY_SIZE(projMatrix));
		cmds = LoadMatrix(cmds, tgenMatrix, ARRAY_SIZE(tgenMatrix));

		boneMatrixNumber = (*cmds++) & 0xFF00007F;
		worldmtxnum = (*cmds++) & 0xFF00000F;
		viewmtxnum = (*cmds++) & 0xFF00000F;
		projmtxnum = (*cmds++) & 0xFF00000F;
		texmtxnum = (*cmds++) & 0xFF00000F;
	}

	if (gpu)
		gpu->ResetMatrices();

	gstate_c.Dirty(DIRTY_CULL_PLANES);
}

bool vertTypeIsSkinningEnabled(u32 vertType) {
	return ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE);
}

struct GPUStateCache_v0 {
	u32 vertexAddr;
	u32 indexAddr;

	u32 offsetAddr;

	bool textureChanged;
	bool textureFullAlpha;
	bool vertexFullAlpha;
	bool framebufChanged;

	int skipDrawReason;

	UVScale uv;
	bool flipTexture;
};

void GPUStateCache::Reset() {
	memset(&gstate_c, 0, sizeof(gstate_c));
}

void GPUStateCache::DoState(PointerWrap &p) {
	auto s = p.Section("GPUStateCache", 0, 5);
	if (!s) {
		// Old state, this was not versioned.
		GPUStateCache_v0 old;
		Do(p, old);

		vertexAddr = old.vertexAddr;
		indexAddr = old.indexAddr;
		offsetAddr = old.offsetAddr;
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		textureFullAlpha = old.textureFullAlpha;
		vertexFullAlpha = old.vertexFullAlpha;
		skipDrawReason = old.skipDrawReason;
		uv = old.uv;

		savedContextVersion = 0;
	} else {
		Do(p, vertexAddr);
		Do(p, indexAddr);
		Do(p, offsetAddr);

		uint8_t textureChanged = 0;
		Do(p, textureChanged);  // legacy
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		Do(p, textureFullAlpha);
		Do(p, vertexFullAlpha);
		bool framebufChanged = false;  // legacy
		Do(p, framebufChanged);

		Do(p, skipDrawReason);

		Do(p, uv);

		bool oldFlipTexture = false;
		Do(p, oldFlipTexture);  // legacy
	}

	// needShaderTexClamp and bgraTexture don't need to be saved.

	if (s >= 3) {
		bool oldTextureSimpleAlpha = false;
		Do(p, oldTextureSimpleAlpha);
	}

	if (s < 2) {
		float l12[12];
		float l4[4];
		Do(p, l12);  // lightpos
		Do(p, l12);  // lightdir
		Do(p, l12);  // lightattr
		Do(p, l12);  // lightcol0
		Do(p, l12);  // lightcol1
		Do(p, l12);  // lightcol2
		Do(p, l4);    // lightangle
		Do(p, l4);  // lightspot
	}

	Do(p, morphWeights);

	Do(p, curTextureWidth);
	Do(p, curTextureHeight);
	Do(p, actualTextureHeight);
	// curTextureXOffset and curTextureYOffset don't need to be saved.  Well, the above don't either...

	Do(p, vpWidth);
	Do(p, vpHeight);
	if (s == 4) {
		float oldDepth = 1.0f;
		Do(p, oldDepth);
	}

	Do(p, curRTWidth);
	Do(p, curRTHeight);

	// curRTBufferWidth, curRTBufferHeight, and cutRTOffsetX don't need to be saved.
	if (s < 5) {
		savedContextVersion = 0;
	} else {
		Do(p, savedContextVersion);
	}

	if (p.GetMode() == PointerWrap::MODE_READ)
		gstate_c.Dirty(DIRTY_CULL_PLANES);
}

static const char *const gpuUseFlagNames[32] = {
	"GPU_USE_DUALSOURCE_BLEND",
	"GPU_USE_LIGHT_UBERSHADER",
	"GPU_USE_FRAGMENT_TEST_CACHE",
	"GPU_USE_VS_RANGE_CULLING",
	"GPU_USE_BLEND_MINMAX",
	"GPU_USE_LOGIC_OP",
	"GPU_USE_FRAGMENT_UBERSHADER",
	"GPU_USE_TEXTURE_NPOT",
	"GPU_USE_ANISOTROPY",
	"GPU_USE_CLEAR_RAM_HACK",
	"GPU_USE_INSTANCE_RENDERING",
	"GPU_USE_VERTEX_TEXTURE_FETCH",
	"GPU_USE_TEXTURE_FLOAT",
	"GPU_USE_16BIT_FORMATS",
	"GPU_USE_DEPTH_CLAMP",
	"GPU_USE_TEXTURE_LOD_CONTROL",
	"GPU_USE_DEPTH_TEXTURE",
	"GPU_USE_ACCURATE_DEPTH",
	"GPU_USE_GS_CULLING",
	"N/A",
	"GPU_USE_FRAMEBUFFER_FETCH",
	"GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT",
	"GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT",
	"GPU_ROUND_DEPTH_TO_16BIT",
	"GPU_USE_CLIP_DISTANCE",
	"GPU_USE_CULL_DISTANCE",
	"N/A", // bit 26
	"N/A", // bit 27
	"N/A", // bit 28
	"GPU_USE_VIRTUAL_REALITY",
	"GPU_USE_SINGLE_PASS_STEREO",
	"GPU_USE_SIMPLE_STEREO_PERSPECTIVE",
};

const char *GpuUseFlagToString(int useFlag) {
	if ((u32)useFlag < 32) {
		return gpuUseFlagNames[useFlag];
	} else {
		return "N/A";
	}
}
