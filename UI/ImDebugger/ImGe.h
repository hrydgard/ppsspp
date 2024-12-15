#pragma once

#include "GPU/Common/GPUDebugInterface.h"

// GE-related windows of the ImDebugger

struct ImConfig;
struct ImControl;

class FramebufferManagerCommon;
class TextureCacheCommon;

constexpr ImU32 ImDebuggerColor_Diff = IM_COL32(255, 96, 32, 255);
constexpr ImU32 ImDebuggerColor_DiffAlpha = IM_COL32(255, 96, 32, 128);

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
	u32 selectedAddr_ = 0;
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

namespace Draw {
class Texture;
}

class ImGePixelViewer {
public:
	~ImGePixelViewer();
	void Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug, Draw::DrawContext *draw);
	void Snapshot() {
		dirty_ = true;
	}
	void Show(uint32_t address, int width, int height, int stride, GEBufferFormat format) {
		addr_ = address;
		width_ = width;
		height_ = height;
		stride_ = stride;
		format_ = format;
	}

private:
	void UpdateTexture(Draw::DrawContext *draw);
	Draw::Texture *texture_ = nullptr;
	GEBufferFormat format_ = GE_FORMAT_565;
	bool dirty_ = true;
	bool useAlpha_ = false;
	bool showAlpha_ = false;
	float scale_ = 1.0f;
	uint32_t addr_ = 0x04000000;
	uint16_t stride_ = 512;
	uint16_t width_ = 480;
	uint16_t height_ = 272;
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
	void NotifyStep() {
		reloadPreview_ = true;
		disasmView_.NotifyStep();
	}

private:
	ImGeDisasmView disasmView_;
	int showBannerInFrames_ = 0;
	bool reloadPreview_ = false;
	GEPrimitiveType previewPrim_;
	std::vector<u16> previewIndices_;
	std::vector<GPUDebugVertex> previewVertices_;
	int previewCount_ = 0;
};
