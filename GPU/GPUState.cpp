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
#include "Core/CoreParameter.h"
#include "Core/System.h"

GPUgstate gstate;
GPUStateCache gstate_c;
GPUInterface *gpu;
GPUStatistics gpuStats;

bool GPU_Init() {
	switch (PSP_CoreParameter().gpuCore) {
	case GPU_NULL:
		gpu = new NullGPU();
		break;
	case GPU_GLES:
#ifndef _XBOX
		gpu = new GLES_GPU();
#endif
		break;
	case GPU_SOFTWARE:
#if !(defined(__SYMBIAN32__) || defined(_XBOX))
		gpu = new SoftGPU();
#endif
		break;
	case GPU_DIRECTX9:
#if defined(_XBOX)
		gpu = new DIRECTX9_GPU();
#elif defined(_WIN32)
		gpu = new DIRECTX9_GPU();
#endif
		break;
	}

	return gpu != NULL;
}

void GPU_Shutdown() {
	delete gpu;
	gpu = 0;
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

	// Command values start 17 bytes in.
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

void GPUgstate::Restore(u32_le *ptr) {
	// Not sure what the first 10 values are, exactly, but these seem right.
	gstate_c.vertexAddr = ptr[5];
	gstate_c.indexAddr = ptr[6];
	gstate_c.offsetAddr = ptr[7];

	// Command values start 17 bytes in.
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
