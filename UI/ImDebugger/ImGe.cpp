#include "UI/ImDebugger/ImGe.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "GPU/Common/FramebufferManagerCommon.h"

#include "Core/HLE/sceDisplay.h"

void DrawFramebuffersWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager) {
	if (!ImGui::Begin("Framebuffers", &cfg.framebuffersOpen)) {
		ImGui::End();
		return;
	}

	framebufferManager->DrawImGuiDebug(cfg.selectedFramebuffer);

	ImGui::End();
}

void DrawDisplayWindow(ImConfig &cfg) {
	if (!ImGui::Begin("Display", &cfg.framebuffersOpen)) {
		ImGui::End();
		return;
	}

	ImGui::End();
}
