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

#include "gfx_es2/gl_state.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "UI/MiscScreens.h"
#include "UI/DevScreens.h"
#include "UI/GameSettingsScreen.h"
#include "Common/LogManager.h"
#include "Core/Config.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "ext/disarm.h"
#include "Common/CPUDetect.h"

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

	parent->Add(new Choice("Log Channels"))->OnClick.Handle(this, &DevMenu::OnLogConfig);
	parent->Add(new Choice("Developer Tools"))->OnClick.Handle(this, &DevMenu::OnDeveloperTools);
	parent->Add(new Choice("Jit Compare"))->OnClick.Handle(this, &DevMenu::OnJitCompare);
}

UI::EventReturn DevMenu::OnLogConfig(UI::EventParams &e) {
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnDeveloperTools(UI::EventParams &e) {
	screenManager()->push(new DeveloperToolsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnJitCompare(UI::EventParams &e) {
	screenManager()->push(new JitCompareScreen());
	return UI::EVENT_DONE;
}

void DevMenu::dialogFinished(const Screen *dialog, DialogResult result) {
	// Close when a subscreen got closed.
	// TODO: a bug in screenmanager causes this not to work here.
	// screenManager()->finishDialog(this, DR_OK);
}


// It's not so critical to translate everything here, most of this is developers only.

void LogConfigScreen::CreateViews() {
	using namespace UI;

	I18NCategory *d = GetI18NCategory("Dialog");

	root_ = new ScrollView(ORIENT_VERTICAL);

	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert->SetSpacing(0);

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice("Back"))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice("Toggle All"))->OnClick.Handle(this, &LogConfigScreen::OnToggleAll);
	topbar->Add(new Choice("Log Level"))->OnClick.Handle(this, &LogConfigScreen::OnLogLevel);

	vert->Add(topbar);

	vert->Add(new ItemHeader("Log Channels"));

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
	auto logLevelScreen = new LogLevelScreen("Log Level");
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

void SystemInfoScreen::CreateViews() {
	// NOTE: Do not translate this section. It will change a lot and will be impossible to keep up.
	I18NCategory *d = GetI18NCategory("Dialog");

	using namespace UI;
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	root_->Add(new Choice(d->T("Back"), "", false, new AnchorLayoutParams(225, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

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
	deviceSpecs->Add(new InfoItem("Vendor", (char *)glGetString(GL_VENDOR)));
	deviceSpecs->Add(new InfoItem("Model", (char *)glGetString(GL_RENDERER)));
	deviceSpecs->Add(new ItemHeader("OpenGL Version Information"));
	deviceSpecs->Add(new InfoItem("OpenGL", (char *)glGetString(GL_VERSION)));
	deviceSpecs->Add(new InfoItem("GLSL", (char *)glGetString(GL_SHADING_LANGUAGE_VERSION)));
	
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

	std::vector<std::string> exts;
	SplitString(g_all_gl_extensions, ' ', exts);
	std::sort(exts.begin(), exts.end());
	for (size_t i = 0; i < exts.size(); i++) {
		oglExtensions->Add(new TextView(exts[i]));
	}

	ViewGroup *eglExtensionsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *eglExtensions = new LinearLayout(ORIENT_VERTICAL);
	eglExtensions->SetSpacing(0);
	eglExtensionsScroll->Add(eglExtensions);
	tabHolder->AddTab("EGL Extensions", eglExtensionsScroll);

	eglExtensions->Add(new ItemHeader("EGL Extensions"));
	exts.clear();
	SplitString(g_all_egl_extensions, ' ', exts);
	std::sort(exts.begin(), exts.end());
	for (size_t i = 0; i < exts.size(); i++) {
		eglExtensions->Add(new TextView(exts[i]));
	}
}



// Three panes: Block chooser, MIPS view, ARM/x86 view
void JitCompareScreen::CreateViews() {
	I18NCategory *d = GetI18NCategory("Dialog");

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

	leftColumn->Add(new Choice("Current"))->OnClick.Handle(this, &JitCompareScreen::OnCurrentBlock);
	leftColumn->Add(new Choice("Random"))->OnClick.Handle(this, &JitCompareScreen::OnRandomBlock);
	leftColumn->Add(new Choice(d->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	blockName_ = leftColumn->Add(new TextView("no block"));
}

#ifdef ARM
std::vector<std::string> DisassembleArm2(const u8 *data, int size) {
	std::vector<std::string> lines;

	char temp[256];
	for (int i = 0; i < size; i += 4) {
		const u32 *codePtr = (const u32 *)(data + i);
		u32 inst = codePtr[0];
		u32 next = (i < size - 4) ? codePtr[1] : 0;
		// MAGIC SPECIAL CASE for MOVW/MOVT readability!
		if ((inst & 0x0FF00000) == 0x03000000 && (next & 0x0FF00000) == 0x03400000) {
			u32 low = ((inst & 0x000F0000) >> 4) | (inst & 0x0FFF);
			u32 hi = ((next & 0x000F0000) >> 4) | (next	 & 0x0FFF);
			int reg0 = (inst & 0x0000F000) >> 12;
			int reg1 = (next & 0x0000F000) >> 12;
			if (reg0 == reg1) {
				sprintf(temp, "MOV32 %s, %04x%04x", ArmRegName(reg0), hi, low);
				// sprintf(temp, "%08x MOV32? %s, %04x%04x", (u32)inst, ArmRegName(reg0), hi, low);
				lines.push_back(temp);
				i += 4;
				continue;
			}
		}
		ArmDis((u32)(intptr_t)codePtr, inst, temp, false);
		std::string buf = temp;
		lines.push_back(buf);
	}
	return lines;
}
#endif

void JitCompareScreen::UpdateDisasm() {
	leftDisasm_->Clear();
	rightDisasm_->Clear();

	using namespace UI;

	if (currentBlock_ == -1) {
		leftDisasm_->Add(new TextView("No block"));
		rightDisasm_->Add(new TextView("No block"));
		return;
	}

	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
	JitBlock *block = blockCache->GetBlock(currentBlock_);

	char temp[256];
	sprintf(temp, "%i/%i\n%08x", currentBlock_, blockCache->GetNumBlocks(), block->originalAddress);
	blockName_->SetText(temp);

	// Alright. First generate the MIPS disassembly.
	
	for (u32 addr = block->originalAddress; addr <= block->originalAddress + block->originalSize * 4; addr += 4) {
		char temp[256];
		MIPSDisAsm(Memory::Read_Instruction(addr), addr, temp, true);
		std::string mipsDis = temp;
		leftDisasm_->Add(new TextView(mipsDis));
	}

#if defined(ARM)
	std::vector<std::string> targetDis = DisassembleArm2(block->normalEntry, block->codeSize);
	for (size_t i = 0; i < targetDis.size(); i++) {
		rightDisasm_->Add(new TextView(targetDis[i]));
	}
#else
	rightDisasm_->Add(new TextView("No x86 disassembler available"));
#endif
}

UI::EventReturn JitCompareScreen::OnRandomBlock(UI::EventParams &e) {
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
	int numBlocks = blockCache->GetNumBlocks();
	if (numBlocks > 0) {
		currentBlock_ = rand() % numBlocks;
	}
	UpdateDisasm();
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnCurrentBlock(UI::EventParams &e) {
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
