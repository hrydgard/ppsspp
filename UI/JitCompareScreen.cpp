#include "UI/JitCompareScreen.h"

#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"

// Three panes: Block chooser, MIPS view, ARM/x86 view
void JitCompareScreen::CreateViews() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	using namespace UI;

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ScrollView *leftColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT)));
	LinearLayout *leftColumn = leftColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));

	LinearLayout *mainView = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	LinearLayout *topBar = mainView->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	topBar->Add(new Button(dev->T("Prev")))->OnClick.Handle(this, &JitCompareScreen::OnPrevBlock);
	topBar->Add(new Button(dev->T("Next")))->OnClick.Handle(this, &JitCompareScreen::OnNextBlock);
	blockAddr_ = topBar->Add(new TextEdit("", dev->T("Block address"), ""));
	blockAddr_->OnEnter.Handle(this, &JitCompareScreen::OnAddressChange);
	blockName_ = topBar->Add(new TextView(dev->T("No block")));
	blockStats_ = topBar->Add(new TextView(""));

	LinearLayout *comparisonView = mainView->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f)));

	ScrollView *midColumnScroll = comparisonView->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	LinearLayout *midColumn = midColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	midColumn->SetTag("JitCompareLeftDisasm");
	leftDisasm_ = midColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	leftDisasm_->SetSpacing(0.0f);

	ScrollView *rightColumnScroll = comparisonView->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	rightColumnScroll->SetTag("JitCompareRightDisasm");
	LinearLayout *rightColumn = rightColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_ = rightColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_->SetSpacing(0.0f);

	leftColumn->Add(new Choice(dev->T("Current")))->OnClick.Handle(this, &JitCompareScreen::OnCurrentBlock);
	leftColumn->Add(new Choice(dev->T("By Address")))->OnClick.Handle(this, &JitCompareScreen::OnSelectBlock);
	leftColumn->Add(new Choice(dev->T("Random")))->OnClick.Handle(this, &JitCompareScreen::OnRandomBlock);
	leftColumn->Add(new Choice(dev->T("FPU")))->OnClick.Handle(this, &JitCompareScreen::OnRandomFPUBlock);
	leftColumn->Add(new Choice(dev->T("VFPU")))->OnClick.Handle(this, &JitCompareScreen::OnRandomVFPUBlock);
	leftColumn->Add(new Choice(dev->T("Stats")))->OnClick.Handle(this, &JitCompareScreen::OnShowStats);
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	EventParams ignore{};
	OnCurrentBlock(ignore);
}

void JitCompareScreen::UpdateDisasm() {
	leftDisasm_->Clear();
	rightDisasm_->Clear();

	using namespace UI;

	if (!MIPSComp::jit) {
		return;
	}

	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (!blockCacheDebug->IsValidBlock(currentBlock_)) {
		return;
	}

	char temp[256];
	snprintf(temp, sizeof(temp), "%d/%d", currentBlock_, blockCacheDebug->GetNumBlocks());
	blockName_->SetText(temp);

	if (currentBlock_ < 0 || !blockCacheDebug || currentBlock_ >= blockCacheDebug->GetNumBlocks()) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
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
	for (const auto &line : debugInfo.origDisasm) {
		leftDisasm_->Add(new TextView(line))->SetFocusable(true);
	}

	// TODO : When we have both target and IR, need a third column.
	if (debugInfo.targetDisasm.size()) {
		for (const auto &line : debugInfo.targetDisasm) {
			rightDisasm_->Add(new TextView(line))->SetFocusable(true);
		}
	} else {
		for (const auto &line : debugInfo.irDisasm) {
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
	if (!blockCache)
		return UI::EVENT_DONE;

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
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

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
		int tries = 100;
		while (tries-- > 0) {
			currentBlock_ = rand() % numBlocks;
			if (blockCache->IsValidBlock(currentBlock_)) {
				break;
			}
		}
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
			if (blockCache->IsValidBlock(currentBlock_)) {
				JitBlockDebugInfo b = blockCache->GetBlockDebugInfo(currentBlock_);
				u32 mipsBytes = (u32)b.origDisasm.size() * 4;
				for (u32 addr = b.originalAddress; addr < b.originalAddress + mipsBytes; addr += 4) {
					MIPSOpcode opcode = Memory::Read_Instruction(addr);
					if (MIPSGetInfo(opcode) & flag) {
						char temp[256];
						MIPSDisAsm(opcode, addr, temp, sizeof(temp));
						// INFO_LOG(HLE, "Stopping at random instruction: %08x %s", addr, temp);
						anyWanted = true;
						break;
					}
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
	currentBlock_ = blockCache->GetBlockNumberFromAddress(currentMIPS->pc);
	UpdateDisasm();
	return UI::EVENT_DONE;
}


void AddressPromptScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

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
	if (addr_ != 0) {
		char temp[32];
		snprintf(temp, 32, "%8X", addr_);
		addrView_->SetText(temp);
	} else {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);
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
