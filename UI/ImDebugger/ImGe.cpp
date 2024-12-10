#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_internal.h"
#include "ext/imgui/imgui_impl_thin3d.h"
#include "UI/ImDebugger/ImGe.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"

#include "Core/HLE/sceDisplay.h"
#include "Core/HW/Display.h"
#include "Common/StringUtils.h"
#include "GPU/Debugger/State.h"
#include "GPU/Debugger/GECommandTable.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/GPUState.h"

void DrawFramebuffersWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Framebuffers", &cfg.framebuffersOpen)) {
		ImGui::End();
		return;
	}

	framebufferManager->DrawImGuiDebug(cfg.selectedFramebuffer);

	ImGui::End();
}

void DrawTexturesWindow(ImConfig &cfg, TextureCacheCommon *textureCache) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Textures", &cfg.texturesOpen)) {
		ImGui::End();
		return;
	}

	textureCache->DrawImGuiDebug(cfg.selectedTexAddr);

	ImGui::End();
}

void DrawDisplayWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Display", &cfg.displayOpen)) {
		ImGui::End();
		return;
	}

	ImGui::Checkbox("Display latched", &cfg.displayLatched);

	PSPPointer<u8> topaddr;
	u32 linesize;
	u32 pixelFormat;

	__DisplayGetFramebuf(&topaddr, &linesize, &pixelFormat, cfg.displayLatched);

	VirtualFramebuffer *fb = framebufferManager->GetVFBAt(topaddr.ptr);
	if (fb && fb->fbo) {
		ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(fb->fbo, Draw::FB_COLOR_BIT, ImGuiPipeline::TexturedOpaque);
		ImGui::Image(texId, ImVec2(fb->width, fb->height));
		ImGui::Text("%s - %08x", fb->fbo->Tag(), topaddr.ptr);
	} else {
		// TODO: Sometimes we should display RAM here.
		ImGui::Text("Framebuffer not available to display");
	}

	ImGui::End();
}

// Note: This is not exclusively graphics.
void DrawDebugStatsWindow(ImConfig &cfg) {
	ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Debug Stats", &cfg.debugStatsOpen)) {
		ImGui::End();
		return;
	}
	char statbuf[4096];
	__DisplayGetDebugStats(statbuf, sizeof(statbuf));
	ImGui::TextUnformatted(statbuf);
	ImGui::End();
}

void ImGeDisasmView::Draw(GPUDebugInterface *gpuDebug) {
	const u32 branchColor = 0xFFA0FFFF;
	const u32 gteColor = 0xFFFFEFA0;

	static ImVec2 scrolling(0.0f, 0.0f);

	ImGui_PushFixedFont();

	ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();      // ImDrawList API uses screen coordinates!
	ImVec2 canvas_sz = ImGui::GetContentRegionAvail();   // Resize canvas to what's available
	int lineHeight = (int)ImGui::GetTextLineHeightWithSpacing();
	if (canvas_sz.x < 50.0f) canvas_sz.x = 50.0f;
	if (canvas_sz.y < 50.0f) canvas_sz.y = 50.0f;
	canvas_sz.y -= lineHeight * 2;

	// This will catch our interactions
	bool pressed = ImGui::InvisibleButton("canvas", canvas_sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

	ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

	// Draw border and background color
	ImGuiIO& io = ImGui::GetIO();
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->PushClipRect(canvas_p0, canvas_p1, true);

	draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(25, 25, 25, 255));

	int numLines = (int)(canvas_sz.y / lineHeight + 1.0f);
	u32 instrWidth = 4;
	u32 windowStartAddr = selectedAddr_ - (numLines / 2) * instrWidth;

	DisplayList displayList;
	u32 pc = 0xFFFFFFFF;
	u32 stallAddr = 0xFFFFFFFF;
	if (gpuDebug->GetCurrentDisplayList(displayList)) {
		pc = displayList.pc;
		stallAddr = displayList.stall;
	}

	if (pc != 0xFFFFFFFF) {
		if (gotoPC_ || followPC_) {
			selectedAddr_ = pc;
			gotoPC_ = false;
		}
	}

	float pcY = canvas_p0.y + ((pc - windowStartAddr) / instrWidth) * lineHeight;
	draw_list->AddRectFilled(ImVec2(canvas_p0.x, pcY), ImVec2(canvas_p1.x, pcY + lineHeight), IM_COL32(0x10, 0x70, 0x10, 255));

	u32 addr = windowStartAddr;
	for (int line = 0; line < numLines; line++) {
		char addrBuffer[128];
		snprintf(addrBuffer, sizeof(addrBuffer), "%08x", addr);

		ImVec2 lineStart = ImVec2(canvas_p0.x + lineHeight + 8, canvas_p0.y + line * lineHeight);
		ImVec2 opcodeStart = ImVec2(canvas_p0.x + 120, canvas_p0.y + line * lineHeight);
		ImVec2 descStart = ImVec2(canvas_p0.x + 220, canvas_p0.y + line * lineHeight);
		ImVec2 liveStart = ImVec2(canvas_p0.x + 250, canvas_p0.y + line * lineHeight);
		if (Memory::IsValid4AlignedAddress(addr)) {
			draw_list->AddText(lineStart, 0xFFC0C0C0, addrBuffer);

			GPUDebugOp op = gpuDebug->DisassembleOp(addr);
			u32 color = 0xFFFFFFFF;
			char temp[16];
			snprintf(temp, sizeof(temp), "%08x", op.op);
			draw_list->AddText(opcodeStart, color, temp);
			draw_list->AddText(descStart, color, op.desc.data(), op.desc.data() + op.desc.size());
			// if (selectedAddr_ == addr && strlen(disMeta.liveInfo)) {
			// 	draw_list->AddText(liveStart, 0xFFFFFFFF, disMeta.liveInfo);
			// }

			bool bp = GPUBreakpoints::IsAddressBreakpoint(addr);
			if (bp) {
				draw_list->AddCircleFilled(ImVec2(canvas_p0.x + lineHeight * 0.5f, lineStart.y + lineHeight * 0.5f), lineHeight * 0.45f, 0xFF0000FF, 12);
			}
		} else {
			draw_list->AddText(lineStart, 0xFF808080, addrBuffer);
		}
		addr += instrWidth;
	}

	// Draw a rectangle around the selected line.
	int selectedY = canvas_p0.y + ((selectedAddr_ - windowStartAddr) / instrWidth) * lineHeight;
	draw_list->AddRect(ImVec2(canvas_p0.x, selectedY), ImVec2(canvas_p1.x, selectedY + lineHeight), IM_COL32(255, 255, 255, 255));
	if (dragAddr_ != selectedAddr_ && dragAddr_ != INVALID_ADDR) {
		int dragY = canvas_p0.y + ((dragAddr_ - windowStartAddr) / instrWidth) * lineHeight;
		draw_list->AddRect(ImVec2(canvas_p0.x, dragY), ImVec2(canvas_p1.x, dragY + lineHeight), IM_COL32(128, 128, 128, 255));
	}

	const bool is_hovered = ImGui::IsItemHovered(); // Hovered
	const bool is_active = ImGui::IsItemActive();   // Held
	const ImVec2 mouse_pos_in_canvas(io.MousePos.x - canvas_p0.x, io.MousePos.y - canvas_p0.y);

	if (is_active) {
		dragAddr_ = windowStartAddr + ((int)mouse_pos_in_canvas.y / lineHeight) * instrWidth;
	}

	if (pressed) {
		if (io.MousePos.x < canvas_p0.x + lineHeight) {
			// Toggle breakpoint
			if (!GPUBreakpoints::IsAddressBreakpoint(dragAddr_)) {
				GPUBreakpoints::AddAddressBreakpoint(dragAddr_);
			} else {
				GPUBreakpoints::RemoveAddressBreakpoint(dragAddr_);
			}
			bpPopup_ = true;
		} else {
			selectedAddr_ = dragAddr_;
			bpPopup_ = false;
		}
	}
	ImGui_PopFont();
	draw_list->PopClipRect();

	// Context menu (under default mouse threshold)
	ImGui::OpenPopupOnItemClick("context", ImGuiPopupFlags_MouseButtonRight);
	if (ImGui::BeginPopup("context")) {
		if (bpPopup_) {
			if (ImGui::MenuItem("Remove breakpoint", NULL, false)) {
				GPUBreakpoints::RemoveAddressBreakpoint(dragAddr_);
			}
		} else if (Memory::IsValid4AlignedAddress(dragAddr_)) {
			char buffer[64];
			GPUDebugOp op = gpuDebug->DisassembleOp(pc);
			// affect dragAddr_?
			if (ImGui::MenuItem("Copy Address", NULL, false)) {
				snprintf(buffer, sizeof(buffer), "%08x", dragAddr_);
				ImGui::SetClipboardText(buffer);
				INFO_LOG(Log::G3D, "Copied '%s'", buffer);
			}
			if (ImGui::MenuItem("Copy Instruction Hex", NULL, false)) {
				snprintf(buffer, sizeof(buffer), "%08x", (u32)op.op);
				ImGui::SetClipboardText(buffer);
				INFO_LOG(Log::G3D, "Copied '%s'", buffer);
			}
			/*
			if (meta.instructionFlags & IF_BRANCHFIXED) {
				if (ImGui::MenuItem("Follow Branch")) {
					u32 target = GetBranchTarget(meta.opcode, dragAddr_, meta.instructionFlags);
					if (target != 0xFFFFFFFF) {
						selectedAddr_ = target;
					}
				}
			}*/
		}
		ImGui::EndPopup();
	}

	if (pressed) {
		// INFO_LOG(Log::UI, "Pressed");
	}
}

void ImGeDebuggerWindow::Draw(ImConfig &cfg, GPUDebugInterface *gpuDebug) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("GE Debugger", &cfg.geDebuggerOpen)) {
		ImGui::End();
		return;
	}

	ImGui::BeginDisabled(coreState != CORE_STEPPING_GE);
	if (ImGui::Button("Run/Resume")) {
		Core_Resume();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted("Break:");
	ImGui::SameLine();
	//if (ImGui::Button("Frame")) {
		// TODO: This doesn't work correctly.
	//	GPUDebug::SetBreakNext(GPUDebug::BreakNext::FRAME);
	//}

	bool disableStepButtons = GPUDebug::GetBreakNext() != GPUDebug::BreakNext::NONE;

	if (disableStepButtons) {
		ImGui::BeginDisabled();
	}
	ImGui::SameLine();
	if (ImGui::Button("Tex")) {
		GPUDebug::SetBreakNext(GPUDebug::BreakNext::TEX);
	}
	ImGui::SameLine();
	if (ImGui::Button("NonTex")) {
		GPUDebug::SetBreakNext(GPUDebug::BreakNext::NONTEX);
	}
	ImGui::SameLine();
	if (ImGui::Button("Prim")) {
		GPUDebug::SetBreakNext(GPUDebug::BreakNext::PRIM);
	}
	ImGui::SameLine();
	if (ImGui::Button("Draw")) {
		GPUDebug::SetBreakNext(GPUDebug::BreakNext::DRAW);
	}
	ImGui::SameLine();
	if (ImGui::Button("Curve")) {
		GPUDebug::SetBreakNext(GPUDebug::BreakNext::CURVE);
	}
	ImGui::SameLine();
	if (ImGui::Button("Single step")) {
		GPUDebug::SetBreakNext(GPUDebug::BreakNext::OP);
	}
	if (disableStepButtons) {
		ImGui::EndDisabled();
	}

	// Line break
	if (ImGui::Button("Goto PC")) {
		disasmView_.GotoPC();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Settings")) {
		ImGui::OpenPopup("disSettings");
	}
	if (ImGui::BeginPopup("disSettings")) {
		ImGui::Checkbox("Follow PC", &disasmView_.followPC_);
		ImGui::EndPopup();
	}

	// Display any pending step event.
	if (GPUDebug::GetBreakNext() != GPUDebug::BreakNext::NONE) {
		ImGui::Text("Step pending (waiting for CPU): %s", GPUDebug::BreakNextToString(GPUDebug::GetBreakNext()));
		ImGui::SameLine();
		if (ImGui::Button("Cancel step")) {
			GPUDebug::SetBreakNext(GPUDebug::BreakNext::NONE);
		}
	}

	// Let's display the current CLUT.

	// First, let's list any active display lists in the left column.
	for (auto index : gpuDebug->GetDisplayListQueue()) {
		const auto &list = gpuDebug->GetDisplayList(index);
		char title[64];
		snprintf(title, sizeof(title), "List %d", list.id);
		if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("PC: %08x (start: %08x)", list.pc, list.startpc);
			ImGui::Text("BBOX result: %d", (int)list.bboxResult);
		}
	}

	// Display the disassembly view.
	disasmView_.Draw(gpuDebug);

	ImGui::End();
}

// TODO: Separate window or merge into Ge debugger?
void DrawGeStateWindow(ImConfig &cfg, GPUDebugInterface *gpuDebug) {
	ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("GE State", &cfg.geStateOpen)) {
		ImGui::End();
		return;
	}
	if (ImGui::BeginTabBar("GeRegs", ImGuiTabBarFlags_None)) {
		auto buildStateTab = [&](const char *tabName, const GECommand *rows, size_t numRows) {
			if (ImGui::BeginTabItem(tabName)) {
				if (ImGui::BeginTable("fpr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
					ImGui::TableSetupColumn("bkpt", ImGuiTableColumnFlags_WidthFixed);
					ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					for (size_t i = 0; i < numRows; i++) {
						const GECmdInfo &info = GECmdInfoByCmd(rows[i]);

						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::Text("-");  // breakpoint
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(info.uiName.data(), info.uiName.data() + info.uiName.size());
						ImGui::TableNextColumn();
						char temp[256];

						const bool enabled = info.enableCmd == 0 || (gstate.cmdmem[info.enableCmd] & 1) == 1;
						const u32 value = gstate.cmdmem[info.cmd] & 0xFFFFFF;
						const u32 otherValue = gstate.cmdmem[info.otherCmd] & 0xFFFFFF;
						const u32 otherValue2 = gstate.cmdmem[info.otherCmd2] & 0xFFFFFF;

						FormatStateRow(gpuDebug, temp, sizeof(temp), info.fmt, value, enabled, otherValue, otherValue2);
						ImGui::TextUnformatted(temp);
					}

					ImGui::EndTable();
				}
				ImGui::EndTabItem();
			}
		};

		buildStateTab("Flags", g_stateFlagsRows, g_stateFlagsRowsSize);
		buildStateTab("Lighting", g_stateLightingRows, g_stateLightingRowsSize);
		buildStateTab("Texture", g_stateTextureRows, g_stateTextureRowsSize);
		buildStateTab("Settings", g_stateSettingsRows, g_stateSettingsRowsSize);

		// Do a vertex tab (maybe later a separate window)
		if (ImGui::BeginTabItem("Vertices")) {
			const ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
			if (ImGui::BeginTabBar("vertexmode", ImGuiTabBarFlags_None)) {
				auto state = gpuDebug->GetGState();
				char fmtTemp[256];
				FormatStateRow(gpuDebug, fmtTemp, sizeof(fmtTemp), CMD_FMT_VERTEXTYPE, state.vertType, true, false, false);
				ImGui::TextUnformatted(fmtTemp);
				// Let's see if it's fast enough to just do all this each frame.
				int rowCount_ = gpuDebug->GetCurrentPrimCount();
				std::vector<GPUDebugVertex> vertices;
				std::vector<u16> indices;
				if (!gpuDebug->GetCurrentSimpleVertices(rowCount_, vertices, indices)) {
					rowCount_ = 0;
				}
				VertexDecoderOptions options{};
				// TODO: Maybe an option?
				options.applySkinInDecode = true;

				auto buildVertexTable = [&](bool raw) {
					// Ignore indices for now.
					if (ImGui::BeginTable("rawverts", VERTEXLIST_COL_COUNT + 1, tableFlags)) {
						static VertexDecoder decoder;
						decoder.SetVertexType(state.vertType, options);

						static const char * const colNames[] = {
							"Index",
							"X",
							"Y",
							"Z",
							"U",
							"V",
							"Color",
							"NX",
							"NY",
							"NZ",
						};
						for (int i = 0; i < ARRAY_SIZE(colNames); i++) {
							ImGui::TableSetupColumn(colNames[i], ImGuiTableColumnFlags_WidthFixed, 0.0f, i);
						}
						ImGui::TableSetupScrollFreeze(0, 1); // Make header row always visible
						ImGui::TableHeadersRow();

						ImGuiListClipper clipper;
						_dbg_assert_(rowCount_ >= 0);
						clipper.Begin(rowCount_);
						while (clipper.Step()) {
							for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
								int index = indices.empty() ? i : indices[i];
								ImGui::PushID(i);

								ImGui::TableNextRow();
								ImGui::TableNextColumn();
								ImGui::Text("%d", index);
								for (int column = 0; column < VERTEXLIST_COL_COUNT; column++) {
									ImGui::TableNextColumn();
									char temp[36];
									if (raw) {
										FormatVertColRaw(&decoder, temp, sizeof(temp), index, column);
									} else {
										FormatVertCol(temp, sizeof(temp), vertices[index], column);
									}
									ImGui::TextUnformatted(temp);
								}
								ImGui::PopID();
							}
						}
						clipper.End();

						ImGui::EndTable();
					}
				};

				if (ImGui::BeginTabItem("Raw")) {
					buildVertexTable(true);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Transformed")) {
					buildVertexTable(false);
					ImGui::EndTabItem();
				}
				// TODO: Let's not include columns for which we have no data.
				ImGui::EndTabBar();
			}
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	ImGui::End();
}
