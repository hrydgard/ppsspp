
#include "ext/imgui/imgui_internal.h"

#include "Common/StringUtils.h"
#include "Core/Core.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Common/System/Request.h"

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
		if (ImGui::BeginTabItem("FPR")) {
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
		if (ImGui::BeginTabItem("VPR")) {
			ImGui::Text("TODO");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void ImDebugger::Frame(MIPSDebugInterface *mipsDebug) {
	// Snapshot the coreState to avoid inconsistency.
	const CoreState coreState = ::coreState;

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
			ImGui::Checkbox("Dear ImGUI Demo", &demoOpen_);
			ImGui::Checkbox("CPU debugger", &disasmOpen_);
			ImGui::Checkbox("Registers", &regsOpen_);
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (demoOpen_) {
		ImGui::ShowDemoWindow(&demoOpen_);
	}

	if (disasmOpen_) {
		disasm_.Draw(mipsDebug, &disasmOpen_, coreState);
	}

	if (regsOpen_) {
		DrawRegisterView(mipsDebug, &regsOpen_);
	}
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
						disasmView_.setCurAddress(symCache_[i].address);
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
