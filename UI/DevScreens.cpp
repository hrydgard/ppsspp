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
#include "Common/GPU/OpenGL/GLFeatures.h"

#if !PPSSPP_PLATFORM(UWP)
#include "Common/GPU/Vulkan/VulkanContext.h"
#endif
#include "Common/File/AndroidStorage.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Net/HTTPClient.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"
#include "Common/Profiler/Profiler.h"

#include "Common/LogManager.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/CoreParameter.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "UI/MiscScreens.h"
#include "UI/DevScreens.h"
#include "UI/MainScreen.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"


#ifdef _WIN32
#include "Common/CommonWindows.h"
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

void DevMenu::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto dev = GetI18NCategory("Developer");
	auto sy = GetI18NCategory("System");

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayout(ORIENT_VERTICAL);

#if !defined(MOBILE_DEVICE)
	items->Add(new Choice(dev->T("Log View")))->OnClick.Handle(this, &DevMenu::OnLogView);
#endif
	items->Add(new Choice(dev->T("Logging Channels")))->OnClick.Handle(this, &DevMenu::OnLogConfig);
	items->Add(new Choice(sy->T("Developer Tools")))->OnClick.Handle(this, &DevMenu::OnDeveloperTools);
	items->Add(new Choice(dev->T("Jit Compare")))->OnClick.Handle(this, &DevMenu::OnJitCompare);
	items->Add(new Choice(dev->T("Shader Viewer")))->OnClick.Handle(this, &DevMenu::OnShaderView);
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		// TODO: Make a new allocator visualizer for VMA.
		// items->Add(new CheckBox(&g_Config.bShowAllocatorDebug, dev->T("Allocator Viewer")));
		items->Add(new CheckBox(&g_Config.bShowGpuProfile, dev->T("GPU Profile")));
	}
	items->Add(new Choice(dev->T("Toggle Freeze")))->OnClick.Handle(this, &DevMenu::OnFreezeFrame);
	items->Add(new Choice(dev->T("Dump Frame GPU Commands")))->OnClick.Handle(this, &DevMenu::OnDumpFrame);
	items->Add(new Choice(dev->T("Toggle Audio Debug")))->OnClick.Handle(this, &DevMenu::OnToggleAudioDebug);
#ifdef USE_PROFILER
	items->Add(new CheckBox(&g_Config.bShowFrameProfiler, dev->T("Frame Profiler"), ""));
#endif
	items->Add(new CheckBox(&g_Config.bDrawFrameGraph, dev->T("Draw Frametimes Graph")));
	items->Add(new Choice(dev->T("Reset limited logging")))->OnClick.Handle(this, &DevMenu::OnResetLimitedLogging);

	scroll->Add(items);
	parent->Add(scroll);

	RingbufferLogListener *ring = LogManager::GetInstance()->GetRingbufferListener();
	if (ring) {
		ring->SetEnabled(true);
	}
}

UI::EventReturn DevMenu::OnToggleAudioDebug(UI::EventParams &e) {
	g_Config.bShowAudioDebug = !g_Config.bShowAudioDebug;
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnResetLimitedLogging(UI::EventParams &e) {
	Reporting::ResetCounts();
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnLogView(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new LogScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnLogConfig(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnDeveloperTools(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new DeveloperToolsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnJitCompare(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new JitCompareScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnShaderView(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	if (gpu)  // Avoid crashing if chosen while the game is being loaded.
		screenManager()->push(new ShaderListScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnFreezeFrame(UI::EventParams &e) {
	if (PSP_CoreParameter().frozen) {
		PSP_CoreParameter().frozen = false;
	} else {
		PSP_CoreParameter().freezeNext = true;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnDumpFrame(UI::EventParams &e) {
	gpu->DumpNextFrame();
	return UI::EVENT_DONE;
}

void DevMenu::dialogFinished(const Screen *dialog, DialogResult result) {
	UpdateUIState(UISTATE_INGAME);
	// Close when a subscreen got closed.
	// TODO: a bug in screenmanager causes this not to work here.
	// TriggerFinish(DR_OK);
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
		case LogTypes::LDEBUG: color = 0xE0E0E0; break;
		case LogTypes::LWARNING: color = 0x50FFFF; break;
		case LogTypes::LERROR: color = 0x5050FF; break;
		case LogTypes::LNOTICE: color = 0x30FF30; break;
		case LogTypes::LINFO: color = 0xFFFFFF; break;
		case LogTypes::LVERBOSE: color = 0xC0C0C0; break;
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
	auto di = GetI18NCategory("Dialog");

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

	NOTICE_LOG(SYSTEM, "Submitted: %s", cmd.c_str());

	UpdateLog();
	cmdLine_->SetText("");
	cmdLine_->SetFocus();
	return UI::EVENT_DONE;
}

void LogConfigScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto dev = GetI18NCategory("Developer");

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
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(cellSize - 50, WRAP_CONTENT));
		row->SetSpacing(0);
		row->Add(new CheckBox(&chan->enabled, "", "", new LinearLayoutParams(50, WRAP_CONTENT)));
		row->Add(new PopupMultiChoice((int *)&chan->level, chan->m_shortName, logLevelList, 1, 6, 0, screenManager(), new LinearLayoutParams(1.0)));
		grid->Add(row);
	}
}

UI::EventReturn LogConfigScreen::OnToggleAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = logMan->GetLogChannel((LogTypes::LOG_TYPE)i);
		chan->enabled = !chan->enabled;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnEnableAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = logMan->GetLogChannel((LogTypes::LOG_TYPE)i);
		chan->enabled = true;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnDisableAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = logMan->GetLogChannel((LogTypes::LOG_TYPE)i);
		chan->enabled = false;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnLogLevelChange(UI::EventParams &e) {
	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnLogLevel(UI::EventParams &e) {
	auto dev = GetI18NCategory("Developer");

	auto logLevelScreen = new LogLevelScreen(dev->T("Log Level"));
	logLevelScreen->OnChoice.Handle(this, &LogConfigScreen::OnLogLevelChange);
	if (e.v)
		logLevelScreen->SetPopupOrigin(e.v);
	screenManager()->push(logLevelScreen);
	return UI::EVENT_DONE;
}

LogLevelScreen::LogLevelScreen(const std::string &title) : ListPopupScreen(title) {
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
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		if (chan->enabled)
			chan->level = (LogTypes::LOG_LEVELS)(selected + 1);
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

	auto di = GetI18NCategory("Dialog");
	auto dev = GetI18NCategory("Developer");

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

const char *GetCompilerABI() {
#if PPSSPP_ARCH(ARMV7)
	return "armeabi-v7a";
#elif PPSSPP_ARCH(ARM)
	return "armeabi";
#elif PPSSPP_ARCH(ARM64)
	return "arm64";
#elif PPSSPP_ARCH(X86)
	return "x86";
#elif PPSSPP_ARCH(AMD64)
	return "x86-64";
#elif PPSSPP_ARCH(RISCV64)
    //https://github.com/riscv/riscv-toolchain-conventions#cc-preprocessor-definitions
    //https://github.com/riscv/riscv-c-api-doc/blob/master/riscv-c-api.md#abi-related-preprocessor-definitions
    #if defined(__riscv_float_abi_single)
        return "lp64f";
    #elif defined(__riscv_float_abi_double)
        return "lp64d";
    #elif defined(__riscv_float_abi_quad)
        return "lp64q";
    #elif defined(__riscv_float_abi_soft)
        return "lp64";
    #endif
#else
	return "other";
#endif
}

void SystemInfoScreen::CreateViews() {
	using namespace Draw;
	using namespace UI;

	// NOTE: Do not translate this section. It will change a lot and will be impossible to keep up.
	auto di = GetI18NCategory("Dialog");
	auto si = GetI18NCategory("SysInfo");
	auto gr = GetI18NCategory("Graphics");
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	AddStandardBack(root_);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 225, new AnchorLayoutParams(10, 0, 10, 0, false));
	tabHolder->SetTag("DevSystemInfo");

	root_->Add(tabHolder);
	ViewGroup *deviceSpecsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	deviceSpecsScroll->SetTag("DevSystemInfoDeviceSpecs");
	LinearLayout *deviceSpecs = new LinearLayoutList(ORIENT_VERTICAL);
	deviceSpecs->SetSpacing(0);
	deviceSpecsScroll->Add(deviceSpecs);
	tabHolder->AddTab(si->T("Device Info"), deviceSpecsScroll);

	deviceSpecs->Add(new ItemHeader(si->T("System Information")));
	deviceSpecs->Add(new InfoItem(si->T("System Name", "Name"), System_GetProperty(SYSPROP_NAME)));
#if PPSSPP_PLATFORM(ANDROID)
	deviceSpecs->Add(new InfoItem(si->T("System Version"), StringFromInt(System_GetPropertyInt(SYSPROP_SYSTEMVERSION))));
#endif
	deviceSpecs->Add(new InfoItem(si->T("Lang/Region"), System_GetProperty(SYSPROP_LANGREGION)));
	std::string board = System_GetProperty(SYSPROP_BOARDNAME);
	if (!board.empty())
		deviceSpecs->Add(new InfoItem(si->T("Board"), board));
	deviceSpecs->Add(new InfoItem(si->T("ABI"), GetCompilerABI()));
#ifdef _WIN32
	if (IsDebuggerPresent()) {
		deviceSpecs->Add(new InfoItem(si->T("Debugger Present"), di->T("Yes")));
	}
#endif

	deviceSpecs->Add(new ItemHeader(si->T("CPU Information")));

	// Don't bother showing the CPU name if we don't have one.
	if (strcmp(cpu_info.brand_string, "Unknown") != 0) {
		deviceSpecs->Add(new InfoItem(si->T("CPU Name", "Name"), cpu_info.brand_string));
	}

	int totalThreads = cpu_info.num_cores * cpu_info.logical_cpu_count;
	std::string cores = StringFromFormat(si->T("%d (%d per core, %d cores)"), totalThreads, cpu_info.logical_cpu_count, cpu_info.num_cores);
	deviceSpecs->Add(new InfoItem(si->T("Threads"), cores));
#if PPSSPP_PLATFORM(IOS)
	deviceSpecs->Add(new InfoItem(si->T("JIT available"), System_GetPropertyBool(SYSPROP_CAN_JIT) ? di->T("Yes") : di->T("No")));
#endif
	deviceSpecs->Add(new ItemHeader(si->T("GPU Information")));

	DrawContext *draw = screenManager()->getDrawContext();

	const std::string apiNameKey = draw->GetInfoString(InfoField::APINAME);
	const char *apiName = gr->T(apiNameKey);
	deviceSpecs->Add(new InfoItem(si->T("3D API"), apiName));
	deviceSpecs->Add(new InfoItem(si->T("Vendor"), draw->GetInfoString(InfoField::VENDORSTRING)));
	std::string vendor = draw->GetInfoString(InfoField::VENDOR);
	if (vendor.size())
		deviceSpecs->Add(new InfoItem(si->T("Vendor (detected)"), vendor));
	deviceSpecs->Add(new InfoItem(si->T("Driver Version"), draw->GetInfoString(InfoField::DRIVER)));
#ifdef _WIN32
	if (GetGPUBackend() != GPUBackend::VULKAN)
		deviceSpecs->Add(new InfoItem(si->T("Driver Version"), System_GetProperty(SYSPROP_GPUDRIVER_VERSION)));
#if !PPSSPP_PLATFORM(UWP)
	if (GetGPUBackend() == GPUBackend::DIRECT3D9) {
		deviceSpecs->Add(new InfoItem(si->T("D3DCompiler Version"), StringFromFormat("%d", GetD3DCompilerVersion())));
	}
#endif
#endif
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		deviceSpecs->Add(new InfoItem(si->T("Core Context"), gl_extensions.IsCoreContext ? di->T("Active") : di->T("Inactive")));
		int highp_int_min = gl_extensions.range[1][5][0];
		int highp_int_max = gl_extensions.range[1][5][1];
		int highp_float_min = gl_extensions.range[1][2][0];
		int highp_float_max = gl_extensions.range[1][2][1];
		if (highp_int_max != 0) {
			char temp[512];
			snprintf(temp, sizeof(temp), "Highp int range: %d-%d", highp_int_min, highp_int_max);
			deviceSpecs->Add(new InfoItem(si->T("High precision int range"), temp));
		}
		if (highp_float_max != 0) {
			char temp[512];
			snprintf(temp, sizeof(temp), "Highp float range: %d-%d", highp_int_min, highp_int_max);
			deviceSpecs->Add(new InfoItem(si->T("High precision float range"), temp));
		}
	}
	deviceSpecs->Add(new ItemHeader(si->T("OS Information")));
	deviceSpecs->Add(new InfoItem(si->T("Memory Page Size"), StringFromFormat(si->T("%d bytes"), GetMemoryProtectPageSize())));
	deviceSpecs->Add(new InfoItem(si->T("RW/RX exclusive"), PlatformIsWXExclusive() ? di->T("Active") : di->T("Inactive")));
#if PPSSPP_PLATFORM(ANDROID)
	deviceSpecs->Add(new InfoItem(si->T("Sustained perf mode"), System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE) ? di->T("Supported") : di->T("Unsupported")));
#endif

	const char *build = si->T("Release");
#ifdef _DEBUG
	build = si->T("Debug");
#endif
	deviceSpecs->Add(new InfoItem(si->T("PPSSPP build"), build));

	deviceSpecs->Add(new ItemHeader(si->T("Audio Information")));
	deviceSpecs->Add(new InfoItem(si->T("Sample rate"), StringFromFormat("%d Hz", System_GetPropertyInt(SYSPROP_AUDIO_SAMPLE_RATE))));
	int framesPerBuffer = System_GetPropertyInt(SYSPROP_AUDIO_FRAMES_PER_BUFFER);
	if (framesPerBuffer > 0) {
		deviceSpecs->Add(new InfoItem(si->T("Frames per buffer"), StringFromFormat("%d", framesPerBuffer)));
	}
#if PPSSPP_PLATFORM(ANDROID)
	deviceSpecs->Add(new InfoItem(si->T("Optimal sample rate"), StringFromFormat("%d Hz", System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE))));
	deviceSpecs->Add(new InfoItem(si->T("Optimal frames per buffer"), StringFromFormat("%d", System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER))));
#endif

	deviceSpecs->Add(new ItemHeader(si->T("Display Information")));
#if PPSSPP_PLATFORM(ANDROID)
	deviceSpecs->Add(new InfoItem(si->T("Native Resolution"), StringFromFormat("%dx%d",
		System_GetPropertyInt(SYSPROP_DISPLAY_XRES),
		System_GetPropertyInt(SYSPROP_DISPLAY_YRES))));
#endif

#if !PPSSPP_PLATFORM(WINDOWS)
	// Don't show on Windows, since it's always treated as 60 there.
	deviceSpecs->Add(new InfoItem(si->T("Refresh rate"), StringFromFormat("%0.3f Hz", (float)System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE))));
#endif

	deviceSpecs->Add(new ItemHeader(si->T("Version Information")));
	std::string apiVersion;
	if (GetGPUBackend() == GPUBackend::OPENGL) {
		if (gl_extensions.IsGLES) {
			apiVersion = StringFromFormat("v%d.%d.%d ES", gl_extensions.ver[0], gl_extensions.ver[1], gl_extensions.ver[2]);
		} else {
			apiVersion = StringFromFormat("v%d.%d.%d", gl_extensions.ver[0], gl_extensions.ver[1], gl_extensions.ver[2]);
		}
	} else {
		apiVersion = draw->GetInfoString(InfoField::APIVERSION);
		if (apiVersion.size() > 30)
			apiVersion.resize(30);
	}
	deviceSpecs->Add(new InfoItem(si->T("API Version"), apiVersion));
	deviceSpecs->Add(new InfoItem(si->T("Shading Language"), draw->GetInfoString(InfoField::SHADELANGVERSION)));

#if PPSSPP_PLATFORM(ANDROID)
	std::string moga = System_GetProperty(SYSPROP_MOGA_VERSION);
	if (moga.empty()) {
		moga = si->T("(none detected)");
	}
	deviceSpecs->Add(new InfoItem("Moga", moga));
#endif

	ViewGroup *storageScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	storageScroll->SetTag("DevSystemInfoBuildConfig");
	LinearLayout *storage = new LinearLayout(ORIENT_VERTICAL);
	storage->SetSpacing(0);
	storageScroll->Add(storage);
	tabHolder->AddTab(si->T("Storage"), storageScroll);

	storage->Add(new ItemHeader(si->T("Directories")));
	// Intentionally non-translated
	storage->Add(new InfoItem("MemStickDirectory", g_Config.memStickDirectory.ToVisualString()));
	storage->Add(new InfoItem("InternalDataDirectory", g_Config.internalDataDirectory.ToVisualString()));
	storage->Add(new InfoItem("AppCacheDir", g_Config.appCacheDirectory.ToVisualString()));
	storage->Add(new InfoItem("DefaultCurrentDir", g_Config.defaultCurrentDirectory.ToVisualString()));

#if PPSSPP_PLATFORM(ANDROID)
	storage->Add(new InfoItem("ExtFilesDir", g_extFilesDir));
	bool scoped = System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE);
	storage->Add(new InfoItem("Scoped Storage Enabled", scoped ? di->T("Yes") : di->T("No")));
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30) {
		// This flag is only relevant on Android API 30+.
		storage->Add(new InfoItem("IsStoragePreservedLegacy", Android_IsExternalStoragePreservedLegacy() ? di->T("Yes") : di->T("No")));
	}
#endif

	ViewGroup *buildConfigScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	buildConfigScroll->SetTag("DevSystemInfoBuildConfig");
	LinearLayout *buildConfig = new LinearLayoutList(ORIENT_VERTICAL);
	buildConfig->SetSpacing(0);
	buildConfigScroll->Add(buildConfig);
	tabHolder->AddTab(si->T("Build Config"), buildConfigScroll);

	buildConfig->Add(new ItemHeader(si->T("Build Configuration")));
#ifdef JENKINS
	buildConfig->Add(new InfoItem(si->T("Built by"), "Jenkins"));
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

	ViewGroup *cpuExtensionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	cpuExtensionsScroll->SetTag("DevSystemInfoCPUExt");
	LinearLayout *cpuExtensions = new LinearLayoutList(ORIENT_VERTICAL);
	cpuExtensions->SetSpacing(0);
	cpuExtensionsScroll->Add(cpuExtensions);

	tabHolder->AddTab(si->T("CPU Extensions"), cpuExtensionsScroll);

	cpuExtensions->Add(new ItemHeader(si->T("CPU Extensions")));
	std::vector<std::string> exts;
	SplitString(cpu_info.Summarize(), ',', exts);
	for (size_t i = 2; i < exts.size(); i++) {
		cpuExtensions->Add(new TextView(exts[i], new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
	}

	ViewGroup *driverBugsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	driverBugsScroll->SetTag("DevSystemInfoDriverBugs");
	LinearLayout *driverBugs = new LinearLayoutList(ORIENT_VERTICAL);
	driverBugs->SetSpacing(0);
	driverBugsScroll->Add(driverBugs);

	tabHolder->AddTab(si->T("Driver bugs"), driverBugsScroll);

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

	ViewGroup *gpuExtensionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	gpuExtensionsScroll->SetTag("DevSystemInfoOGLExt");
	LinearLayout *gpuExtensions = new LinearLayoutList(ORIENT_VERTICAL);
	gpuExtensions->SetSpacing(0);
	gpuExtensionsScroll->Add(gpuExtensions);

	if (GetGPUBackend() == GPUBackend::OPENGL) {
		tabHolder->AddTab(si->T("OGL Extensions"), gpuExtensionsScroll);

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
			ViewGroup *eglExtensionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
			eglExtensionsScroll->SetTag("DevSystemInfoEGLExt");
			LinearLayout *eglExtensions = new LinearLayoutList(ORIENT_VERTICAL);
			eglExtensions->SetSpacing(0);
			eglExtensionsScroll->Add(eglExtensions);

			tabHolder->AddTab(si->T("EGL Extensions"), eglExtensionsScroll);

			eglExtensions->Add(new ItemHeader(si->T("EGL Extensions")));

			for (auto &extension : exts) {
				eglExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
			}
		}
	} else if (GetGPUBackend() == GPUBackend::VULKAN) {
		tabHolder->AddTab(si->T("Vulkan Features"), gpuExtensionsScroll);

		gpuExtensions->Add(new ItemHeader(si->T("Vulkan Features")));
		std::vector<std::string> features = draw->GetFeatureList();
		for (auto &feature : features) {
			gpuExtensions->Add(new TextView(feature, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}
		
#if !PPSSPP_PLATFORM(UWP)

		// Vulkan specific code here, can't be bothered to abstract.
		// OK because of above check.
		gpuExtensions->Add(new ItemHeader(si->T("Display Color Formats")));
		VulkanContext *vk = (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
		if (vk) {
			for (auto &format : vk->SurfaceFormats()) {
				std::string line = StringFromFormat("%s : %s", VulkanFormatToString(format.format), VulkanColorSpaceToString(format.colorSpace));
				gpuExtensions->Add(new TextView(line,
					new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
			}
		}
#endif

		gpuExtensions->Add(new ItemHeader(si->T("Vulkan Extensions")));
		std::vector<std::string> extensions = draw->GetExtensionList();
		for (auto &extension : extensions) {
			gpuExtensions->Add(new TextView(extension, new LayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetFocusable(true);
		}
	}
}

void AddressPromptScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto dev = GetI18NCategory("Developer");

	addrView_ = new TextView(dev->T("Enter address"), ALIGN_HCENTER, false);
	parent->Add(addrView_);

	ViewGroup *grid = new GridLayout(GridLayoutSettings(60, 40));
	parent->Add(grid);

	for (int i = 0; i < 16; ++i) {
		char temp[16];
		snprintf(temp, 16, " %X ", i);
		buttons_[i] = new Button(temp);
		grid->Add(buttons_[i])->OnClick.Handle(this, &AddressPromptScreen::OnDigitButton);
	}

	parent->Add(new Button(dev->T("Backspace")))->OnClick.Handle(this, &AddressPromptScreen::OnBackspace);
}

void AddressPromptScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		UI::EventParams e{};
		e.v = root_;
		e.a = addr_;
		OnChoice.Trigger(e);
	}
}

UI::EventReturn AddressPromptScreen::OnDigitButton(UI::EventParams &e) {
	for (int i = 0; i < 16; ++i) {
		if (buttons_[i] == e.v) {
			AddDigit(i);
		}
	}
	return UI::EVENT_DONE;
}

UI::EventReturn AddressPromptScreen::OnBackspace(UI::EventParams &e) {
	BackspaceDigit();
	return UI::EVENT_DONE;
}

void AddressPromptScreen::AddDigit(int n) {
	if ((addr_ & 0xF0000000) == 0) {
		addr_ = addr_ * 16 + n;
	}
	UpdatePreviewDigits();
}

void AddressPromptScreen::BackspaceDigit() {
	addr_ /= 16;
	UpdatePreviewDigits();
}

void AddressPromptScreen::UpdatePreviewDigits() {
	auto dev = GetI18NCategory("Developer");

	if (addr_ != 0) {
		char temp[32];
		snprintf(temp, 32, "%8X", addr_);
		addrView_->SetText(temp);
	} else {
		addrView_->SetText(dev->T("Enter address"));
	}
}

bool AddressPromptScreen::key(const KeyInput &key) {
	if (key.flags & KEY_DOWN) {
		if (key.keyCode >= NKCODE_0 && key.keyCode <= NKCODE_9) {
			AddDigit(key.keyCode - NKCODE_0);
		} else if (key.keyCode >= NKCODE_A && key.keyCode <= NKCODE_F) {
			AddDigit(10 + key.keyCode - NKCODE_A);
		// NKCODE_DEL is backspace.
		} else if (key.keyCode == NKCODE_DEL) {
			BackspaceDigit();
		} else if (key.keyCode == NKCODE_ENTER) {
			TriggerFinish(DR_OK);
		} else {
			return UIDialogScreen::key(key);
		}
	} else {
		return UIDialogScreen::key(key);
	}
	return true;
}

// Three panes: Block chooser, MIPS view, ARM/x86 view
void JitCompareScreen::CreateViews() {
	auto di = GetI18NCategory("Dialog");
	auto dev = GetI18NCategory("Developer");

	using namespace UI;
	
	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ScrollView *leftColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	LinearLayout *leftColumn = leftColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));

	ScrollView *midColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(2.0f)));
	LinearLayout *midColumn = midColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	midColumn->SetTag("JitCompareLeftDisasm");
	leftDisasm_ = midColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	leftDisasm_->SetSpacing(0.0f);

	ScrollView *rightColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(2.0f)));
	rightColumnScroll->SetTag("JitCompareRightDisasm");
	LinearLayout *rightColumn = rightColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_ = rightColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_->SetSpacing(0.0f);

	leftColumn->Add(new Choice(dev->T("Current")))->OnClick.Handle(this, &JitCompareScreen::OnCurrentBlock);
	leftColumn->Add(new Choice(dev->T("By Address")))->OnClick.Handle(this, &JitCompareScreen::OnSelectBlock);
	leftColumn->Add(new Choice(dev->T("Prev")))->OnClick.Handle(this, &JitCompareScreen::OnPrevBlock);
	leftColumn->Add(new Choice(dev->T("Next")))->OnClick.Handle(this, &JitCompareScreen::OnNextBlock);
	leftColumn->Add(new Choice(dev->T("Random")))->OnClick.Handle(this, &JitCompareScreen::OnRandomBlock);
	leftColumn->Add(new Choice(dev->T("FPU")))->OnClick.Handle(this, &JitCompareScreen::OnRandomFPUBlock);
	leftColumn->Add(new Choice(dev->T("VFPU")))->OnClick.Handle(this, &JitCompareScreen::OnRandomVFPUBlock);
	leftColumn->Add(new Choice(dev->T("Stats")))->OnClick.Handle(this, &JitCompareScreen::OnShowStats);
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	blockName_ = leftColumn->Add(new TextView(dev->T("No block")));
	blockAddr_ = leftColumn->Add(new TextEdit("", dev->T("Block address"), "", new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	blockAddr_->OnTextChange.Handle(this, &JitCompareScreen::OnAddressChange);
	blockStats_ = leftColumn->Add(new TextView(""));

	EventParams ignore{};
	OnCurrentBlock(ignore);
}

void JitCompareScreen::UpdateDisasm() {
	leftDisasm_->Clear();
	rightDisasm_->Clear();

	using namespace UI;

	auto dev = GetI18NCategory("Developer");

	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();

	char temp[256];
	snprintf(temp, sizeof(temp), "%i/%i", currentBlock_, blockCacheDebug->GetNumBlocks());
	blockName_->SetText(temp);

	if (currentBlock_ < 0 || currentBlock_ >= blockCacheDebug->GetNumBlocks()) {
		leftDisasm_->Add(new TextView(dev->T("No block")));
		rightDisasm_->Add(new TextView(dev->T("No block")));
		blockStats_->SetText("");
		return;
	}

	JitBlockDebugInfo debugInfo = blockCacheDebug->GetBlockDebugInfo(currentBlock_);

	snprintf(temp, sizeof(temp), "%08x", debugInfo.originalAddress);
	blockAddr_->SetText(temp);

	// Alright. First generate the MIPS disassembly.
	
	// TODO: Need a way to communicate branch continuing.
	for (auto line : debugInfo.origDisasm) {
		leftDisasm_->Add(new TextView(line))->SetFocusable(true);
	}

	// TODO : When we have both target and IR, need a third column.
	if (debugInfo.targetDisasm.size()) {
		for (auto line : debugInfo.targetDisasm) {
			rightDisasm_->Add(new TextView(line))->SetFocusable(true);
		}
	} else {
		for (auto line : debugInfo.irDisasm) {
			rightDisasm_->Add(new TextView(line))->SetFocusable(true);
		}
	}

	int numMips = leftDisasm_->GetNumSubviews();
	int numHost = rightDisasm_->GetNumSubviews();

	snprintf(temp, sizeof(temp), "%d to %d : %d%%", numMips, numHost, 100 * numHost / numMips);
	blockStats_->SetText(temp);
}

UI::EventReturn JitCompareScreen::OnAddressChange(UI::EventParams &e) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}
	JitBlockCacheDebugInterface *blockCache = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (!blockCache)
		return UI::EVENT_DONE;
	u32 addr;
	if (blockAddr_->GetText().size() > 8)
		return UI::EVENT_DONE;
	if (1 == sscanf(blockAddr_->GetText().c_str(), "%08x", &addr)) {
		if (Memory::IsValidAddress(addr)) {
			currentBlock_ = blockCache->GetBlockNumberFromStartAddress(addr);
			UpdateDisasm();
		}
	}
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnShowStats(UI::EventParams &e) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}

	JitBlockCacheDebugInterface *blockCache = MIPSComp::jit->GetBlockCacheDebugInterface();
	BlockCacheStats bcStats;
	blockCache->ComputeStats(bcStats);
	NOTICE_LOG(JIT, "Num blocks: %i", bcStats.numBlocks);
	NOTICE_LOG(JIT, "Average Bloat: %0.2f%%", 100 * bcStats.avgBloat);
	NOTICE_LOG(JIT, "Min Bloat: %0.2f%%  (%08x)", 100 * bcStats.minBloat, bcStats.minBloatBlock);
	NOTICE_LOG(JIT, "Max Bloat: %0.2f%%  (%08x)", 100 * bcStats.maxBloat, bcStats.maxBloatBlock);

	int ctr = 0, sz = (int)bcStats.bloatMap.size();
	for (auto iter : bcStats.bloatMap) {
		if (ctr < 10 || ctr > sz - 10) {
			NOTICE_LOG(JIT, "%08x: %f", iter.second, iter.first);
		} else if (ctr == 10) {
			NOTICE_LOG(JIT, "...");
		}
		ctr++;
	}
	return UI::EVENT_DONE;
}


UI::EventReturn JitCompareScreen::OnSelectBlock(UI::EventParams &e) {
	auto dev = GetI18NCategory("Developer");

	auto addressPrompt = new AddressPromptScreen(dev->T("Block address"));
	addressPrompt->OnChoice.Handle(this, &JitCompareScreen::OnBlockAddress);
	screenManager()->push(addressPrompt);
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnPrevBlock(UI::EventParams &e) {
	currentBlock_--;
	UpdateDisasm();
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnNextBlock(UI::EventParams &e) {
	currentBlock_++;
	UpdateDisasm();
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnBlockAddress(UI::EventParams &e) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}

	JitBlockCacheDebugInterface *blockCache = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (!blockCache)
		return UI::EVENT_DONE;

	if (Memory::IsValidAddress(e.a)) {
		currentBlock_ = blockCache->GetBlockNumberFromStartAddress(e.a);
	} else {
		currentBlock_ = -1;
	}
	UpdateDisasm();
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnRandomBlock(UI::EventParams &e) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}

	JitBlockCacheDebugInterface *blockCache = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (!blockCache)
		return UI::EVENT_DONE;

	int numBlocks = blockCache->GetNumBlocks();
	if (numBlocks > 0) {
		currentBlock_ = rand() % numBlocks;
	}
	UpdateDisasm();
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnRandomVFPUBlock(UI::EventParams &e) {
	OnRandomBlock(IS_VFPU);
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnRandomFPUBlock(UI::EventParams &e) {
	OnRandomBlock(IS_FPU);
	return UI::EVENT_DONE;
}

void JitCompareScreen::OnRandomBlock(int flag) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return;
	}
	JitBlockCacheDebugInterface *blockCache = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (!blockCache)
		return;

	int numBlocks = blockCache->GetNumBlocks();
	if (numBlocks > 0) {
		bool anyWanted = false;
		int tries = 0;
		while (!anyWanted && tries < numBlocks) {
			currentBlock_ = rand() % numBlocks;
			JitBlockDebugInfo b = blockCache->GetBlockDebugInfo(currentBlock_);
			u32 mipsBytes = (u32)b.origDisasm.size() * 4;
			for (u32 addr = b.originalAddress; addr <= b.originalAddress + mipsBytes; addr += 4) {
				MIPSOpcode opcode = Memory::Read_Instruction(addr);
				if (MIPSGetInfo(opcode) & flag) {
					char temp[256];
					MIPSDisAsm(opcode, addr, temp);
					// INFO_LOG(HLE, "Stopping at random instruction: %08x %s", addr, temp);
					anyWanted = true;
					break;
				}
			}
			tries++;
		}

		if (!anyWanted)
			currentBlock_ = -1;
	}
	UpdateDisasm();
}

UI::EventReturn JitCompareScreen::OnCurrentBlock(UI::EventParams &e) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
	if (!blockCache)
		return UI::EVENT_DONE;
	std::vector<int> blockNum;
	blockCache->GetBlockNumbersFromAddress(currentMIPS->pc, &blockNum);
	if (blockNum.size() > 0) {
		currentBlock_ = blockNum[0];
	} else {
		currentBlock_ = -1;
	}
	UpdateDisasm();
	return UI::EVENT_DONE;
}

int ShaderListScreen::ListShaders(DebugShaderType shaderType, UI::LinearLayout *view) {
	using namespace UI;
	std::vector<std::string> shaderIds_ = gpu->DebugGetShaderIDs(shaderType);
	int count = 0;
	for (auto id : shaderIds_) {
		Choice *choice = view->Add(new Choice(gpu->DebugGetShaderString(id, shaderType, SHADER_STRING_SHORT_DESC)));
		choice->SetTag(id);
		choice->OnClick.Handle(this, &ShaderListScreen::OnShaderClick);
		count++;
	}
	return count;
}

struct { DebugShaderType type; const char *name; } shaderTypes[] = {
	{ SHADER_TYPE_VERTEX, "Vertex" },
	{ SHADER_TYPE_FRAGMENT, "Fragment" },
	// { SHADER_TYPE_GEOMETRY, "Geometry" },
	{ SHADER_TYPE_VERTEXLOADER, "VertexLoader" },
	{ SHADER_TYPE_PIPELINE, "Pipeline" },
	{ SHADER_TYPE_DEPAL, "Depal" },
	{ SHADER_TYPE_SAMPLER, "Sampler" },
};

void ShaderListScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");

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

	auto di = GetI18NCategory("Dialog");

	LinearLayout *layout = new LinearLayout(ORIENT_VERTICAL);
	root_ = layout;

	layout->Add(new TextView(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC)));

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0));
	scroll->SetTag("DevShaderView");
	layout->Add(scroll);

	LinearLayout *lineLayout = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	lineLayout->SetSpacing(0.0);
	scroll->Add(lineLayout);

	std::vector<std::string> lines;
	SplitString(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SOURCE_CODE), '\n', lines);

	for (auto line : lines) {
		lineLayout->Add(new TextView(line, FLAG_DYNAMIC_ASCII, true));
	}

	layout->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
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
	auto di = GetI18NCategory("Dialog");

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
	tabHolder->AddTab("Dumps", dumps);

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
	INFO_LOG(COMMON, "Trying to launch '%s'", url.c_str());
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
		listing_ = g_DownloadManager.StartDownload(framedumpsBaseUrl, Path(), acceptMime);
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
							INFO_LOG(COMMON, "Found ppdmp: '%s'", trimmed.c_str());
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
