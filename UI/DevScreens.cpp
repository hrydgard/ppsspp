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

#include <algorithm>

#include "base/compat.h"
#include "gfx_es2/gl_state.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "profiler/profiler.h"

#include "Common/LogManager.h"
#include "Common/CPUDetect.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/CoreParameter.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/NativeJit.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "UI/MiscScreens.h"
#include "UI/DevScreens.h"
#include "UI/GameSettingsScreen.h"

#ifdef _WIN32
// Want to avoid including the full header here as it includes d3dx.h
int GetD3DXVersion();
#endif

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
	I18NCategory *de = GetI18NCategory("Developer");
	I18NCategory *sy = GetI18NCategory("System");

#if !defined(MOBILE_DEVICE)
	parent->Add(new Choice(de->T("Log View")))->OnClick.Handle(this, &DevMenu::OnLogView);
#endif
	parent->Add(new Choice(de->T("Logging Channels")))->OnClick.Handle(this, &DevMenu::OnLogConfig);
	parent->Add(new Choice(sy->T("Developer Tools")))->OnClick.Handle(this, &DevMenu::OnDeveloperTools);
	parent->Add(new Choice(de->T("Jit Compare")))->OnClick.Handle(this, &DevMenu::OnJitCompare);
	parent->Add(new Choice(de->T("Toggle Freeze")))->OnClick.Handle(this, &DevMenu::OnFreezeFrame);
	parent->Add(new Choice(de->T("Dump Frame GPU Commands")))->OnClick.Handle(this, &DevMenu::OnDumpFrame);
	parent->Add(new Choice(de->T("Toggle Audio Debug")))->OnClick.Handle(this, &DevMenu::OnToggleAudioDebug);
#ifdef USE_PROFILER
	parent->Add(new CheckBox(&g_Config.bShowFrameProfiler, de->T("Frame Profiler"), ""));
#endif

	RingbufferLogListener *ring = LogManager::GetInstance()->GetRingbufferListener();
	if (ring) {
		ring->SetEnable(true);
	}
}

UI::EventReturn DevMenu::OnToggleAudioDebug(UI::EventParams &e) {
	g_Config.bShowAudioDebug = !g_Config.bShowAudioDebug;
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
	// screenManager()->finishDialog(this, DR_OK);
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

void LogScreen::update(InputState &input) {
	UIDialogScreenWithBackground::update(input);
	if (toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}
}

void LogScreen::CreateViews() {
	using namespace UI;
	I18NCategory *di = GetI18NCategory("Dialog");

	LinearLayout *outer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	root_ = outer;

	scroll_ = outer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	LinearLayout *bottom = outer->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	bottom->Add(new Button(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	cmdLine_ = bottom->Add(new TextEdit("", "Command Line", new LinearLayoutParams(1.0)));
	cmdLine_->OnEnter.Handle(this, &LogScreen::OnSubmit);
	bottom->Add(new Button(di->T("Submit")))->OnClick.Handle(this, &LogScreen::OnSubmit);

	vert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert_->SetSpacing(0);

	UpdateLog();
}

UI::EventReturn LogScreen::OnSubmit(UI::EventParams &e) {
	std::string cmd = cmdLine_->GetText();

	// TODO: Can add all sorts of fun stuff here that we can't be bothered writing proper UI for, like various memdumps etc.

	NOTICE_LOG(HLE, "Submitted: %s", cmd.c_str());

	UpdateLog();
	cmdLine_->SetText("");
	cmdLine_->SetFocus();
	return UI::EVENT_DONE;
}

void LogConfigScreen::CreateViews() {
	using namespace UI;

	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *de = GetI18NCategory("Developer");

	root_ = new ScrollView(ORIENT_VERTICAL);

	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert->SetSpacing(0);

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice(di->T("Toggle All")))->OnClick.Handle(this, &LogConfigScreen::OnToggleAll);
	topbar->Add(new Choice(de->T("Log Level")))->OnClick.Handle(this, &LogConfigScreen::OnLogLevel);

	vert->Add(topbar);

	vert->Add(new ItemHeader(de->T("Logging Channels")));

	LogManager *logMan = LogManager::GetInstance();

	int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(cellSize - 50, WRAP_CONTENT));
		row->SetSpacing(0);
		row->Add(new CheckBox(&chan->enable_, "", "", new LinearLayoutParams(50, WRAP_CONTENT)));
		row->Add(new PopupMultiChoice(&chan->level_, chan->GetFullName(), logLevelList, 1, 6, 0, screenManager(), new LinearLayoutParams(1.0)));
		grid->Add(row);
	}
}

UI::EventReturn LogConfigScreen::OnToggleAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		chan->enable_ = !chan->enable_;
	}

	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnLogLevelChange(UI::EventParams &e) {
	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn LogConfigScreen::OnLogLevel(UI::EventParams &e) {
	I18NCategory *de = GetI18NCategory("Developer");

	auto logLevelScreen = new LogLevelScreen(de->T("Log Level"));
	logLevelScreen->OnChoice.Handle(this, &LogConfigScreen::OnLogLevelChange);
	screenManager()->push(logLevelScreen);
	return UI::EVENT_DONE;
}

LogLevelScreen::LogLevelScreen(const std::string &title) : ListPopupScreen(title) {
	int NUMLOGLEVEL = 6;    
	std::vector<std::string> list;
	for(int i = 0; i < NUMLOGLEVEL; ++i) {
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
		if(chan->enable_ )
			chan->level_ = selected + 1;
	}
}

const char *GetCompilerABI() {
#ifdef HAVE_ARMV7
	return "armeabi-v7a";
#elif defined(ARM)
	return "armeabi";
#elif defined(ARM64)
	return "arm64";
#elif defined(_M_IX86)
	return "x86";
#elif defined(_M_X64)
	return "x86-64";
#else
	return "other";
#endif
}

void SystemInfoScreen::CreateViews() {
	// NOTE: Do not translate this section. It will change a lot and will be impossible to keep up.
	I18NCategory *d = GetI18NCategory("Dialog");

	using namespace UI;
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	root_->Add(new Choice(d->T("Back"), "", false, new AnchorLayoutParams(225, 64, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 225, new AnchorLayoutParams(10, 0, 10, 0, false));

	root_->Add(tabHolder);
	ViewGroup *deviceSpecsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *deviceSpecs = new LinearLayout(ORIENT_VERTICAL);
	deviceSpecs->SetSpacing(0);
	deviceSpecsScroll->Add(deviceSpecs);
	tabHolder->AddTab("Device Info", deviceSpecsScroll);

	deviceSpecs->Add(new ItemHeader("System Information"));
	deviceSpecs->Add(new InfoItem("Name", System_GetProperty(SYSPROP_NAME)));
	deviceSpecs->Add(new InfoItem("Lang/Region", System_GetProperty(SYSPROP_LANGREGION)));
	deviceSpecs->Add(new InfoItem("ABI", GetCompilerABI()));
	deviceSpecs->Add(new ItemHeader("CPU Information"));
	deviceSpecs->Add(new InfoItem("Name", cpu_info.brand_string));
#ifdef ARM
	deviceSpecs->Add(new InfoItem("Cores", StringFromInt(cpu_info.num_cores)));
#else
	int totalThreads = cpu_info.num_cores * cpu_info.logical_cpu_count;
	std::string cores = StringFromFormat("%d (%d per core, %d cores)", totalThreads, cpu_info.logical_cpu_count, cpu_info.num_cores);
	deviceSpecs->Add(new InfoItem("Threads", cores));
#endif
	deviceSpecs->Add(new ItemHeader("GPU Information"));

	Thin3DContext *thin3d = screenManager()->getThin3DContext();

	deviceSpecs->Add(new InfoItem("3D API", thin3d->GetInfoString(T3DInfo::APINAME)));
	deviceSpecs->Add(new InfoItem("Vendor", thin3d->GetInfoString(T3DInfo::VENDOR)));
	deviceSpecs->Add(new InfoItem("Model", thin3d->GetInfoString(T3DInfo::RENDERER)));
#ifdef _WIN32
	deviceSpecs->Add(new InfoItem("Driver Version", System_GetProperty(SYSPROP_GPUDRIVER_VERSION)));
	if (g_Config.iGPUBackend == GPU_BACKEND_DIRECT3D9) {
		deviceSpecs->Add(new InfoItem("D3DX Version", StringFromFormat("%d", GetD3DXVersion())));
	}
#endif

#ifdef ANDROID
	deviceSpecs->Add(new ItemHeader("Audio Information"));
	deviceSpecs->Add(new InfoItem("Sample rate", StringFromFormat("%d Hz", System_GetPropertyInt(SYSPROP_AUDIO_SAMPLE_RATE))));
	deviceSpecs->Add(new InfoItem("Frames per buffer", StringFromFormat("%d", System_GetPropertyInt(SYSPROP_AUDIO_FRAMES_PER_BUFFER))));
	deviceSpecs->Add(new InfoItem("Optimal sample rate", StringFromFormat("%d Hz", System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE))));
	deviceSpecs->Add(new InfoItem("Optimal frames per buffer", StringFromFormat("%d", System_GetPropertyInt(SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER))));

	deviceSpecs->Add(new ItemHeader("Display Information"));
	deviceSpecs->Add(new InfoItem("Native Resolution", StringFromFormat("%dx%d",
		System_GetPropertyInt(SYSPROP_DISPLAY_XRES),
		System_GetPropertyInt(SYSPROP_DISPLAY_YRES))));
	deviceSpecs->Add(new InfoItem("Refresh rate", StringFromFormat("%0.3f Hz", (float)System_GetPropertyInt(SYSPROP_DISPLAY_REFRESH_RATE) / 1000.0f)));
#endif


	deviceSpecs->Add(new ItemHeader("Version Information"));
	std::string apiVersion = thin3d->GetInfoString(T3DInfo::APIVERSION);
	apiVersion.resize(30);
	deviceSpecs->Add(new InfoItem("API Version", apiVersion));
	deviceSpecs->Add(new InfoItem("Shading Language", thin3d->GetInfoString(T3DInfo::SHADELANGVERSION)));

#ifdef ANDROID
	std::string moga = System_GetProperty(SYSPROP_MOGA_VERSION);
	if (moga.empty()) {
		moga = "(none detected)";
	}
	deviceSpecs->Add(new InfoItem("Moga", moga));
#endif

#ifdef ANDROID
	char temp[256];
	sprintf(temp, "%dx%d", System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES));
	deviceSpecs->Add(new InfoItem("Display resolution", temp));
#endif

	if (gl_extensions.precision[0] != 0) {
		const char *stypes[2] = { "Vertex", "Fragment" };
		const char *ptypes[6] = { "LowF", "MediumF", "HighF", "LowI", "MediumI", "HighI" };

		for (int st = 0; st < 2; st++) {
			char bufValue[256], bufTitle[256];
			for (int p = 0; p < 6; p++) {
				snprintf(bufTitle, sizeof(bufTitle), "Precision %s %s:", stypes[st], ptypes[p]);
				snprintf(bufValue, sizeof(bufValue), "(%i, %i): %i", gl_extensions.range[st][p][0], gl_extensions.range[st][p][1], gl_extensions.precision[st][p]);
				deviceSpecs->Add(new InfoItem(bufTitle, bufValue, new LayoutParams(FILL_PARENT, 30)));
			}
		}
	}

	ViewGroup *cpuExtensionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *cpuExtensions = new LinearLayout(ORIENT_VERTICAL);
	cpuExtensions->SetSpacing(0);
	cpuExtensionsScroll->Add(cpuExtensions);

	tabHolder->AddTab("CPU Extensions", cpuExtensionsScroll);

	cpuExtensions->Add(new ItemHeader("CPU Extensions"));
	std::vector<std::string> exts;
	SplitString(cpu_info.Summarize(), ',', exts);
	for (size_t i = 2; i < exts.size(); i++) {
		cpuExtensions->Add(new TextView(exts[i]));
	}
	
	ViewGroup *oglExtensionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *oglExtensions = new LinearLayout(ORIENT_VERTICAL);
	oglExtensions->SetSpacing(0);
	oglExtensionsScroll->Add(oglExtensions);

	tabHolder->AddTab("OGL Extensions", oglExtensionsScroll);

#ifndef USING_GLES2
	oglExtensions->Add(new ItemHeader("OpenGL Extensions"));
#else
	if (gl_extensions.GLES3)
		oglExtensions->Add(new ItemHeader("OpenGL ES 3.0 Extensions"));
	else
		oglExtensions->Add(new ItemHeader("OpenGL ES 2.0 Extensions"));
#endif

	exts.clear();
	SplitString(g_all_gl_extensions, ' ', exts);
	std::sort(exts.begin(), exts.end());
	for (size_t i = 0; i < exts.size(); i++) {
		oglExtensions->Add(new TextView(exts[i]))->SetFocusable(true);
	}

	exts.clear();
	SplitString(g_all_egl_extensions, ' ', exts);
	std::sort(exts.begin(), exts.end());

	// If there aren't any EGL extensions, no need to show the tab.
	if (exts.size() > 0) {
		ViewGroup *eglExtensionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		LinearLayout *eglExtensions = new LinearLayout(ORIENT_VERTICAL);
		eglExtensions->SetSpacing(0);
		eglExtensionsScroll->Add(eglExtensions);

		tabHolder->AddTab("EGL Extensions", eglExtensionsScroll);

		eglExtensions->Add(new ItemHeader("EGL Extensions"));

		for (size_t i = 0; i < exts.size(); i++) {
			eglExtensions->Add(new TextView(exts[i]))->SetFocusable(true);
		}
	}
}

void AddressPromptScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	I18NCategory *de = GetI18NCategory("Developer");

	addrView_ = new TextView(de->T("Enter address"), ALIGN_HCENTER, false);
	parent->Add(addrView_);

	ViewGroup *grid = new GridLayout(GridLayoutSettings(60, 40));
	parent->Add(grid);

	for (int i = 0; i < 16; ++i) {
		char temp[16];
		snprintf(temp, 16, " %X ", i);
		buttons_[i] = new Button(temp);
		grid->Add(buttons_[i])->OnClick.Handle(this, &AddressPromptScreen::OnDigitButton);
	}

	parent->Add(new Button(de->T("Backspace")))->OnClick.Handle(this, &AddressPromptScreen::OnBackspace);
}

void AddressPromptScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK) {
		UI::EventParams e;
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
	I18NCategory *de = GetI18NCategory("Developer");

	if (addr_ != 0) {
		char temp[32];
		snprintf(temp, 32, "%8X", addr_);
		addrView_->SetText(temp);
	} else {
		addrView_->SetText(de->T("Enter address"));
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
			OnCompleted(DR_OK);
			screenManager()->finishDialog(this, DR_OK);
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
	I18NCategory *d = GetI18NCategory("Dialog");
	I18NCategory *de = GetI18NCategory("Developer");

	using namespace UI;
	
	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ScrollView *leftColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	LinearLayout *leftColumn = leftColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));

	ScrollView *midColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(2.0f)));
	LinearLayout *midColumn = midColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	leftDisasm_ = midColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	leftDisasm_->SetSpacing(0.0f);

	ScrollView *rightColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(2.0f)));
	LinearLayout *rightColumn = rightColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_ = rightColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_->SetSpacing(0.0f);

	leftColumn->Add(new Choice(de->T("Current")))->OnClick.Handle(this, &JitCompareScreen::OnCurrentBlock);
	leftColumn->Add(new Choice(de->T("By Address")))->OnClick.Handle(this, &JitCompareScreen::OnSelectBlock);
	leftColumn->Add(new Choice(de->T("Prev")))->OnClick.Handle(this, &JitCompareScreen::OnPrevBlock);
	leftColumn->Add(new Choice(de->T("Next")))->OnClick.Handle(this, &JitCompareScreen::OnNextBlock);
	leftColumn->Add(new Choice(de->T("Random")))->OnClick.Handle(this, &JitCompareScreen::OnRandomBlock);
	leftColumn->Add(new Choice(de->T("FPU")))->OnClick.Handle(this, &JitCompareScreen::OnRandomFPUBlock);
	leftColumn->Add(new Choice(de->T("VFPU")))->OnClick.Handle(this, &JitCompareScreen::OnRandomVFPUBlock);
	leftColumn->Add(new Choice(de->T("Stats")))->OnClick.Handle(this, &JitCompareScreen::OnShowStats);
	leftColumn->Add(new Choice(d->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	blockName_ = leftColumn->Add(new TextView(de->T("No block")));
	blockAddr_ = leftColumn->Add(new TextEdit("", "", new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	blockAddr_->OnTextChange.Handle(this, &JitCompareScreen::OnAddressChange);
	blockStats_ = leftColumn->Add(new TextView(""));

	EventParams ignore = {0};
	OnCurrentBlock(ignore);
}

void JitCompareScreen::UpdateDisasm() {
	leftDisasm_->Clear();
	rightDisasm_->Clear();

	using namespace UI;

	I18NCategory *de = GetI18NCategory("Developer");

	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();

	char temp[256];
	snprintf(temp, sizeof(temp), "%i/%i", currentBlock_, blockCache->GetNumBlocks());
	blockName_->SetText(temp);

	if (currentBlock_ < 0 || currentBlock_ >= blockCache->GetNumBlocks()) {
		leftDisasm_->Add(new TextView(de->T("No block")));
		rightDisasm_->Add(new TextView(de->T("No block")));
		blockStats_->SetText("");
		return;
	}

	JitBlock *block = blockCache->GetBlock(currentBlock_);

	snprintf(temp, sizeof(temp), "%08x", block->originalAddress);
	blockAddr_->SetText(temp);

	// Alright. First generate the MIPS disassembly.
	
	// TODO: Need a way to communicate branch continuing.
	for (u32 addr = block->originalAddress; addr <= block->originalAddress + block->originalSize * 4; addr += 4) {
		char temp[256];
		MIPSDisAsm(Memory::Read_Instruction(addr), addr, temp, true);
		std::string mipsDis = temp;
		leftDisasm_->Add(new TextView(mipsDis))->SetFocusable(true);
	}

#if defined(ARM)
	std::vector<std::string> targetDis = DisassembleArm2(block->normalEntry, block->codeSize);
#elif defined(ARM64)
	std::vector<std::string> targetDis = DisassembleArm64(block->normalEntry, block->codeSize);
#elif defined(_M_IX86) || defined(_M_X64)
	std::vector<std::string> targetDis = DisassembleX86(block->normalEntry, block->codeSize);
#endif
#if defined(ARM) || defined(ARM64) || defined(_M_IX86) || defined(_M_X64)
	for (size_t i = 0; i < targetDis.size(); i++) {
		rightDisasm_->Add(new TextView(targetDis[i]))->SetFocusable(true);
	}
#endif

	int numMips = leftDisasm_->GetNumSubviews();
	int numHost = rightDisasm_->GetNumSubviews();

	snprintf(temp, sizeof(temp), "%d to %d : %d%%", numMips, numHost, 100 * numHost / numMips);
	blockStats_->SetText(temp);
}

UI::EventReturn JitCompareScreen::OnAddressChange(UI::EventParams &e) {
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
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
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
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
	I18NCategory *de = GetI18NCategory("Developer");

	auto addressPrompt = new AddressPromptScreen(de->T("Block address"));
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
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}

	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
	if (Memory::IsValidAddress(e.a)) {
		currentBlock_ = blockCache->GetBlockNumberFromStartAddress(e.a);
	} else {
		currentBlock_ = -1;
	}
	UpdateDisasm();
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnRandomBlock(UI::EventParams &e) {
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}

	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
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
	if (!MIPSComp::jit) {
		return;
	}
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
	int numBlocks = blockCache->GetNumBlocks();
	if (numBlocks > 0) {
		bool anyWanted = false;
		int tries = 0;
		while (!anyWanted && tries < 10000) {
			currentBlock_ = rand() % numBlocks;
			const JitBlock *b = blockCache->GetBlock(currentBlock_);
			for (u32 addr = b->originalAddress; addr <= b->originalAddress + b->originalSize; addr += 4) {
				MIPSOpcode opcode = Memory::Read_Instruction(addr);
				if (MIPSGetInfo(opcode) & flag) {
					char temp[256];
					MIPSDisAsm(opcode, addr, temp);
					// INFO_LOG(HLE, "Stopping VFPU instruction: %s", temp);
					anyWanted = true;
					break;
				}
			}
			tries++;
		}
	}
	UpdateDisasm();
}


UI::EventReturn JitCompareScreen::OnCurrentBlock(UI::EventParams &e) {
	if (!MIPSComp::jit) {
		return UI::EVENT_DONE;
	}
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
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

static const uint32_t nice_colors[] = {
	0xFF8040,
	0x80FF40,
	0x8040FF,
	0xFFFF40,

	0x40FFFF,
	0xFF70FF,
	0xc0c0c0,
	0xb040c0,

	0x184099,
	0xCC3333,
	0xFF99CC,
	0x3399CC,

	0x990000,
	0x003366,
	0xF8F8F8,
	0x33FFFF,
};

void DrawProfile(UIContext &ui) {
#ifdef USE_PROFILER
	int numCategories = Profiler_GetNumCategories();
	int historyLength = Profiler_GetHistoryLength();

	float legendWidth = 80.0f;
	for (int i = 0; i < numCategories; i++) {
		const char *name = Profiler_GetCategoryName(i);
		float w = 0.0f, h = 0.0f;
		ui.MeasureText(ui.GetFontStyle(), name, &w, &h);
		if (w > legendWidth) {
			legendWidth = w;
		}
	}
	legendWidth += 20.0f;

	float rowH = 30.0f;
	float legendHeight = rowH * numCategories;
	float legendStartY = legendHeight > ui.GetBounds().centerY() ? ui.GetBounds().y2() - legendHeight : ui.GetBounds().centerY();
	float legendStartX = ui.GetBounds().x2() - std::min(legendWidth, 200.0f);

	const uint32_t opacity = 140 << 24;

	for (int i = 0; i < numCategories; i++) {
		const char *name = Profiler_GetCategoryName(i);
		uint32_t color = nice_colors[i % ARRAY_SIZE(nice_colors)];

		float y = legendStartY + i * rowH;
		ui.FillRect(UI::Drawable(opacity | color), Bounds(legendStartX, y, rowH - 2, rowH - 2));
		ui.DrawTextShadow(name, legendStartX + rowH + 2, y, 0xFFFFFFFF, ALIGN_VBASELINE);
	}

	float graphWidth = ui.GetBounds().x2() - legendWidth - 20.0f;
	float graphHeight = ui.GetBounds().h * 0.8f;

	std::vector<float> history;
	std::vector<float> total;
	history.resize(historyLength);
	total.resize(historyLength);

	float dx = graphWidth / historyLength;

	/*
	ui.Flush();

	ui.BeginNoTex();
	*/

	bool area = true;
	static float lastMaxVal = 1.0f / 60.0f;
	float minVal = 0.0f;
	float maxVal = lastMaxVal;  // TODO - adjust to frame length
	if (maxVal < 0.001f)
		maxVal = 0.001f;
	if (maxVal > 1.0f / 15.0f)
		maxVal = 1.0f / 15.0f;

	float scale = (graphHeight) / (maxVal - minVal);

	float y_60th = ui.GetBounds().y2() - 10 - (1.0f / 60.0f) * scale;
	float y_1ms = ui.GetBounds().y2() - 10 - (1.0f / 1000.0f) * scale;

	ui.FillRect(UI::Drawable(0x80FFFF00), Bounds(0, y_60th, graphWidth, 2));
	ui.FillRect(UI::Drawable(0x80FFFF00), Bounds(0, y_1ms, graphWidth, 2));
	ui.DrawTextShadow("1/60s", 5, y_60th, 0x80FFFF00);
	ui.DrawTextShadow("1ms", 5, y_1ms, 0x80FFFF00);

	maxVal = 0.0f;
	float maxTotal = 0.0f;
	for (int i = 0; i < numCategories; i++) {
		Profiler_GetHistory(i, &history[0], historyLength);

		float x = 10;
		uint32_t col = nice_colors[i % ARRAY_SIZE(nice_colors)];
		if (area)
			col = opacity | (col & 0xFFFFFF);
		UI::Drawable color(col);
		UI::Drawable outline((opacity >> 1) | 0xFFFFFF);

		if (area) {
			for (int n = 0; n < historyLength; n++) {
				float val = history[n];
				float valY1 = ui.GetBounds().y2() - 10 - (val + total[n]) * scale;
				float valY2 = ui.GetBounds().y2() - 10 - total[n] * scale;
				ui.FillRect(outline, Bounds(x, valY2, dx, 1.0f));
				ui.FillRect(color, Bounds(x, valY1, dx, valY2 - valY1));
				x += dx;
				total[n] += val;
			}
		} else {
			for (int n = 0; n < historyLength; n++) {
				float val = history[n];
				if (val > maxVal)
					maxVal = val;
				float valY = ui.GetBounds().y2() - 10 - history[n] * scale;
				ui.FillRect(color, Bounds(x, valY, dx, 5));
				x += dx;
			}
		}
	}

	for (int n = 0; n < historyLength; n++) {
		if (total[n] > maxTotal)
			maxTotal = total[n];
	}

	if (area) {
		maxVal = maxTotal;
	}

	lastMaxVal = lastMaxVal * 0.95f + maxVal * 0.05f;
#endif
}
