#include "ppsspp_config.h"

#include "UI/ImDebugger/ImJitViewer.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "Core/Config.h"

#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/JitCommon/JitState.h"

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

#if PPSSPP_ARCH(X64)
	const char *TARGET = "X86-64";
#elif PPSSPP_ARCH(X86)
	const char *TARGET = "X86";
#elif PPSSPP_ARCH(ARM64)

	const char *TARGET = "ARM64";
#elif PPSSPP_ARCH(ARM32)
	const char *TARGET = "ARM32";
#else
	cosnt char *TARGET = "TARGET";
#endif

	// Three or four columns: One table for blocks that can be sorted in various ways,
	// then the remaining are either MIPS/IR/TARGET or MIPS/TARGET or MIPS/IR depending on the JIT type.
	int numColumns = 3;
	if (core == CPUCore::JIT_IR) {
		numColumns = 4;
	}

	if (ImGui::BeginTable("columns", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH | ImGuiTableFlags_Resizable)) {
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

		ImGui::Text("Blocklist");
		JitBlockCacheDebugInterface *blockCacheDebug = MIPSComp::jit->GetBlockCacheDebugInterface();
		int64_t sumTotalNanos = 0;
		int64_t sumExecutions = 0;
		bool profiling = blockCacheDebug->SupportsProfiling();
		for (int i = 0; i < blockCacheDebug->GetNumBlocks(); i++) {
			if (!blockCacheDebug->IsValidBlock(i)) {
				continue;
			}
			JitBlockMeta meta = blockCacheDebug->GetBlockMeta(i);
			ImGui::Text("%08x %d", meta.addr, meta.sizeInBytes);
		}

		ImGui::TableNextColumn();

		ImGui::Text("MIPS");

		if (core == CPUCore::JIT_IR || core == CPUCore::IR_INTERPRETER) {

			ImGui::TableNextColumn();

			ImGui::Text("IR");
		}
		if (core == CPUCore::JIT_IR || core == CPUCore::JIT) {
			ImGui::TableNextColumn();
			ImGui::Text("Target asm");
		}

		/*
		if (ImGui::BeginTable("blocks", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH | ImGuiTableFlags_ScrollY)) {


			for (auto &block : blocks) {
				char label[64];
				snprintf(label, sizeof(label), "0x%08X", block.startAddress);
				if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns)) {
					", 3)
					*/

		ImGui::EndTable();
	}

	ImGui::End();
}
