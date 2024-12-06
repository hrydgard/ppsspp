#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_impl_thin3d.h"
#include "UI/ImDebugger/ImGe.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"

#include "Core/HLE/sceDisplay.h"
#include "Core/HW/Display.h"
#include "GPU/Debugger/State.h"
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

// Stub
void DrawGeDebuggerWindow(ImConfig &cfg, GPUDebugInterface *gpuDebug) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("GE Debugger", &cfg.geDebuggerOpen)) {
		ImGui::End();
		return;
	}

	if (ImGui::Button("Run/Resume")) {
		Core_Resume();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("Break:");
	ImGui::SameLine();
	if (ImGui::Button("Frame")) {
		GPUDebug::SetBreakNext(GPUDebug::BreakNext::FRAME);
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

	// Let's display the current CLUT.

	// First, let's list any active display lists.
	// ImGui::Text("Next list ID: %d", nextListID);

	for (auto index : gpuDebug->GetDisplayListQueue()) {
		const auto &list = gpuDebug->GetDisplayList(index);
		char title[64];
		snprintf(title, sizeof(title), "List %d", list.id);
		if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("PC: %08x (start: %08x)", list.pc, list.startpc);
			ImGui::Text("BBOX result: %d", (int)list.bboxResult);
		}
	}
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
		auto buildStateTab = [&](const char *tabName, const TabStateRow *rows, size_t numRows) {
			if (ImGui::BeginTabItem(tabName)) {
				if (ImGui::BeginTable("fpr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
					ImGui::TableSetupColumn("bkpt", ImGuiTableColumnFlags_WidthFixed);
					ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					for (size_t i = 0; i < numRows; i++) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::Text("-");  // breakpoint
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(rows[i].title.data(), rows[i].title.data() + rows[i].title.size());
						ImGui::TableNextColumn();
						char temp[256];
						auto &info = rows[i];

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

						static const char *colNames[] = {
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
