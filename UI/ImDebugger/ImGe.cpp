#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_internal.h"
#include "ext/imgui/imgui_extras.h"
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

	if (!framebufferManager) {
		ImGui::TextUnformatted("(Framebuffers not available in software mode)");
		ImGui::End();
		return;
	}

	ImGui::BeginTable("framebuffers", 4);
	ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Color Addr", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Depth Addr", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);

	ImGui::TableHeadersRow();

	const std::vector<VirtualFramebuffer *> &vfbs = framebufferManager->GetVFBs();

	for (int i = 0; i < (int)vfbs.size(); i++) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		auto &vfb = vfbs[i];

		const char *tag = vfb->fbo ? vfb->fbo->Tag() : "(no tag)";

		ImGui::PushID(i);
		if (ImGui::Selectable(tag, cfg.selectedFramebuffer == i, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
			cfg.selectedFramebuffer = i;
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			cfg.selectedFramebuffer = i;
			ImGui::OpenPopup("framebufferPopup");
		}
		ImGui::TableNextColumn();
		ImGui::Text("%08x", vfb->fb_address);
		ImGui::TableNextColumn();
		ImGui::Text("%08x", vfb->z_address);
		ImGui::TableNextColumn();
		ImGui::Text("%dx%d", vfb->width, vfb->height);
		if (ImGui::BeginPopup("framebufferPopup")) {
			ImGui::Text("Framebuffer: %s", tag);
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndTable();

	// Fix out-of-bounds issues when framebuffers are removed.
	if (cfg.selectedFramebuffer >= vfbs.size()) {
		cfg.selectedFramebuffer = -1;
	}

	if (cfg.selectedFramebuffer != -1) {
		ImGui::SliderFloat("Scale", &cfg.fbViewerZoom, 0.5f, 16.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

		// Now, draw the image of the selected framebuffer.
		Draw::Framebuffer *fb = vfbs[cfg.selectedFramebuffer]->fbo;
		ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(fb, Draw::Aspect::COLOR_BIT, ImGuiPipeline::TexturedOpaque);
		ImGui::Image(texId, ImVec2(fb->Width() * cfg.fbViewerZoom, fb->Height() * cfg.fbViewerZoom));
	}

	ImGui::End();
}

void DrawTexturesWindow(ImConfig &cfg, TextureCacheCommon *textureCache) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Textures", &cfg.texturesOpen)) {
		ImGui::End();
		return;
	}

	if (!textureCache) {
		ImGui::TextUnformatted("Texture cache not available");
		ImGui::End();
		return;
	}

	ImVec2 avail = ImGui::GetContentRegionAvail();
	auto &style = ImGui::GetStyle();
	ImGui::BeginChild("left", ImVec2(140.0f, 0.0f), ImGuiChildFlags_ResizeX);
	float window_visible_x2 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

	// Global texture stats
	int replacementStateCounts[(int)ReplacementState::COUNT]{};
	if (!textureCache->SecondCache().empty()) {
		ImGui::Text("Primary Cache");
	}

	for (auto &iter : textureCache->Cache()) {
		u64 id = iter.first;
		const TexCacheEntry *entry = iter.second.get();
		void *nativeView = textureCache->GetNativeTextureView(iter.second.get(), true);
		int w = 128;
		int h = 128;

		if (entry->replacedTexture) {
			replacementStateCounts[(int)entry->replacedTexture->State()]++;
		}

		ImTextureID texId = ImGui_ImplThin3d_AddNativeTextureTemp(nativeView);
		float last_button_x2 = ImGui::GetItemRectMax().x;
		float next_button_x2 = last_button_x2 + style.ItemSpacing.x + w; // Expected position if next button was on same line
		if (next_button_x2 < window_visible_x2)
			ImGui::SameLine();

		float x = ImGui::GetCursorPosX();
		if (ImGui::Selectable(("##Image" + std::to_string(id)).c_str(), cfg.selectedTexAddr == id, 0, ImVec2(w, h))) {
			cfg.selectedTexAddr = id; // Update the selected index if clicked
		}

		ImGui::SameLine();
		ImGui::SetCursorPosX(x + 2.0f);
		ImGui::Image(texId, ImVec2(128, 128));
	}

	if (!textureCache->SecondCache().empty()) {
		ImGui::Text("Secondary Cache (%d): TODO", (int)textureCache->SecondCache().size());
		// TODO
	}

	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("right", ImVec2(0.f, 0.0f));
	if (ImGui::CollapsingHeader("Texture", nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
		if (cfg.selectedTexAddr) {
			auto iter = textureCache->Cache().find(cfg.selectedTexAddr);
			if (iter != textureCache->Cache().end()) {
				void *nativeView = textureCache->GetNativeTextureView(iter->second.get(), true);
				ImTextureID texId = ImGui_ImplThin3d_AddNativeTextureTemp(nativeView);
				const TexCacheEntry *entry = iter->second.get();
				int dim = entry->dim;
				int w = dimWidth(dim);
				int h = dimHeight(dim);
				ImGui::Image(texId, ImVec2(w, h));
				ImGui::Text("%08x: %dx%d, %d mips, %s", (uint32_t)(cfg.selectedTexAddr & 0xFFFFFFFF), w, h, entry->maxLevel + 1, GeTextureFormatToString((GETextureFormat)entry->format));
				ImGui::Text("Stride: %d", entry->bufw);
				ImGui::Text("Status: %08x", entry->status);  // TODO: Show the flags
				ImGui::Text("Hash: %08x", entry->fullhash);
				ImGui::Text("CLUT Hash: %08x", entry->cluthash);
				ImGui::Text("Minihash: %08x", entry->minihash);
				ImGui::Text("MaxSeenV: %08x", entry->maxSeenV);
				if (entry->replacedTexture) {
					if (ImGui::CollapsingHeader("Replacement", ImGuiTreeNodeFlags_DefaultOpen)) {
						const auto &desc = entry->replacedTexture->Desc();
						ImGui::Text("State: %s", StateString(entry->replacedTexture->State()));
						// ImGui::Text("Original: %dx%d (%dx%d)", desc.w, desc.h, desc.newW, desc.newH);
						if (entry->replacedTexture->State() == ReplacementState::ACTIVE) {
							int w, h;
							entry->replacedTexture->GetSize(0, &w, &h);
							int numLevels = entry->replacedTexture->NumLevels();
							ImGui::Text("Replaced: %dx%d, %d mip levels", w, h, numLevels);
							ImGui::Text("Level 0 size: %d bytes, format: %s", entry->replacedTexture->GetLevelDataSizeAfterCopy(0), Draw::DataFormatToString(entry->replacedTexture->Format()));
						}
						ImGui::Text("Key: %08x_%08x", (u32)(desc.cachekey >> 32), (u32)desc.cachekey);
						ImGui::Text("Hashfiles: %s", desc.hashfiles.c_str());
						ImGui::Text("Base: %s", desc.basePath.c_str());
						ImGui::Text("Alpha status: %02x", entry->replacedTexture->AlphaStatus());
					}
				} else {
					ImGui::Text("Not replaced");
				}
				ImGui::Text("Frames until next full hash: %08x", entry->framesUntilNextFullHash);  // TODO: Show the flags
			} else {
				cfg.selectedTexAddr = 0;
			}
		} else {
			ImGui::Text("(no texture selected)");
		}
	}

	if (ImGui::CollapsingHeader("Texture Cache State", nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Cache: %d textures, size est %d", (int)textureCache->Cache().size(), (int)textureCache->CacheSizeEstimate());
		if (!textureCache->SecondCache().empty()) {
			ImGui::Text("Second: %d textures, size est %d", (int)textureCache->SecondCache().size(), (int)textureCache->SecondCacheSizeEstimate());
		}
		/*
		ImGui::Text("Standard/shader scale factor: %d/%d", standardScaleFactor_, shaderScaleFactor_);
		ImGui::Text("Texels scaled this frame: %d", texelsScaledThisFrame_);
		ImGui::Text("Low memory mode: %d", (int)lowMemoryMode_);
		*/
		if (ImGui::CollapsingHeader("Texture Replacement", ImGuiTreeNodeFlags_DefaultOpen)) {
			// ImGui::Text("Frame time/budget: %0.3f/%0.3f ms", replacementTimeThisFrame_ * 1000.0f, replacementFrameBudgetSeconds_ * 1000.0f);
			ImGui::Text("UNLOADED: %d PENDING: %d NOT_FOUND: %d ACTIVE: %d CANCEL_INIT: %d",
				replacementStateCounts[(int)ReplacementState::UNLOADED],
				replacementStateCounts[(int)ReplacementState::PENDING],
				replacementStateCounts[(int)ReplacementState::NOT_FOUND],
				replacementStateCounts[(int)ReplacementState::ACTIVE],
				replacementStateCounts[(int)ReplacementState::CANCEL_INIT]);
		}
		if (textureCache->Videos().size()) {
			if (ImGui::CollapsingHeader("Tracked video playback memory")) {
				for (auto &video : textureCache->Videos()) {
					ImGui::Text("%08x: %d flips, size = %d", video.addr, video.flips, video.size);
				}
			}
		}
	}

	ImGui::EndChild();
	ImGui::End();
}

void DrawDisplayWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Display", &cfg.displayOpen)) {
		ImGui::End();
		return;
	}

	if (framebufferManager) {
		ImGui::Checkbox("Display latched", &cfg.displayLatched);

		PSPPointer<u8> topaddr;
		u32 linesize;
		u32 pixelFormat;

		__DisplayGetFramebuf(&topaddr, &linesize, &pixelFormat, cfg.displayLatched);

		VirtualFramebuffer *fb = framebufferManager->GetVFBAt(topaddr.ptr);
		if (fb && fb->fbo) {
			ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(fb->fbo, Draw::Aspect::COLOR_BIT, ImGuiPipeline::TexturedOpaque);
			ImGui::Image(texId, ImVec2(fb->width, fb->height));
			ImGui::Text("%s - %08x", fb->fbo->Tag(), topaddr.ptr);
		} else {
			// TODO: Sometimes we should display RAM here.
			ImGui::Text("Framebuffer not available to display");
		}
	} else {
		// TODO: We should implement this anyway for software mode.
		// In the meantime, use the pixel viewer.
		ImGui::TextUnformatted("Framebuffer manager not available");
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

void ImGePixelViewerWindow::Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug, Draw::DrawContext *draw) {
	ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Pixel Viewer", &cfg.pixelViewerOpen)) {
		ImGui::End();
		return;
	}

	if (gpuDebug->GetFramebufferManagerCommon()) {
		if (gpuDebug->GetFramebufferManagerCommon()->GetVFBAt(viewer_.addr)) {
			ImGui::Text("NOTE: There's a hardware framebuffer at %08x.", viewer_.addr);
			// TODO: Add a button link.
		}
	}

	if (ImGui::BeginChild("left", ImVec2(200.0f, 0.0f))) {
		if (ImGui::InputScalar("Address", ImGuiDataType_U32, &viewer_.addr, 0, 0, "%08x")) {
			viewer_.Snapshot();
		}

		if (ImGui::BeginCombo("Aspect", GeBufferFormatToString(viewer_.format))) {
			for (int i = 0; i < 5; i++) {
				if (ImGui::Selectable(GeBufferFormatToString((GEBufferFormat)i), i == (int)viewer_.format)) {
					viewer_.format = (GEBufferFormat)i;
					viewer_.Snapshot();
				}
			}
			ImGui::EndCombo();
		}

		bool alphaPresent = viewer_.format == GE_FORMAT_8888 || viewer_.format == GE_FORMAT_4444 || viewer_.format == GE_FORMAT_5551;

		if (!alphaPresent) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Checkbox("Use alpha", &viewer_.useAlpha)) {
			viewer_.Snapshot();
		}
		if (ImGui::Checkbox("Show alpha", &viewer_.showAlpha)) {
			viewer_.Snapshot();
		}
		if (!alphaPresent) {
			ImGui::EndDisabled();
		}
		if (ImGui::InputScalar("Width", ImGuiDataType_U16, &viewer_.width)) {
			viewer_.Snapshot();
		}
		if (ImGui::InputScalar("Height", ImGuiDataType_U16, &viewer_.height)) {
			viewer_.Snapshot();
		}
		if (ImGui::InputScalar("Stride", ImGuiDataType_U16, &viewer_.stride)) {
			viewer_.Snapshot();
		}
		if (viewer_.format == GE_FORMAT_DEPTH16) {
			if (ImGui::SliderFloat("Scale", &viewer_.scale, 0.5f, 256.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
				viewer_.Snapshot();
			}
		}
		if (ImGui::Button("Refresh")) {
			viewer_.Snapshot();
		}
		if (ImGui::Button("Show cur depth")) {
			viewer_.addr = gstate.getDepthBufRawAddress() | 0x04000000;
			viewer_.format = GE_FORMAT_DEPTH16;
			viewer_.stride = gstate.DepthBufStride();
			viewer_.width = viewer_.stride;
			viewer_.Snapshot();
		}
		if (ImGui::Button("Show cur color")) {
			viewer_.addr = gstate.getFrameBufAddress();
			viewer_.format = gstate.FrameBufFormat();
			viewer_.stride = gstate.FrameBufStride();
			viewer_.width = viewer_.stride;
			viewer_.Snapshot();
		}
		ImGui::Checkbox("Realtime", &cfg.realtimePixelPreview);
	}
	ImGui::EndChild();

	if (cfg.realtimePixelPreview) {
		viewer_.Snapshot();
	}

	ImGui::SameLine();
	if (ImGui::BeginChild("right")) {
		ImVec2 p0 = ImGui::GetCursorScreenPos();
		viewer_.Draw(gpuDebug, draw, 1.0f);
		if (ImGui::IsItemHovered()) {
			int x = (int)(ImGui::GetMousePos().x - p0.x);
			int y = (int)(ImGui::GetMousePos().y - p0.y);
			char temp[128];
			if (viewer_.FormatValueAt(temp, sizeof(temp), x, y)) {
				ImGui::Text("(%d, %d): %s", x, y, temp);
			} else {
				ImGui::Text("%d, %d: N/A", x, y);
			}
		} else {
			ImGui::TextUnformatted("(no pixel hovered)");
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

ImGePixelViewer::~ImGePixelViewer() {
	if (texture_)
		texture_->Release();
}

void ImGePixelViewer::DeviceLost() {
	if (texture_) {
		texture_->Release();
		texture_ = nullptr;
	}
}

bool ImGePixelViewer::Draw(GPUDebugInterface *gpuDebug, Draw::DrawContext *draw, float zoom) {
	if (dirty_) {
		UpdateTexture(draw);
		dirty_ = false;
	}

	if (Memory::IsValid4AlignedAddress(addr)) {
		if (texture_) {
			ImTextureID texId = ImGui_ImplThin3d_AddTextureTemp(texture_, useAlpha ? ImGuiPipeline::TexturedAlphaBlend : ImGuiPipeline::TexturedOpaque);
			ImGui::Image(texId, ImVec2((float)width * zoom, (float)height * zoom));
			return true;
		} else {
			ImGui::Text("(invalid params: %dx%d, %08x)", width, height, addr);
		}
	} else {
		ImGui::Text("(invalid address %08x)", addr);
	}
	return false;
}

bool ImGePixelViewer::FormatValueAt(char *buf, size_t bufSize, int x, int y) const {
	// Go look directly in RAM.
	int bpp = BufferFormatBytesPerPixel(format);
	u32 pixelAddr = addr + (y * stride + x) * bpp;
	switch (format) {
	case GE_FORMAT_8888:
		snprintf(buf, bufSize, "%08x", Memory::Read_U32(pixelAddr));
		break;
	case GE_FORMAT_4444:
	{
		u16 raw = Memory::Read_U16(pixelAddr);
		snprintf(buf, bufSize, "%08x (raw: %04x)", RGBA4444ToRGBA8888(raw), raw);
		break;
	}
	case GE_FORMAT_565:
	{
		u16 raw = Memory::Read_U16(pixelAddr);
		snprintf(buf, bufSize, "%08x (raw: %04x)", RGB565ToRGBA8888(raw), raw);
		break;
	}
	case GE_FORMAT_5551:
	{
		u16 raw = Memory::Read_U16(pixelAddr);
		snprintf(buf, bufSize, "%08x (raw: %04x)", RGBA5551ToRGBA8888(raw), raw);
		break;
	}
	case GE_FORMAT_DEPTH16:
	{
		u16 raw = Memory::Read_U16(pixelAddr);
		snprintf(buf, bufSize, "%0.4f (raw: %04x / %d)", (float)raw / 65535.0f, raw, raw);
		break;
	}
	default:
		snprintf(buf, bufSize, "N/A");
		return false;
	}
	return true;
}

void ImGePixelViewer::UpdateTexture(Draw::DrawContext *draw) {
	if (texture_) {
		texture_->Release();
		texture_ = nullptr;
	}
	if (!Memory::IsValid4AlignedAddress(addr) || width == 0 || height == 0 || stride > 1024 || stride == 0) {
		// TODO: Show a warning triangle or something.
		return;
	}

	int bpp = BufferFormatBytesPerPixel(format);

	int srcBytes = width * stride * bpp;
	if (stride > width) {
		srcBytes -= stride - width;
	}
	if (Memory::ClampValidSizeAt(addr, srcBytes) != srcBytes) {
		// TODO: Show a message that the address is out of bounds.
		return;
	}

	// Read pixels into a buffer and transform them accordingly.
	// For now we convert all formats to RGBA here, for backend compatibility.
	uint8_t *buf = new uint8_t[width * height * 4];

	for (int y = 0; y < height; y++) {
		u32 rowAddr = addr + y * stride * bpp;
		const u8 *src = Memory::GetPointerUnchecked(rowAddr);
		u8 *dst = buf + y * width * 4;
		switch (format) {
		case GE_FORMAT_8888:
			if (showAlpha) {
				for (int x = 0; x < width; x++) {
					dst[0] = src[3];
					dst[1] = src[3];
					dst[2] = src[3];
					dst[3] = 0xFF;
					src += 4;
					dst += 4;
				}
			} else {
				memcpy(dst, src, width * 4);
			}
			break;
		case GE_FORMAT_565:
			// No showAlpha needed (would just be white)
			ConvertRGB565ToRGBA8888((u32 *)dst, (const u16 *)src, width);
			break;
		case GE_FORMAT_5551:
			if (showAlpha) {
				uint32_t *dst32 = (uint32_t *)dst;
				const uint16_t *src16 = (const uint16_t *)src;
				for (int x = 0; x < width; x++) {
					dst32[x] = (src16[x] >> 15) ? 0xFFFFFFFF : 0xFF000000;
				}
			} else {
				ConvertRGBA5551ToRGBA8888((u32 *)dst, (const u16 *)src, width);
			}
			break;
		case GE_FORMAT_4444:
			ConvertRGBA4444ToRGBA8888((u32 *)dst, (const u16 *)src, width);
			break;
		case GE_FORMAT_DEPTH16:
		{
			const uint16_t *src16 = (const uint16_t *)src;
			float scale = this->scale / 256.0f;
			for (int x = 0; x < width; x++) {
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
			memset(buf, 0x80, width * height * 4);
			break;
		}
	}

	Draw::TextureDesc desc{ Draw::TextureType::LINEAR2D,
		Draw::DataFormat::R8G8B8A8_UNORM,
		(int)width,
		(int)height,
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

ImGeReadbackViewer::ImGeReadbackViewer() {
	// These are only forward declared in the header, so we initialize them here.
	aspect = Draw::Aspect::COLOR_BIT;
	readbackFmt_ = Draw::DataFormat::UNDEFINED;
}

ImGeReadbackViewer::~ImGeReadbackViewer() {
	if (texture_)
		texture_->Release();
	delete[] data_;
}

void ImGeReadbackViewer::DeviceLost() {
	if (texture_) {
		texture_->Release();
		texture_ = nullptr;
	}
}

bool ImGeReadbackViewer::Draw(GPUDebugInterface *gpuDebug, Draw::DrawContext *draw, float zoom) {
	FramebufferManagerCommon *fbmanager = gpuDebug->GetFramebufferManagerCommon();
	if (!vfb || !vfb->fbo || !fbmanager) {
		ImGui::TextUnformatted("(N/A)");
		return false;
	}

	if (dirty_) {
		dirty_ = false;

		delete[] data_;
		int w = vfb->fbo->Width();
		int h = vfb->fbo->Height();
		int rbBpp = 4;
		switch (aspect) {
		case Draw::Aspect::COLOR_BIT:
			readbackFmt_ = Draw::DataFormat::R8G8B8A8_UNORM;
			break;
		case Draw::Aspect::DEPTH_BIT:
			// TODO: Add fallback
			readbackFmt_ = Draw::DataFormat::D32F;
			break;
		case Draw::Aspect::STENCIL_BIT:
			readbackFmt_ = Draw::DataFormat::S8;
			rbBpp = 1;
			break;
		default:
			break;
		}

		data_ = new uint8_t[w * h * rbBpp];
		draw->CopyFramebufferToMemory(vfb->fbo, aspect, 0, 0, w, h, readbackFmt_, data_, w, Draw::ReadbackMode::BLOCK, "debugger");

		if (texture_) {
			texture_->Release();
			texture_ = nullptr;
		}

		// For now, we just draw the color texture. The others we convert.
		if (aspect != Draw::Aspect::COLOR_BIT) {
			uint8_t *texData = data_;
			if (aspect == Draw::Aspect::DEPTH_BIT && scale != 1.0f) {
				texData = new uint8_t[w * h * rbBpp];
				// Apply scale
				float *ptr = (float *)data_;
				float *tptr = (float *)texData;
				for (int i = 0; i < w * h; i++) {
					tptr[i] = ptr[i] * scale;
				}
			}

			Draw::DataFormat fmt = rbBpp == 1 ? Draw::DataFormat::R8_UNORM : Draw::DataFormat::R32_FLOAT;
			Draw::TextureDesc desc{ Draw::TextureType::LINEAR2D,
				fmt,
				(int)w,
				(int)h,
				1,
				1,
				false,
				Draw::DataFormatNumChannels(fmt) == 1 ? Draw::TextureSwizzle::R8_AS_GRAYSCALE: Draw::TextureSwizzle::DEFAULT,
				"PixelViewer temp",
				{ texData },
				nullptr,
			};

			texture_ = draw->CreateTexture(desc);

			if (texData != data_) {
				delete[] texData;
			}
		}
	}

	ImTextureID texId;
	if (texture_) {
		texId = ImGui_ImplThin3d_AddTextureTemp(texture_, ImGuiPipeline::TexturedOpaque);
	} else {
		texId = ImGui_ImplThin3d_AddFBAsTextureTemp(vfb->fbo, Draw::Aspect::COLOR_BIT, ImGuiPipeline::TexturedOpaque);
	}
	ImGui::Image(texId, ImVec2((float)vfb->fbo->Width() * zoom, (float)vfb->fbo->Height() * zoom));
	return true;
}

bool ImGeReadbackViewer::FormatValueAt(char *buf, size_t bufSize, int x, int y) const {
	if (!vfb || !vfb->fbo || !data_) {
		snprintf(buf, bufSize, "N/A");
		return true;
	}
	int bpp = (int)Draw::DataFormatSizeInBytes(readbackFmt_);
	int offset = (y * vfb->fbo->Width() + x) * bpp;
	switch (readbackFmt_) {
	case Draw::DataFormat::R8G8B8A8_UNORM:
	{
		const uint32_t *read32 = (const uint32_t *)(data_ + offset);
		snprintf(buf, bufSize, "%08x", *read32);
		return true;
	}
	case Draw::DataFormat::D32F:
	{
		const float *read = (const float *)(data_ + offset);
		float value = *read;
		int ivalue = *read * 65535.0f;
		snprintf(buf, bufSize, "%0.4f (raw: %04x / %d)", *read, ivalue, ivalue);
		return true;
	}
	case Draw::DataFormat::S8:
	{
		uint8_t value = data_[offset];
		snprintf(buf, bufSize, "%d (%02x)", value, value);
		return true;
	}
	default:
		return false;
	}
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

ImGeDebuggerWindow::ImGeDebuggerWindow() {
	selectedAspect_ = Draw::Aspect::COLOR_BIT;
}

void ImGeDebuggerWindow::DeviceLost() {
	rbViewer_.DeviceLost();
	swViewer_.DeviceLost();
}

void ImGeDebuggerWindow::NotifyStep() {
	reloadPreview_ = true;
	disasmView_.NotifyStep();

	// In software mode, or written back to RAM, the alpha channel is the stencil channel
	switch (selectedAspect_) {
	case Draw::Aspect::COLOR_BIT:
	case Draw::Aspect::STENCIL_BIT:
		swViewer_.width = gstate.FrameBufStride();
		// Height heuristic
		swViewer_.height = gstate.getScissorY2() + 1 - gstate.getScissorY1();  // Just guessing the height, we have no reliable way to tell
		swViewer_.format = gstate.FrameBufFormat();
		swViewer_.addr = gstate.getFrameBufAddress();
		swViewer_.showAlpha = selectedAspect_ == Draw::Aspect::STENCIL_BIT;
		swViewer_.useAlpha = false;
		swViewer_.Snapshot();
		break;
	case Draw::Aspect::DEPTH_BIT:
		swViewer_.width = gstate.DepthBufStride();
		swViewer_.height = gstate.getScissorY2() + 1 - gstate.getScissorY1();  // Just guessing the height, we have no reliable way to tell
		swViewer_.format = GE_FORMAT_DEPTH16;
		swViewer_.addr = gstate.getDepthBufAddress();
		swViewer_.showAlpha = false;
		swViewer_.useAlpha = false;
		break;
	default:
		break;
	}

	FramebufferManagerCommon *fbman = gpuDebug->GetFramebufferManagerCommon();
	if (fbman) {
		rbViewer_.vfb = fbman->GetExactVFB(gstate.getFrameBufAddress(), gstate.FrameBufStride(), gstate.FrameBufFormat());
		rbViewer_.aspect = selectedAspect_;
	}
	rbViewer_.Snapshot();
}

void ImGeDebuggerWindow::Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug, Draw::DrawContext *draw) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(Title(), &cfg.geDebuggerOpen)) {
		ImGui::End();
		return;
	}

	ImGui::BeginDisabled(coreState != CORE_STEPPING_GE);
	if (ImGui::Button("Run/Resume")) {
		// Core_Resume();
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::DEBUG_RUN);
	}
	ImGui::SameLine();
	if (ImGui::Button("...")) {
		ImGui::OpenPopup("dotdotdot");
	}
	if (ImGui::BeginPopup("dotdotdot")) {
		if (ImGui::MenuItem("RunFast")) {
			gpuDebug->ClearBreakNext();
			Core_Resume();
		}
		ImGui::EndPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted("Break:");
	ImGui::SameLine();
	//if (ImGui::Button("Frame")) {
		// TODO: This doesn't work correctly.
	//	GPUDebug::SetBreakNext(GPUDebug::BreakNext::FRAME);
	//}

	bool disableStepButtons = gpuDebug->GetBreakNext() != GPUDebug::BreakNext::NONE && gpuDebug->GetBreakNext() != GPUDebug::BreakNext::DEBUG_RUN;

	constexpr float fastRepeatRate = 0.025f;

	if (disableStepButtons) {
		ImGui::BeginDisabled();
	}
	ImGui::SameLine();
	if (ImGui::RepeatButtonShift("Tex", fastRepeatRate)) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::TEX);
	}
	ImGui::SameLine();
	if (ImGui::RepeatButtonShift("Prim", fastRepeatRate)) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::PRIM);
	}
	ImGui::SameLine();
	if (ImGui::RepeatButtonShift("Draw", fastRepeatRate)) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::DRAW);
	}
	ImGui::SameLine();
	if (ImGui::Button("Block xfer")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::BLOCK_TRANSFER);
	}
	ImGui::SameLine();
	if (ImGui::Button("Curve")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::CURVE);
	}
	ImGui::SameLine();
	if (ImGui::RepeatButtonShift("Single step", fastRepeatRate)) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::OP);
	}
	if (disableStepButtons) {
		ImGui::EndDisabled();
	}

	ImGui::Text("%d/%d", gpuDebug->PrimsThisFrame(), gpuDebug->PrimsLastFrame());

	if (disableStepButtons) {
		ImGui::BeginDisabled();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputInt("Number", &cfg.breakCount);

	ImGui::SameLine();
	if (ImGui::Button("Break on #")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::COUNT);
		gpuDebug->SetBreakCount(cfg.breakCount);
	}
	ImGui::SameLine();
	if (ImGui::Button("Step by")) {
		gpuDebug->SetBreakNext(GPUDebug::BreakNext::COUNT);
		gpuDebug->SetBreakCount(cfg.breakCount, true);  // relative
	}
	if (disableStepButtons) {
		ImGui::EndDisabled();
	}

	// Display any pending step event.
	if (gpuDebug->GetBreakNext() != GPUDebug::BreakNext::NONE && gpuDebug->GetBreakNext() != GPUDebug::BreakNext::DEBUG_RUN) {
		if (showBannerInFrames_ > 0) {
			showBannerInFrames_--;
		}
		if (showBannerInFrames_ == 0) {
			ImGui::Text("Step pending: %s", GPUDebug::BreakNextToString(gpuDebug->GetBreakNext()));
			ImGui::SameLine();
			if (gpuDebug->GetBreakNext() == GPUDebug::BreakNext::COUNT) {
				ImGui::Text("(%d)", gpuDebug->GetBreakCount());
				ImGui::SameLine();
			}
			if (ImGui::Button("Cancel step")) {
				gpuDebug->ClearBreakNext();
			}
		}
	} else {
		showBannerInFrames_ = 2;
	}

	// Line break
	if (ImGui::Button("Goto PC")) {
		disasmView_.GotoPC();
	}
	ImGui::SameLine();
	if (ImGui::Button("Settings")) {
		ImGui::OpenPopup("disSettings");
	}
	if (ImGui::BeginPopup("disSettings")) {
		ImGui::Checkbox("Follow PC", &disasmView_.followPC_);
		ImGui::EndPopup();
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
				ImClickableValue("pc", list.pc, control, ImCmd::SHOW_IN_GE_DISASM);
				ImGui::Text("StartPC:");
				ImGui::SameLine();
				ImClickableValue("startpc", list.startpc, control, ImCmd::SHOW_IN_GE_DISASM);
				if (list.pendingInterrupt) {
					ImGui::TextUnformatted("(Pending interrupt)");
				}
				if (list.stall) {
					ImGui::TextUnformatted("Stall addr:");
					ImGui::SameLine();
					ImClickableValue("stall", list.pc, control, ImCmd::SHOW_IN_GE_DISASM);
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
	bool isOnBlockTransfer = false;
	if (gpuDebug->GetCurrentDisplayList(list)) {
		op = Memory::Read_U32(list.pc);

		// TODO: Also add support for block transfer previews!

		bool isOnPrim = false;
		switch (op >> 24) {
		case GE_CMD_PRIM:
			isOnPrim = true;
			if (reloadPreview_) {
				GetPrimPreview(op, previewPrim_, previewVertices_, previewIndices_, previewCount_);
				reloadPreview_ = false;
			}
			break;
		case GE_CMD_TRANSFERSTART:
			isOnBlockTransfer = true;
			break;
		default:
			// Disable the current preview.
			previewCount_ = 0;
			break;
		}

	}

	ImGui::BeginChild("texture/fb view");

	ImDrawList *drawList = ImGui::GetWindowDrawList();

	if (coreState == CORE_STEPPING_GE) {
		if (isOnBlockTransfer) {
			ImGui::Text("Block transfer! Proper preview coming in the future.\n");
			ImGui::Text("%08x -> %08x, %d bpp (strides: %d, %d)", gstate.getTransferSrcAddress(), gstate.getTransferDstAddress(), gstate.getTransferBpp(), gstate.getTransferSrcStride(), gstate.getTransferDstStride());
			ImGui::Text("%dx%d pixels", gstate.getTransferWidth(), gstate.getTransferHeight());
			ImGui::Text("Src pos: %d, %d", gstate.getTransferSrcX(), gstate.getTransferSrcY());
			ImGui::Text("Dst pos: %d, %d", gstate.getTransferDstX(), gstate.getTransferDstY());
			ImGui::Text("Total bytes to transfer: %d", gstate.getTransferWidth() * gstate.getTransferHeight() * gstate.getTransferBpp());
		} else {
			// Visualize prim by default (even if we're not directly on a prim instruction).
			VirtualFramebuffer *vfb = rbViewer_.vfb;
			if (vfb) {
				if (vfb->fbo) {
					ImGui::Text("Framebuffer: %s", vfb->fbo->Tag());
				} else {
					ImGui::Text("Framebuffer");
				}
				ImGui::SameLine();
			}
			ImGui::SetNextItemWidth(200.0f);
			ImGui::SliderFloat("Zoom", &previewZoom_, 0.125f, 2.f, "%.3f", ImGuiSliderFlags_Logarithmic);

			// Use selectable instead of tab bar so we can get events (haven't figured that out).
			static const Draw::Aspect aspects[3] = { Draw::Aspect::COLOR_BIT, Draw::Aspect::DEPTH_BIT, Draw::Aspect::STENCIL_BIT, };
			static const char *const aspectNames[3] = { "Color", "Depth", "Stencil" };
			for (int i = 0; i < ARRAY_SIZE(aspects); i++) {
				if (i != 0)
					ImGui::SameLine();
				if (ImGui::Selectable(aspectNames[i], aspects[i] == selectedAspect_, 0, ImVec2(120.0f, 0.0f))) {
					selectedAspect_ = aspects[i];
					NotifyStep();
				}
			}

			if (selectedAspect_ == Draw::Aspect::DEPTH_BIT) {
				float minimum = 0.5f;
				float maximum = 256.0f;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::DragFloat("Z value scale", &rbViewer_.scale, 1.0f, 0.5f, 256.0f, "%0.2f", ImGuiSliderFlags_Logarithmic)) {
					rbViewer_.Snapshot();
					swViewer_.Snapshot();
				}
			}

			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			ImVec2 p1;
			float scale = 1.0f;
			if (vfb && vfb->fbo) {
				scale = vfb->renderScaleFactor;
				p1 = ImVec2(p0.x + vfb->fbo->Width() * previewZoom_, p0.y + vfb->fbo->Height() * previewZoom_);
			} else {
				// Guess
				p1 = ImVec2(p0.x + swViewer_.width, p0.y + swViewer_.height);
			}

			// Draw border and background color
			drawList->PushClipRect(p0, p1, true);

			PixelLookup *lookup = nullptr;
			if (vfb) {
				rbViewer_.Draw(gpuDebug, draw, previewZoom_);
				lookup = &rbViewer_;
				// ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(vfb->fbo, Draw::Aspect::COLOR_BIT, ImGuiPipeline::TexturedOpaque);
				// ImGui::Image(texId, ImVec2(vfb->width, vfb->height));
			} else {
				swViewer_.Draw(gpuDebug, draw, previewZoom_);
				lookup = &swViewer_;
			}

			// Draw vertex preview on top!
			DrawPreviewPrimitive(drawList, p0, previewPrim_, previewIndices_, previewVertices_, previewCount_, false, scale * previewZoom_, scale * previewZoom_);

			drawList->PopClipRect();

			if (ImGui::IsItemHovered()) {
				int x = (int)(ImGui::GetMousePos().x - p0.x) * previewZoom_;
				int y = (int)(ImGui::GetMousePos().y - p0.y) * previewZoom_;
				char temp[128];
				if (lookup->FormatValueAt(temp, sizeof(temp), x, y)) {
					ImGui::Text("(%d, %d): %s", x, y, temp);
				} else {
					ImGui::Text("%d, %d: N/A", x, y);
				}
			} else {
				ImGui::TextUnformatted("(no pixel hovered)");
			}

			if (vfb && vfb->fbo) {
				ImGui::Text("VFB %dx%d (emulated: %dx%d)", vfb->width, vfb->height, vfb->fbo->Width(), vfb->fbo->Height());
			} else {
				// Use the swViewer_!
				ImGui::Text("Raw FB: %08x (%s)", gstate.getFrameBufRawAddress(), GeBufferFormatToString(gstate.FrameBufFormat()));
			}

			if (gstate.isModeClear()) {
				ImGui::Text("(clear mode - texturing not used)");
			} else if (!gstate.isTextureMapEnabled()) {
				ImGui::Text("(texturing not enabled");
			} else {
				TextureCacheCommon *texcache = gpuDebug->GetTextureCacheCommon();
				TexCacheEntry *tex = texcache ? texcache->SetTexture() : nullptr;
				if (tex) {
					ImGui::Text("Texture: %08x", tex->addr);
					texcache->ApplyTexture(false);

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
		}
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
								ImClickableValue(info.uiName, val, control, ImCmd::NONE);
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

		if (ImGui::BeginTabItem("Matrices")) {
			auto visMatrix = [](const char *name, const float *data, bool is4x4) {
				ImGui::TextUnformatted(name);
				int stride = (is4x4 ? 4 : 3);
				if (ImGui::BeginTable(name, stride, ImGuiTableFlags_Borders, ImVec2(90.0f * stride, 0.0f))) {
					for (int row = 0; row < 4; ++row) {
						ImGui::TableNextRow();
						for (int col = 0; col < stride; ++col) {
							ImGui::TableSetColumnIndex(col);
							ImGui::Text("%.4f", data[row * stride + col]);
						}
					}
					ImGui::EndTable();
				}
			};

			if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (gstate.isModeThrough()) {
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 160));
					ImGui::TextUnformatted("Through mode: No matrices are used");
				}
				visMatrix("World", gstate.worldMatrix, false);
				visMatrix("View", gstate.viewMatrix, false);
				visMatrix("Proj", gstate.projMatrix, true);
				visMatrix("Tex", gstate.tgenMatrix, false);
				if (gstate.isModeThrough()) {
					ImGui::PopStyleColor();
				}
			}

			if (ImGui::CollapsingHeader("Bone matrices")) {
				for (int i = 0; i < 8; i++) {
					char n[16];
					snprintf(n, sizeof(n), "Bone %d", i);
					visMatrix(n, gstate.boneMatrix + 12 * i, false);
				}
			}

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void DrawImGeVertsWindow(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug) {
	ImGui::SetNextWindowSize(ImVec2(300, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("GE Vertices", &cfg.geVertsOpen)) {
		ImGui::End();
		return;
	}
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
		if (!gpuDebug->GetCurrentDrawAsDebugVertices(rowCount_, vertices, indices)) {
			rowCount_ = 0;
		}
		auto buildVertexTable = [&](bool raw) {
			// Ignore indices for now.
			if (ImGui::BeginTable("rawverts", VERTEXLIST_COL_COUNT + 1, tableFlags)) {
				static VertexDecoder decoder;
				u32 vertTypeID = GetVertTypeID(state.vertType, state.getUVGenMode(), true);
				VertexDecoderOptions options{};
				decoder.SetVertexType(vertTypeID, options);

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
						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						ImGui::PushID(i);
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
	ImGui::End();
}
