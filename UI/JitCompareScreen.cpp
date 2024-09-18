#include <algorithm>

#include "UI/JitCompareScreen.h"

#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"

JitCompareScreen::JitCompareScreen() : UIDialogScreenWithBackground() {
	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();
	// The only defaults that make sense.
	if (blockCacheDebug->SupportsProfiling()) {
		listSort_ = ListSort::TIME_SPENT;
	} else {
		listSort_ = ListSort::BLOCK_LENGTH_DESC;
	}
	FillBlockList();
}

void JitCompareScreen::Flip() {
	using namespace UI;
	// If we add more, let's convert to a for loop.
	switch (viewMode_) {
	case ViewMode::DISASM:
		comparisonView_->SetVisibility(V_VISIBLE);
		blockListView_->SetVisibility(V_GONE);
		statsView_->SetVisibility(V_GONE);
		break;
	case ViewMode::BLOCK_LIST:
		comparisonView_->SetVisibility(V_GONE);
		blockListView_->SetVisibility(V_VISIBLE);
		statsView_->SetVisibility(V_GONE);
		break;
	case ViewMode::STATS:
		comparisonView_->SetVisibility(V_GONE);
		blockListView_->SetVisibility(V_GONE);
		statsView_->SetVisibility(V_VISIBLE);
		break;
	}
}

// Three panes: Block chooser, MIPS view, ARM/x86 view
void JitCompareScreen::CreateViews() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	using namespace UI;

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ScrollView *leftColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT)));
	LinearLayout *leftColumn = leftColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));

	comparisonView_ = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	comparisonView_->SetVisibility(V_VISIBLE);
	LinearLayout *blockTopBar = comparisonView_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	blockTopBar->Add(new Button("", ImageID("I_ARROW_UP")))->OnClick.Add([this](UI::EventParams &e) {
		viewMode_ = ViewMode::BLOCK_LIST;
		Flip();
		return UI::EVENT_DONE;
	});
	blockTopBar->Add(new Button("", ImageID("I_ARROW_LEFT")))->OnClick.Add([=](UI::EventParams &e) {
		if (currentBlock_ >= 1)
			currentBlock_--;
		UpdateDisasm();
		return UI::EVENT_DONE;
	});
	blockTopBar->Add(new Button("", ImageID("I_ARROW_RIGHT")))->OnClick.Add([=](UI::EventParams &e) {
		if (currentBlock_ < blockList_.size() - 1)
			currentBlock_++;
		UpdateDisasm();
		return UI::EVENT_DONE;
	});
	blockTopBar->Add(new Button(dev->T("Random")))->OnClick.Add([=](UI::EventParams &e) {
		if (blockList_.empty()) {
			return UI::EVENT_DONE;
		}
		currentBlock_ = rand() % blockList_.size();
		UpdateDisasm();
		return UI::EVENT_DONE;
	});

	blockAddr_ = blockTopBar->Add(new TextEdit("", dev->T("Block address"), ""));
	blockAddr_->OnEnter.Handle(this, &JitCompareScreen::OnAddressChange);
	blockName_ = blockTopBar->Add(new TextView(dev->T("No block")));
	blockStats_ = blockTopBar->Add(new TextView(""));

	LinearLayout *columns = comparisonView_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f)));

	ScrollView *midColumnScroll = columns->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	LinearLayout *midColumn = midColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	midColumn->SetTag("JitCompareLeftDisasm");
	leftDisasm_ = midColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	leftDisasm_->SetSpacing(0.0f);

	ScrollView *rightColumnScroll = columns->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	rightColumnScroll->SetTag("JitCompareRightDisasm");
	LinearLayout *rightColumn = rightColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_ = rightColumn->Add(new LinearLayout(ORIENT_VERTICAL));
	rightDisasm_->SetSpacing(0.0f);

	blockListView_ = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	blockListView_->SetVisibility(V_GONE);

	// Should match the ListSort enum
	static ContextMenuItem sortMenu[] = {
		{ "Block number", "I_ARROW_UP" },
		{ "Block length", "I_ARROW_DOWN" },
		{ "Block length", "I_ARROW_UP" },
		{ "Time spent", "I_ARROW_DOWN" },
		{ "Executions", "I_ARROW_DOWN" },
	};
	int sortCount = ARRAY_SIZE(sortMenu);
	if (MIPSComp::jit) {
		JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();
		if (!blockCacheDebug->SupportsProfiling()) {
			sortCount -= 2;
		}
	}

	LinearLayout *listTopBar = blockListView_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	Button *sortButton = new Button(dev->T("Sort..."));
	listTopBar->Add(sortButton)->OnClick.Add([this, sortButton, sortCount](UI::EventParams &e) {
		PopupContextMenuScreen *contextMenu = new UI::PopupContextMenuScreen(sortMenu, sortCount, I18NCat::DEVELOPER, sortButton);
		screenManager()->push(contextMenu);
		contextMenu->OnChoice.Add([=](EventParams &e) -> UI::EventReturn {
			if (e.a < (int)ListSort::MAX) {
				listSort_ = (ListSort)e.a;
				UpdateDisasm();
			}
			return UI::EVENT_DONE;
		});
		return UI::EVENT_DONE;
	});

	ScrollView *blockScroll = blockListView_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	blockListContainer_ = blockScroll->Add(new LinearLayout(ORIENT_VERTICAL));

	statsView_ = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	statsView_->SetVisibility(V_GONE);

	LinearLayout *statsTopBar = statsView_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	ScrollView *statsScroll = statsView_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	statsContainer_ = statsScroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	// leftColumn->Add(new Choice(dev->T("By Address")))->OnClick.Handle(this, &JitCompareScreen::OnSelectBlock);
	leftColumn->Add(new Choice(dev->T("All")))->OnClick.Add([=](UI::EventParams &e) {
		listType_ = ListType::ALL_BLOCKS;
		viewMode_ = ViewMode::BLOCK_LIST;
		UpdateDisasm();
		return UI::EVENT_DONE;
	});
	leftColumn->Add(new Choice(dev->T("FPU")))->OnClick.Add([=](UI::EventParams &e) {
		listType_ = ListType::FPU_BLOCKS;
		viewMode_ = ViewMode::BLOCK_LIST;
		UpdateDisasm();
		return UI::EVENT_DONE;
	});
	leftColumn->Add(new Choice(dev->T("VFPU")))->OnClick.Add([=](UI::EventParams &e) {
		listType_ = ListType::VFPU_BLOCKS;
		viewMode_ = ViewMode::BLOCK_LIST;
		UpdateDisasm();
		return UI::EVENT_DONE;
	});

	leftColumn->Add(new Choice(dev->T("Stats")))->OnClick.Handle(this, &JitCompareScreen::OnShowStats);
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	UpdateDisasm();
}

void JitCompareScreen::FillBlockList() {
	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();
	blockList_.clear();
	int64_t sumTotalNanos = 0;
	int64_t sumExecutions = 0;
	bool profiling = blockCacheDebug->SupportsProfiling();
	for (int i = 0; i < blockCacheDebug->GetNumBlocks(); i++) {
		if (!blockCacheDebug->IsValidBlock(i)) {
			continue;
		}

		switch (listType_) {
		case ListType::ALL_BLOCKS:
			blockList_.push_back(i);
			break;
		case ListType::FPU_BLOCKS:
		case ListType::VFPU_BLOCKS:
		{
			const uint64_t flags = listType_ == ListType::FPU_BLOCKS ? IS_FPU : IS_VFPU;
			// const uint64_t antiFlags = IS_SYSCALL;
			const uint64_t antiFlags = 0;
			JitBlockMeta meta = blockCacheDebug->GetBlockMeta(i);
			if (meta.valid) {
				for (u32 addr = meta.addr; addr < meta.addr + meta.sizeInBytes; addr += 4) {
					MIPSOpcode opcode = Memory::Read_Instruction(addr);
					MIPSInfo info = MIPSGetInfo(opcode);
					if ((info & flags) && !(info & antiFlags)) {
						blockList_.push_back(i);
						break;
					}
				}
			}
		}
		default:
			break;
		}

		if (profiling) {
			JitBlockProfileStats stats = blockCacheDebug->GetBlockProfileStats(i);
			sumTotalNanos += stats.totalNanos;
			sumExecutions += stats.executions;
		}
	}

	sumTotalNanos_ = sumTotalNanos;
	sumExecutions_ = sumExecutions;

	if (listSort_ == ListSort::BLOCK_NUM) {
		// Already sorted, effectively.
		return;
	}

	std::sort(blockList_.begin(), blockList_.end(), [=](const int &a_index, const int &b_index) {
		// First, check metadata sorts.
		switch (listSort_) {
		case ListSort::BLOCK_LENGTH_DESC:
		{
			JitBlockMeta a_meta = blockCacheDebug->GetBlockMeta(a_index);
			JitBlockMeta b_meta = blockCacheDebug->GetBlockMeta(b_index);
			return a_meta.sizeInBytes > b_meta.sizeInBytes;  // reverse for descending
		}
		case ListSort::BLOCK_LENGTH_ASC:
		{
			JitBlockMeta a_meta = blockCacheDebug->GetBlockMeta(a_index);
			JitBlockMeta b_meta = blockCacheDebug->GetBlockMeta(b_index);
			return a_meta.sizeInBytes < b_meta.sizeInBytes;
		}
		default:
			break;
		}
		JitBlockProfileStats a_stats = blockCacheDebug->GetBlockProfileStats(a_index);
		JitBlockProfileStats b_stats = blockCacheDebug->GetBlockProfileStats(b_index);
		switch (listSort_) {
		case ListSort::EXECUTIONS:
			return a_stats.executions > b_stats.executions;
		case ListSort::TIME_SPENT:
			return a_stats.totalNanos > b_stats.totalNanos;
		default:
			return false;
		}
	});
}

void JitCompareScreen::UpdateDisasm() {
	leftDisasm_->Clear();
	rightDisasm_->Clear();

	using namespace UI;

	if (!MIPSComp::jit) {
		return;
	}

	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (viewMode_ == ViewMode::DISASM && (currentBlock_ < 0 || currentBlock_ >= (int)blockList_.size())) {
		viewMode_ = ViewMode::BLOCK_LIST;
	}

	FillBlockList();
	Flip();

	if (viewMode_ == ViewMode::DISASM) {
		char temp[256];
		snprintf(temp, sizeof(temp), "%d/%d", currentBlock_, (int)blockList_.size());
		blockName_->SetText(temp);

		int blockNum = blockList_[currentBlock_];

		if (!blockCacheDebug->IsValidBlock(blockNum)) {
			auto dev = GetI18NCategory(I18NCat::DEVELOPER);
			leftDisasm_->Add(new TextView(dev->T("No block")));
			rightDisasm_->Add(new TextView(dev->T("No block")));
			blockStats_->SetText("");
			return;
		}

		JitBlockDebugInfo debugInfo = blockCacheDebug->GetBlockDebugInfo(blockNum);
		snprintf(temp, sizeof(temp), "%08x", debugInfo.originalAddress);
		blockAddr_->SetText(temp);

		// Alright. First generate the MIPS disassembly.

		// TODO: Need a way to communicate branch continuing.
		for (const auto &line : debugInfo.origDisasm) {
			leftDisasm_->Add(new TextView(line, FLAG_DYNAMIC_ASCII, false))->SetFocusable(true);
		}

		// TODO : When we have both target and IR, need a third column.
		if (debugInfo.targetDisasm.size()) {
			for (const auto &line : debugInfo.targetDisasm) {
				rightDisasm_->Add(new TextView(line, FLAG_DYNAMIC_ASCII, false))->SetFocusable(true);
			}
		} else {
			for (const auto &line : debugInfo.irDisasm) {
				rightDisasm_->Add(new TextView(line, FLAG_DYNAMIC_ASCII, false))->SetFocusable(true);
			}
		}

		int numMips = leftDisasm_->GetNumSubviews();
		int numHost = rightDisasm_->GetNumSubviews();
		double bloat = 100.0 * numHost / numMips;
		if (blockCacheDebug->SupportsProfiling()) {
			JitBlockProfileStats stats = blockCacheDebug->GetBlockProfileStats(blockNum);
			int execs = (int)stats.executions;
			double us = (double)stats.totalNanos / 1000000.0;
			double percentage = 100.0 * (double)stats.totalNanos / (double)sumTotalNanos_;
			snprintf(temp, sizeof(temp), "%d runs, %0.2f ms, %0.2f%%, bloat: %0.1f%%", execs, us, percentage, bloat);
		} else {
			snprintf(temp, sizeof(temp), "bloat: %0.1f%%", bloat);
		}
		blockStats_->SetText(temp);
	} else if (viewMode_ == ViewMode::BLOCK_LIST) {
		blockListContainer_->Clear();
		bool profiling = blockCacheDebug->SupportsProfiling();
		for (int i = 0; i < std::min(100, (int)blockList_.size()); i++) {
			int blockNum = blockList_[i];
			JitBlockMeta meta = blockCacheDebug->GetBlockMeta(blockNum);
			char temp[512], small[512];
			if (profiling) {
				JitBlockProfileStats stats = blockCacheDebug->GetBlockProfileStats(blockNum);
				int execs = (int)stats.executions;
				double us = (double)stats.totalNanos / 1000000.0;
				double percentage = 100.0 * (double)stats.totalNanos / (double)sumTotalNanos_;
				snprintf(temp, sizeof(temp), "%08x: %d instrs (%d runs, %0.2f ms, %0.2f%%)", meta.addr, meta.sizeInBytes / 4, execs, us, percentage);
			} else {
				snprintf(temp, sizeof(temp), "%08x: %d instrs", meta.addr, meta.sizeInBytes / 4);
			}
			snprintf(small, sizeof(small), "Small text");
			Choice *blockChoice = blockListContainer_->Add(new Choice(temp, small));
			blockChoice->OnClick.Handle(this, &JitCompareScreen::OnBlockClick);
		}
	} else {  // viewMode_ == ViewMode::STATS
		statsContainer_->Clear();

		BlockCacheStats bcStats;
		blockCacheDebug->ComputeStats(bcStats);

		char stats[1024];
		snprintf(stats, sizeof(stats),
			"Num blocks: %d\n"
			"Average Bloat: %0.2f%%\n"
			"Min Bloat: %0.2f%%  (%08x)\n"
			"Max Bloat: %0.2f%%  (%08x)\n",
			blockCacheDebug->GetNumBlocks(),
			100.0 * bcStats.avgBloat,
			100.0 * bcStats.minBloat, bcStats.minBloatBlock,
			100.0 * bcStats.maxBloat, bcStats.maxBloatBlock);

		statsContainer_->Add(new TextView(stats));
	}
}

UI::EventReturn JitCompareScreen::OnBlockClick(UI::EventParams &e) {
	int blockIndex = blockListContainer_->IndexOfSubview(e.v);
	if (blockIndex >= 0) {
		viewMode_ = ViewMode::DISASM;
		currentBlock_ = blockIndex;
		UpdateDisasm();
	}
	return UI::EVENT_DONE;
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

	viewMode_ = ViewMode::STATS;
	UpdateDisasm();
	return UI::EVENT_DONE;
}


UI::EventReturn JitCompareScreen::OnSelectBlock(UI::EventParams &e) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	auto addressPrompt = new AddressPromptScreen(dev->T("Block address"));
	addressPrompt->OnChoice.Handle(this, &JitCompareScreen::OnBlockAddress);
	screenManager()->push(addressPrompt);
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

/*
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
						// INFO_LOG(Log::HLE, "Stopping at random instruction: %08x %s", addr, temp);
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
}*/

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
