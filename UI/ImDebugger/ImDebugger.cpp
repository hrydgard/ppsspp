
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
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (demoOpen_) {
		ImGui::ShowDemoWindow(&demoOpen_);
	}

	if (disasmOpen_) {
		disasm_.Draw(mipsDebug, &disasmOpen_);
	}

	ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.
	ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
	ImGui::End();
}

void ImDisasmWindow::Draw(MIPSDebugInterface *mipsDebug, bool *open) {
	char title[256];
	snprintf(title, sizeof(title), "%s - Disassembly", "Allegrex MIPS");

	disasmView_.setDebugger(mipsDebug);

	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, open)) {
		ImGui::End();
		return;
	}

	if (ImGui::SmallButton("Run")) {
		Core_Resume();
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Pause")) {
		Core_Break("Pause");
	}

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
	ImGui::Checkbox("Follow PC", &followPC_);

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
			std::vector<SymbolEntry> syms = g_symbolMap->GetAllSymbols(SymbolType::ST_FUNCTION);
			for (auto &sym : syms) {
				if (ImGui::Selectable(sym.name.c_str(), false)) {
					disasmView_.setCurAddress(sym.address);
					disasmView_.scrollAddressIntoView();
				}
			}
			ImGui::EndListBox();
		}

		ImGui::TableSetColumnIndex(1);
		// Draw border and background color
		disasmView_.Draw(ImGui::GetWindowDrawList());
		ImGui::EndTable();
	}
	ImGui::End();
}
