#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_internal.h"
#include "ext/imgui/imgui_impl_thin3d.h"
#include "Common/Data/Convert/ColorConv.h"
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

	if (framebufferManager) {
		framebufferManager->DrawImGuiDebug(cfg.selectedFramebuffer);
	} else {
		// Although technically, we could track them...
		ImGui::TextUnformatted("(Framebuffers not available in software mode)");
	}

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

ImGePixelViewer::~ImGePixelViewer() {
	texture_->Release();
}

void ImGePixelViewer::Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug, Draw::DrawContext *draw) {
	ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Pixel Viewer", &cfg.pixelViewerOpen)) {
		ImGui::End();
		return;
	}

	if (dirty_) {
		UpdateTexture(draw);
		dirty_ = false;
	}

	if (gpuDebug->GetFramebufferManagerCommon()) {
		if (gpuDebug->GetFramebufferManagerCommon()->GetVFBAt(addr_)) {
			ImGui::Text("NOTE: There's a hardware framebuffer at %08x.", addr_);
			// TODO: Add a button link.
		}
	}

	if (ImGui::BeginChild("left", ImVec2(200.0f, 0.0f))) {
		if (ImGui::InputScalar("Address", ImGuiDataType_U32, &addr_, 0, 0, "%08x")) {
			dirty_ = true;
		}

		if (ImGui::BeginCombo("Aspect", GeBufferFormatToString(format_))) {
			for (int i = 0; i < 5; i++) {
				if (ImGui::Selectable(GeBufferFormatToString((GEBufferFormat)i), i == (int)format_)) {
					format_ = (GEBufferFormat)i;
					dirty_ = true;
				}
			}
			ImGui::EndCombo();
		}

		bool alphaPresent = format_ == GE_FORMAT_8888 || format_ == GE_FORMAT_4444 || format_ == GE_FORMAT_5551;

		if (!alphaPresent) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Checkbox("Use alpha", &useAlpha_)) {
			dirty_ = true;
		}
		if (ImGui::Checkbox("Show alpha", &showAlpha_)) {
			dirty_ = true;
		}
		if (!alphaPresent) {
			ImGui::EndDisabled();
		}
		if (ImGui::InputScalar("Width", ImGuiDataType_U16, &width_)) {
			dirty_ = true;
		}
		if (ImGui::InputScalar("Height", ImGuiDataType_U16, &height_)) {
			dirty_ = true;
		}
		if (ImGui::InputScalar("Stride", ImGuiDataType_U16, &stride_)) {
			dirty_ = true;
		}
		if (format_ == GE_FORMAT_DEPTH16) {
			if (ImGui::SliderFloat("Scale", &scale_, 0.5f, 256.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
				dirty_ = true;
			}
		}
		if (ImGui::Button("Refresh")) {
			dirty_ = true;
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();
	if (ImGui::BeginChild("right")) {
		if (Memory::IsValid4AlignedAddress(addr_)) {
			if (texture_) {
				ImTextureID texId = ImGui_ImplThin3d_AddTextureTemp(texture_, useAlpha_ ? ImGuiPipeline::TexturedAlphaBlend : ImGuiPipeline::TexturedOpaque);
				ImGui::Image(texId, ImVec2((float)width_, (float)height_));
			} else {
				ImGui::Text("(invalid params: %dx%d, %08x)", width_, height_, addr_);
			}
		} else {
			ImGui::Text("(invalid address %08x)", addr_);
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void ImGePixelViewer::UpdateTexture(Draw::DrawContext *draw) {
	if (texture_) {
		texture_->Release();
		texture_ = nullptr;
	}
	if (!Memory::IsValid4AlignedAddress(addr_) || width_ == 0 || height_ == 0 || stride_ > 1024 || stride_ == 0) {
		INFO_LOG(Log::GeDebugger, "PixelViewer: Bad texture params");
		return;
	}

	int bpp = BufferFormatBytesPerPixel(format_);

	int srcBytes = width_ * stride_ * bpp;
	if (stride_ > width_)
		srcBytes -= stride_ - width_;
	if (Memory::ValidSize(addr_, srcBytes) != srcBytes) {
		// TODO: Show a message that the address is out of bounds.
		return;
	}

	// Read pixels into a buffer and transform them accordingly.
	// For now we convert all formats to RGBA here, for backend compatibility.
	uint8_t *buf = new uint8_t[width_ * height_ * 4];

	for (int y = 0; y < height_; y++) {
		u32 rowAddr = addr_ + y * stride_ * bpp;
		const u8 *src = Memory::GetPointerUnchecked(rowAddr);
		u8 *dst = buf + y * width_ * 4;
		switch (format_) {
		case GE_FORMAT_8888:
			if (showAlpha_) {
				for (int x = 0; x < width_; x++) {
					dst[0] = src[3];
					dst[1] = src[3];
					dst[2] = src[3];
					dst[3] = 0xFF;
					src += 4;
					dst += 4;
				}
			} else {
				memcpy(dst, src, width_ * 4);
			}
			break;
		case GE_FORMAT_565:
			// No showAlpha needed (would just be white)
			ConvertRGB565ToRGBA8888((u32 *)dst, (const u16 *)src, width_);
			break;
		case GE_FORMAT_5551:
			if (showAlpha_) {
				uint32_t *dst32 = (uint32_t *)dst;
				uint16_t *src16 = (uint16_t *)dst;
				for (int x = 0; x < width_; x++) {
					dst[x] = (src16[x] >> 15) ? 0xFFFFFFFF : 0xFF000000;
				}
			} else {
				ConvertRGBA5551ToRGBA8888((u32 *)dst, (const u16 *)src, width_);
			}
			break;
		case GE_FORMAT_4444:
			ConvertRGBA4444ToRGBA8888((u32 *)dst, (const u16 *)src, width_);
			break;
		case GE_FORMAT_DEPTH16:
		{
			uint16_t *src16 = (uint16_t *)src;
			float scale = scale_ / 256.0f;
			for (int x = 0; x < width_; x++) {
				// Just pick off the upper bits by adding 1 to the byte address
				// We don't visualize the lower bits for now, although we could - should add a scale slider like RenderDoc.
				float fval = (float)src16[x] * scale;
				u8 val;
				if (fval < 0.0f) {
					val = 0;
				} else if (fval >= 255.0f) {
					val = 255;
				} else {
					val = (u8)fval;
				}
				dst[0] = val;
				dst[1] = val;
				dst[2] = val;
				dst[3] = 0xFF;
				dst += 4;
			}
			break;
		}
		default:
			memset(buf, 0x80, width_ * height_ * 4);
			break;
		}
	}

	Draw::TextureDesc desc{ Draw::TextureType::LINEAR2D,
		Draw::DataFormat::R8G8B8A8_UNORM,
		(int)width_,
		(int)height_,
		1,
		1,
		false,
		Draw::TextureSwizzle::DEFAULT,
		"PixelViewer temp",
		{ buf },
		nullptr,
	};

	texture_ = draw->CreateTexture(desc);
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

	if (pc != 0xFFFFFFFF && gotoPC_) {
		selectedAddr_ = pc;
		gotoPC_ = false;
	}

	float pcY = canvas_p0.y + ((pc - windowStartAddr) / instrWidth) * lineHeight;
	draw_list->AddRectFilled(ImVec2(canvas_p0.x, pcY), ImVec2(canvas_p1.x, pcY + lineHeight), IM_COL32(0x10, 0x70, 0x10, 255));
	float stallY = canvas_p0.y + ((stallAddr - windowStartAddr) / instrWidth) * lineHeight;
	draw_list->AddRectFilled(ImVec2(canvas_p0.x, stallY), ImVec2(canvas_p1.x, stallY + lineHeight), IM_COL32(0x70, 0x20, 0x10, 255));
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

			u32 opcode = Memory::Read_U32(addr);
			GPUDebugOp op = gpuDebug->DisassembleOp(addr, opcode);
			u32 color = 0xFFFFFFFF;
			char temp[16];
			snprintf(temp, sizeof(temp), "%08x", op.op);
			draw_list->AddText(opcodeStart, color, temp);
			draw_list->AddText(descStart, color, op.desc.data(), op.desc.data() + op.desc.size());
			// if (selectedAddr_ == addr && strlen(disMeta.liveInfo)) {
			// 	draw_list->AddText(liveStart, 0xFFFFFFFF, disMeta.liveInfo);
			// }

			bool bp = gpuDebug->GetBreakpoints()->IsAddressBreakpoint(addr);
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
			if (!gpuDebug->GetBreakpoints()->IsAddressBreakpoint(dragAddr_)) {
				gpuDebug->GetBreakpoints()->AddAddressBreakpoint(dragAddr_);
			} else {
				gpuDebug->GetBreakpoints()->RemoveAddressBreakpoint(dragAddr_);
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
				gpuDebug->GetBreakpoints()->RemoveAddressBreakpoint(dragAddr_);
			}
		} else if (Memory::IsValid4AlignedAddress(dragAddr_)) {
			char buffer[64];
			u32 opcode = Memory::Read_U32(dragAddr_);
			GPUDebugOp op = gpuDebug->DisassembleOp(dragAddr_, opcode);
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

static void DrawPreviewPrimitive(ImDrawList *drawList, ImVec2 p0, GEPrimitiveType prim, const std::vector<u16> &indices, const std::vector<GPUDebugVertex> &verts, int count, bool uvToPos, float sx = 1.0f, float sy = 1.0f) {
	if (count) {
		auto x = [sx, uvToPos](const GPUDebugVertex &vert) {
			return sx * (uvToPos ? vert.u : vert.x);
		};
		auto y = [sy, uvToPos](const GPUDebugVertex &vert) {
			return sy * (uvToPos ? vert.v : vert.y);
		};

		// TODO: Maybe not the best idea to use the heavy AddTriangleFilled API instead of just adding raw triangles.
		switch (prim) {
		case GE_PRIM_TRIANGLES:
		case GE_PRIM_RECTANGLES:
		{
			for (int i = 0; i < count - 2; i += 3) {
				const auto &v1 = indices.empty() ? verts[i] : verts[indices[i]];
				const auto &v2 = indices.empty() ? verts[i + 1] : verts[indices[i + 1]];
				const auto &v3 = indices.empty() ? verts[i + 2] : verts[indices[i + 2]];
				drawList->AddTriangleFilled(
					ImVec2(p0.x + x(v1), p0.y + y(v1)),
					ImVec2(p0.x + x(v2), p0.y + y(v2)),
					ImVec2(p0.x + x(v3), p0.y + y(v3)), ImColor(0x600000FF));
			}
			break;
		}
		case GE_PRIM_TRIANGLE_FAN:
		{
			for (int i = 0; i < count - 2; i++) {
				const auto &v1 = indices.empty() ? verts[0] : verts[indices[0]];
				const auto &v2 = indices.empty() ? verts[i + 1] : verts[indices[i + 1]];
				const auto &v3 = indices.empty() ? verts[i + 2] : verts[indices[i + 2]];
				drawList->AddTriangleFilled(
					ImVec2(p0.x + x(v1), p0.y + y(v1)),
					ImVec2(p0.x + x(v2), p0.y + y(v2)),
					ImVec2(p0.x + x(v3), p0.y + y(v3)), ImColor(0x600000FF));
			}
			break;
		}
		case GE_PRIM_TRIANGLE_STRIP:
		{
			int t = 2;
			for (int i = 0; i < count - 2; i++) {
				int i0 = i;
				int i1 = i + t;
				int i2 = i + (t ^ 3);
				const auto &v1 = indices.empty() ? verts[i0] : verts[indices[i0]];
				const auto &v2 = indices.empty() ? verts[i1] : verts[indices[i1]];
				const auto &v3 = indices.empty() ? verts[i2] : verts[indices[i2]];
				drawList->AddTriangleFilled(
					ImVec2(p0.x + x(v1), p0.y + y(v1)),
					ImVec2(p0.x + x(v2), p0.y + y(v2)),
					ImVec2(p0.x + x(v3), p0.y + y(v3)), ImColor(0x600000FF));
				t ^= 3;
			}
			break;
		}
		case GE_PRIM_LINES:
		{
			for (int i = 0; i < count - 1; i += 2) {
				const auto &v1 = indices.empty() ? verts[i] : verts[indices[i]];
				const auto &v2 = indices.empty() ? verts[i + 1] : verts[indices[i + 1]];
				drawList->AddLine(
					ImVec2(p0.x + x(v1), p0.y + y(v1)),
					ImVec2(p0.x + x(v2), p0.y + y(v2)), ImColor(0x600000FF));
			}
			break;
		}
		case GE_PRIM_LINE_STRIP:
		{
			for (int i = 0; i < count - 2; i++) {
				const auto &v1 = indices.empty() ? verts[i] : verts[indices[i]];
				const auto &v2 = indices.empty() ? verts[i + 1] : verts[indices[i + 1]];
				drawList->AddLine(
					ImVec2(p0.x + x(v1), p0.y + y(v1)),
					ImVec2(p0.x + x(v2), p0.y + y(v2)), ImColor(0x600000FF));
			}
			break;
		}
		default:
			// TODO: Support lines etc.
			break;
		}
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

	bool disableStepButtons = gpuDebug->GetBreakNext() != GPUDebug::BreakNext::NONE;

	if (disableStepButtons) {
		ImGui::BeginDisabled();
	}
	ImGui::SameLine();
	if (ImGui::Button("Tex")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::TEX);
	}
	ImGui::SameLine();
	if (ImGui::Button("NonTex")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::NONTEX);
	}
	ImGui::SameLine();
	if (ImGui::Button("Prim")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::PRIM);
	}
	ImGui::SameLine();
	if (ImGui::Button("Draw")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::DRAW);
	}
	ImGui::SameLine();
	if (ImGui::Button("Curve")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::CURVE);
	}
	ImGui::SameLine();
	if (ImGui::Button("Single step")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::OP);
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
	if (gpuDebug->GetBreakNext() != GPUDebug::BreakNext::NONE) {
		if (showBannerInFrames_ > 0) {
			showBannerInFrames_--;
		}
		if (showBannerInFrames_ == 0) {
			ImGui::Text("Step pending (waiting for CPU): %s", GPUDebug::BreakNextToString(gpuDebug->GetBreakNext()));
			ImGui::SameLine();
			if (ImGui::Button("Cancel step")) {
				gpuDebug->ClearBreakNext();
			}
		}
	} else {
		showBannerInFrames_ = 2;
	}

	// First, let's list any active display lists in the left column, on top of the disassembly.

	ImGui::BeginChild("left pane", ImVec2(400, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

	if (ImGui::CollapsingHeader("Display lists")) {
		for (auto index : gpuDebug->GetDisplayListQueue()) {
			const auto &list = gpuDebug->GetDisplayList(index);
			char title[64];
			snprintf(title, sizeof(title), "List %d", list.id);
			if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
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
				if (list.stall) {
					ImGui::TextUnformatted("Stall addr:");
					ImGui::SameLine();
					ImClickableAddress(list.pc, control, ImCmd::SHOW_IN_GE_DISASM);
				}
				ImGui::Text("Stack depth: %d", (int)list.stackptr);
				ImGui::Text("BBOX result: %d", (int)list.bboxResult);
			}
		}
	}

	// Display the disassembly view.
	disasmView_.Draw(gpuDebug);

	ImGui::EndChild();

	ImGui::SameLine();

	u32 op = 0;
	DisplayList list;
	if (gpuDebug->GetCurrentDisplayList(list)) {
		op = Memory::Read_U32(list.pc);

		// TODO: Also add support for block transfer previews!

		bool isOnPrim = (op >> 24) == GE_CMD_PRIM;
		if (isOnPrim) {
			if (reloadPreview_) {
				GetPrimPreview(op, previewPrim_, previewVertices_, previewIndices_, previewCount_);
				reloadPreview_ = false;
			}
		} else {
			previewCount_ = 0;
		}
	}

	ImGui::BeginChild("texture/fb view"); // Leave room for 1 line below us

	ImDrawList *drawList = ImGui::GetWindowDrawList();

	if (coreState == CORE_STEPPING_GE) {
		FramebufferManagerCommon *fbman = gpuDebug->GetFramebufferManagerCommon();
		u32 fbptr = gstate.getFrameBufAddress();
		VirtualFramebuffer *vfb = fbman ? fbman->GetVFBAt(fbptr) : nullptr;
		if (vfb) {
			if (vfb->fbo) {
				ImGui::Text("Framebuffer: %s", vfb->fbo->Tag());
			} else {
				ImGui::Text("Framebuffer");
			}

			if (ImGui::BeginTabBar("aspects")) {
				if (ImGui::BeginTabItem("Color")) {
					ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(vfb->fbo, Draw::FB_COLOR_BIT, ImGuiPipeline::TexturedOpaque);
					const ImVec2 p0 = ImGui::GetCursorScreenPos();
					const ImVec2 p1 = ImVec2(p0.x + vfb->width, p0.y + vfb->height);

					// Draw border and background color
					drawList->PushClipRect(p0, p1, true);

					ImGui::Image(texId, ImVec2(vfb->width, vfb->height));

					// Draw vertex preview on top!
					DrawPreviewPrimitive(drawList, p0, previewPrim_, previewIndices_, previewVertices_, previewCount_, false);

					drawList->PopClipRect();

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

		if (gstate.isModeClear()) {
			ImGui::Text("(clear mode - texturing not used)");
		} else if (!gstate.isTextureMapEnabled()) {
			ImGui::Text("(texturing not enabled");
		} else {
			TextureCacheCommon *texcache = gpuDebug->GetTextureCacheCommon();
			TexCacheEntry *tex = texcache ? texcache->SetTexture() : nullptr;
			if (tex) {
				ImGui::Text("Texture: ");
				texcache->ApplyTexture();

				void *nativeView = texcache->GetNativeTextureView(tex, true);
				ImTextureID texId = ImGui_ImplThin3d_AddNativeTextureTemp(nativeView);

				float texW = dimWidth(tex->dim);
				float texH = dimHeight(tex->dim);

				const ImVec2 p0 = ImGui::GetCursorScreenPos();
				const ImVec2 sz = ImGui::GetContentRegionAvail();
				const ImVec2 p1 = ImVec2(p0.x + texW, p0.y + texH);

				// Draw border and background color
				drawList->PushClipRect(p0, p1, true);

				ImGui::Image(texId, ImVec2(texW, texH));
				DrawPreviewPrimitive(drawList, p0, previewPrim_, previewIndices_, previewVertices_, previewCount_, true, texW, texH);

				drawList->PopClipRect();
			} else {
				ImGui::Text("(no valid texture bound)");
				// In software mode, we should just decode the texture here.
				// TODO: List some of the texture params here.
			}
		}

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
	// Not needed for now, we have GPUStepping::LastState()
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
						const GPUgstate &lastState = GPUStepping::LastState();
						bool diff = lastState.cmdmem[rows[i].cmd] != gstate.cmdmem[rows[i].cmd];

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
						if (diff) {
							ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImDebuggerColor_Diff : ImDebuggerColor_DiffAlpha);
						} else if (!enabled) {
							ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 128));
						}
						if (!rows[i].header) {
							ImGui::TextUnformatted(info.uiName);
							ImGui::TableNextColumn();
						}
						if (rows[i].cmd != GE_CMD_NOP) {
							char temp[128], temp2[128];

							const u32 value = gstate.cmdmem[info.cmd];
							const u32 otherValue = gstate.cmdmem[info.otherCmd];

							// Special handling for pointer and pointer/width entries - create an address control
							if (info.fmt == CMD_FMT_PTRWIDTH) {
								const u32 val = (value & 0xFFFFFF) | (otherValue & 0x00FF0000) << 8;
								ImClickableAddress(val, control, ImCmd::NONE);
								ImGui::SameLine();
								ImGui::Text("w=%d", otherValue & 0xFFFF);
							} else {
								FormatStateRow(gpuDebug, temp, sizeof(temp), info.fmt, value, true, otherValue, gstate.cmdmem[info.otherCmd2]);
								ImGui::TextUnformatted(temp);
							}
							if (diff && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
								FormatStateRow(gpuDebug, temp2, sizeof(temp2), info.fmt, lastState.cmdmem[info.cmd], true, lastState.cmdmem[info.otherCmd], lastState.cmdmem[info.otherCmd2]);
								ImGui::SetTooltip("Previous: %s", temp2);
							}
						}
						if (diff || !enabled)
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
