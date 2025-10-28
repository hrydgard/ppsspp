#include <algorithm>

#include "UI/JitCompareScreen.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Render/DrawBuffer.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"
#include "UI/PopupScreens.h"

JitCompareScreen::JitCompareScreen() : UITabbedBaseDialogScreen(Path()) {
	if (!MIPSComp::jit) {
		return;
	}
	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();
	// The only defaults that make sense.
	if (blockCacheDebug->SupportsProfiling()) {
		listSort_ = ListSort::TIME_SPENT;
	} else {
		listSort_ = ListSort::BLOCK_LENGTH_DESC;
	}
	FillBlockList();
}

// Three panes: Block chooser, MIPS view, ARM/x86 view
void JitCompareScreen::CreateTabs() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	using namespace UI;

	AddTab("Block List", dev->T("Block List"), [=](LinearLayout *tabContent) {
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

		LinearLayout *listTopBar = tabContent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		Choice *sortButton = new Choice(dev->T("Sort..."), new LinearLayoutParams());
		listTopBar->Add(sortButton)->OnClick.Add([this, sortButton, sortCount](UI::EventParams &e) {
			PopupContextMenuScreen *contextMenu = new UI::PopupContextMenuScreen(sortMenu, sortCount, I18NCat::DEVELOPER, sortButton);
			screenManager()->push(contextMenu);
			contextMenu->OnChoice.Add([=](EventParams &e) -> void {
				if (e.a < (int)ListSort::MAX) {
					listSort_ = (ListSort)e.a;
					UpdateDisasm();
				}
			});
		});
		// leftColumn->Add(new Choice(dev->T("By Address")))->OnClick.Handle(this, &JitCompareScreen::OnSelectBlock);
		listTopBar->Add(new Choice(dev->T("All"), new LinearLayoutParams()))->OnClick.Add([=](UI::EventParams &e) {
			listType_ = ListType::ALL_BLOCKS;
			UpdateDisasm();
		});
		listTopBar->Add(new Choice(dev->T("FPU"), new LinearLayoutParams()))->OnClick.Add([=](UI::EventParams &e) {
			listType_ = ListType::FPU_BLOCKS;
			UpdateDisasm();
		});
		listTopBar->Add(new Choice(dev->T("VFPU"), new LinearLayoutParams()))->OnClick.Add([=](UI::EventParams &e) {
			listType_ = ListType::VFPU_BLOCKS;
			UpdateDisasm();
		});

		blockListContainer_ = tabContent->Add(new LinearLayout(ORIENT_VERTICAL));
	});

	AddTab("Comparison", dev->T("Jit Compare"), [=](LinearLayout *tabContent) {
		LinearLayout *blockTopBar = tabContent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		blockTopBar->Add(new Button("", ImageID("I_ARROW_LEFT")))->OnClick.Add([=](UI::EventParams &e) {
			if (currentBlock_ >= 1)
				currentBlock_--;
			UpdateDisasm();
		});
		blockTopBar->Add(new Button("", ImageID("I_ARROW_RIGHT")))->OnClick.Add([=](UI::EventParams &e) {
			if (currentBlock_ < (int)blockList_.size() - 1)
				currentBlock_++;
			if (currentBlock_ == -1 && !blockList_.empty()) {
				currentBlock_ = 0;
			}
			UpdateDisasm();
		});
		blockTopBar->Add(new Button(dev->T("Random")))->OnClick.Add([=](UI::EventParams &e) {
			if (blockList_.empty()) {
				return;
			}
			currentBlock_ = rand() % blockList_.size();
			UpdateDisasm();
		});

		blockAddr_ = blockTopBar->Add(new TextEdit("", dev->T("Block address"), ""));
		blockAddr_->OnEnter.Handle(this, &JitCompareScreen::OnAddressChange);
		blockName_ = blockTopBar->Add(new TextView(dev->T("No block")));
		blockStats_ = blockTopBar->Add(new TextView(""));

		tabContent->Add(new Button("test"));

		LinearLayout *columns = tabContent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f)));

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
	}, TabFlags::NonScrollable);

	AddTab("Stats", dev->T("Stats"), [=](LinearLayout *tabContent) {
		globalStats_ = tabContent->Add(new TextView("N/A"));
	});

	EnsureTabs();  // don't create them lazily, due to the interdependences
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
			break;
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

	if (currentBlock_ < 0 && !blockList_.empty()) {
		currentBlock_ = 0;
	}
}

void JitCompareScreen::UpdateDisasm() {
	leftDisasm_->Clear();
	rightDisasm_->Clear();

	using namespace UI;

	if (!MIPSComp::jit) {
		return;
	}

	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();

	FillBlockList();

	if (currentBlock_ >= 0 && currentBlock_ < blockList_.size()) {  // Update disassembly
		char temp[256];
		snprintf(temp, sizeof(temp), "%d/%d", currentBlock_, (int)blockList_.size());
		blockName_->SetText(temp);

		int blockNum = blockList_[currentBlock_];

		if (!blockCacheDebug->IsValidBlock(blockNum)) {
			auto dev = GetI18NCategory(I18NCat::DEVELOPER);
			leftDisasm_->Add(new TextView(dev->T("No block")));
			rightDisasm_->Add(new TextView(dev->T("No block")));
			blockStats_->SetText("(no stats)");
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
	}
	{   // Update block list
		blockListContainer_->Clear();
		bool profiling = blockCacheDebug->SupportsProfiling();
		for (int i = 0; i < std::min(200, (int)blockList_.size()); i++) {
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
	}

	// Update stats
	{
		BlockCacheStats bcStats{};
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

		globalStats_->SetText(stats);
	}
}

void JitCompareScreen::OnBlockClick(UI::EventParams &e) {
	int blockIndex = blockListContainer_->IndexOfSubview(e.v);
	if (blockIndex >= 0) {
		currentBlock_ = blockIndex;
		SetCurrentTab(1);
		UpdateDisasm();
	}
}

void JitCompareScreen::OnAddressChange(UI::EventParams &e) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return;
	}
	JitBlockCacheDebugInterface *blockCache = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (!blockCache)
		return;
	u32 addr;
	if (blockAddr_->GetText().size() > 8)
		return;
	if (1 == sscanf(blockAddr_->GetText().c_str(), "%08x", &addr)) {
		if (Memory::IsValidAddress(addr)) {
			currentBlock_ = blockCache->GetBlockNumberFromStartAddress(addr);
			UpdateDisasm();
		}
	}
}

void JitCompareScreen::OnSelectBlock(UI::EventParams &e) {
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	auto addressPrompt = new AddressPromptScreen(dev->T("Block address"));
	addressPrompt->OnChoice.Handle(this, &JitCompareScreen::OnBlockAddress);
	screenManager()->push(addressPrompt);
}

void JitCompareScreen::OnBlockAddress(UI::EventParams &e) {
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (!MIPSComp::jit) {
		return;
	}

	JitBlockCacheDebugInterface *blockCache = MIPSComp::jit->GetBlockCacheDebugInterface();
	if (!blockCache)
		return;

	if (Memory::IsValidAddress(e.a)) {
		currentBlock_ = blockCache->GetBlockNumberFromStartAddress(e.a);
	} else {
		currentBlock_ = -1;
	}
	UpdateDisasm();
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

void AddressPromptScreen::OnDigitButton(UI::EventParams &e) {
	for (int i = 0; i < 16; ++i) {
		if (buttons_[i] == e.v) {
			AddDigit(i);
		}
	}
}

void AddressPromptScreen::OnBackspace(UI::EventParams &e) {
	BackspaceDigit();
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
