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

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#ifndef _XBOX
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/GLES_GPU.h"
#endif
#include "GPU/Null/NullGpu.h"
#include "GPU/Software/SoftGpu.h"
#if defined(_XBOX) || defined(_WIN32)
#include "GPU/Directx9/helper/global.h"
#include "GPU/Directx9/GPU_DX9.h"
#endif
#include "Common/ChunkFile.h"
#include "Core/CoreParameter.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/MemMap.h"
#ifdef _M_SSE
#include <emmintrin.h>
#endif

// This must be aligned so that the matrices within are aligned.
GPUgstate MEMORY_ALIGNED16(gstate);
// Let's align this one too for good measure.
GPUStateCache MEMORY_ALIGNED16(gstate_c);

GPUInterface *gpu;
GPUDebugInterface *gpuDebug;
GPUStatistics gpuStats;

template <typename T>
static void SetGPU(T *obj) {
	gpu = obj;
	gpuDebug = obj;
}

bool GPU_Init() {
	switch (PSP_CoreParameter().gpuCore) {
	case GPU_NULL:
		SetGPU(new NullGPU());
		break;
	case GPU_GLES:
#ifndef _XBOX
		SetGPU(new GLES_GPU());
#endif
		break;
	case GPU_SOFTWARE:
#ifndef _XBOX
		SetGPU(new SoftGPU());
#endif
		break;
	case GPU_DIRECTX9:
#if defined(_XBOX) || defined(_WIN32)
		SetGPU(new DIRECTX9_GPU());
#endif
		break;
	}

	return gpu != NULL;
}

void GPU_Shutdown() {
	delete gpu;
	gpu = 0;
	gpuDebug = 0;
}

void GPU_Reinitialize() {
	if (gpu) {
		gpu->Reinitialize();
	}
}

void InitGfxState() {
	memset(&gstate, 0, sizeof(gstate));
	memset(&gstate_c, 0, sizeof(gstate_c));
	for (int i = 0; i < 256; i++) {
		gstate.cmdmem[i] = i << 24;
	}

	// Lighting is not enabled by default, matrices are zero initialized.
	memset(gstate.worldMatrix, 0, sizeof(gstate.worldMatrix));
	memset(gstate.viewMatrix, 0, sizeof(gstate.viewMatrix));
	memset(gstate.projMatrix, 0, sizeof(gstate.projMatrix));
	memset(gstate.tgenMatrix, 0, sizeof(gstate.tgenMatrix));
	memset(gstate.boneMatrix, 0, sizeof(gstate.boneMatrix));
}

void ShutdownGfxState() {
}

// When you have changed state outside the psp gfx core,
// or saved the context and has reloaded it, call this function.
void ReapplyGfxState() {
	if (!gpu)
		return;
	gpu->ReapplyGfxState();
}

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

void GPUgstate::Save(u32_le *ptr) {
	// Not sure what the first 10 values are, exactly, but these seem right.
	ptr[5] = gstate_c.vertexAddr;
	ptr[6] = gstate_c.indexAddr;
	ptr[7] = gstate_c.offsetAddr;

	// Command values start 17 ints in.
	u32_le *cmds = ptr + 17;
	for (size_t i = 0; i < ARRAY_SIZE(contextCmdRanges); ++i) {
		for (int n = contextCmdRanges[i].start; n <= contextCmdRanges[i].end; ++n) {
			*cmds++ = cmdmem[n];
		}
	}

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
#else
	for (int i = 0; i < 12; i++) {
		dst[i] = src[i] << 8;
	}
#endif

	num += 12;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x7F);
}

void GPUgstate::Restore(u32_le *ptr) {
	// Not sure what the first 10 values are, exactly, but these seem right.
	gstate_c.vertexAddr = ptr[5];
	gstate_c.indexAddr = ptr[6];
	gstate_c.offsetAddr = ptr[7];

	// Command values start 17 ints in.
	u32_le *cmds = ptr + 17;
	for (size_t i = 0; i < ARRAY_SIZE(contextCmdRanges); ++i) {
		for (int n = contextCmdRanges[i].start; n <= contextCmdRanges[i].end; ++n) {
			cmdmem[n] = *cmds++;
		}
	}

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
}

bool vertTypeIsSkinningEnabled(u32 vertType) {
	if (g_Config.bSoftwareSkinning && ((vertType & GE_VTYPE_MORPHCOUNT_MASK) == 0))
		return false;
	else
		return ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE);
}

struct GPUStateCache_v0
{
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

void GPUStateCache::DoState(PointerWrap &p) {
	auto s = p.Section("GPUStateCache", 0, 3);
	if (!s) {
		// Old state, this was not versioned.
		GPUStateCache_v0 old;
		p.Do(old);

		vertexAddr = old.vertexAddr;
		indexAddr = old.indexAddr;
		offsetAddr = old.offsetAddr;
		textureChanged = TEXCHANGE_UPDATED;
		textureFullAlpha = old.textureFullAlpha;
		vertexFullAlpha = old.vertexFullAlpha;
		framebufChanged = old.framebufChanged;
		skipDrawReason = old.skipDrawReason;
		uv = old.uv;
		flipTexture = old.flipTexture;
	} else {
		p.Do(vertexAddr);
		p.Do(indexAddr);
		p.Do(offsetAddr);

		p.Do(textureChanged);
		p.Do(textureFullAlpha);
		p.Do(vertexFullAlpha);
		p.Do(framebufChanged);

		p.Do(skipDrawReason);

		p.Do(uv);
		p.Do(flipTexture);
	}

	// needShaderTexClamp doesn't need to be saved.

	if (s >= 3) {
		p.Do(textureSimpleAlpha);
	} else {
		textureSimpleAlpha = false;
	}

	if (s < 2) {
		float l12[12];
		float l4[4];
		p.Do(l12);  // lightpos
		p.Do(l12);  // lightdir
		p.Do(l12);  // lightattr
		p.Do(l12);  // lightcol0
		p.Do(l12);  // lightcol1
		p.Do(l12);  // lightcol2
		p.Do(l4);    // lightangle
		p.Do(l4);  // lightspot
	}

	p.Do(morphWeights);

	p.Do(curTextureWidth);
	p.Do(curTextureHeight);
	p.Do(actualTextureHeight);
	// curTextureXOffset and curTextureYOffset don't need to be saved.  Well, the above don't either...

	p.Do(vpWidth);
	p.Do(vpHeight);

	p.Do(curRTWidth);
	p.Do(curRTHeight);

	// curRTBufferWidth, curRTBufferHeight, and cutRTOffsetX don't need to be saved.
}
