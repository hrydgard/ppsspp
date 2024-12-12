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
#include "GPU/Debugger/Stepping.h"
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

void ImGeDisasmView::NotifyStep() {
	if (followPC_) {
		gotoPC_ = true;
	}
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
		if (gotoPC_) {
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

static const char *DLStateToString(DisplayListState state) {
	switch (state) {
	case PSP_GE_DL_STATE_NONE: return "None";
	case PSP_GE_DL_STATE_QUEUED: return "Queued";
	case PSP_GE_DL_STATE_RUNNING: return "Running";
	case PSP_GE_DL_STATE_COMPLETED: return "Completed";
	case PSP_GE_DL_STATE_PAUSED: return "Paused";
	default: return "N/A (bad)";
	}
}

void ImGeDebuggerWindow::Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(Title(), &cfg.geDebuggerOpen)) {
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
		if (showBannerInFrames_ > 0) {
			showBannerInFrames_--;
		}
		if (showBannerInFrames_ == 0) {
			ImGui::Text("Step pending (waiting for CPU): %s", GPUDebug::BreakNextToString(GPUDebug::GetBreakNext()));
			ImGui::SameLine();
			if (ImGui::Button("Cancel step")) {
				GPUDebug::SetBreakNext(GPUDebug::BreakNext::NONE);
			}
		}
	} else {
		showBannerInFrames_ = 2;
	}

	// First, let's list any active display lists in the left column, on top of the disassembly.

	ImGui::BeginChild("left pane", ImVec2(400, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

	for (auto index : gpuDebug->GetDisplayListQueue()) {
		const auto &list = gpuDebug->GetDisplayList(index);
		char title[64];
		snprintf(title, sizeof(title), "List %d", list.id);
		if (ImGui::CollapsingHeader(title)) {
			ImGui::Text("State: %s", DLStateToString(list.state));
			ImGui::TextUnformatted("PC:");
			ImGui::SameLine();
			ImClickableAddress(list.pc, control, ImCmd::SHOW_IN_GE_DISASM);
			ImGui::Text("StartPC:");
			ImGui::SameLine();
			ImClickableAddress(list.startpc, control, ImCmd::SHOW_IN_GE_DISASM);
			if (list.pendingInterrupt) {
				ImGui::TextUnformatted("(Pending interrupt)");
			}
			ImGui::Text("Stack depth: %d", (int)list.stackptr);
			ImGui::Text("BBOX result: %d", (int)list.bboxResult);
		}
	}

	// Display the disassembly view.
	disasmView_.Draw(gpuDebug);

	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("texture/fb view"); // Leave room for 1 line below us

	if (coreState == CORE_STEPPING_GE) {
		FramebufferManagerCommon *fbman = gpuDebug->GetFramebufferManagerCommon();
		u32 fbptr = gstate.getFrameBufAddress();
		VirtualFramebuffer *vfb = fbman->GetVFBAt(fbptr);

		if (vfb) {
			if (vfb->fbo) {
				ImGui::Text("Framebuffer: %s", vfb->fbo->Tag());
			} else {
				ImGui::Text("Framebuffer");
			}

			if (ImGui::BeginTabBar("aspects")) {
				if (ImGui::BeginTabItem("Color")) {
					ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(vfb->fbo, Draw::FB_COLOR_BIT, ImGuiPipeline::TexturedOpaque);
					ImGui::Image(texId, ImVec2(vfb->width, vfb->height));
					// TODO: Draw vertex preview on top!
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Depth")) {
					ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(vfb->fbo, Draw::FB_DEPTH_BIT, ImGuiPipeline::TexturedOpaque);
					ImGui::Image(texId, ImVec2(vfb->width, vfb->height));
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Stencil")) {
					// Nah, this isn't gonna work. We better just do a readback to texture, but then we need a message and some storage..
					//
					//ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(vfb->fbo, Draw::FB_STENCIL_BIT, ImGuiPipeline::TexturedOpaque);
					//ImGui::Image(texId, ImVec2(vfb->width, vfb->height));
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

			ImGui::Text("%dx%d (emulated: %dx%d)", vfb->width, vfb->height, vfb->bufferWidth, vfb->bufferHeight);
		}

		ImGui::Text("Texture: ");

		TextureCacheCommon *texcache = gpuDebug->GetTextureCacheCommon();
		TexCacheEntry *tex = texcache->SetTexture();
		texcache->ApplyTexture();

		void *nativeView = texcache->GetNativeTextureView(tex, true);
		ImTextureID texId = ImGui_ImplThin3d_AddNativeTextureTemp(nativeView);

		ImGui::Image(texId, ImVec2(128, 128));

		// Let's display the current CLUT.

	} else {
		ImGui::Text("Click the buttons above (Tex, etc) to stop");
	}

	ImGui::EndChild();

	ImGui::End();
}

struct StateItem {
	bool header; GECommand cmd; const char *title; bool closedByDefault;
};

static const StateItem g_rasterState[] = {
	{true, GE_CMD_NOP, "Framebuffer"},
	{false, GE_CMD_FRAMEBUFPTR},
	{false, GE_CMD_FRAMEBUFPIXFORMAT},
	{false, GE_CMD_CLEARMODE},

	{true, GE_CMD_ZTESTENABLE},
	{false, GE_CMD_ZBUFPTR},
	{false, GE_CMD_ZTEST},
	{false, GE_CMD_ZWRITEDISABLE},

	{true, GE_CMD_STENCILTESTENABLE},
	{false, GE_CMD_STENCILTEST},
	{false, GE_CMD_STENCILOP},

	{true, GE_CMD_ALPHABLENDENABLE},
	{false, GE_CMD_BLENDMODE},
	{false, GE_CMD_BLENDFIXEDA},
	{false, GE_CMD_BLENDFIXEDB},

	{true, GE_CMD_ALPHATESTENABLE},
	{false, GE_CMD_ALPHATEST},

	{true, GE_CMD_COLORTESTENABLE},
	{false, GE_CMD_COLORTEST},
	{false, GE_CMD_COLORTESTMASK},

	{true, GE_CMD_FOGENABLE},
	{false, GE_CMD_FOGCOLOR},
	{false, GE_CMD_FOG1},
	{false, GE_CMD_FOG2},

	{true, GE_CMD_CULLFACEENABLE},
	{false, GE_CMD_CULL},

	{true, GE_CMD_LOGICOPENABLE},
	{false, GE_CMD_LOGICOP},

	{true, GE_CMD_NOP, "Clipping/Clamping"},
	{false, GE_CMD_MINZ},
	{false, GE_CMD_MAXZ},
	{false, GE_CMD_DEPTHCLAMPENABLE},

	{true, GE_CMD_NOP, "Other raster state"},
	{false, GE_CMD_MASKRGB},
	{false, GE_CMD_MASKALPHA},
	{false, GE_CMD_SCISSOR1},
	{false, GE_CMD_REGION1},
	{false, GE_CMD_OFFSETX},
	{false, GE_CMD_DITH0},
	{false, GE_CMD_DITH1},
	{false, GE_CMD_DITH2},
	{false, GE_CMD_DITH3},
};

static const StateItem g_textureState[] = {
	{true, GE_CMD_TEXTUREMAPENABLE},
	{false, GE_CMD_TEXADDR0},
	{false, GE_CMD_TEXSIZE0},
	{false, GE_CMD_TEXENVCOLOR},
	{false, GE_CMD_TEXMAPMODE},
	{false, GE_CMD_TEXSHADELS},
	{false, GE_CMD_TEXFORMAT},
	{false, GE_CMD_CLUTFORMAT},
	{false, GE_CMD_TEXFILTER},
	{false, GE_CMD_TEXWRAP},
	{false, GE_CMD_TEXLEVEL},
	{false, GE_CMD_TEXFUNC},
	{false, GE_CMD_TEXLODSLOPE},

	{false, GE_CMD_TEXSCALEU},
	{false, GE_CMD_TEXSCALEV},
	{false, GE_CMD_TEXOFFSETU},
	{false, GE_CMD_TEXOFFSETV},

	{true, GE_CMD_NOP, "Additional mips", true},
	{false, GE_CMD_TEXADDR1},
	{false, GE_CMD_TEXADDR2},
	{false, GE_CMD_TEXADDR3},
	{false, GE_CMD_TEXADDR4},
	{false, GE_CMD_TEXADDR5},
	{false, GE_CMD_TEXADDR6},
	{false, GE_CMD_TEXADDR7},
	{false, GE_CMD_TEXSIZE1},
	{false, GE_CMD_TEXSIZE2},
	{false, GE_CMD_TEXSIZE3},
	{false, GE_CMD_TEXSIZE4},
	{false, GE_CMD_TEXSIZE5},
	{false, GE_CMD_TEXSIZE6},
	{false, GE_CMD_TEXSIZE7},
};

static const StateItem g_lightingState[] = {
	{false, GE_CMD_AMBIENTCOLOR},
	{false, GE_CMD_AMBIENTALPHA},
	{false, GE_CMD_MATERIALUPDATE},
	{false, GE_CMD_MATERIALEMISSIVE},
	{false, GE_CMD_MATERIALAMBIENT},
	{false, GE_CMD_MATERIALDIFFUSE},
	{false, GE_CMD_MATERIALALPHA},
	{false, GE_CMD_MATERIALSPECULAR},
	{false, GE_CMD_MATERIALSPECULARCOEF},
	{false, GE_CMD_REVERSENORMAL},
	{false, GE_CMD_SHADEMODE},
	{false, GE_CMD_LIGHTMODE},
	{false, GE_CMD_LIGHTTYPE0},
	{false, GE_CMD_LIGHTTYPE1},
	{false, GE_CMD_LIGHTTYPE2},
	{false, GE_CMD_LIGHTTYPE3},
	{false, GE_CMD_LX0},
	{false, GE_CMD_LX1},
	{false, GE_CMD_LX2},
	{false, GE_CMD_LX3},
	{false, GE_CMD_LDX0},
	{false, GE_CMD_LDX1},
	{false, GE_CMD_LDX2},
	{false, GE_CMD_LDX3},
	{false, GE_CMD_LKA0},
	{false, GE_CMD_LKA1},
	{false, GE_CMD_LKA2},
	{false, GE_CMD_LKA3},
	{false, GE_CMD_LKS0},
	{false, GE_CMD_LKS1},
	{false, GE_CMD_LKS2},
	{false, GE_CMD_LKS3},
	{false, GE_CMD_LKO0},
	{false, GE_CMD_LKO1},
	{false, GE_CMD_LKO2},
	{false, GE_CMD_LKO3},
	{false, GE_CMD_LAC0},
	{false, GE_CMD_LDC0},
	{false, GE_CMD_LSC0},
	{false, GE_CMD_LAC1},
	{false, GE_CMD_LDC1},
	{false, GE_CMD_LSC1},
	{false, GE_CMD_LAC2},
	{false, GE_CMD_LDC2},
	{false, GE_CMD_LSC2},
	{false, GE_CMD_LAC3},
	{false, GE_CMD_LDC3},
	{false, GE_CMD_LSC3},
};

static const StateItem g_vertexState[] = {
	{true, GE_CMD_NOP, "Vertex type and transform"},
	{false, GE_CMD_VERTEXTYPE},
	{false, GE_CMD_VADDR},
	{false, GE_CMD_IADDR},
	{false, GE_CMD_OFFSETADDR},
	{false, GE_CMD_VIEWPORTXSCALE},
	{false, GE_CMD_VIEWPORTXCENTER},
	{false, GE_CMD_MORPHWEIGHT0},
	{false, GE_CMD_MORPHWEIGHT1},
	{false, GE_CMD_MORPHWEIGHT2},
	{false, GE_CMD_MORPHWEIGHT3},
	{false, GE_CMD_MORPHWEIGHT4},
	{false, GE_CMD_MORPHWEIGHT5},
	{false, GE_CMD_MORPHWEIGHT6},
	{false, GE_CMD_MORPHWEIGHT7},
	{false, GE_CMD_TEXSCALEU},
	{false, GE_CMD_TEXSCALEV},
	{false, GE_CMD_TEXOFFSETU},
	{false, GE_CMD_TEXOFFSETV},

	{true, GE_CMD_NOP, "Tessellation"},
	{false, GE_CMD_PATCHPRIMITIVE},
	{false, GE_CMD_PATCHDIVISION},
	{false, GE_CMD_PATCHCULLENABLE},
	{false, GE_CMD_PATCHFACING},
};

void ImGeStateWindow::Snapshot() {

}

// TODO: Separate window or merge into Ge debugger?
void ImGeStateWindow::Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug) {
	ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("GE State", &cfg.geStateOpen)) {
		ImGui::End();
		return;
	}
	if (ImGui::BeginTabBar("GeRegs", ImGuiTabBarFlags_None)) {
		auto buildStateTab = [&](const char *tabName, const StateItem *rows, size_t numRows) {
			if (ImGui::BeginTabItem(tabName)) {
				if (ImGui::BeginTable("fpr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
					ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("bkpt", ImGuiTableColumnFlags_WidthFixed);
					ImGui::TableHeadersRow();

					bool anySection = false;
					bool sectionOpen = false;
					for (size_t i = 0; i < numRows; i++) {
						const GECmdInfo &info = GECmdInfoByCmd(rows[i].cmd);

						if (rows[i].header) {
							anySection = true;
							if (sectionOpen) {
								ImGui::TreePop();
							}
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							sectionOpen = ImGui::TreeNodeEx(rows[i].cmd ? info.uiName : rows[i].title, rows[i].closedByDefault ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
							ImGui::TableNextColumn();
						} else {
							if (!sectionOpen && anySection) {
								continue;
							}
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
						}

						const bool enabled = info.enableCmd == 0 || (gstate.cmdmem[info.enableCmd] & 1) == 1;
						if (!enabled)
							ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 128));
						if (!rows[i].header) {
							ImGui::TextUnformatted(info.uiName);
							ImGui::TableNextColumn();
						}
						if (rows[i].cmd != GE_CMD_NOP) {
							char temp[256];

							const u32 value = gstate.cmdmem[info.cmd] & 0xFFFFFF;
							const u32 otherValue = gstate.cmdmem[info.otherCmd] & 0xFFFFFF;
							const u32 otherValue2 = gstate.cmdmem[info.otherCmd2] & 0xFFFFFF;

							// Special handling for pointer and pointer/width entries
							if (info.fmt == CMD_FMT_PTRWIDTH) {
								const u32 val = value | (otherValue & 0x00FF0000) << 8;
								ImClickableAddress(val, control, ImCmd::NONE);
								ImGui::SameLine();
								ImGui::Text("w=%d", otherValue & 0xFFFF);
							} else {
								FormatStateRow(gpuDebug, temp, sizeof(temp), info.fmt, value, true, otherValue, otherValue2);
								ImGui::TextUnformatted(temp);
							}
						}
						if (!enabled)
							ImGui::PopStyleColor();
					}
					if (sectionOpen) {
						ImGui::TreePop();
					}

					ImGui::EndTable();
				}
				ImGui::EndTabItem();
			}
		};

		buildStateTab("Raster", g_rasterState, ARRAY_SIZE(g_rasterState));
		buildStateTab("Texture", g_textureState, ARRAY_SIZE(g_textureState));
		buildStateTab("Lighting", g_lightingState, ARRAY_SIZE(g_lightingState));
		buildStateTab("Transform/Tess", g_vertexState, ARRAY_SIZE(g_vertexState));

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
