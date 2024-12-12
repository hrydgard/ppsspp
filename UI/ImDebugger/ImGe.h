#pragma once

// GE-related windows of the ImDebugger

struct ImConfig;
struct ImControl;

class FramebufferManagerCommon;
class TextureCacheCommon;
class GPUDebugInterface;

void DrawFramebuffersWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);
void DrawTexturesWindow(ImConfig &cfg, TextureCacheCommon *textureCache);
void DrawDisplayWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);
void DrawDebugStatsWindow(ImConfig &cfg);

class ImGeDisasmView {
public:
	void Draw(GPUDebugInterface *gpuDebug);

	bool followPC_ = true;

	void GotoPC() {
		gotoPC_ = true;
	}

	void GotoAddr(uint32_t addr) {
		selectedAddr_ = addr;
	}

	void NotifyStep();

private:
	u32 selectedAddr_ = INVALID_ADDR;
	u32 dragAddr_ = INVALID_ADDR;
	bool bpPopup_ = false;
	bool gotoPC_ = false;
	enum : u32 {
		INVALID_ADDR = 0xFFFFFFFF
	};
};

class ImGeStateWindow {
public:
	void Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug);
	void Snapshot();
private:
	u32 prevState_[256]{};
};

class ImGeDebuggerWindow {
public:
	void Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug);
	ImGeDisasmView &View() {
		return disasmView_;
	}
	const char *Title() const {
		return "GE Debugger";
	}

private:
	ImGeDisasmView disasmView_;
	int showBannerInFrames_ = 0;
};
