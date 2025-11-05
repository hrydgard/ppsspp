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
#include "Common/System/OSD.h"
#include "Common/Audio/AudioBackend.h"

#include "Common/File/AndroidStorage.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Net/HTTPClient.h"
#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"
#include "Common/UI/IconCache.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Profiler/Profiler.h"

#include "Common/Log/LogManager.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"
#include "Common/GPU/ShaderWriter.h"

#include "Core/WebServer.h"
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
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "UI/BaseScreens.h"
#include "UI/DevScreens.h"
#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DeveloperToolsScreen.h"
#include "UI/JitCompareScreen.h"
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

void SaveFrameDump() {
	if (!gpuDebug) {
		return;
	}
	gpuDebug->GetRecorder()->RecordNextFrame([](const Path &dumpPath) {
		NOTICE_LOG(Log::System, "Frame dump created at '%s'", dumpPath.c_str());
		if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
			System_ShowFileInFolder(dumpPath);
		} else {
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, dumpPath.ToVisualString(), 7.0f);
		}
	});
}

void DevMenuScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayout(ORIENT_VERTICAL);

	items->Add(new Choice(dev->T("Log View")))->OnClick.Add([this](UI::EventParams & e) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new LogViewScreen());
	});

	items->Add(new Choice(dev->T("Logging Channels")))->OnClick.Add([this](UI::EventParams & e) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new LogConfigScreen());
	});

	items->Add(new Choice(dev->T("Debugger")))->OnClick.Add([](UI::EventParams &e) {
		g_Config.bShowImDebugger = !g_Config.bShowImDebugger;
	});

	if (WebServerRunning(WebServerFlags::DEBUGGER)) {
		items->Add(new Choice(dev->T("Remote debugger")))->OnClick.Add([](UI::EventParams &e) {
			int port = g_Config.iRemoteISOPort;  // Also used for serving a local remote debugger.
			if (g_Config.bRemoteDebuggerLocal) {
				// TODO: Need to modify this URL to add /cpu when we upgrade to the latest version of the web debugger.
				char uri[64];
				snprintf(uri, sizeof(uri), "http://localhost:%d/debugger/", port);
				System_LaunchUrl(LaunchUrlType::BROWSER_URL, uri);
			} else {
				System_LaunchUrl(LaunchUrlType::BROWSER_URL, "http://ppsspp-debugger.unknownbrackets.org/cpu");  // NOTE: https doesn't work
			}
		});
	}

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
	});

	items->Add(new Choice(dev->T("Reset limited logging")))->OnClick.Handle(this, &DevMenuScreen::OnResetLimitedLogging);

	items->Add(new Choice(dev->T("GPI/GPO switches/LEDs")))->OnClick.Add([=](UI::EventParams &e) {
		screenManager()->push(new GPIGPOScreen(dev->T("GPI/GPO switches/LEDs")));
	});

	if (PSP_CoreParameter().fileType != IdentifiedFileType::PPSSPP_GE_DUMP) {
		items->Add(new Choice(dev->T("Create frame dump")))->OnClick.Add([](UI::EventParams &e) {
			SaveFrameDump();
		});
	}

	// This one is not very useful these days, and only really on desktop. Hide it on other platforms.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
		items->Add(new Choice(dev->T("Dump next frame to log")))->OnClick.Add([](UI::EventParams &e) {
			gpu->DumpNextFrame();
		});
	}

	scroll->Add(items);
	parent->Add(scroll);

	g_logManager.EnableOutput(LogOutput::RingBuffer);
}

void DevMenuScreen::OnResetLimitedLogging(UI::EventParams &e) {
	Reporting::ResetCounts();
}

void DevMenuScreen::OnDeveloperTools(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new DeveloperToolsScreen(gamePath_));
}

void DevMenuScreen::OnJitCompare(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	screenManager()->push(new JitCompareScreen());
}

void DevMenuScreen::OnShaderView(UI::EventParams &e) {
	UpdateUIState(UISTATE_PAUSEMENU);
	if (gpu)  // Avoid crashing if chosen while the game is being loaded.
		screenManager()->push(new ShaderListScreen());
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

void LogViewScreen::UpdateLog() {
	using namespace UI;
	const RingbufferLog &ring = g_logManager.GetRingbuffer();
	vert_->Clear();

	// TODO: Direct rendering without TextViews.
	for (int i = ring.GetCount() - 1; i >= 0; i--) {
		TextView *v = vert_->Add(new TextView(StripSpaces(ring.TextAt(i)), FLAG_DYNAMIC_ASCII, true));
		uint32_t color = LogManager::GetLevelColor(ring.LevelAt(i));
		v->SetTextColor(0xFF000000 | color);
	}
	toBottom_ = true;
}

void LogViewScreen::update() {
	UIBaseDialogScreen::update();
	if (toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}
}

void LogViewScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);

	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	root_ = outer;

	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	bottom->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	vert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert_->SetSpacing(0);

	UpdateLog();
}

void LogConfigScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	root_ = new ScrollView(ORIENT_VERTICAL);

	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert->SetSpacing(0);

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice(ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice(di->T("Toggle All")))->OnClick.Handle(this, &LogConfigScreen::OnToggleAll);
	topbar->Add(new Choice(di->T("Enable All")))->OnClick.Handle(this, &LogConfigScreen::OnEnableAll);
	topbar->Add(new Choice(di->T("Disable All")))->OnClick.Handle(this, &LogConfigScreen::OnDisableAll);
	topbar->Add(new Choice(dev->T("Log Level")))->OnClick.Handle(this, &LogConfigScreen::OnLogLevel);

	vert->Add(topbar);

	vert->Add(new ItemHeader(dev->T("Logging Channels")));

	int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		Log type = (Log)i;
		LogChannel *chan = g_logManager.GetLogChannel(type);
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(cellSize - 50, WRAP_CONTENT));
		row->SetSpacing(0);
		row->Add(new CheckBox(&chan->enabled, "", "", new LinearLayoutParams(50, WRAP_CONTENT)));
		row->Add(new PopupMultiChoice((int *)&chan->level, LogManager::GetLogTypeName(type), logLevelList, 1, 6, I18NCat::NONE, screenManager(), new LinearLayoutParams(1.0)));
		grid->Add(row);
	}
}

void LogConfigScreen::OnToggleAll(UI::EventParams &e) {
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = g_logManager.GetLogChannel((Log)i);
		chan->enabled = !chan->enabled;
	}
}

void LogConfigScreen::OnEnableAll(UI::EventParams &e) {
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = g_logManager.GetLogChannel((Log)i);
		chan->enabled = true;
	}
}

void LogConfigScreen::OnDisableAll(UI::EventParams &e) {
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogChannel *chan = g_logManager.GetLogChannel((Log)i);
		chan->enabled = false;
	}
}

void LogConfigScreen::OnLogLevelChange(UI::EventParams &e) {
	RecreateViews();
}

void LogConfigScreen::OnLogLevel(UI::EventParams &e) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	auto logLevelScreen = new LogLevelScreen(dev->T("Log Level"));
	logLevelScreen->OnChoice.Handle(this, &LogConfigScreen::OnLogLevelChange);
	if (e.v)
		logLevelScreen->SetPopupOrigin(e.v);
	screenManager()->push(logLevelScreen);
}

LogLevelScreen::LogLevelScreen(std::string_view title) : ListPopupScreen(title) {
	int NUMLOGLEVEL = 6;    
	std::vector<std::string> list;
	for (int i = 0; i < NUMLOGLEVEL; ++i) {
		list.push_back(logLevelList[i]);
	}
	adaptor_ = UI::StringVectorListAdaptor(list, -1);

	// CreateViews takes care of, well, that.
}

void LogLevelScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	int selected = listView_->GetSelected();
	
	for (int i = 0; i < LogManager::GetNumChannels(); ++i) {
		Log type = (Log)i;
		LogChannel *chan = g_logManager.GetLogChannel(type);
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
	topbar->Add(new Choice(ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice(di->T("Disable All")))->OnClick.Handle(this, &JitDebugScreen::OnDisableAll);
	topbar->Add(new Choice(di->T("Enable All")))->OnClick.Handle(this, &JitDebugScreen::OnEnableAll);

	vert->Add(topbar);
	vert->Add(new ItemHeader(dev->T("Disabled JIT functionality")));

	for (auto flag : jitDisableFlags) {
		// Do not add translation of these.
		vert->Add(new BitCheckBox(&g_Config.uJitDisableFlags, (uint32_t)flag.flag, flag.name));
	}
}

void JitDebugScreen::OnEnableAll(UI::EventParams &e) {
	g_Config.uJitDisableFlags &= ~(uint32_t)MIPSComp::JitDisable::ALL_FLAGS;
}

void JitDebugScreen::OnDisableAll(UI::EventParams &e) {
	g_Config.uJitDisableFlags |= (uint32_t)MIPSComp::JitDisable::ALL_FLAGS;
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

void ShaderListScreen::CreateTabs() {
	using namespace UI;

	for (size_t i = 0; i < ARRAY_SIZE(shaderTypes); i++) {
		int count = (int)gpu->DebugGetShaderIDs(shaderTypes[i].type).size();
		AddTab(shaderTypes[i].name, StringFromFormat("%s (%d)", shaderTypes[i].name, count), [this, i](UI::LinearLayout *tabContent) {
			LinearLayout *shaderList = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
			int count = ListShaders(shaderTypes[i].type, shaderList);
			tabContent->Add(shaderList);
		});
	}
}

void ShaderListScreen::OnShaderClick(UI::EventParams &e) {
	using namespace UI;
	std::string id = e.v->Tag();
	DebugShaderType type = shaderTypes[GetCurrentTab()].type;
	screenManager()->push(new ShaderViewScreen(id, type));
}

void ShaderViewScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);

	LinearLayout *layout = new LinearLayout(ORIENT_VERTICAL);
	root_ = layout;

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams()))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice(ImageID("I_FILE_COPY"), new LinearLayoutParams()))->OnClick.Add([this](UI::EventParams &e) {
		System_CopyStringToClipboard(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC));
	});
	topbar->Add(new TextView(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC), FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT, false));
	layout->Add(topbar);

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
}

bool ShaderViewScreen::key(const KeyInput &ki) {
	if (ki.flags & KEY_CHAR) {
		if (ki.unicodeChar == 'C' || ki.unicodeChar == 'c') {
			System_CopyStringToClipboard(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC));
		}
	}
	return UIBaseDialogScreen::key(ki);
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
	tabHolder = new TabHolder(ORIENT_VERTICAL, 200, TabHolderFlags::Default, nullptr, new AnchorLayoutParams(10, 0, 10, 0, false));
	root_->Add(tabHolder);
	tabHolder->AddBack(this);
	tabHolder->SetTag("DumpTypes");
	root_->SetDefaultFocusView(tabHolder);

	ViewGroup *dumpsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	dumpsScroll->SetTag("GameSettingsGraphics");
	LinearLayout *dumps = new LinearLayoutList(ORIENT_VERTICAL);
	dumps->SetSpacing(0);
	dumpsScroll->Add(dumps);
	tabHolder->AddTab("Dumps", ImageID::invalid(), dumpsScroll);

	dumps->Add(new ItemHeader("GE Frame Dumps"));

	for (auto &file : files_) {
		std::string url = framedumpsBaseUrl + file;
		Choice *c = dumps->Add(new Choice(file));
		c->SetTag(url);
		c->OnClick.Handle<FrameDumpTestScreen>(this, &FrameDumpTestScreen::OnLoadDump);
	}
}

void FrameDumpTestScreen::OnLoadDump(UI::EventParams &params) {
	Path url = Path(params.v->Tag());
	INFO_LOG(Log::Common, "Trying to launch '%s'", url.c_str());
	// Our disc streaming functionality detects the URL and takes over and handles loading framedumps well,
	// except for some reason the game ID.
	// TODO: Fix that since it can be important for compat settings.
	screenManager()->switchScreen(new EmuScreen(url));
}

void FrameDumpTestScreen::update() {
	UIScreen::update();

	if (!listing_) {
		const char *acceptMime = "text/html, */*; q=0.8";
		listing_ = g_DownloadManager.StartDownload(framedumpsBaseUrl, Path(), http::RequestFlags::ProgressBar | http::RequestFlags::ProgressBarDelayed, acceptMime);
	}

	if (listing_ && listing_->Done() && files_.empty()) {
		if (listing_->ResultCode() == 200) {
			std::string listingHtml;
			listing_->buffer().TakeAll(&listingHtml);

			std::vector<std::string> lines;
			// We rely slightly on nginx listing format here. Not great.
			SplitString(listingHtml, '\n', lines);
			for (auto &line : lines) {
				std::string trimmed(StripSpaces(line));
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
	UIBaseDialogScreen::touch(touch);
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
		if (!found && touch.buttons) {
			WARN_LOG(Log::System, "Move with buttons %d without touch down: %d", touch.buttons, touch.id);
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
	static const char *renderingBackend[] = { "OpenGL", "(n/a)", "Direct3D 11", "Vulkan" };
	PopupMultiChoice *renderingBackendChoice = root_->Add(new PopupMultiChoice(&g_Config.iGPUBackend, gr->T("Backend"), renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend), I18NCat::GRAPHICS, screenManager()));
	renderingBackendChoice->OnChoice.Handle(this, &TouchTestScreen::OnRenderingBackend);

	if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
		renderingBackendChoice->HideChoice((int)GPUBackend::OPENGL);
	renderingBackendChoice->HideChoice(1);   // previously D3D9
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D11);
	if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
		renderingBackendChoice->HideChoice((int)GPUBackend::VULKAN);
#endif

#if PPSSPP_PLATFORM(ANDROID)
	root_->Add(new Choice(gr->T("Recreate Activity")))->OnClick.Handle(this, &TouchTestScreen::OnRecreateActivity);
#endif
	root_->Add(new CheckBox(&g_Config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &TouchTestScreen::OnImmersiveModeChange);
	root_->Add(new Button(di->T("Back"), new LinearLayoutParams(FILL_PARENT, 64, Margins(10, 0))))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
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
	if (axis.deviceId == DEVICE_ID_MOUSE && (axis.axisId == JOYSTICK_AXIS_MOUSE_REL_X || axis.axisId == JOYSTICK_AXIS_MOUSE_REL_Y)) {
		// These spam a lot, don't log for now.
		return;
	}

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
	truncate_cpy(extra_debug, Android_GetInputDeviceDebugString());

	snprintf(buffer, sizeof(buffer),
		"display_res: %dx%d\n"
		"dp_res: %dx%d pixel_res: %dx%d\n"
		"dpi_scale: %0.3fx%0.3f\n"
		"dpi_scale_real: %0.3fx%0.3f\n"
		"delta: %0.2f ms fps: %0.3f\n%s",
		(int)System_GetPropertyInt(SYSPROP_DISPLAY_XRES), (int)System_GetPropertyInt(SYSPROP_DISPLAY_YRES),
		g_display.dp_xres, g_display.dp_yres, g_display.pixel_xres, g_display.pixel_yres,
		g_display.dpi_scale_x, g_display.dpi_scale_y,
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

void TouchTestScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_Notify(SystemNotification::IMMERSIVE_MODE_CHANGE);
	if (g_Config.iAndroidHwScale != 0) {
		RecreateActivity();
	}
}

void TouchTestScreen::OnRenderingBackend(UI::EventParams &e) {
	g_Config.Save("GameSettingsScreen::RenderingBackend");
	System_RestartApp("--touchscreentest");
}

void TouchTestScreen::OnRecreateActivity(UI::EventParams &e) {
	RecreateActivity();
}
