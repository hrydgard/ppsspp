#pragma once

// GE-related windows of the ImDebugger

struct ImConfig;

class FramebufferManagerCommon;
class TextureCacheCommon;
class GPUDebugInterface;

void DrawFramebuffersWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);
void DrawTexturesWindow(ImConfig &cfg, TextureCacheCommon *textureCache);
void DrawDisplayWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);
void DrawDebugStatsWindow(ImConfig &cfg);
void DrawGeStateWindow(ImConfig &cfg, GPUDebugInterface *gpuDebug);

class ImGeDisasmView {
public:
	void Draw(GPUDebugInterface *gpuDebug);

	bool followPC_ = true;

	void GotoPC() {
		gotoPC_ = true;
	}

private:
	u32 selectedAddr_ = INVALID_ADDR;
	u32 dragAddr_ = INVALID_ADDR;
	bool bpPopup_ = false;
	bool gotoPC_ = false;
	enum : u32 {
		INVALID_ADDR = 0xFFFFFFFF
	};
};

class ImGeDebuggerWindow {
public:
	void Draw(ImConfig &cfg, GPUDebugInterface *gpuDebug);
	ImGeDisasmView &View() {
		return disasmView_;
	}

private:
	ImGeDisasmView disasmView_;
};
