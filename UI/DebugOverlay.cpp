#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/System.h"
#include "UI/DebugOverlay.h"
#include "Core/HW/Display.h"
#include "Core/FrameTiming.h"
#include "Core/HLE/sceSas.h"
#include "Core/ControlMapper.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPU.h"
// TODO: This should be moved here or to Common, doesn't belong in /GPU
#include "GPU/Vulkan/DebugVisVulkan.h"

// For std::max
#include <algorithm>


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
	double *sleepHistory;
	int valid, pos;
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

static void DrawFrameTiming(UIContext *ctx, const Bounds &bounds) {
	FontID ubuntu24("UBUNTU24");

	char statBuf[1024]{};

	ctx->Flush();
	ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(0.5f, 0.5f);

	snprintf(statBuf, sizeof(statBuf),
		"Mode (interval): %s (%d)",
		Draw::PresentModeToString(g_frameTiming.presentMode),
		g_frameTiming.presentInterval);

	ctx->Draw()->DrawTextRect(ubuntu24, statBuf, bounds.x + 10, bounds.y + 50, bounds.w - 20, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);

	for (int i = 0; i < 5; i++) {
		size_t curIndex = i + 6;
		size_t prevIndex = i + 7;

		FrameTimeData data = ctx->GetDrawContext()->FrameTimeHistory().Back(curIndex);
		FrameTimeData prevData = ctx->GetDrawContext()->FrameTimeHistory().Back(prevIndex);
		if (data.frameBegin == 0.0) {
			snprintf(statBuf, sizeof(statBuf), "(No frame time data)");
		} else {
			double stride = data.frameBegin - prevData.frameBegin;
			double fenceLatency_s = data.afterFenceWait - data.frameBegin;
			double submitLatency_s = data.firstSubmit - data.frameBegin;
			double queuePresentLatency_s = data.queuePresent - data.frameBegin;
			double actualPresentLatency_s = data.actualPresent - data.frameBegin;
			double presentMargin = data.presentMargin;
			double computedMargin = data.actualPresent - data.queuePresent;

			char presentStats[256] = "";
			if (data.actualPresent != 0.0) {
				snprintf(presentStats, sizeof(presentStats),
					"* Present: %0.1f ms\n"
					"* Margin: %0.1f ms\n"
					"* Margin(c): %0.1f ms\n",
					actualPresentLatency_s * 1000.0,
					presentMargin * 1000.0,
					computedMargin * 1000.0);
			}
			snprintf(statBuf, sizeof(statBuf),
				"* Stride: %0.1f (waits: %d)\n"
				"%llu: From start:\n"
				"* Past fence: %0.1f ms\n"
				"* Submit #1: %0.1f ms\n"
				"* Queue-p: %0.1f ms\n"
				"%s",
				stride * 1000.0,
				data.waitCount,
				(long long)data.frameId,
				fenceLatency_s * 1000.0,
				submitLatency_s * 1000.0,
				queuePresentLatency_s * 1000.0,
				presentStats
			);
		}
		ctx->Draw()->DrawTextRect(ubuntu24, statBuf, bounds.x + 10 + i * 150, bounds.y + 150, bounds.w - 20, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	}
	ctx->Draw()->SetFontScale(1.0f, 1.0f);
	ctx->Flush();
	ctx->RebindTexture();
}

void DrawControlMapperOverlay(UIContext *ctx, const Bounds &bounds, const ControlMapper &controlMapper) {
	DrawControlDebug(ctx, controlMapper, ctx->GetLayoutBounds());
}

void DrawDebugOverlay(UIContext *ctx, const Bounds &bounds, DebugOverlay overlay) {
	bool inGame = GetUIState() == UISTATE_INGAME;

	switch (overlay) {
	case DebugOverlay::DEBUG_STATS:
		if (inGame)
			DrawDebugStats(ctx, ctx->GetLayoutBounds());
		break;
	case DebugOverlay::FRAME_GRAPH:
		if (inGame)
			DrawFrameTimes(ctx, ctx->GetLayoutBounds());
		break;
	case DebugOverlay::FRAME_TIMING:
		DrawFrameTiming(ctx, ctx->GetLayoutBounds());
		break;
	case DebugOverlay::AUDIO:
		DrawAudioDebugStats(ctx, ctx->GetLayoutBounds());
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
	default:
		break;
	}
}
