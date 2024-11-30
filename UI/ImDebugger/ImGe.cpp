#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_impl_thin3d.h"
#include "UI/ImDebugger/ImGe.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"

#include "Core/HLE/sceDisplay.h"

void DrawFramebuffersWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager) {
	if (!ImGui::Begin("Framebuffers", &cfg.framebuffersOpen)) {
		ImGui::End();
		return;
	}

	framebufferManager->DrawImGuiDebug(cfg.selectedFramebuffer);

	ImGui::End();
}

void DrawTexturesWindow(ImConfig &cfg, TextureCacheCommon *textureCache) {
	if (!ImGui::Begin("Textures", &cfg.texturesOpen)) {
		ImGui::End();
		return;
	}

	textureCache->DrawImGuiDebug(cfg.selectedTexAddr);

	ImGui::End();
}

void DrawDisplayWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager) {
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
