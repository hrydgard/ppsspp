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
};

namespace Draw {
class Texture;
enum class Aspect;
enum class DataFormat : uint8_t;
}

class PixelLookup {
public:
	virtual ~PixelLookup() {}

	virtual bool FormatValueAt(char *buf, size_t bufSize, int x, int y) const = 0;
};

struct ImGePixelViewer : public PixelLookup {
	~ImGePixelViewer();
	bool Draw(GPUDebugInterface *gpuDebug, Draw::DrawContext *draw);
	void Snapshot() {
		dirty_ = true;
	}
	bool FormatValueAt(char *buf, size_t bufSize, int x, int y) const override;

	uint32_t addr = 0x04110000;
	uint16_t stride = 512;
	uint16_t width = 480;
	uint16_t height = 272;
	GEBufferFormat format = GE_FORMAT_DEPTH16;
	bool useAlpha = false;
	bool showAlpha = false;
	float scale = 20.0f;

private:
	void UpdateTexture(Draw::DrawContext *draw);
	Draw::Texture *texture_ = nullptr;
	bool dirty_ = true;
};

// Reads back framebuffers, not textures.
struct ImGeReadbackViewer : public PixelLookup {
	ImGeReadbackViewer();
	~ImGeReadbackViewer();
	bool Draw(GPUDebugInterface *gpuDebug, Draw::DrawContext *);
	void Snapshot() {
		dirty_ = true;
	}
	bool FormatValueAt(char *buf, size_t bufSize, int x, int y) const override;

	// TODO: This is unsafe! If you load state for example with the debugger open...
	// We need to re-fetch this each frame from the parameters.
	VirtualFramebuffer *vfb = nullptr;

	// This specifies what to show
	Draw::Aspect aspect;
	float scale = 1.0f;  // Scales depth values.

private:
	uint8_t *data_ = nullptr;
	Draw::DataFormat readbackFmt_;
	Draw::Texture *texture_ = nullptr;
	bool dirty_ = true;
};

class ImGePixelViewerWindow {
public:
	void Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug, Draw::DrawContext *draw);
	void Snapshot() {
		viewer_.Snapshot();
	}
	void Show(uint32_t address, int width, int height, int stride, GEBufferFormat format) {
		viewer_.addr = address;
		viewer_.width = width;
		viewer_.height = height;
		viewer_.stride = stride;
		viewer_.format = format;
	}

private:
	ImGePixelViewer viewer_;
};

class ImGeDebuggerWindow {
public:
	ImGeDebuggerWindow();
	void Draw(ImConfig &cfg, ImControl &control, GPUDebugInterface *gpuDebug, Draw::DrawContext *draw);
	ImGeDisasmView &View() {
		return disasmView_;
	}
	const char *Title() const {
		return "GE Debugger";
	}
	void NotifyStep();

private:
	ImGeDisasmView disasmView_;
	ImGeReadbackViewer rbViewer_;
	ImGePixelViewer swViewer_;
	int showBannerInFrames_ = 0;
	bool reloadPreview_ = false;
	GEPrimitiveType previewPrim_ = GEPrimitiveType::GE_PRIM_TRIANGLES;
	std::vector<u16> previewIndices_;
	std::vector<GPUDebugVertex> previewVertices_;
	int previewCount_ = 0;
	Draw::Aspect selectedAspect_;
};
