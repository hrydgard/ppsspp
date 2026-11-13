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
#include "Common/File/DirListing.h"
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
#include "Common/UI/ScreenManager.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/Profiler/Profiler.h"

#include "Common/Log/LogManager.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/VR/PPSSPPVR.h"

#include "Core/WebServer.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/RetroAchievements.h"
#include "Core/CoreParameter.h"
#include "Core/HLE/sceKernel.h"  // GPI/GPO
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "GPU/Debugger/Record.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

// These two are for pipeline analysis, maybe should move elsewhere.
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "UI/BaseScreens.h"
#include "UI/DevScreens.h"
#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/ControlMappingScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/JitCompareScreen.h"
#include "UI/ShareFiles.h"
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
	"Frame profile",
	"Control Debug",
	"Audio Debug",
	"GPU Profile",
	"GPU Allocator Viewer",
	"Framebuffer list",
	"JIT stats",
};

void AddOverlayList(UI::ViewGroup *items, ScreenManager *screenManager) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	static_assert(ARRAY_SIZE(g_debugOverlayList) == (int)DebugOverlay::JIT_STATS + 1, "g_debugOverlayList must match DebugOverlay");
	PopupMultiChoice *choice = items->Add(new PopupMultiChoice((int *)&g_Config.iDebugOverlay, dev->T("Debug overlay"), g_debugOverlayList, 0, ARRAY_SIZE(g_debugOverlayList), I18NCat::DEVELOPER, screenManager));
	if (!(g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL)) {
		choice->HideChoice((int)DebugOverlay::GPU_PROFILE);
		choice->HideChoice((int)DebugOverlay::GPU_ALLOCATOR);
	}
}

// Zipped, so apps that only take known file types accept it. The zip import recognizes it too.
static void ShareFrameDump(const Path &dumpPath) {
	std::string name = dumpPath.GetFilename();
	if (endsWithNoCase(name, ".ppdmp")) {
		name.resize(name.size() - strlen(".ppdmp"));
	}
	if (!ShareFilesAsZip(name, { { dumpPath, "" } })) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		g_OSD.Show(OSDType::MESSAGE_ERROR, sy->T("Failed"));
	}
}

static bool CopyFileContents(const Path &src, const Path &dest) {
	FILE *in = File::OpenCFile(src, "rb");
	if (!in) {
		return false;
	}
	// Content URIs can be written but not created this way, so this works on a document the picker made.
	FILE *out = File::OpenCFile(dest, "wb");
	if (!out) {
		fclose(in);
		return false;
	}
	bool success = true;
	char buffer[65536];
	size_t count;
	while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0) {
		if (fwrite(buffer, 1, count, out) != count) {
			success = false;
			break;
		}
	}
	if (ferror(in)) {
		success = false;
	}
	fclose(in);
	if (fclose(out) != 0) {
		success = false;
	}
	return success;
}

static bool CanSaveFileAs() {
#if PPSSPP_PLATFORM(ANDROID)
	return System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER);
#else
	return false;
#endif
}

// Where a dump can't be shown in its folder: share it, save it somewhere the user picks, or neither.
class FrameDumpPopupScreen : public UI::PopupScreen {
public:
	FrameDumpPopupScreen(const Path &dumpPath) : PopupScreen(dumpPath.GetFilename()), dumpPath_(dumpPath) {}
	const char *tag() const override { return "FrameDumpPopup"; }

protected:
	bool ShowButtons() const override { return false; }
	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		auto di = GetI18NCategory(I18NCat::DIALOG);
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		if (CanShareFiles()) {
			parent->Add(new Choice(di->T("Share"), ImageID("I_SHARE")))->OnClick.Add([this](UI::EventParams &e) {
				ShareFrameDump(dumpPath_);
				TriggerFinish(DR_OK);
			});
		}
		if (CanSaveFileAs()) {
			parent->Add(new Choice(di->T("Save"), ImageID("I_FILE_SAVE")))->OnClick.Add([this, di](UI::EventParams &e) {
				// Stays open until the picker answers, since the answer is dropped once the screen is gone.
				System_BrowseForFileSave(GetRequesterToken(), di->T("Save"), dumpPath_.GetFilename(), BrowseFileType::ANY, [this](std::string_view value, int) {
					auto sy = GetI18NCategory(I18NCat::SYSTEM);
					const Path dest(value);
					if (CopyFileContents(dumpPath_, dest)) {
						// A picked document's URI ends in an ID, so ask for its name.
						File::FileInfo info;
						std::string name = dumpPath_.GetFilename();
						if (File::GetFileInfo(dest, &info) && !info.name.empty()) {
							name = info.name;
						}
						g_OSD.Show(OSDType::MESSAGE_SUCCESS, name);
					} else {
						g_OSD.Show(OSDType::MESSAGE_ERROR, sy->T("Failed"));
					}
					TriggerFinish(DR_OK);
				});
			});
		}
		parent->Add(new Choice(pa->T("Continue")))->OnClick.Add([this](UI::EventParams &e) {
			TriggerFinish(DR_CANCEL);
		});
	}

private:
	Path dumpPath_;
};

extern ScreenManager *g_screenManager;

void OnFrameDumpCreated(const Path &dumpPath, bool openNow) {
	NOTICE_LOG(Log::System, "Frame dump created at '%s'", dumpPath.c_str());
	std::function<void()> open;
	if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
		open = [dumpPath]() { System_ShowFileInFolder(dumpPath); };
	} else if (CanShareFiles() || CanSaveFileAs()) {
		open = [dumpPath]() { g_screenManager->push(new FrameDumpPopupScreen(dumpPath)); };
	}
	g_OSD.Show(OSDType::MESSAGE_SUCCESS, GetFriendlyPath(dumpPath), 7.0f, "framedump");
	if (!open) {
		return;
	}
	if (openNow) {
		// The recorder calls back on the emulation thread.
		System_RunOnMainThread(open);
	} else {
		g_OSD.SetClickCallback("framedump", open);
	}
}

void SaveFrameDump() {
	if (!gpu) {
		return;
	}
	gpu->GetRecorder()->RecordNextFrame([](const Path &dumpPath) {
		OnFrameDumpCreated(dumpPath, true);
	});
}

void DevMenuScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayout(ORIENT_VERTICAL);

	items->SetSpacing(0.0f);

	items->Add(new Choice(dev->T("Log View")))->OnClick.Add([this](UI::EventParams & e) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new LogViewScreen());
	});

	items->Add(new Choice(dev->T("Logging Channels")))->OnClick.Add([this](UI::EventParams & e) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new LogConfigScreen());
	});

	if (PSP_CoreParameter().fileType != IdentifiedFileType::PPSSPP_GE_DUMP) {
		items->Add(new Choice(dev->T("Create frame dump")))->OnClick.Add([](UI::EventParams &e) {
			SaveFrameDump();
		});
	}

	items->Add(new Choice(dev->T("Debugger"), ImageID("I_DEBUGGER")))->OnClick.Add([](UI::EventParams &e) {
		g_Config.bShowImDebugger = !g_Config.bShowImDebugger;
	});

	if (WebServerRunning(WebServerFlags::DEBUGGER)) {
		items->Add(new Choice(dev->T("Remote debugger"), ImageID("I_NETWORK")))->OnClick.Add([](UI::EventParams &e) {
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

	items->Add(new Choice(sy->T("Developer Tools")))->OnClick.Add([this](UI::EventParams &e) {
		UpdateUIState(UISTATE_PAUSEMENU);
		GameSettingsScreen *settings = new GameSettingsScreen(gamePath_);
		settings->OpenDeveloperTools();
		screenManager()->push(settings);
	});

	// Debug overlay
	AddOverlayList(items, screenManager());

	items->Add(new Choice(dev->T("Jit Compare")))->OnClick.Add([this](UI::EventParams &e) {
		UpdateUIState(UISTATE_PAUSEMENU);
		screenManager()->push(new JitCompareScreen());
	});
	items->Add(new Choice(dev->T("Shader Viewer")))->OnClick.Add([this](UI::EventParams &e) {
		UpdateUIState(UISTATE_PAUSEMENU);
		if (gpu)  // Avoid crashing if chosen while the game is being loaded.
			screenManager()->push(new ShaderListScreen());
	});

	items->Add(new Choice(dev->T("Toggle Freeze")))->OnClick.Add([](UI::EventParams &e) {
		// Freezing restores a savestate every frame, so it's not allowed in hardcore mode.
		if (Achievements::WarnUserIfHardcoreModeActive(false)) {
			return;
		}
		if (PSP_CoreParameter().frozen) {
			PSP_CoreParameter().frozen = false;
		} else {
			PSP_CoreParameter().freezeNext = true;
		}
	});

	items->Add(new Choice(dev->T("Reset limited logging")))->OnClick.Add([](UI::EventParams &e) {
		Reporting::ResetCounts();
	});

	items->Add(new Choice(dev->T("GPI/GPO switches/LEDs")))->OnClick.Add([this, dev](UI::EventParams &e) {
		screenManager()->push(new GPIGPOScreen(dev->T("GPI/GPO switches/LEDs")));
	});

	// This one is not very useful these days, and only really on desktop. Hide it on other platforms.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
		items->Add(new Choice(dev->T("Dump next frame to log")))->OnClick.Add([](UI::EventParams &e) {
			gpu->DumpNextFrame();
		});
	}

	items->Add(new Choice(dev->T("Compatibility flags")))->OnClick.Add([this](UI::EventParams &e) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
		auto di = GetI18NCategory(I18NCat::DIALOG);
		std::string activeFlags = PSP_CoreParameter().compat.GetActiveFlagsString();
		std::string compatText = join(di->T("Enabled"), "\n");
		compatText += activeFlags.empty() ? "None" : activeFlags;
		compatText += "\n\n";
		std::string ignored = g_Config.sIgnoreCompatSettings;
		compatText += join(di->T("Ignored"), ":");
		if (ignored.empty()) {
			compatText += " None";
		} else {
			compatText += "\n" + ignored;
		}
		compatText += "\n\n";
		compatText += "Loaded files: ";
		for (const auto &file : PSP_CoreParameter().compat.filesLoaded()) {
			compatText += "\n" + file;
		}
		screenManager()->push(new MessagePopupScreen(dev->T("Compatibility flags"), compatText, di->T("OK"), ""));
	});

	scroll->Add(items);
	parent->Add(scroll);

	g_logManager.EnableOutput(LogOutput::RingBuffer);
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

std::string_view LogConfigScreen::GetTitle() const {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);
	return dev->T("Logging Channels");
}

void LogConfigScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	parent->Add(new Choice(di->T("Toggle All")))->OnClick.Handle(this, &LogConfigScreen::OnToggleAll);
	parent->Add(new Choice(di->T("Enable All")))->OnClick.Handle(this, &LogConfigScreen::OnEnableAll);
	parent->Add(new Choice(di->T("Disable All")))->OnClick.Handle(this, &LogConfigScreen::OnDisableAll);
	parent->Add(new Choice(dev->T("Log Level")))->OnClick.Handle(this, &LogConfigScreen::OnLogLevel);
}

void LogConfigScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	int cellSize = 350;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = parent->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

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
	ChoiceGroup *disabledFlags = vert->Add(new ChoiceGroupList())->Add(new ChoiceGroup(dev->T("Disabled JIT functionality")));

	for (auto flag : jitDisableFlags) {
		// Do not add translation of these.
		disabledFlags->Add(new BitCheckBox(&g_Config.uJitDisableFlags, (uint32_t)flag.flag, flag.name));
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

static constexpr struct { DebugShaderType type; const char *name; } shaderTypes[] = {
	{ SHADER_TYPE_VERTEX, "Vertex" },
	{ SHADER_TYPE_FRAGMENT, "Fragment" },
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
			if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN && shaderTypes[i].type == SHADER_TYPE_PIPELINE) {
				CollapsibleSection *pipelineAnalysisSection = tabContent->Add(new CollapsibleSection("Pipeline Analysis"));
				AddPipelineAnalysis(pipelineAnalysisSection);
				pipelineAnalysisSection->SetOpen(false);
			}

			LinearLayout *shaderList = new LinearLayoutList(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
			int count = ListShaders(shaderTypes[i].type, shaderList);
			tabContent->Add(shaderList);
		});
	}
}

const bool IsSameExceptForShaders(const VulkanPipelineKey &a, const VulkanPipelineKey &b) {
	return a.useHWTransform == b.useHWTransform && a.raster == b.raster && a.vtxFmtId == b.vtxFmtId;
}

const bool IsSameExceptForVertexFormat(const VulkanPipelineKey &a, const VulkanPipelineKey &b) {
	return a.useHWTransform == b.useHWTransform && a.raster == b.raster && a.vid == b.vid && a.fid == b.fid;
}

void ShaderListScreen::AddPipelineAnalysis(UI::LinearLayout *tabContent) {
#if !PPSSPP_PLATFORM(UWP)
	GPU_Vulkan *vgpu = dynamic_cast<GPU_Vulkan *>(gpu);
	const PipelineManagerVulkan *pipelineManager = vgpu->GetPipelineManager();
	// Collect all pipeline keys in a vector.
	std::vector<VulkanPipelineKey> keys;
	pipelineManager->GetPipelines().Iterate([&keys](const VulkanPipelineKey &key, VulkanPipeline *pipeline) {
		keys.push_back(key);
	});

	using namespace UI;

	// Now, see if there are two pipelines that are identical except for the shader IDs. If so, we may be able to
	// merge them by making the shaders a bit more "uber".
	for (size_t i = 0; i < keys.size(); i++) {
		std::string toBeMerged;
		for (size_t j = i + 1; j < keys.size(); j++) {
			const VulkanPipelineKey &keyA = keys[i];
			const VulkanPipelineKey &keyB = keys[j];
			if (IsSameExceptForShaders(keyA, keyB)) {
				// Found a pair of pipelines that are identical except for the shaders.
				if (keyA.vid != keyB.vid) {
					toBeMerged += "VShader: " + keyA.vid.Description(false) + " vs " + keyB.vid.Description(false) + "\n";
				}
				if (keyA.fid != keyB.fid) {
					toBeMerged += "FShader: " + keyA.fid.Description(false) + " vs " + keyB.fid.Description(false) + "\n";
				}
			}
			if (IsSameExceptForVertexFormat(keyA, keyB)) {
				if (keyA.vtxFmtId != 0 && keyB.vtxFmtId != 0 && keyA.vtxFmtId != keyB.vtxFmtId) {
					DecVtxFormat fmtA;
					DecVtxFormat fmtB;
					fmtA.InitializeFromID(keyA.vtxFmtId);
					fmtB.InitializeFromID(keyB.vtxFmtId);
					toBeMerged += "VertexFmt: " + fmtA.ToString() + " vs " + fmtB.ToString() + "\n";
				}
			}
		}
		if (!toBeMerged.empty()) {
			tabContent->Add(new UI::TextView(keys[i].GetDescription(SHADER_STRING_SHORT_DESC), FLAG_DYNAMIC_ASCII, true));
			tabContent->Add(new UI::TextView(toBeMerged, FLAG_DYNAMIC_ASCII, true));
		}
	}
#endif
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
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	LinearLayout *layout = new LinearLayout(ORIENT_VERTICAL);
	root_ = layout;

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams()))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice(ImageID("I_FILE_COPY"), new LinearLayoutParams()))->OnClick.Add([this](UI::EventParams &e) {
		System_CopyStringToClipboard(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC));
	});
	topbar->Add(new Choice(dev->T("Copy source code"), ImageID("I_FILE_COPY"), new LinearLayoutParams()))->OnClick.Add([this](UI::EventParams &e) {
		System_CopyStringToClipboard(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SOURCE_CODE));
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
	if (ki.flags & KeyInputFlags::CHAR) {
		if (ki.unicodeChar == 'C' || ki.unicodeChar == 'c') {
			System_CopyStringToClipboard(gpu->DebugGetShaderString(id_, type_, SHADER_STRING_SHORT_DESC));
		}
	}
	return UIBaseDialogScreen::key(ki);
}


const std::string framedumpsBaseUrl = "http://framedump.ppsspp.org/repro/";

FrameDumpTestScreen::~FrameDumpTestScreen() {
	if (listing_) {
		listing_->Cancel();
	}
	if (dumpDownload_) {
		dumpDownload_->Cancel();
	}
}

void FrameDumpTestScreen::CreateTabs() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	AddTab("General", di->T("Dumps"), [this](UI::LinearLayout *parent) {
		ChoiceGroup *dumps = parent->Add(new ChoiceGroupList())->Add(new ChoiceGroup("GE Frame Dumps"));

		for (auto &file : files_) {
			std::string url = framedumpsBaseUrl + file;
			Choice *c = dumps->Add(new Choice(file));
			c->SetTag(url);
			c->OnClick.Add([this, url = Path(url)](UI::EventParams &e) {
				INFO_LOG(Log::Common, "Trying to launch '%s'", url.c_str());
				// Our disc streaming functionality detects the URL and takes over and handles loading framedumps well,
				// except for some reason the game ID.
				// TODO: Fix that since it can be important for compat settings.
				screenManager()->switchScreen(new EmuScreen(url));
			});
		}
	});

	EnsureTabs();
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

bool TouchTestScreen::touch(const TouchInput &touch) {
	UIBaseDialogScreen::touch(touch);
	if (touch.flags & TouchInputFlags::DOWN) {
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
	if (touch.flags & TouchInputFlags::MOVE) {
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
	if (touch.flags & TouchInputFlags::UP) {
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
	return true;
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
	static const char *renderingBackend[] = { "OpenGL", "(n/a)", "Direct3D 11", "Vulkan", "Metal" };
	PopupMultiChoice *renderingBackendChoice = root_->Add(new PopupMultiChoice(&g_Config.iGPUBackend, gr->T("Backend"), renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend), I18NCat::GRAPHICS, screenManager()));
	renderingBackendChoice->OnChoice.Handle(this, &TouchTestScreen::OnRenderingBackend);

	if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
		renderingBackendChoice->HideChoice((int)GPUBackend::OPENGL);
	renderingBackendChoice->HideChoice(1);   // previously D3D9
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D11);
	if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
		renderingBackendChoice->HideChoice((int)GPUBackend::VULKAN);
	if (!g_Config.IsBackendEnabled(GPUBackend::METAL))
		renderingBackendChoice->HideChoice((int)GPUBackend::METAL);
#endif

#if PPSSPP_PLATFORM(ANDROID)
	root_->Add(new Choice(gr->T("Recreate Activity")))->OnClick.Handle(this, &TouchTestScreen::OnRecreateActivity);
#endif
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
	snprintf(buf, sizeof(buf), "%s (%d) Device ID: %d [%s%s%s%s%s%s%s%s]", KeyMap::GetKeyName(key.keyCode).c_str(), key.keyCode, key.deviceId,
		(key.flags & KeyInputFlags::IS_REPEAT) ? "REP " : "",
		(key.flags & KeyInputFlags::UP) ? "UP " : "",
		(key.flags & KeyInputFlags::DOWN) ? "DOWN " : "",
		(key.flags & KeyInputFlags::CHAR) ? "CHAR " : "",
		(key.flags & KeyInputFlags::ModCtrl) ? "CTRL " : "",
		(key.flags & KeyInputFlags::ModShift) ? "SHIFT " : "",
		(key.flags & KeyInputFlags::ModAlt) ? "ALT " : "",
		(key.flags & KeyInputFlags::ModMeta) ? "META " : "");
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

#undef DrawText

void TouchTestScreen::DrawForeground(UIContext &dc) {
	Bounds bounds = GetLayoutBounds(dc);

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
			dc.DrawText(dc.GetTheme().uiFont, buffer, touches_[i].x, touches_[i].y + (touches_[i].y > g_display.dp_yres - 100.0f ? -135.0f : 95.0f), 0xFFFFFFFF, ALIGN_HCENTER | FLAG_DYNAMIC_ASCII);
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
	dc.DrawTextShadow(dc.GetTheme().uiFont, buffer, bounds.centerX(), bounds.y + 20.0f, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
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
	System_RestartApp("--start-screen=touchscreentest");
}

void TouchTestScreen::OnRecreateActivity(UI::EventParams &e) {
	RecreateActivity();
}

static const float kPi = 3.14159265f;

static uint32_t RGB8(int r, int g, int b) {
	return 0xFF000000 | (b << 16) | (g << 8) | r;
}

static uint32_t Gray8(int v) {
	return RGB8(v, v, v);
}

// Fully saturated, full value. HSV is piecewise linear between multiples of 60 degrees, so a wheel
// with vertices on those angles interpolates to what a texture would show.
static uint32_t HueColor(float degrees) {
	float h = fmodf(degrees, 360.0f) / 60.0f;
	int sector = (int)h;
	float f = h - sector;
	float r = 0.0f, g = 0.0f, b = 0.0f;
	switch (sector) {
	case 0: r = 1.0f; g = f; break;
	case 1: r = 1.0f - f; g = 1.0f; break;
	case 2: g = 1.0f; b = f; break;
	case 3: g = 1.0f - f; b = 1.0f; break;
	case 4: r = f; b = 1.0f; break;
	default: r = 1.0f; b = 1.0f - f; break;
	}
	return RGB8((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f));
}

class ColorTestView : public UI::View {
public:
	ColorTestView(UI::LayoutParams *layoutParams) : UI::View(layoutParams) {}
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return "Color test"; }

private:
	struct Label {
		std::string text;
		float x;
		float y;
		uint32_t color;
	};

	static void HRamp(DrawBuffer *draw, float x, float y, float w, float h, uint32_t left, uint32_t right);
	static void Steps(DrawBuffer *draw, std::vector<Label> &labels, float x, float y, float w, float h, int first, int step, int count);
	static void Wheel(DrawBuffer *draw, float cx, float cy, float radius, uint32_t center);
	static void GammaCheck(DrawBuffer *draw, std::vector<Label> &labels, float x, float y, float w, float h);
};

void ColorTestView::HRamp(DrawBuffer *draw, float x, float y, float w, float h, uint32_t left, uint32_t right) {
	draw->V(x, y, left, 0.0f, 0.0f);
	draw->V(x + w, y, right, 1.0f, 0.0f);
	draw->V(x + w, y + h, right, 1.0f, 1.0f);
	draw->V(x, y, left, 0.0f, 0.0f);
	draw->V(x + w, y + h, right, 1.0f, 1.0f);
	draw->V(x, y + h, left, 0.0f, 1.0f);
}

void ColorTestView::Steps(DrawBuffer *draw, std::vector<Label> &labels, float x, float y, float w, float h, int first, int step, int count) {
	const float stepW = w / count;
	for (int i = 0; i < count; i++) {
		const int v = std::min(first + i * step, 255);
		draw->Rect(x + i * stepW, y, stepW, h, Gray8(v));
		labels.push_back({ StringFromFormat("%d", v), x + (i + 0.5f) * stepW, y + h * 0.5f, v < 128 ? 0xFFFFFFFF : 0xFF000000 });
	}
}

void ColorTestView::Wheel(DrawBuffer *draw, float cx, float cy, float radius, uint32_t center) {
	// A multiple of 6, so the 60 degree corners are vertices.
	const int segments = 360;
	for (int i = 0; i < segments; i++) {
		const float a1 = i * (360.0f / segments);
		const float a2 = (i + 1) * (360.0f / segments);
		const float r1 = a1 * kPi / 180.0f;
		const float r2 = a2 * kPi / 180.0f;
		draw->V(cx, cy, center, 0.0f, 0.0f);
		draw->V(cx + cosf(r1) * radius, cy - sinf(r1) * radius, HueColor(a1), 0.0f, 0.0f);
		draw->V(cx + cosf(r2) * radius, cy - sinf(r2) * radius, HueColor(a2), 0.0f, 0.0f);
	}
}

// Alternating black and white pixel rows average to 50% light. Blurred (by squinting, or resampling
// like a VR compositor's), they match 188 when averaged in linear light, 128 when averaged as encoded.
void ColorTestView::GammaCheck(DrawBuffer *draw, std::vector<Label> &labels, float x, float y, float w, float h) {
	// The height of a pixel in UI coordinates. Not pixel_in_dps_y, which is the inverse.
	const float px = (float)g_display.dp_yres / (float)g_display.pixel_yres;
	const int firstRow = (int)ceilf(y / px);
	const int lastRow = (int)floorf((y + h) / px);
	for (int row = firstRow; row < lastRow; row++) {
		draw->Rect(x, row * px, w, px, (row & 1) ? 0xFFFFFFFF : 0xFF000000);
	}
	const float patchW = w * 0.3f;
	const float patchH = h * 0.5f;
	const float patchY = y + (h - patchH) * 0.5f;
	const int values[2] = { 128, 188 };
	for (int i = 0; i < 2; i++) {
		const float patchX = x + w * (i == 0 ? 0.15f : 0.55f);
		draw->Rect(patchX, patchY, patchW, patchH, Gray8(values[i]));
		labels.push_back({ StringFromFormat("%d", values[i]), patchX + patchW * 0.5f, patchY + patchH * 0.5f, values[i] < 160 ? 0xFFFFFFFF : 0xFF000000 });
	}
}

void ColorTestView::Draw(UIContext &dc) {
	using namespace UI;
	const float margin = 8.0f;
	const float x = bounds_.x + margin;
	const float w = bounds_.w - margin * 2.0f;
	const float headerH = 30.0f;
	float y = bounds_.y + headerH;
	const float h = bounds_.y2() - y - margin;

	std::vector<Label> labels;
	DrawBuffer *draw = dc.Draw();
	dc.Flush();
	dc.BeginNoTex();
	draw->Rect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, 0xFF000000);

	// Smooth ramps.
	const float rampH = h * 0.07f;
	const uint32_t rampColors[4] = { RGB8(255, 0, 0), RGB8(0, 255, 0), RGB8(0, 0, 255), RGB8(255, 255, 255) };
	for (uint32_t color : rampColors) {
		HRamp(draw, x, y, w, rampH, 0xFF000000, color);
		y += rampH + 2.0f;
	}
	y += margin;

	// Steps: the whole range, then the ends, where crushed blacks and clipped whites show.
	const float stepH = h * 0.075f;
	Steps(draw, labels, x, y, w, stepH, 0, 16, 17);
	y += stepH + 2.0f;
	Steps(draw, labels, x, y, w * 0.5f - 1.0f, stepH, 0, 2, 16);
	Steps(draw, labels, x + w * 0.5f + 1.0f, y, w * 0.5f - 1.0f, stepH, 225, 2, 16);
	y += stepH + margin;

	// Bars: white, the secondaries and primaries, black.
	const uint32_t bars[8] = { RGB8(255, 255, 255), RGB8(255, 255, 0), RGB8(0, 255, 255), RGB8(0, 255, 0), RGB8(255, 0, 255), RGB8(255, 0, 0), RGB8(0, 0, 255), RGB8(0, 0, 0) };
	const float barW = w / 8.0f;
	for (int i = 0; i < 8; i++) {
		draw->Rect(x + i * barW, y, barW, stepH, bars[i]);
	}
	y += stepH + margin;

	// The wheels (fading to white and to black in the middle), and the gamma check.
	const float bottomH = bounds_.y2() - margin - y;
	const float radius = std::max(0.0f, std::min(bottomH, w * 0.25f) * 0.5f - margin);
	Wheel(draw, x + radius, y + bottomH * 0.5f, radius, 0xFFFFFFFF);
	Wheel(draw, x + radius * 3.0f + margin * 2.0f, y + bottomH * 0.5f, radius, 0xFF000000);
	const float gammaX = x + radius * 4.0f + margin * 4.0f;
	const float captionH = 24.0f;
	GammaCheck(draw, labels, gammaX, y + captionH, x + w - gammaX, bottomH - captionH);
	dc.Flush();

	dc.Begin();
	const FontStyle &tinyFont = *GetTextStyle(dc, TextSize::Tiny);
	for (const Label &label : labels) {
		dc.DrawText(tinyFont, label.text, label.x, label.y, label.color, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
	}
	dc.DrawText(tinyFont, "Blurred, the lines match 188 if averaged in linear light, 128 if averaged as encoded", gammaX, y, 0xFFFFFFFF, ALIGN_TOPLEFT);
	const std::string header = StringFromFormat("%s%s, %dx%d pixels", dc.GetDrawContext()->GetInfoString(Draw::InfoField::APINAME).c_str(), IsVREnabled() ? ", VR" : "", g_display.pixel_xres, g_display.pixel_yres);
	dc.DrawText(*GetTextStyle(dc, TextSize::Small), header, bounds_.x + margin, bounds_.y + 4.0f, 0xFFFFFFFF, ALIGN_TOPLEFT);
	dc.Flush();
	dc.Begin();
}

void ColorTestScreen::CreateViews() {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	root_ = new LinearLayout(ORIENT_VERTICAL);
	root_->Add(new ColorTestView(new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
	root_->Add(new Button(di->T("Back"), new LinearLayoutParams(FILL_PARENT, 64, Margins(10, 0))))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}
