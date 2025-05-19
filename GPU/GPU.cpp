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

#include "ppsspp_config.h"

#include "Common/TimeUtil.h"
#include "Common/GraphicsContext.h"
#include "Core/Core.h"
#include "Core/System.h"

#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"

#if PPSSPP_API(ANY_GL)
#include "GPU/GLES/GPU_GLES.h"
#endif
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Software/SoftGpu.h"

#if PPSSPP_API(D3D9)
#include "GPU/Directx9/GPU_DX9.h"
#endif

#if PPSSPP_API(D3D11)
#include "GPU/D3D11/GPU_D3D11.h"
#endif

GPUStatistics gpuStats;
GPUCommon *gpu;
GPUDebugInterface *gpuDebug;

#ifdef USE_CRT_DBG
#undef new
#endif

static GPUCommon *CreateGPUCore(GPUCore gpuCore, GraphicsContext *ctx, Draw::DrawContext *draw) {
#if PPSSPP_PLATFORM(UWP)
	// TODO: Can probably remove this special case.
	if (gpuCore == GPUCORE_SOFTWARE) {
		return new SoftGPU(ctx, draw);
	} else {
		return new GPU_D3D11(ctx, draw);
	}
#else
	switch (gpuCore) {
	case GPUCORE_GLES:
		// Disable GLES on ARM Windows (but leave it enabled on other ARM platforms).
#if PPSSPP_API(ANY_GL)
		return new GPU_GLES(ctx, draw);
#else
		return nullptr;
#endif
	case GPUCORE_SOFTWARE:
		return new SoftGPU(ctx, draw);
	case GPUCORE_DIRECTX9:
#if PPSSPP_API(D3D9)
		return new GPU_DX9(ctx, draw);
#else
		return nullptr;
#endif
	case GPUCORE_DIRECTX11:
#if PPSSPP_API(D3D11)
		return new GPU_D3D11(ctx, draw);
#else
		return nullptr;
#endif
#if !PPSSPP_PLATFORM(SWITCH)
	case GPUCORE_VULKAN:
		if (!ctx) {
			// Can this happen?
			ERROR_LOG(Log::G3D, "Unable to init Vulkan GPU backend, no context");
			return nullptr;
		}
		return new GPU_Vulkan(ctx, draw);
#endif
	default:
		return nullptr;
	}
#endif
}

bool GPU_Init(GPUCore gpuCore, GraphicsContext *ctx, Draw::DrawContext *draw) {
	_dbg_assert_(draw || gpuCore == GPUCORE_SOFTWARE);
	GPUCommon *createdGPU = CreateGPUCore(gpuCore, ctx, draw);

	// This can happen on some memory allocation failure, but can probably just be ignored in practice.
	if (createdGPU && !createdGPU->IsStarted()) {
		delete createdGPU;
		createdGPU = nullptr;
	}

	if (createdGPU) {
		gpu = createdGPU;
		gpuDebug = createdGPU;
	}

	return gpu != nullptr;
}

#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

void GPU_Shutdown() {
	delete gpu;
	gpu = nullptr;
	gpuDebug = nullptr;
}

const char *RasterChannelToString(RasterChannel channel) {
	return channel == RASTER_COLOR ? "COLOR" : "DEPTH";
}
