#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/System.h"
#include "UI/DebugOverlay.h"
#include "Core/HW/Display.h"
#include "Core/HLE/sceSas.h"
#include "Core/ControlMapper.h"
#include "Core/Config.h"
#include "GPU/GPU.h"
// TODO: This should be moved here or to Common, doesn't belong in /GPU
#include "GPU/Vulkan/DebugVisVulkan.h"

static void DrawDebugStats(UIContext *ctx, const Bounds &bounds) {
	FontID ubuntu24("UBUNTU24");

	float left = std::max(bounds.w / 2 - 20.0f, 550.0f);
	float right = bounds.w - left - 20.0f;

	char statbuf[4096];

	ctx->Flush();
	ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(.7f, .7f);

	__DisplayGetDebugStats(statbuf, sizeof(statbuf));
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 11, bounds.y + 31, left, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 10, bounds.y + 30, left, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT);

	__SasGetDebugStats(statbuf, sizeof(statbuf));
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + left + 21, bounds.y + 31, right, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + left + 20, bounds.y + 30, right, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT);

	ctx->Draw()->SetFontScale(1.0f, 1.0f);
	ctx->Flush();
	ctx->RebindTexture();
}

static void DrawAudioDebugStats(UIContext *ctx, const Bounds &bounds) {
	FontID ubuntu24("UBUNTU24");

	char statbuf[4096] = { 0 };
	System_AudioGetDebugStats(statbuf, sizeof(statbuf));

	ctx->Flush();
	ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(0.7f, 0.7f);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 11, bounds.y + 31, bounds.w - 20, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 10, bounds.y + 30, bounds.w - 20, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT);
	ctx->Draw()->SetFontScale(1.0f, 1.0f);
	ctx->Flush();
	ctx->RebindTexture();
}

static void DrawControlDebug(UIContext *ctx, const ControlMapper &mapper, const Bounds &bounds) {
	FontID ubuntu24("UBUNTU24");

	char statbuf[4096] = { 0 };
	mapper.GetDebugString(statbuf, sizeof(statbuf));

	ctx->Flush();
	ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(0.5f, 0.5f);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 11, bounds.y + 31, bounds.w - 20, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 10, bounds.y + 30, bounds.w - 20, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	ctx->Draw()->SetFontScale(1.0f, 1.0f);
	ctx->Flush();
	ctx->RebindTexture();
}

static void DrawFrameTimes(UIContext *ctx, const Bounds &bounds) {
	FontID ubuntu24("UBUNTU24");
	int valid, pos;
	double *sleepHistory;
	double *history = __DisplayGetFrameTimes(&valid, &pos, &sleepHistory);
	int scale = 7000;
	int width = 600;

	ctx->Flush();
	ctx->BeginNoTex();
	int bottom = bounds.y2();
	for (int i = 0; i < valid; ++i) {
		double activeTime = history[i] - sleepHistory[i];
		ctx->Draw()->vLine(bounds.x + i, bottom, bottom - activeTime * scale, 0xFF3FFF3F);
		ctx->Draw()->vLine(bounds.x + i, bottom - activeTime * scale, bottom - history[i] * scale, 0x7F3FFF3F);
	}
	ctx->Draw()->vLine(bounds.x + pos, bottom, bottom - 512, 0xFFff3F3f);

	ctx->Draw()->hLine(bounds.x, bottom - 0.0333 * scale, bounds.x + width, 0xFF3f3Fff);
	ctx->Draw()->hLine(bounds.x, bottom - 0.0167 * scale, bounds.x + width, 0xFF3f3Fff);

	ctx->Flush();
	ctx->Begin();
	ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(0.5f, 0.5f);
	ctx->Draw()->DrawText(ubuntu24, "33.3ms", bounds.x + width, bottom - 0.0333 * scale, 0xFF3f3Fff, ALIGN_BOTTOMLEFT | FLAG_DYNAMIC_ASCII);
	ctx->Draw()->DrawText(ubuntu24, "16.7ms", bounds.x + width, bottom - 0.0167 * scale, 0xFF3f3Fff, ALIGN_BOTTOMLEFT | FLAG_DYNAMIC_ASCII);
	ctx->Draw()->SetFontScale(1.0f, 1.0f);
	ctx->Flush();
	ctx->RebindTexture();
}


void DrawDebugOverlay(UIContext *ctx, const Bounds &bounds, const ControlMapper &controlMapper, DebugOverlay overlay) {
	switch (overlay) {
	case DebugOverlay::DEBUG_STATS:
		DrawDebugStats(ctx, ctx->GetLayoutBounds());
		break;
	case DebugOverlay::FRAME_GRAPH:
		DrawFrameTimes(ctx, ctx->GetLayoutBounds());
		break;
	case DebugOverlay::AUDIO:
		DrawAudioDebugStats(ctx, ctx->GetLayoutBounds());
		break;
	case DebugOverlay::CONTROL:
		DrawControlDebug(ctx, controlMapper, ctx->GetLayoutBounds());
		break;
#if !PPSSPP_PLATFORM(UWP) && !PPSSPP_PLATFORM(SWITCH)

	case DebugOverlay::GPU_PROFILE:
		if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
			DrawGPUProfilerVis(ctx, gpu);
		}
		break;
	case DebugOverlay::GPU_ALLOCATOR:
		if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
			DrawGPUMemoryVis(ctx, gpu);
		}
		break;
#endif
	}
}
