#include "ppsspp_config.h"

#include "UI/ImDebugger/ImJitViewer.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "Core/Config.h"

#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"

void ImJitViewerWindow::GoToBlockAtAddr(u32 addr) {
	for (auto &block : blockList_) {
		if (addr >= block.addr && addr < block.addr + (u32)block.sizeInBytes) {
			curBlockNum_ = block.blockNum;
			break;
		}
	}
}

void ImJitViewerWindow::Draw(ImConfig &cfg, ImControl &control) {
	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("JitViewer", &cfg.jitViewerOpen)) {
		ImGui::End();
		return;
	}

	const CPUCore core = (CPUCore)g_Config.iCpuCore;

	if (core == CPUCore::INTERPRETER) {
		ImGui::TextUnformatted("JIT Viewer is only available with JIT or IR-based CPU cores.");
		ImGui::End();
		return;
	}

	// Options menu
	if (ImGui::BeginPopup("Options")) {
		// ImGui::Checkbox("Auto-scroll", &AutoScroll);
		ImGui::EndPopup();
	}

#if PPSSPP_ARCH(AMD64)
	const char *TARGET = "X86-64";
#elif PPSSPP_ARCH(X86)
	const char *TARGET = "X86";
#elif PPSSPP_ARCH(ARM64)

	const char *TARGET = "ARM64";
#elif PPSSPP_ARCH(ARM)
	const char *TARGET = "ARM32";
#else
	const char *TARGET = "TARGET";
#endif

	// Three or four columns: One table for blocks that can be sorted in various ways,
	// then the remaining are either MIPS/IR/TARGET or MIPS/TARGET or MIPS/IR depending on the JIT type.
	int numColumns = 3;
	if (core == CPUCore::JIT_IR) {
		numColumns = 4;
	}

	JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();

	if (core_ != (int)core) {
		// Core changed, need to refresh.
		refresh_ = true;
		core_ = (int)core;
	}

	if (sortSpecs_ && sortSpecs_->SpecsDirty) {
		refresh_ = true;
		sortSpecs_->SpecsDirty = false;
	}

	// If the CPU moved, and we're not currently running, update the cached blocklist.
	const CoreState coreStateCached = coreState;
	if (coreStateCached == CORE_STEPPING_CPU || coreStateCached == CORE_NEXTFRAME || refresh_) {
		const int cpuStepCount = Core_GetSteppingCounter();
		if (lastCpuStepCount_ != cpuStepCount || refresh_) {
			refresh_ = false;
			lastCpuStepCount_ = cpuStepCount;
			blockList_.clear();
			curBlockNum_ = -1;
			const int blockCount = blockCacheDebug->GetNumBlocks();
			blockList_.reserve(blockCount);
			INFO_LOG(Log::Debugger, "Updating JIT block list... %d blocks", blockCacheDebug->GetNumBlocks());
			for (int i = 0; i < blockCount; i++) {
				if (!blockCacheDebug->IsValidBlock(i)) {
					continue;
				}
				JitBlockMeta meta = blockCacheDebug->GetBlockMeta(i);
				if (!meta.valid) {
					continue;
				}
				CachedBlock cb;
				cb.addr = meta.addr;
				cb.sizeInBytes = meta.sizeInBytes;
				cb.blockNum = i;
				blockList_.push_back(cb);
			}

			std::sort(blockList_.begin(), blockList_.end(), [this](const CachedBlock &a, const CachedBlock &b) {
				if (!sortSpecs_ || sortSpecs_->SpecsCount <= 0) {
					return a.addr < b.addr;
				}
				const ImGuiTableColumnSortSpecs *spec = &sortSpecs_->Specs[0];
				int delta = 0;
				if (spec->ColumnUserID == 0) {
					delta = (int)a.blockNum - (int)b.blockNum;
				} else
				if (spec->ColumnUserID == 1) {
					delta = (int)a.addr - (int)b.addr;
				} else if (spec->ColumnUserID == 2) {
					delta = a.sizeInBytes - b.sizeInBytes;
				}
				if (delta == 0) {
					return a.addr < b.addr;
				}
				if (spec->SortDirection == ImGuiSortDirection_Ascending) {
					return delta < 0;
				} else {
					return delta > 0;
				}
			});
		}
	} else {
		ImGui::Text("Pause to update block list");
	}

	if (ImGui::BeginTable("columns", numColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("Blocks");
		ImGui::TableSetupColumn("MIPS");
		if (core == CPUCore::JIT_IR || core == CPUCore::IR_INTERPRETER) {
			ImGui::TableSetupColumn("IR");
		}
		if (core == CPUCore::JIT_IR || core == CPUCore::JIT) {
			ImGui::TableSetupColumn(TARGET);
		}

		ImGui::TableHeadersRow();

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		// For separate scrolling.
		ImGui::BeginChild("LeftPane", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

		if (ImGui::BeginTable("blocks", 3, ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable)) {
			ImGui::TableSetupColumn("Num", 0, 0, 0);
			ImGui::TableSetupColumn("Addr", 0, 0, 1);
			ImGui::TableSetupColumn("Size", 0, 0, 2);
			ImGui::TableHeadersRow();
			sortSpecs_ = ImGui::TableGetSortSpecs();

			ImGuiListClipper clipper;
			clipper.Begin((int)blockList_.size());

			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
					const auto &block = blockList_[i];

					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					char label[32];
					snprintf(label, sizeof(label), "%d", block.blockNum);

					if (ImGui::Selectable(label, block.blockNum == curBlockNum_, ImGuiSelectableFlags_SpanAllColumns)) {
						curBlockNum_ = block.blockNum;
						if (curBlockNum_ >= 0 && curBlockNum_ < blockCacheDebug->GetNumBlocks()) {
							debugInfo_ = blockCacheDebug->GetBlockDebugInfo(curBlockNum_);
						}
					}

					ImGui::TableNextColumn();
					ImGui::Text("%08x", block.addr);

					ImGui::TableNextColumn();
					ImGui::Text("%d", block.sizeInBytes);
				}
			}

			clipper.End();
			ImGui::EndTable();

		}

		ImGui::EndChild();

		ImGui::TableNextColumn();

		ImGui::BeginChild("MIPSPane", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

		for (const auto &line : debugInfo_.origDisasm) {
			ImGui::TextUnformatted(line.c_str());
		}
		ImGui::EndChild();

		if (core == CPUCore::JIT_IR || core == CPUCore::IR_INTERPRETER) {
			ImGui::TableNextColumn();

			ImGui::BeginChild("IRPane", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
			// TODO : When we have both target and IR, need a third column.
			for (const auto &line : debugInfo_.irDisasm) {
				ImGui::TextUnformatted(line.c_str());
			}
			ImGui::EndChild();
		}

		if (core == CPUCore::JIT_IR || core == CPUCore::JIT) {
			ImGui::TableNextColumn();

			ImGui::BeginChild("TargetPane", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
			for (const auto &line : debugInfo_.targetDisasm) {
				ImGui::TextUnformatted(line.c_str());
			}
			ImGui::EndChild();
		}

		ImGui::EndTable();
	}

	ImGui::End();
}
