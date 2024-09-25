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

// Hack around name collisions between UI and xlib
// Only affects this file.
#undef VK_USE_PLATFORM_XLIB_KHR
#undef VK_USE_PLATFORM_XCB_KHR
#undef VK_USE_PLATFORM_DIRECTFB_EXT
#undef VK_USE_PLATFORM_XLIB_XRANDR_EXT

#include <algorithm>
#include <cstring>

#include "ppsspp_config.h"

#include "Common/Common.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "Common/File/AndroidStorage.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Net/HTTPClient.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"
#include "Common/UI/IconCache.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Profiler/Profiler.h"

#include "Common/Log/LogManager.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/CoreParameter.h"
#include "Core/HLE/sceKernel.h"  // GPI/GPO
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "GPU/Debugger/Record.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "UI/MiscScreens.h"
#include "UI/DevScreens.h"
#include "UI/MainScreen.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/JitCompareScreen.h"

#ifdef _WIN32
// Want to avoid including the full header here as it includes d3dx.h
int GetD3DCompilerVersion();
#endif

#include "android/jni/app-android.h"

static const char *logLevelList[] = {
	"Notice",
	"Error",
	"Warn",
	"Info",
	"Debug",
	"Verb."
};

static const char *g_debugOverlayList[] = {
	"Off",
	"Debug stats",
	"Draw Frametimes Graph",
	"Frame timing",
#ifdef USE_PROFILER
	"Frame profile",
#endif
	"Control Debug",
	"Audio Debug",
	"GPU Profile",
	"GPU Allocator Viewer",
	"Framebuffer List",
};

void AddOverlayList(UI::ViewGroup *items, ScreenManager *screenManager) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	int numOverlays = ARRAY_SIZE(g_debugOverlayList);
	if (!(g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL)) {
		numOverlays -= 2;  // skip the last 2.
	}
	items->Add(new PopupMultiChoice((int *)&g_Config.iDebugOverlay, dev->T("Debug overlay"), g_debugOverlayList, 0, numOverlays, I18NCat::DEVELOPER, screenManager));
}

void DevMenuScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayout(ORIENT_VERTICAL);

#if !defined(MOBILE_DEVICE)
	items->Add(new Choice(dev->T("Log View")))->OnClick.Handle(this, &DevMenuScreen::OnLogView);
#endif
	items->Add(new Choice(dev->T("Logging Channels")))->OnClick.Handle(this, &DevMenuScreen::OnLogConfig);
	items->Add(new Choice(sy->T("Developer Tools")))->OnClick.Handle(this, &DevMenuScreen::OnDeveloperTools);

	// Debug overlay
	AddOverlayList(items, screenManager());

	items->Add(new Choice(dev->T("Jit Compare")))->OnClick.Handle(this, &DevMenuScreen::OnJitCompare);
	items->Add(new Choice(dev->T("Shader Viewer")))->OnClick.Handle(this, &DevMenuScreen::OnShaderView);

	items->Add(new Choice(dev->T("Toggle Freeze")))->OnClick.Add([](UI::EventParams &e) {
		if (PSP_CoreParameter().frozen) {
			PSP_CoreParameter().frozen = false;
		} else {
			PSP_CoreParameter().freezeNext = true;
		}
		return UI::EVENT_DONE;
	});

	items->Add(new Choice(dev->T("Reset limited logging")))->OnClick.Handle(this, &DevMenuScreen::OnResetLimitedLogging);

	items->Add(new Choice(dev->T("GPI/GPO switches/LEDs")))->OnClick.Add([=](UI::EventParams &e) {
		screenManager()->push(new GPIGPOScreen(dev->T("GPI/GPO switches/LEDs")));
		return UI::EVENT_DONE;
	});

	items->Add(new Choice(dev->T("Create frame dump")))->OnClick.Add([](UI::EventParams &e) {
		GPURecord::RecordNextFrame([](const Path &dumpPath) {
			NOTICE_LOG(Log::System, "Frame dump created at '%s'", dumpPath.c_str());
			if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
				System_ShowFileInFolder(dumpPath);
			} else {
				g_OSD.Show(OSDType::MESSAGE_SUCCESS, dumpPath.ToVisualString(), 7.0f);
			}
		});
		return UI::EVENT_DONE;
	});

	// This one is not very useful these days, and only really on desktop. Hide it on other platforms.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
		items->Add(new Choice(dev->T("Dump next frame to log")))->OnClick.Add([](UI::EventParams &e) {
			gpu->DumpNextFrame();
			return UI::EVENT_DONE;
		});
	}

	scroll->Add(items);
	parent->Add(scroll);

	RingbufferLogListener *ring = LogManager::GetInstance()->GetRingbufferListener();
	if (ring) {
		ring->SetEnabled(true);
	}
}

UI::EventReturn DevMenuScreen::OnResetLimitedLogging(UI::EventParams &e) {
	Reporting::ResetCounts();
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenuScreen::OnLogView(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new LogScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenuScreen::OnLogConfig(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenuScreen::OnDeveloperTools(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new DeveloperToolsScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenuScreen::OnJitCompare(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new JitCompareScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenuScreen::OnShaderView(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	if (gpu)  // Avoid crashing if chosen while the game is being loaded.
		screenManager()->push(new ShaderListScreen());
	return UI::EVENT_DONE;
}

void DevMenuScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	UpdateUIState(UISTATE_INGAME);
	// Close when a subscreen got closed.
	// TODO: a bug in screenmanager causes this not to work here.
	// TriggerFinish(DR_OK);
}

void GPIGPOScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	parent->Add(new CheckBox(&g_Config.bShowGPOLEDs, dev->T("Show GPO LEDs")));
	for (int i = 0; i < 8; i++) {
		std::string name = ApplySafeSubstitutions(dev->T("GPI switch %1"), i);
		parent->Add(new BitCheckBox(&g_GPIBits, 1 << i, name));
	}
}

void LogScreen::UpdateLog() {
	using namespace UI;
	RingbufferLogListener *ring = LogManager::GetInstance()->GetRingbufferListener();
	if (!ring)
		return;
	vert_->Clear();
	for (int i = ring->GetCount() - 1; i >= 0; i--) {
		TextView *v = vert_->Add(new TextView(ring->TextAt(i), FLAG_DYNAMIC_ASCII, false));
		uint32_t color = 0xFFFFFF;
		switch (ring->LevelAt(i)) {
		case LogLevel::LDEBUG: color = 0xE0E0E0; break;
		case LogLevel::LWARNING: color = 0x50FFFF; break;
		case LogLevel::LERROR: color = 0x5050FF; break;
		case LogLevel::LNOTICE: color = 0x30FF30; break;
		case LogLevel::LINFO: color = 0xFFFFFF; break;
		case LogLevel::LVERBOSE: color = 0xC0C0C0; break;
		}
		v->SetTextColor(0xFF000000 | color);
	}
	toBottom_ = true;
}

void LogScreen::update() {
	UIDialogScreenWithBackground::update();
	if (toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}
}

void LogScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);

	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	root_ = outer;

	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	bottom->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	cmdLine_ = bottom->Add(new TextEdit("", "Command", "Command Line", new LinearLayoutParams(1.0)));
	cmdLine_->OnEnter.Handle(this, &LogScreen::OnSubmit);
	bottom->Add(new Button(di->T("Submit")))->OnClick.Handle(this, &LogScreen::OnSubmit);

	vert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert_->SetSpacing(0);

	UpdateLog();
}

UI::EventReturn LogScreen::OnSubmit(UI::EventParams &e) {
	std::string cmd = cmdLine_->GetText();

	// TODO: Can add all sorts of fun stuff here that we can't be bothered writing proper UI for, like various memdumps etc.

	NOTICE_LOG(Log::System, "Submitted: %s", cmd.c_str());

	UpdateLog();
	cmdLine_->SetText("");
	cmdLine_->SetFocus();
	return UI::EVENT_DONE;
}

void LogConfigScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	root_ = new ScrollView(ORIENT_VERTICAL);

	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert->SetSpacing(0);

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice(di->T("Toggle All")))->OnClick.Handle(this, &LogConfigScreen::OnToggleAll);
	topbar->Add(new Choice(di->T("Enable All")))->OnClick.Handle(this, &LogConfigScreen::OnEnableAll);
	topbar->Add(new Choice(di->T("Disable All")))->OnClick.Handle(this, &LogConfigScreen::OnDisableAll);
	topbar->Add(new Choice(dev->T("Log Level")))->OnClick.Handle(this, &LogConfigScreen::OnLogLevel);

	vert->Add(topbar);

	vert->Add(new ItemHeader(dev->T("Logging Channels")));

	LogManager *logMan = LogManager::GetInstance();

	int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		Log type = (Log)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(cellSize - 50, WRAP_CONTENT));
		row->SetSpacing(0);
		row->Add(new CheckBox(&chan->enabled, "", "", new LinearLayoutParams(50, WRAP_CONTENT)));
		row->Add(new PopupMultiChoice((int *)&chan->level, chan->m_shortName, logLevelList, 1, 6, I18NCat::NONE, screenManager(), new LinearLayoutParams(1.0)));
		grid->Add(row);
	}
}

UI::EventReturn LogConfigScreen::OnToggleAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = logMan->GetLogChannel((Log)i);
		chan->enabled = !chan->enabled;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnEnableAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = logMan->GetLogChannel((Log)i);
		chan->enabled = true;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnDisableAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = logMan->GetLogChannel((Log)i);
		chan->enabled = false;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnLogLevelChange(UI::EventParams &e) {
	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnLogLevel(UI::EventParams &e) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	auto logLevelScreen = new LogLevelScreen(dev->T("Log Level"));
	logLevelScreen->OnChoice.Handle(this, &LogConfigScreen::OnLogLevelChange);
	if (e.v)
		logLevelScreen->SetPopupOrigin(e.v);
	screenManager()->push(logLevelScreen);
	return UI::EVENT_DONE;
}

LogLevelScreen::LogLevelScreen(std::string_view title) : ListPopupScreen(title) {
	int NUMLOGLEVEL = 6;    
	std::vector<std::string> list;
	for (int i = 0; i < NUMLOGLEVEL; ++i) {
		list.push_back(logLevelList[i]);
	}
	adaptor_ = UI::StringVectorListAdaptor(list, -1);
}

void LogLevelScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	int selected = listView_->GetSelected();
	LogManager *logMan = LogManager::GetInstance();
	
	for (int i = 0; i < LogManager::GetNumChannels(); ++i) {
		Log type = (Log)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		if (chan->enabled)
			chan->level = (LogLevel)(selected + 1);
	}
}

struct JitDisableFlag {
	MIPSComp::JitDisable flag;
	const char *name;
};

// Please do not try to translate these :)
static const JitDisableFlag jitDisableFlags[] = {
	{ MIPSComp::JitDisable::ALU, "ALU" },
	{ MIPSComp::JitDisable::ALU_IMM, "ALU_IMM" },
	{ MIPSComp::JitDisable::ALU_BIT, "ALU_BIT" },
	{ MIPSComp::JitDisable::MULDIV, "MULDIV" },
	{ MIPSComp::JitDisable::FPU, "FPU" },
	{ MIPSComp::JitDisable::FPU_COMP, "FPU_COMP" },
	{ MIPSComp::JitDisable::FPU_XFER, "FPU_XFER" },
	{ MIPSComp::JitDisable::VFPU_VEC, "VFPU_VEC" },
	{ MIPSComp::JitDisable::VFPU_MTX_VTFM, "VFPU_MTX_VTFM" },
	{ MIPSComp::JitDisable::VFPU_MTX_VMSCL, "VFPU_MTX_VMSCL" },
	{ MIPSComp::JitDisable::VFPU_MTX_VMMUL, "VFPU_MTX_VMMUL" },
	{ MIPSComp::JitDisable::VFPU_MTX_VMMOV, "VFPU_MTX_VMMOV" },
	{ MIPSComp::JitDisable::VFPU_COMP, "VFPU_COMP" },
	{ MIPSComp::JitDisable::VFPU_XFER, "VFPU_XFER" },
	{ MIPSComp::JitDisable::LSU, "LSU" },
	{ MIPSComp::JitDisable::LSU_UNALIGNED, "LSU_UNALIGNED" },
	{ MIPSComp::JitDisable::LSU_FPU, "LSU_FPU" },
	{ MIPSComp::JitDisable::LSU_VFPU, "LSU_VFPU" },
	{ MIPSComp::JitDisable::SIMD, "SIMD" },
	{ MIPSComp::JitDisable::BLOCKLINK, "Block Linking" },
	{ MIPSComp::JitDisable::POINTERIFY, "Pointerify" },
	{ MIPSComp::JitDisable::STATIC_ALLOC, "Static regalloc" },
	{ MIPSComp::JitDisable::CACHE_POINTERS, "Cached pointers" },
	{ MIPSComp::JitDisable::REGALLOC_GPR, "GPR Regalloc across instructions" },
	{ MIPSComp::JitDisable::REGALLOC_FPR, "FPR Regalloc across instructions" },
};

void JitDebugScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	root_ = new ScrollView(ORIENT_VERTICAL);

	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert->SetSpacing(0);

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice(di->T("Disable All")))->OnClick.Handle(this, &JitDebugScreen::OnDisableAll);
	topbar->Add(new Choice(di->T("Enable All")))->OnClick.Handle(this, &JitDebugScreen::OnEnableAll);

	vert->Add(topbar);
	vert->Add(new ItemHeader(dev->T("Disabled JIT functionality")));

	for (auto flag : jitDisableFlags) {
		// Do not add translation of these.
		vert->Add(new BitCheckBox(&g_Config.uJitDisableFlags, (uint32_t)flag.flag, flag.name));
	}
}

UI::EventReturn JitDebugScreen::OnEnableAll(UI::EventParams &e) {
	g_Config.uJitDisableFlags &= ~(uint32_t)MIPSComp::JitDisable::ALL_FLAGS;
	return UI::EVENT_DONE;
}

UI::EventReturn JitDebugScreen::OnDisableAll(UI::EventParams &e) {
	g_Config.uJitDisableFlags |= (uint32_t)MIPSComp::JitDisable::ALL_FLAGS;
	return UI::EVENT_DONE;
}

void SystemInfoScreen::update() {
	TabbedUIDialogScreenWithGameBackground::update();
	g_OSD.NudgeSidebar();
}

void SystemInfoScreen::CreateTabs() {
	using namespace Draw;
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto si = GetI18NCategory(I18NCat::SYSINFO);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);

	LinearLayout *deviceSpecs = AddTab("Device Info", si->T("Device Info"));

	CollapsibleSection *systemInfo = deviceSpecs->Add(new CollapsibleSection(si->T("System Information")));
	systemInfo->Add(new InfoItem(si->T("System Name", "Name"), System_GetProperty(SYSPROP_NAME)));
#if PPSSPP_PLATFORM(ANDROID)
	systemInfo->Add(new InfoItem(si->T("System Version"), StringFromInt(System_GetPropertyInt(SYSPROP_SYSTEMVERSION))));
#elif PPSSPP_PLATFORM(WINDOWS)
	std::string sysVersion = System_GetProperty(SYSPROP_SYSTEMBUILD);
	if (!sysVersion.empty()) {
		systemInfo->Add(new InfoItem(si->T("OS Build"), sysVersion));
	}
#endif
	systemInfo->Add(new InfoItem(si->T("Lang/Region"), System_GetProperty(SYSPROP_LANGREGION)));
	std::string board = System_GetProperty(SYSPROP_BOARDNAME);
	if (!board.empty())
		systemInfo->Add(new InfoItem(si->T("Board"), board));
	systemInfo->Add(new InfoItem(si->T("ABI"), GetCompilerABI()));
	if (System_GetPropertyBool(SYSPROP_DEBUGGER_PRESENT)) {
		systemInfo->Add(new InfoItem(si->T("Debugger Present"), di->T("Yes")));
	}

	CollapsibleSection *cpuInfo = deviceSpecs->Add(new CollapsibleSection(si->T("CPU Information")));

	// Don't bother showing the CPU name if we don't have one.
	if (strcmp(cpu_info.brand_string, "Unknown") != 0) {
		cpuInfo->Add(new InfoItem(si->T("CPU Name", "Name"), cpu_info.brand_string));
	}

	int totalThreads = cpu_info.num_cores * cpu_info.logical_cpu_count;
	std::string cores = StringFromFormat(si->T_cstr("%d (%d per core, %d cores)"), totalThreads, cpu_info.logical_cpu_count, cpu_info.num_cores);
	cpuInfo->Add(new InfoItem(si->T("Threads"), cores));
#if PPSSPP_PLATFORM(IOS)
	cpuInfo->Add(new InfoItem(si->T("JIT available"), System_GetPropertyBool(SYSPROP_CAN_JIT) ? di->T("Yes") : di->T("No")));
#endif

	CollapsibleSection *gpuInfo = deviceSpecs->Add(new CollapsibleSection(si->T("GPU Information")));

	DrawContext *draw = screenManager()->getDrawContext();

	const std::string apiNameKey = draw->GetInfoString(InfoField::APINAME);
	std::string_view apiName = gr->T(apiNameKey);
	gpuInfo->Add(new InfoItem(si->T("3D API"), apiName));

	// TODO: Not really vendor, on most APIs it's a device name (GL calls it vendor though).
	std::string vendorString;
	if (draw->GetDeviceCaps().deviceID != 0) {
		vendorString = StringFromFormat("%s (%08x)", draw->GetInfoString(InfoField::VENDORSTRING).c_str(), draw->GetDeviceCaps().deviceID);
	} else {
		vendorString = draw->GetInfoString(InfoField::VENDORSTRING);
	}
	gpuInfo->Add(new InfoItem(si->T("Vendor"), vendorString));
	std::string vendor = draw->GetInfoString(InfoField::VENDOR);
	if (vendor.size())
		gpuInfo->Add(new InfoItem(si->T("Vendor (detected)"), vendor));
	gpuInfo->Add(new InfoItem(si->T("Driver Version"), draw->GetInfoString(InfoField::DRIVER)));
#ifdef _WIN32
	if (GetGPUBackend() != GPUBackend::VULKAN) {
		gpuInfo->Add(new InfoItem(si->T("Driver Version"), System_GetProperty(SYSPROP_GPUDRIVER_VERSION)));
	}
#if !PPSSPP_PLATFORM(UWP)
	if (GetGPUBackend() == GPUBackend::DIRECT3D9) {
		gpuInfo->Add(new InfoItem(si->T("D3DCompiler Version"), StringFromFormat("%d", GetD3DCompilerVersion())));
	}
#endif
#endif
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		gpuInfo->Add(new InfoItem(si->T("Core Context"), gl_extensions.IsCoreContext ? di->T("Active") : di->T("Inactive")));
		int highp_int_min = gl_extensions.range[1][5][0];
		int highp_int_max = gl_extensions.range[1][5][1];
		int highp_float_min = gl_extensions.range[1][2][0];
		int highp_float_max = gl_extensions.range[1][2][1];
		if (highp_int_max != 0) {
			char temp[128];
			snprintf(temp, sizeof(temp), "%d-%d", highp_int_min, highp_int_max);
			gpuInfo->Add(new InfoItem(si->T("High precision int range"), temp));
		}
		if (highp_float_max != 0) {
			char temp[128];
			snprintf(temp, sizeof(temp), "%d-%d", highp_int_min, highp_int_max);
			gpuInfo->Add(new InfoItem(si->T("High precision float range"), temp));
		}
	}
	gpuInfo->Add(new InfoItem(si->T("Depth buffer format"), DataFormatToString(draw->GetDeviceCaps().preferredDepthBufferFormat)));

	std::string texCompressionFormats;
	// Simple non-detailed summary of supported tex compression formats.
	if (draw->GetDataFormatSupport(Draw::DataFormat::ETC2_R8G8B8_UNORM_BLOCK)) texCompressionFormats += "ETC2 ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::ASTC_4x4_UNORM_BLOCK)) texCompressionFormats += "ASTC ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::BC1_RGBA_UNORM_BLOCK)) texCompressionFormats += "BC1-3 ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::BC4_UNORM_BLOCK)) texCompressionFormats += "BC4-5 ";
	if (draw->GetDataFormatSupport(Draw::DataFormat::BC7_UNORM_BLOCK)) texCompressionFormats += "BC7 ";
	gpuInfo->Add(new InfoItem(si->T("Compressed texture formats"), texCompressionFormats));

	CollapsibleSection *osInformation = deviceSpecs->Add(new CollapsibleSection(si->T("OS Information")));
	osInformation->Add(new InfoItem(si->T("Memory Page Size"), StringFromFormat(si->T_cstr("%d bytes"), GetMemoryProtectPageSize())));
	osInformation->Add(new InfoItem(si->T("RW/RX exclusive"), PlatformIsWXExclusive() ? di->T("Active") : di->T("Inactive")));
#if PPSSPP_PLATFORM(ANDROID)
	osInformation->Add(new InfoItem(si->T("Sustained perf mode"), System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE) ? di->T("Supported") : di->T("Unsupported")));
#endif

	std::string_view build = si->T("Release");
#ifdef _DEBUG
	build = si->T("Debug");
#endif
	osInformation->Add(new InfoItem(si->T("PPSSPP build"), build));

	CollapsibleSection *audioInformation = deviceSpecs->Add(new CollapsibleSection(si->T("Audio Information")));
	audioInformation->Add(new InfoItem(si->T("Sample rate"), StringFromFormat(si->T_cstr("%d Hz"), System_GetPropertyInt(SYSPROP_AUDIO_SAMPLE_RATE))));
	int framesPerBuffer = System_GetPropertyInt(SYSPROP_AUDIO_FRAMES_PER_BUFFER);
	if (framesPerBuffer > 0) {
		audioInformation->Add(new InfoItem(si->T("Frames per buffer"), StringFromFormat("%d", framesPerBuffer)));
	}
#if PPSSPP_PLATFORM(ANDROID)
	audioInformation->Add(new InfoItem(si->T("Optimal sample rate"), StringFromFormat(si->T_cstr("%d Hz"), System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE))));
	audioInformation->Add(new InfoItem(si->T("Optimal frames per buffer"), StringFromFormat("%d", System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER))));
#endif

	CollapsibleSection *displayInfo = deviceSpecs->Add(new CollapsibleSection(si->T("Display Information")));
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(UWP)
	displayInfo->Add(new InfoItem(si->T("Native resolution"), StringFromFormat("%dx%d",
		System_GetPropertyInt(SYSPROP_DISPLAY_XRES),
		System_GetPropertyInt(SYSPROP_DISPLAY_YRES))));
#endif
	displayInfo->Add(new InfoItem(si->T("UI resolution"), StringFromFormat("%dx%d (%s: %0.2f)",
		g_display.dp_xres,
		g_display.dp_yres,
		si->T_cstr("DPI"),
		g_display.dpi)));
	displayInfo->Add(new InfoItem(si->T("Pixel resolution"), StringFromFormat("%dx%d",
		g_display.pixel_xres,
		g_display.pixel_yres)));

	const float insets[4] = {
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT),
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP),
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_RIGHT),
		System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_BOTTOM),
	};
	if (insets[0] != 0.0f || insets[1] != 0.0f || insets[2] != 0.0f || insets[3] != 0.0f) {
		displayInfo->Add(new InfoItem(si->T("Screen notch insets"), StringFromFormat("%0.1f %0.1f %0.1f %0.1f", insets[0], insets[1], insets[2], insets[3])));
	}

	// Don't show on Windows, since it's always treated as 60 there.
	displayInfo->Add(new InfoItem(si->T("Refresh rate"), StringFromFormat(si->T_cstr("%0.2f Hz"), (float)System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE))));
	std::string presentModes;
	if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::FIFO) presentModes += "FIFO, ";
	if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::IMMEDIATE) presentModes += "IMMEDIATE, ";
	if (draw->GetDeviceCaps().presentModesSupported & Draw::PresentMode::MAILBOX) presentModes += "MAILBOX, ";
	if (!presentModes.empty()) {
		presentModes.pop_back();
		presentModes.pop_back();
	}
	displayInfo->Add(new InfoItem(si->T("Present modes"), presentModes));

	CollapsibleSection *versionInfo = deviceSpecs->Add(new CollapsibleSection(si->T("Version Information")));
	std::string apiVersion;
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		if (gl_extensions.IsGLES) {
			apiVersion = StringFromFormat("v%d.%d.%d ES", gl_extensions.ver[0], gl_extensions.ver[1], gl_extensions.ver[2]);
		} else {
			apiVersion = StringFromFormat("v%d.%d.%d", gl_extensions.ver[0], gl_extensions.ver[1], gl_extensions.ver[2]);
		}
		versionInfo->Add(new InfoItem(si->T("API Version"), apiVersion));
	} else {
		apiVersion = draw->GetInfoString(InfoField::APIVERSION);
		if (apiVersion.size() > 30)
			apiVersion.resize(30);
		versionInfo->Add(new InfoItem(si->T("API Version"), apiVersion));

		if (GetGPUBackend() == GPUBackend::VULKAN) {
			std::string deviceApiVersion = draw->GetInfoString(InfoField::DEVICE_API_VERSION);
			versionInfo->Add(new InfoItem(si->T("Device API Version"), deviceApiVersion));
		}
	}
	versionInfo->Add(new InfoItem(si->T("Shading Language"), draw->GetInfoString(InfoField::SHADELANGVERSION)));

#if PPSSPP_PLATFORM(ANDROID)
	std::string moga = System_GetProperty(SYSPROP_MOGA_VERSION);
	if (moga.empty()) {
		moga = si->T("(none detected)");
	}
	versionInfo->Add(new InfoItem("Moga", moga));
#endif

	if (gstate_c.GetUseFlags()) {
		// We're in-game, and can determine these.
		// TODO: Call a static version of GPUCommon::CheckGPUFeatures() and derive them here directly.

		CollapsibleSection *gpuFlags = deviceSpecs->Add(new CollapsibleSection(si->T("GPU Flags")));

		for (int i = 0; i < 32; i++) {
			if (gstate_c.Use((1 << i))) {
				gpuFlags->Add(new TextView(GpuUseFlagToString(i), new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
			}
		}
	}

	LinearLayout *storage = AddTab("Storage", si->T("Storage"));

	storage->Add(new ItemHeader(si->T("Directories")));
	// Intentionally non-translated
	storage->Add(new InfoItem("MemStickDirectory", g_Config.memStickDirectory.ToVisualString()));
	storage->Add(new InfoItem("InternalDataDirectory", g_Config.internalDataDirectory.ToVisualString()));
	storage->Add(new InfoItem("AppCacheDir", g_Config.appCacheDirectory.ToVisualString()));
	storage->Add(new InfoItem("DefaultCurrentDir", g_Config.defaultCurrentDirectory.ToVisualString()));

#if PPSSPP_PLATFORM(ANDROID)
	storage->Add(new InfoItem("ExtFilesDir", g_extFilesDir));
	bool scoped = System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE);
	storage->Add(new InfoItem("Scoped Storage", scoped ? di->T("Yes") : di->T("No")));
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30) {
		// This flag is only relevant on Android API 30+.
		storage->Add(new InfoItem("IsStoragePreservedLegacy", Android_IsExternalStoragePreservedLegacy() ? di->T("Yes") : di->T("No")));
	}
#endif

	LinearLayout *buildConfig = AddTab("DevSystemInfoBuildConfig", si->T("Build Config"));

	buildConfig->Add(new ItemHeader(si->T("Build Configuration")));
#ifdef JENKINS
	buildConfig->Add(new InfoItem(si->T("Built by"), "Jenkins"));
#endif
#ifdef ANDROID_LEGACY
	buildConfig->Add(new InfoItem("ANDROID_LEGACY", ""));
#endif
#ifdef _DEBUG
	buildConfig->Add(new InfoItem("_DEBUG", ""));
#else
	buildConfig->Add(new InfoItem("NDEBUG", ""));
#endif
#ifdef USE_ASAN
	buildConfig->Add(new InfoItem("USE_ASAN", ""));
#endif
#ifdef USING_GLES2
	buildConfig->Add(new InfoItem("USING_GLES2", ""));
#endif
#ifdef MOBILE_DEVICE
	buildConfig->Add(new InfoItem("MOBILE_DEVICE", ""));
#endif
#if PPSSPP_ARCH(ARMV7S)
	buildConfig->Add(new InfoItem("ARMV7S", ""));
#endif
#if PPSSPP_ARCH(ARM_NEON)
	buildConfig->Add(new InfoItem("ARM_NEON", ""));
#endif
#ifdef _M_SSE
	buildConfig->Add(new InfoItem("_M_SSE", StringFromFormat("0x%x", _M_SSE)));
#endif
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		buildConfig->Add(new InfoItem("GOLD", ""));
	}

	LinearLayout *cpuExtensions = AddTab("DevSystemInfoCPUExt", si->T("CPU Extensions"));
	cpuExtensions->Add(new ItemHeader(si->T("CPU Extensions")));
	std::vector<std::string> exts = cpu_info.Features();
	for (std::string &ext : exts) {
		cpuExtensions->Add(new TextView(ext, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	LinearLayout *driverBugs = AddTab("DevSystemInfoDriverBugs", si->T("Driver bugs"));

	bool anyDriverBugs = false;
	for (int i = 0; i < (int)draw->GetBugs().MaxBugIndex(); i++) {
		if (draw->GetBugs().Has(i)) {
			anyDriverBugs = true;
			driverBugs->Add(new TextView(draw->GetBugs().GetBugName(i), new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}
	}

	if (!anyDriverBugs) {
		driverBugs->Add(new TextView(si->T("No GPU driver bugs detected"), new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	if (GetGPUBackend() == GPUBackend::OPENGL) {
		LinearLayout *gpuExtensions = AddTab("DevSystemInfoOGLExt", si->T("OGL Extensions"));

		if (!gl_extensions.IsGLES) {
			gpuExtensions->Add(new ItemHeader(si->T("OpenGL Extensions")));
		} else if (gl_extensions.GLES3) {
			gpuExtensions->Add(new ItemHeader(si->T("OpenGL ES 3.0 Extensions")));
		} else {
			gpuExtensions->Add(new ItemHeader(si->T("OpenGL ES 2.0 Extensions")));
		}
		exts.clear();
		SplitString(g_all_gl_extensions, ' ', exts);
		std::sort(exts.begin(), exts.end());
		for (auto &extension : exts) {
			gpuExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}

		exts.clear();
		SplitString(g_all_egl_extensions, ' ', exts);
		std::sort(exts.begin(), exts.end());

		// If there aren't any EGL extensions, no need to show the tab.
		if (exts.size() > 0) {
			LinearLayout *eglExtensions = AddTab("EglExt", si->T("EGL Extensions"));
			eglExtensions->SetSpacing(0);
			eglExtensions->Add(new ItemHeader(si->T("EGL Extensions")));
			for (auto &extension : exts) {
				eglExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
			}
		}
	} else if (GetGPUBackend() == GPUBackend::VULKAN) {
		LinearLayout *gpuExtensions = AddTab("DevSystemInfoOGLExt", si->T("Vulkan Features"));

		CollapsibleSection *vulkanFeatures = gpuExtensions->Add(new CollapsibleSection(si->T("Vulkan Features")));
		std::vector<std::string> features = draw->GetFeatureList();
		for (auto &feature : features) {
			vulkanFeatures->Add(new TextView(feature, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}

		CollapsibleSection *presentModes = gpuExtensions->Add(new CollapsibleSection(si->T("Present modes")));
		for (auto mode : draw->GetPresentModeList(di->T("Current"))) {
			presentModes->Add(new TextView(mode, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}

		CollapsibleSection *colorFormats = gpuExtensions->Add(new CollapsibleSection(si->T("Display Color Formats")));
		for (auto &format : draw->GetSurfaceFormatList()) {
			colorFormats->Add(new TextView(format, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}

		CollapsibleSection *enabledExtensions = gpuExtensions->Add(new CollapsibleSection(std::string(si->T("Vulkan Extensions")) + " (" + std::string(di->T("Enabled")) + ")"));
		std::vector<std::string> extensions = draw->GetExtensionList(true, true);
		std::sort(extensions.begin(), extensions.end());
		for (auto &extension : extensions) {
			enabledExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}
		// Also get instance extensions
		enabledExtensions->Add(new ItemHeader(si->T("Instance")));
		extensions = draw->GetExtensionList(false, true);
		std::sort(extensions.begin(), extensions.end());
		for (auto &extension : extensions) {
			enabledExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}

		CollapsibleSection *vulkanExtensions = gpuExtensions->Add(new CollapsibleSection(si->T("Vulkan Extensions")));
		extensions = draw->GetExtensionList(true, false);
		std::sort(extensions.begin(), extensions.end());
		for (auto &extension : extensions) {
			vulkanExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}

		vulkanExtensions->Add(new ItemHeader(si->T("Instance")));
		// Also get instance extensions
		extensions = draw->GetExtensionList(false, false);
		std::sort(extensions.begin(), extensions.end());
		for (auto &extension : extensions) {
			vulkanExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}
	}

#ifdef _DEBUG
	LinearLayout *internals = AddTab("DevSystemInfoInternals", si->T("Internals"));
	CreateInternalsTab(internals);
#endif
}

void SystemInfoScreen::CreateInternalsTab(UI::ViewGroup *internals) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto si = GetI18NCategory(I18NCat::SYSINFO);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	internals->Add(new ItemHeader(si->T("Icon cache")));
	IconCacheStats iconStats = g_iconCache.GetStats();
	internals->Add(new InfoItem(si->T("Image data count"), StringFromFormat("%d", iconStats.cachedCount)));
	internals->Add(new InfoItem(si->T("Texture count"), StringFromFormat("%d", iconStats.textureCount)));
	internals->Add(new InfoItem(si->T("Data size"), NiceSizeFormat(iconStats.dataSize)));
	internals->Add(new Choice(di->T("Clear")))->OnClick.Add([&](UI::EventParams &) {
		g_iconCache.ClearData();
		RecreateViews();
		return UI::EVENT_DONE;
	});

	internals->Add(new ItemHeader(si->T("Notification tests")));
	internals->Add(new Choice(si->T("Error")))->OnClick.Add([&](UI::EventParams &) {
		std::string str = "Error " + CodepointToUTF8(0x1F41B) + CodepointToUTF8(0x1F41C) + CodepointToUTF8(0x1F914);
		g_OSD.Show(OSDType::MESSAGE_ERROR, str);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("Warning")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_WARNING, "Warning", "Some\nAdditional\nDetail");
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("Info")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_INFO, "Info");
		return UI::EVENT_DONE;
	});
	// This one is clickable
	internals->Add(new Choice(si->T("Success")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, "Success", 0.0f, "clickable");
		g_OSD.SetClickCallback("clickable", [](bool clicked, void *) {
			if (clicked) {
				System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.google.com/");
			}
		}, nullptr);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(sy->T("RetroAchievements")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.Show(OSDType::MESSAGE_WARNING, "RetroAchievements warning", "", "I_RETROACHIEVEMENTS_LOGO");
		return UI::EVENT_DONE;
	});
	internals->Add(new ItemHeader(si->T("Progress tests")));
	internals->Add(new Choice(si->T("30%")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 1, 100, 30, 0.0f);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("100%")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 1, 100, 100, 1.0f);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("N/A%")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.SetProgressBar("testprogress", "Test Progress", 0, 0, 0, 0.0f);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("Success")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.RemoveProgressBar("testprogress", true, 0.5f);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("Failure")))->OnClick.Add([&](UI::EventParams &) {
		g_OSD.RemoveProgressBar("testprogress", false, 0.5f);
		return UI::EVENT_DONE;
	});
	internals->Add(new ItemHeader(si->T("Achievement tests")));
	internals->Add(new Choice(si->T("Leaderboard tracker: Show")))->OnClick.Add([=](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "My leaderboard tracker", true);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("Leaderboard tracker: Update")))->OnClick.Add([=](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, "Updated tracker", true);
		return UI::EVENT_DONE;
	});
	internals->Add(new Choice(si->T("Leaderboard tracker: Hide")))->OnClick.Add([=](UI::EventParams &) {
		g_OSD.ShowLeaderboardTracker(1, nullptr, false);
		return UI::EVENT_DONE;
	});

	static const char *positions[] = { "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right", "None" };

	internals->Add(new ItemHeader(ac->T("Notifications")));
	internals->Add(new PopupMultiChoice(&g_Config.iAchievementsLeaderboardTrackerPos, ac->T("Leaderboard tracker"), positions, 0, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bAchievementsEnable);

#if PPSSPP_PLATFORM(ANDROID)
	internals->Add(new Choice(si->T("Exception")))->OnClick.Add([&](UI::EventParams &) {
		System_Notify(SystemNotification::TEST_JAVA_EXCEPTION);
		return UI::EVENT_DONE;
	});
#endif
}

int ShaderListScreen::ListShaders(DebugShaderType shaderType, UI::LinearLayout *view) {
	using namespace UI;
	std::vector<std::string> shaderIds_ = gpu->DebugGetShaderIDs(shaderType);
	int count = 0;
	for (const auto &id : shaderIds_) {
		Choice *choice = view->Add(new Choice(gpu->DebugGetShaderString(id, shaderType, SHADER_STRING_SHORT_DESC)));
		choice->SetTag(id);
		choice->SetDrawTextFlags(FLAG_DYNAMIC_ASCII);
		choice->OnClick.Handle(this, &ShaderListScreen::OnShaderClick);
		count++;
	}
	return count;
}

struct { DebugShaderType type; const char *name; } shaderTypes[] = {
	{ SHADER_TYPE_VERTEX, "Vertex" },
	{ SHADER_TYPE_FRAGMENT, "Fragment" },
	{ SHADER_TYPE_GEOMETRY, "Geometry" },
	{ SHADER_TYPE_VERTEXLOADER, "VertexLoader" },
	{ SHADER_TYPE_PIPELINE, "Pipeline" },
	{ SHADER_TYPE_TEXTURE, "Texture" },
	{ SHADER_TYPE_SAMPLER, "Sampler" },
};

void ShaderListScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);

	LinearLayout *layout = new LinearLayout(ORIENT_VERTICAL);
	root_ = layout;

	tabs_ = new TabHolder(ORIENT_HORIZONTAL, 40, new LinearLayoutParams(1.0));
	tabs_->SetTag("DevShaderList");
	layout->Add(tabs_);
	layout->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	for (size_t i = 0; i < ARRAY_SIZE(shaderTypes); i++) {
		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
		LinearLayout *shaderList = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
		int count = ListShaders(shaderTypes[i].type, shaderList);
		scroll->Add(shaderList);
		tabs_->AddTab(StringFromFormat("%s (%d)", shaderTypes[i].name, count), scroll);
	}
}

UI::EventReturn ShaderListScreen::OnShaderClick(UI::EventParams &e) {
	using namespace UI;
	std::string id = e.v->Tag();
	DebugShaderType type = shaderTypes[tabs_->GetCurrentTab()].type;
	screenManager()->push(new ShaderViewScreen(id, type));
	return EVENT_DONE;
}

void ShaderViewScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);

	LinearLayout *layout = new LinearLayout(ORIENT_VERTICAL);
	root_ = layout;

	layout->Add(new TextView(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC), FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT, false));

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	scroll->SetTag("DevShaderView");
	layout->Add(scroll);

	LinearLayout *lineLayout = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	lineLayout->SetSpacing(0.0);
	scroll->Add(lineLayout);

	std::vector<std::string> lines;
	SplitString(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SOURCE_CODE), '\n', lines);

	for (const auto &line : lines) {
		lineLayout->Add(new TextView(line, FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT, true));
	}

	layout->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

bool ShaderViewScreen::key(const KeyInput &ki) {
	if (ki.flags & KEY_CHAR) {
		if (ki.unicodeChar == 'C' || ki.unicodeChar == 'c') {
			System_CopyStringToClipboard(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC));
		}
	}
	return UIDialogScreenWithBackground::key(ki);
}


const std::string framedumpsBaseUrl = "http://framedump.ppsspp.org/repro/";

FrameDumpTestScreen::FrameDumpTestScreen() {

}

FrameDumpTestScreen::~FrameDumpTestScreen() {
	g_DownloadManager.CancelAll();
}

void FrameDumpTestScreen::CreateViews() {
	using namespace UI;

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	auto di = GetI18NCategory(I18NCat::DIALOG);

	TabHolder *tabHolder;
	tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new AnchorLayoutParams(10, 0, 10, 0, false));
	root_->Add(tabHolder);
	AddStandardBack(root_);
	tabHolder->SetTag("DumpTypes");
	root_->SetDefaultFocusView(tabHolder);

	ViewGroup *dumpsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	dumpsScroll->SetTag("GameSettingsGraphics");
	LinearLayout *dumps = new LinearLayoutList(ORIENT_VERTICAL);
	dumps->SetSpacing(0);
	dumpsScroll->Add(dumps);
	tabHolder->AddTab("Dumps", dumpsScroll);

	dumps->Add(new ItemHeader("GE Frame Dumps"));

	for (auto &file : files_) {
		std::string url = framedumpsBaseUrl + file;
		Choice *c = dumps->Add(new Choice(file));
		c->SetTag(url);
		c->OnClick.Handle<FrameDumpTestScreen>(this, &FrameDumpTestScreen::OnLoadDump);
	}
}

UI::EventReturn FrameDumpTestScreen::OnLoadDump(UI::EventParams &params) {
	std::string url = params.v->Tag();
	INFO_LOG(Log::Common, "Trying to launch '%s'", url.c_str());
	// Our disc streaming functionality detects the URL and takes over and handles loading framedumps well,
	// except for some reason the game ID.
	// TODO: Fix that since it can be important for compat settings.
	LaunchFile(screenManager(), Path(url));
	return UI::EVENT_DONE;
}

void FrameDumpTestScreen::update() {
	UIScreen::update();

	if (!listing_) {
		const char *acceptMime = "text/html, */*; q=0.8";
		listing_ = g_DownloadManager.StartDownload(framedumpsBaseUrl, Path(), http::ProgressBarMode::DELAYED, acceptMime);
	}

	if (listing_ && listing_->Done() && files_.empty()) {
		if (listing_->ResultCode() == 200) {
			std::string listingHtml;
			listing_->buffer().TakeAll(&listingHtml);

			std::vector<std::string> lines;
			// We rely slightly on nginx listing format here. Not great.
			SplitString(listingHtml, '\n', lines);
			for (auto &line : lines) {
				std::string trimmed = StripSpaces(line);
				if (startsWith(trimmed, "<a href=\"")) {
					trimmed = trimmed.substr(strlen("<a href=\""));
					size_t offset = trimmed.find('\"');
					if (offset != std::string::npos) {
						trimmed = trimmed.substr(0, offset);
						if (endsWith(trimmed, ".ppdmp")) {
							INFO_LOG(Log::Common, "Found ppdmp: '%s'", trimmed.c_str());
							files_.push_back(trimmed);
						}
					}
				}
			}
		} else {
			// something went bad. Too lazy to make UI, so let's just finish this screen.
			TriggerFinish(DialogResult::DR_CANCEL);
		}
		RecreateViews();
	}
}

void TouchTestScreen::touch(const TouchInput &touch) {
	UIDialogScreenWithGameBackground::touch(touch);
	if (touch.flags & TOUCH_DOWN) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				WARN_LOG(Log::System, "Double touch");
				touches_[i].x = touch.x;
				touches_[i].y = touch.y;
				found = true;
			}
		}
		if (!found) {
			for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
				if (touches_[i].id == -1) {
					touches_[i].id = touch.id;
					touches_[i].x = touch.x;
					touches_[i].y = touch.y;
					break;
				}
			}
		}
	}
	if (touch.flags & TOUCH_MOVE) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				touches_[i].x = touch.x;
				touches_[i].y = touch.y;
				found = true;
			}
		}
		if (!found) {
			WARN_LOG(Log::System, "Move without touch down: %d", touch.id);
		}
	}
	if (touch.flags & TOUCH_UP) {
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			if (touches_[i].id == touch.id) {
				found = true;
				touches_[i].id = -1;
				break;
			}
		}
		if (!found) {
			WARN_LOG(Log::System, "Touch release without touch down");
		}
	}
}

// TODO: Move this screen out into its own file.
void TouchTestScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	root_ = new LinearLayout(ORIENT_VERTICAL);
	LinearLayout *theTwo = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));

	// TODO: This one should use DYNAMIC_ASCII. Though doesn't matter much.
	lastKeyEvents_ = theTwo->Add(new TextView("-", new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	root_->Add(theTwo);

#if !PPSSPP_PLATFORM(UWP)
	static const char *renderingBackend[] = { "OpenGL", "Direct3D 9", "Direct3D 11", "Vulkan" };
	PopupMultiChoice *renderingBackendChoice = root_->Add(new PopupMultiChoice(&g_Config.iGPUBackend, gr->T("Backend"), renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend), I18NCat::GRAPHICS, screenManager()));
	renderingBackendChoice->OnChoice.Handle(this, &TouchTestScreen::OnRenderingBackend);

	if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
		renderingBackendChoice->HideChoice((int)GPUBackend::OPENGL);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D9))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D9);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D11);
	if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
		renderingBackendChoice->HideChoice((int)GPUBackend::VULKAN);
#endif

#if PPSSPP_PLATFORM(ANDROID)
	root_->Add(new Choice(gr->T("Recreate Activity")))->OnClick.Handle(this, &TouchTestScreen::OnRecreateActivity);
#endif
	root_->Add(new CheckBox(&g_Config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &TouchTestScreen::OnImmersiveModeChange);
	root_->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

void TouchTestScreen::UpdateLogView() {
	while (keyEventLog_.size() > 8) {
		keyEventLog_.erase(keyEventLog_.begin());
	}

	std::string text;
	for (auto &iter : keyEventLog_) {
		text += iter + "\n";
	}

	if (lastKeyEvents_) {
		lastKeyEvents_->SetText(text);
	}
}

bool TouchTestScreen::key(const KeyInput &key) {
	UIScreen::key(key);
	char buf[512];
	snprintf(buf, sizeof(buf), "%s (%d) Device ID: %d [%s%s%s%s]", KeyMap::GetKeyName(key.keyCode).c_str(), key.keyCode, key.deviceId,
		(key.flags & KEY_IS_REPEAT) ? "REP" : "",
		(key.flags & KEY_UP) ? "UP" : "",
		(key.flags & KEY_DOWN) ? "DOWN" : "",
		(key.flags & KEY_CHAR) ? "CHAR" : "");
	keyEventLog_.push_back(buf);
	UpdateLogView();
	return true;
}

void TouchTestScreen::axis(const AxisInput &axis) {
	char buf[512];
	snprintf(buf, sizeof(buf), "Axis: %s (%d) (value %1.3f) Device ID: %d",
		KeyMap::GetAxisName(axis.axisId).c_str(), axis.axisId, axis.value, axis.deviceId);

	keyEventLog_.push_back(buf);
	if (keyEventLog_.size() > 8) {
		keyEventLog_.erase(keyEventLog_.begin());
	}
	UpdateLogView();
}

void TouchTestScreen::DrawForeground(UIContext &dc) {
	Bounds bounds = dc.GetLayoutBounds();

	double now = dc.FrameStartTime();
	double delta = now - lastFrameTime_;
	lastFrameTime_ = now;

	dc.BeginNoTex();
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touches_[i].id != -1) {
			dc.Draw()->Circle(touches_[i].x, touches_[i].y, 100.0, 3.0, 80, 0.0f, 0xFFFFFFFF, 1.0);
		}
	}
	dc.Flush();

	dc.Begin();

	char buffer[4096];
	for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
		if (touches_[i].id != -1) {
			dc.Draw()->Circle(touches_[i].x, touches_[i].y, 100.0, 3.0, 80, 0.0f, 0xFFFFFFFF, 1.0);
			snprintf(buffer, sizeof(buffer), "%0.1fx%0.1f", touches_[i].x, touches_[i].y);
			dc.DrawText(buffer, touches_[i].x, touches_[i].y + (touches_[i].y > g_display.dp_yres - 100.0f ? -135.0f : 95.0f), 0xFFFFFFFF, ALIGN_HCENTER | FLAG_DYNAMIC_ASCII);
		}
	}

	char extra_debug[2048]{};

#if PPSSPP_PLATFORM(ANDROID)
	truncate_cpy(extra_debug, Android_GetInputDeviceDebugString().c_str());
#endif

	snprintf(buffer, sizeof(buffer),
#if PPSSPP_PLATFORM(ANDROID)
		"display_res: %dx%d\n"
#endif
		"dp_res: %dx%d pixel_res: %dx%d\n"
		"g_dpi: %0.3f g_dpi_scale: %0.3fx%0.3f\n"
		"g_dpi_scale_real: %0.3fx%0.3f\n"
		"delta: %0.2f ms fps: %0.3f\n%s",
#if PPSSPP_PLATFORM(ANDROID)
		(int)System_GetPropertyInt(SYSPROP_DISPLAY_XRES), (int)System_GetPropertyInt(SYSPROP_DISPLAY_YRES),
#endif
		g_display.dp_xres, g_display.dp_yres, g_display.pixel_xres, g_display.pixel_yres,
		g_display.dpi, g_display.dpi_scale_x, g_display.dpi_scale_y,
		g_display.dpi_scale_real_x, g_display.dpi_scale_real_y,
		delta * 1000.0, 1.0 / delta,
		extra_debug);

	// On Android, also add joystick debug data.
	dc.DrawTextShadow(buffer, bounds.centerX(), bounds.y + 20.0f, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	dc.Flush();
}

void RecreateActivity() {
	const int SYSTEM_JELLYBEAN = 16;
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= SYSTEM_JELLYBEAN) {
		INFO_LOG(Log::System, "Sending recreate");
		System_Notify(SystemNotification::FORCE_RECREATE_ACTIVITY);
		INFO_LOG(Log::System, "Got back from recreate");
	} else {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		System_Toast(gr->T_cstr("Must Restart", "You must restart PPSSPP for this change to take effect"));
	}
}

UI::EventReturn TouchTestScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	if (g_Config.iAndroidHwScale != 0) {
		RecreateActivity();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn TouchTestScreen::OnRenderingBackend(UI::EventParams &e) {
	g_Config.Save("GameSettingsScreen::RenderingBackend");
	System_RestartApp("--touchscreentest");
	return UI::EVENT_DONE;
}

UI::EventReturn TouchTestScreen::OnRecreateActivity(UI::EventParams &e) {
	RecreateActivity();
	return UI::EVENT_DONE;
}
