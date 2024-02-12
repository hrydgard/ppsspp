#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/System.h"
#include "Common/Data/Text/I18n.h"
#include "Core/MIPS/MIPS.h"
#include "Core/HW/Display.h"
#include "Core/FrameTiming.h"
#include "Core/HLE/sceSas.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/scePower.h"
#include "Core/HLE/Plugins.h"
#include "Core/ControlMapper.h"
#include "Core/Config.h"
#include "Core/MemFault.h"
#include "Core/Reporting.h"
#include "Core/CwCheat.h"
#include "Core/Core.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/Util/GameDB.h"
#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
// TODO: This should be moved here or to Common, doesn't belong in /GPU
#include "GPU/Vulkan/DebugVisVulkan.h"
#include "GPU/Common/FramebufferManagerCommon.h"

#include "UI/DevScreens.h"
#include "UI/DebugOverlay.h"

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
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 11, bounds.y + 31, left, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 10, bounds.y + 30, left, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);

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
	ctx->Draw()->SetFontScale(0.5f, 0.5f);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 11, bounds.y + 31, bounds.w - 20, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + 10, bounds.y + 30, bounds.w - 20, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);

	float left = std::max(bounds.w / 2 - 20.0f, 500.0f);

	__SasGetDebugStats(statbuf, sizeof(statbuf));
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + left + 21, bounds.y + 31, bounds.w - left, bounds.h - 30, 0xc0000000, FLAG_DYNAMIC_ASCII);
	ctx->Draw()->DrawTextRect(ubuntu24, statbuf, bounds.x + left + 20, bounds.y + 30, bounds.w - left, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);

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

void DrawFramebufferList(UIContext *ctx, GPUInterface *gpu, const Bounds &bounds) {
	if (!gpu) {
		return;
	}
	FontID ubuntu24("UBUNTU24");
	auto list = gpu->GetFramebufferList();
	ctx->Flush();
	ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(0.7f, 0.7f);

	int i = 0;
	for (const VirtualFramebuffer *vfb : list) {
		char buf[512];
		snprintf(buf, sizeof(buf), "%08x (Z %08x): %dx%d (stride %d, %d)",
			vfb->fb_address, vfb->z_address, vfb->width, vfb->height, vfb->fb_stride, vfb->z_stride);
		ctx->Draw()->DrawTextRect(ubuntu24, buf, bounds.x + 10, bounds.y + 20 + i * 50, bounds.w - 20, bounds.h - 30, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
		i++;
	}
	ctx->Flush();
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
	case DebugOverlay::FRAMEBUFFER_LIST:
		if (inGame)
			DrawFramebufferList(ctx, gpu, bounds);
		break;
	default:
		break;
	}
}


static const char *CPUCoreAsString(int core) {
	switch (core) {
	case 0: return "Interpreter";
	case 1: return "JIT";
	case 2: return "IR Interpreter";
	case 3: return "JIT using IR";
	default: return "N/A";
	}
}

void DrawCrashDump(UIContext *ctx, const Path &gamePath) {
	const MIPSExceptionInfo &info = Core_GetExceptionInfo();

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	FontID ubuntu24("UBUNTU24");
	std::string discID = g_paramSFO.GetDiscID();
	int x = 20 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT);
	int y = 20 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP);

	ctx->Flush();
	if (ctx->Draw()->GetFontAtlas()->getFont(ubuntu24))
		ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(1.1f, 1.1f);
	ctx->Draw()->DrawTextShadow(ubuntu24, sy->T_cstr("Game crashed"), x, y, 0xFFFFFFFF);

	char statbuf[4096];
	char versionString[256];
	snprintf(versionString, sizeof(versionString), "%s", PPSSPP_GIT_VERSION);

	bool checkingISO = false;
	bool isoOK = false;

	char crcStr[50]{};
	if (Reporting::HasCRC(gamePath)) {
		u32 crc = Reporting::RetrieveCRC(gamePath);
		std::vector<GameDBInfo> dbInfos;
		if (g_gameDB.GetGameInfos(discID, &dbInfos)) {
			for (auto &dbInfo : dbInfos) {
				if (dbInfo.crc == crc) {
					isoOK = true;
				}
			}
		}
		snprintf(crcStr, sizeof(crcStr), "CRC: %08x %s\n", crc, isoOK ? "(Known good!)" : "(not identified)");
	} else {
		// Queue it for calculation, we want it!
		// It's OK to call this repeatedly until we have it, which is natural here.
		Reporting::QueueCRC(gamePath);
		checkingISO = true;
	}

	// TODO: Draw a lot more information. Full register set, and so on.

#ifdef _DEBUG
	char build[] = "debug";
#else
	char build[] = "release";
#endif

	std::string sysName = System_GetProperty(SYSPROP_NAME);
	int sysVersion = System_GetPropertyInt(SYSPROP_SYSTEMVERSION);

	// First column
	y += 65;

	int columnWidth = (ctx->GetBounds().w - x - 10) / 2;
	int height = ctx->GetBounds().h;

	ctx->PushScissor(Bounds(x, y, columnWidth, height));

	// INFO_LOG(SYSTEM, "DrawCrashDump (%d %d %d %d)", x, y, columnWidth, height);

	snprintf(statbuf, sizeof(statbuf), R"(%s
%s (%s)
%s (%s)
%s v%d (%s)
%s
)",
ExceptionTypeAsString(info.type),
discID.c_str(), g_paramSFO.GetValueString("TITLE").c_str(),
versionString, build,
sysName.c_str(), sysVersion, GetCompilerABI(),
crcStr
);

	ctx->Draw()->SetFontScale(.7f, .7f);
	ctx->Draw()->DrawTextShadow(ubuntu24, statbuf, x, y, 0xFFFFFFFF);
	y += 160;

	if (info.type == MIPSExceptionType::MEMORY) {
		snprintf(statbuf, sizeof(statbuf), R"(
Access: %s at %08x (sz: %d)
PC: %08x
%s)",
MemoryExceptionTypeAsString(info.memory_type),
info.address,
info.accessSize,
info.pc,
info.info.c_str());
		ctx->Draw()->DrawTextShadow(ubuntu24, statbuf, x, y, 0xFFFFFFFF);
		y += 180;
	} else if (info.type == MIPSExceptionType::BAD_EXEC_ADDR) {
		snprintf(statbuf, sizeof(statbuf), R"(
Destination: %s to %08x
PC: %08x
RA: %08x)",
ExecExceptionTypeAsString(info.exec_type),
info.address,
info.pc,
info.ra);
		ctx->Draw()->DrawTextShadow(ubuntu24, statbuf, x, y, 0xFFFFFFFF);
		y += 180;
	} else if (info.type == MIPSExceptionType::BREAK) {
		snprintf(statbuf, sizeof(statbuf), R"(
BREAK
PC: %08x
)", info.pc);
		ctx->Draw()->DrawTextShadow(ubuntu24, statbuf, x, y, 0xFFFFFFFF);
		y += 180;
	} else {
		snprintf(statbuf, sizeof(statbuf), R"(
Invalid / Unknown (%d)
)", (int)info.type);
		ctx->Draw()->DrawTextShadow(ubuntu24, statbuf, x, y, 0xFFFFFFFF);
		y += 180;
	}

	std::string kernelState = __KernelStateSummary();

	ctx->Draw()->DrawTextShadow(ubuntu24, kernelState.c_str(), x, y, 0xFFFFFFFF);

	y += 40;

	ctx->Draw()->SetFontScale(.5f, .5f);

	ctx->Draw()->DrawTextShadow(ubuntu24, info.stackTrace.c_str(), x, y, 0xFFFFFFFF);

	ctx->Draw()->SetFontScale(.7f, .7f);

	ctx->PopScissor();

	// Draw some additional stuff to the right.

	std::string tips;
	if (CheatsInEffect()) {
		tips += "* Turn off cheats.\n";
	}
	if (GetLockedCPUSpeedMhz()) {
		tips += "* Set CPU clock to default (0)\n";
	}
	if (checkingISO) {
		tips += "* (waiting for CRC...)\n";
	} else if (!isoOK) {  // TODO: Should check that it actually is an ISO and not a homebrew
		tips += "* Verify and possibly re-dump your ISO\n  (CRC not recognized)\n";
	}
	if (!tips.empty()) {
		tips = "Things to try:\n" + tips;
	}

	x += columnWidth + 10;
	y = 85;
	snprintf(statbuf, sizeof(statbuf),
		"CPU Core: %s (flags: %08x)\n"
		"Locked CPU freq: %d MHz\n"
		"Cheats: %s, Plugins: %s\n\n%s",
		CPUCoreAsString(g_Config.iCpuCore), g_Config.uJitDisableFlags,
		GetLockedCPUSpeedMhz(),
		CheatsInEffect() ? "Y" : "N", HLEPlugins::HasEnabled() ? "Y" : "N", tips.c_str());

	ctx->Draw()->DrawTextShadow(ubuntu24, statbuf, x, y, 0xFFFFFFFF);
	ctx->Flush();
	ctx->Draw()->SetFontScale(1.0f, 1.0f);
	ctx->RebindTexture();
}

void DrawFPS(UIContext *ctx, const Bounds &bounds) {
	FontID ubuntu24("UBUNTU24");
	float vps, fps, actual_fps;
	__DisplayGetFPS(&vps, &fps, &actual_fps);

	char fpsbuf[256]{};
	if (g_Config.iShowStatusFlags == ((int)ShowStatusFlags::FPS_COUNTER | (int)ShowStatusFlags::SPEED_COUNTER)) {
		snprintf(fpsbuf, sizeof(fpsbuf), "%0.0f/%0.0f (%0.1f%%)", actual_fps, fps, vps / (59.94f / 100.0f));
	} else {
		if (g_Config.iShowStatusFlags & (int)ShowStatusFlags::FPS_COUNTER) {
			snprintf(fpsbuf, sizeof(fpsbuf), "FPS: %0.1f", actual_fps);
		}
		if (g_Config.iShowStatusFlags & (int)ShowStatusFlags::SPEED_COUNTER) {
			snprintf(fpsbuf, sizeof(fpsbuf), "%s Speed: %0.1f%%", fpsbuf, vps / (59.94f / 100.0f));
		}
	}

#ifdef CAN_DISPLAY_CURRENT_BATTERY_CAPACITY
	if (g_Config.iShowStatusFlags & (int)ShowStatusFlags::BATTERY_PERCENT) {
		snprintf(fpsbuf, sizeof(fpsbuf), "%s Battery: %d%%", fpsbuf, getCurrentBatteryCapacity());
	}
#endif

	ctx->Flush();
	ctx->BindFontTexture();
	ctx->Draw()->SetFontScale(0.7f, 0.7f);
	ctx->Draw()->DrawText(ubuntu24, fpsbuf, bounds.x2() - 8, 20, 0xc0000000, ALIGN_TOPRIGHT | FLAG_DYNAMIC_ASCII);
	ctx->Draw()->DrawText(ubuntu24, fpsbuf, bounds.x2() - 10, 19, 0xFF3fFF3f, ALIGN_TOPRIGHT | FLAG_DYNAMIC_ASCII);
	ctx->Draw()->SetFontScale(1.0f, 1.0f);
	ctx->Flush();
	ctx->RebindTexture();
}
