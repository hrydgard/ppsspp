
#include "ext/imgui/imgui_internal.h"

#include "Common/StringUtils.h"
#include "Core/RetroAchievements.h"
#include "Core/Core.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Common/System/Request.h"

// Threads window
#include "Core/HLE/sceKernelThread.h"

// Callstack window
#include "Core/MIPS/MIPSStackWalk.h"

#include "UI/ImDebugger/ImDebugger.h"

void DrawRegisterView(MIPSDebugInterface *mipsDebug, bool *open) {
	if (!ImGui::Begin("Registers", open)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("RegisterGroups", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("GPR")) {
			if (ImGui::BeginTable("gpr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
				ImGui::TableSetupColumn("regname", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("value_i", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();

				auto gprLine = [&](const char *regname, int value) {
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%s", regname);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%08x", value);
					if (value >= -1000000 && value <= 1000000) {
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%d", value);
					}
					ImGui::TableNextRow();
				};
				for (int i = 0; i < 32; i++) {
					gprLine(mipsDebug->GetRegName(0, i).c_str(), mipsDebug->GetGPR32Value(i));
				}
				gprLine("hi", mipsDebug->GetHi());
				gprLine("lo", mipsDebug->GetLo());
				gprLine("pc", mipsDebug->GetPC());
				gprLine("ll", mipsDebug->GetLLBit());
				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("FPU")) {
			if (ImGui::BeginTable("fpr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
				ImGui::TableSetupColumn("regname", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("value_i", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				for (int i = 0; i < 32; i++) {
					float fvalue = mipsDebug->GetFPR32Value(i);
					u32 fivalue;
					memcpy(&fivalue, &fvalue, sizeof(fivalue));
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%s", mipsDebug->GetRegName(1, i).c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%0.7f", fvalue);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%08x", fivalue);
					ImGui::TableNextRow();
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("VFPU")) {
			ImGui::Text("TODO");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

static const char *ThreadStatusToString(u32 status) {
	switch (status) {
	case THREADSTATUS_RUNNING: return "Running";
	case THREADSTATUS_READY: return "Ready";
	case THREADSTATUS_WAIT: return "Wait";
	case THREADSTATUS_SUSPEND: return "Suspended";
	case THREADSTATUS_DORMANT: return "Dormant";
	case THREADSTATUS_DEAD: return "Dead";
	case THREADSTATUS_WAITSUSPEND: return "WaitSuspended";
	default:
		break;
	}
	return "(unk)";
}

void DrawThreadView(bool *open) {
	if (!ImGui::Begin("Threads", open)) {
		ImGui::End();
		return;
	}

	std::vector<DebugThreadInfo> info = GetThreadsInfo();
	if (ImGui::BeginTable("threads", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("PC", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableHeadersRow();
		ImGui::TableNextRow();

		// TODO: Add context menu
		for (auto &thread : info) {
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%s", thread.name);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%08x", thread.curPC);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%08x", thread.entrypoint);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d", thread.priority);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%s", ThreadStatusToString(thread.status));
			ImGui::TableNextRow();
			// TODO: More fields?
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

void DrawCallStacks(MIPSDebugInterface *debug, bool *open) {
	if (!ImGui::Begin("Callstacks", open)) {
		ImGui::End();
		return;
	}

	std::vector<DebugThreadInfo> info = GetThreadsInfo();
	// TODO: Add dropdown for thread choice.
	u32 entry = 0;
	u32 stackTop = 0;
	for (auto &thread : info) {
		if (thread.isCurrent) {
			entry = thread.entrypoint;
			stackTop = thread.initialStack;
			break;
		}
	}

	if (entry != 0 && ImGui::BeginTable("frames", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("EntryAddr", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("CurPC", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("CurOpCode", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("CurSP", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableHeadersRow();
		ImGui::TableNextRow();

		std::vector<MIPSStackWalk::StackFrame> frames = MIPSStackWalk::Walk(debug->GetPC(), debug->GetRegValue(0, 31), debug->GetRegValue(0, 29), entry, stackTop);

		// TODO: Add context menu and clickability
		for (auto &frame : frames) {
			const std::string entrySym = g_symbolMap->GetLabelString(frame.entry);

			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%s", entrySym.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%08x", frame.entry);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%08x", frame.pc);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%s", "N/A");  // opcode, see the old debugger
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%08x", frame.sp);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d", frame.stackSize);
			ImGui::TableNextRow();
			// TODO: More fields?
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

void DrawModules(MIPSDebugInterface *debug, bool *open) {
	if (!ImGui::Begin("Modules", open) || !g_symbolMap) {
		ImGui::End();
		return;
	}

	std::vector<LoadedModuleInfo> modules = g_symbolMap->getAllModules();

	if (ImGui::BeginTable("modules", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed);

		ImGui::TableHeadersRow();
		ImGui::TableNextRow();

		// TODO: Add context menu and clickability
		for (auto &module : modules) {
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%s", module.name.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%08x", module.address);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%08x", module.size);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%s", module.active ? "yes" : "no");
			ImGui::TableNextRow();
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

void DrawHLEModules(ImConfig &config) {
	if (!ImGui::Begin("HLE Modules", &config.hleModulesOpen)) {
		ImGui::End();
		return;
	}

	const int moduleCount = GetNumRegisteredModules();
	for (int i = 0; i < moduleCount; i++) {
		const HLEModule *module = GetModuleByIndex(i);
		if (ImGui::TreeNode(module->name)) {
			for (int j = 0; j < module->numFunctions; j++) {
				auto &func = module->funcTable[j];
				ImGui::Text("%s(%s)", func.name, func.argmask);
			}
			ImGui::TreePop();
		}
	}

	ImGui::End();
}

void ImDebugger::Frame(MIPSDebugInterface *mipsDebug) {
	// Snapshot the coreState to avoid inconsistency.
	const CoreState coreState = ::coreState;

	if (Achievements::HardcoreModeActive()) {
		ImGui::Begin("RetroAchievements hardcore mode");
		ImGui::Text("The debugger may not be used when the\nRetroAchievements hardcore mode is enabled.");
		ImGui::Text("To use the debugger, go into Settings / Tools / RetroAchievements and disable them.");
		ImGui::End();
		return;
	}

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("Debug")) {
			if (coreState == CoreState::CORE_STEPPING) {
				if (ImGui::MenuItem("Run")) {
					Core_Resume();
				}
				if (ImGui::MenuItem("Step Into", "F11")) {
					Core_RequestSingleStep(CPUStepType::Into, 1);
				}
				if (ImGui::MenuItem("Step Over", "F10")) {
					Core_RequestSingleStep(CPUStepType::Over, 1);
				}
				if (ImGui::MenuItem("Step Out", "Shift+F11")) {
					Core_RequestSingleStep(CPUStepType::Out, 1);
				}
			} else {
				if (ImGui::MenuItem("Break")) {
					Core_Break("Menu:Break");
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Toggle Breakpoint")) {
				// TODO
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window")) {
			ImGui::Checkbox("Dear ImGUI Demo", &cfg_.demoOpen);
			ImGui::Checkbox("CPU debugger", &cfg_.disasmOpen);
			ImGui::Checkbox("Registers", &cfg_.regsOpen);
			ImGui::Checkbox("Callstacks", &cfg_.callstackOpen);
			ImGui::Checkbox("HLE Modules", &cfg_.modulesOpen);
			ImGui::Checkbox("HLE Threads", &cfg_.threadsOpen);
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (cfg_.demoOpen) {
		ImGui::ShowDemoWindow(&cfg_.demoOpen);
	}

	if (cfg_.disasmOpen) {
		disasm_.Draw(mipsDebug, &cfg_.disasmOpen, coreState);
	}

	if (cfg_.regsOpen) {
		DrawRegisterView(mipsDebug, &cfg_.regsOpen);
	}

	if (cfg_.threadsOpen) {
		DrawThreadView(&cfg_.threadsOpen);
	}

	if (cfg_.callstackOpen) {
		DrawCallStacks(mipsDebug, &cfg_.callstackOpen);
	}

	if (cfg_.modulesOpen) {
		DrawModules(mipsDebug, &cfg_.modulesOpen);
	}

	DrawHLEModules(cfg_);
}

void ImDisasmWindow::Draw(MIPSDebugInterface *mipsDebug, bool *open, CoreState coreState) {
	char title[256];
	snprintf(title, sizeof(title), "%s - Disassembly", "Allegrex MIPS");

	disasmView_.setDebugger(mipsDebug);

	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, open, ImGuiWindowFlags_NoNavInputs)) {
		ImGui::End();
		return;
	}

	if (ImGui::IsWindowFocused()) {
		// Process stepping keyboard shortcuts.
		if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
			Core_RequestSingleStep(CPUStepType::Over, 0);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
			Core_RequestSingleStep(CPUStepType::Into, 0);
		}
	}

	ImGui::BeginDisabled(coreState != CORE_STEPPING);
	if (ImGui::SmallButton("Run")) {
		Core_Resume();
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(coreState != CORE_RUNNING);
	if (ImGui::SmallButton("Pause")) {
		Core_Break("Pause");
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::SmallButton("Step Into")) {
		u32 stepSize = disasmView_.getInstructionSizeAt(mipsDebug->GetPC());
		Core_RequestSingleStep(CPUStepType::Into, stepSize);
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Step Over")) {
		Core_RequestSingleStep(CPUStepType::Over, 0);
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Step Out")) {
		Core_RequestSingleStep(CPUStepType::Out, 0);
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Goto PC")) {
		disasmView_.gotoPC();
	}

	ImGui::SameLine();
	ImGui::Checkbox("Follow PC", &disasmView_.followPC_);

	ImGui::SetNextItemWidth(100);
	if (ImGui::InputScalar("Go to addr: ", ImGuiDataType_U32, &gotoAddr_, NULL, NULL, "%08X", ImGuiInputTextFlags_EnterReturnsTrue)) {
		disasmView_.setCurAddress(gotoAddr_);
		disasmView_.scrollAddressIntoView();
	}

	if (ImGui::BeginTable("main", 2)) {
		ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImVec2 sz = ImGui::GetContentRegionAvail();
		if (ImGui::BeginListBox("##symbols", ImVec2(150.0, sz.y - ImGui::GetTextLineHeightWithSpacing() * 2))) {
			if (symCache_.empty()) {
				symCache_ = g_symbolMap->GetAllSymbols(SymbolType::ST_FUNCTION);
			}
			ImGuiListClipper clipper;
			clipper.Begin((int)symCache_.size(), -1);
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
					if (ImGui::Selectable(symCache_[i].name.c_str(), false)) {
						disasmView_.gotoAddr(symCache_[i].address);
						disasmView_.scrollAddressIntoView();
					}
				}
			}
			clipper.End();
			ImGui::EndListBox();
		}

		ImGui::TableSetColumnIndex(1);
		disasmView_.Draw(ImGui::GetWindowDrawList());
		ImGui::EndTable();

		ImGui::Text("%s", disasmView_.StatusBarText().c_str());
	}
	ImGui::End();
}
