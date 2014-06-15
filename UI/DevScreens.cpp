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
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/CoreParameter.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
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
	parent->Add(new Choice("Toggle Freeze"))->OnClick.Handle(this, &DevMenu::OnFreezeFrame);
	parent->Add(new Choice("Dump Frame GPU Commands"))->OnClick.Handle(this, &DevMenu::OnDumpFrame);
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
	std::string openGL = (char *)glGetString(GL_VERSION);
	openGL.resize(30);
	deviceSpecs->Add(new InfoItem("OpenGL", openGL));
	deviceSpecs->Add(new InfoItem("GLSL", (char *)glGetString(GL_SHADING_LANGUAGE_VERSION)));

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
		oglExtensions->Add(new TextView(exts[i]));
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
			eglExtensions->Add(new TextView(exts[i]));
		}
	}
}

void AddressPromptScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	addrView_ = new TextView("Enter address", ALIGN_HCENTER, false);
	parent->Add(addrView_);

	ViewGroup *grid = new GridLayout(GridLayoutSettings(60, 40));
	parent->Add(grid);

	for (int i = 0; i < 16; ++i) {
		char temp[16];
		snprintf(temp, 16, " %X ", i);
		buttons_[i] = new Button(temp);
		grid->Add(buttons_[i])->OnClick.Handle(this, &AddressPromptScreen::OnDigitButton);
	}

	parent->Add(new Button("Backspace"))->OnClick.Handle(this, &AddressPromptScreen::OnBackspace);
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
	if (addr_ != 0) {
		char temp[32];
		snprintf(temp, 32, "%8X", addr_);
		addrView_->SetText(temp);
	} else {
		addrView_->SetText("Enter address");
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
	leftColumn->Add(new Choice("By Address"))->OnClick.Handle(this, &JitCompareScreen::OnSelectBlock);
	leftColumn->Add(new Choice("Random"))->OnClick.Handle(this, &JitCompareScreen::OnRandomBlock);
	leftColumn->Add(new Choice("Random VFPU"))->OnClick.Handle(this, &JitCompareScreen::OnRandomVFPUBlock);
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
	
	// TODO: Need a way to communicate branch continuing.
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

UI::EventReturn JitCompareScreen::OnSelectBlock(UI::EventParams &e) {
	auto addressPrompt = new AddressPromptScreen("Block address");
	addressPrompt->OnChoice.Handle(this, &JitCompareScreen::OnBlockAddress);
	screenManager()->push(addressPrompt);
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnBlockAddress(UI::EventParams &e) {
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
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
	int numBlocks = blockCache->GetNumBlocks();
	if (numBlocks > 0) {
		currentBlock_ = rand() % numBlocks;
	}
	UpdateDisasm();
	return UI::EVENT_DONE;
}

UI::EventReturn JitCompareScreen::OnRandomVFPUBlock(UI::EventParams &e) {
	JitBlockCache *blockCache = MIPSComp::jit->GetBlockCache();
	int numBlocks = blockCache->GetNumBlocks();
	if (numBlocks > 0) {
		bool anyVFPU = false;
		int tries = 0;
		while (!anyVFPU && tries < 10000) {
			currentBlock_ = rand() % numBlocks;
			const JitBlock *b = blockCache->GetBlock(currentBlock_);
			for (u32 addr = b->originalAddress; addr <= b->originalAddress + b->originalSize; addr += 4) {
				MIPSOpcode opcode = Memory::Read_Instruction(addr);
				if (MIPSGetInfo(opcode) & IS_VFPU) {
					char temp[256];
					MIPSDisAsm(opcode, addr, temp);
					INFO_LOG(HLE, "Stopping VFPU instruction: %s", temp)
					anyVFPU = true;
					break;
				}
			}
			tries++;
		}
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
