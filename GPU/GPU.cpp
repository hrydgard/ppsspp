// Copyright (c) 2015- PPSSPP Project.

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

#include "Core/Core.h"

#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/Null/NullGpu.h"
#include "GPU/Software/SoftGpu.h"

#if defined(_WIN32)
#include "GPU/Directx9/helper/global.h"
#include "GPU/Directx9/GPU_DX9.h"
#endif

GPUStatistics gpuStats;
GPUInterface *gpu;
GPUDebugInterface *gpuDebug;

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
		SetGPU(new GLES_GPU());
		break;
	case GPU_SOFTWARE:
		SetGPU(new SoftGPU());
		break;
	case GPU_DIRECTX9:
#if defined(_WIN32)
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
