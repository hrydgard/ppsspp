#pragma once

#include "GPU/GPUCommon.h"
#include "Common/GPU/thin3d.h"

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
	void Draw(GPUCommon *gpuDebug);

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
	void Draw(ImConfig &cfg, ImControl &control, GPUCommon *gpuDebug);
	void Snapshot();
};

void DrawImGeVertsWindow(ImConfig &cfg, ImControl &control, GPUCommon *gpuDebug);

namespace Draw {
class Texture;
enum class Aspect;
enum class DataFormat : uint8_t;
}

class PixelLookup {
public:
	virtual ~PixelLookup() {}

	virtual bool FormatValueAt(char *buf, size_t bufSize, int x, int y) const = 0;
	virtual float GetHistogramValue(int idx) const = 0;
	virtual int GetHistogramSize() const = 0;
};

struct ImGePixelViewer : public PixelLookup {
	~ImGePixelViewer();
	bool Draw(GPUCommon *gpuDebug, Draw::DrawContext *draw, float zoom);
	void Snapshot() {
		dirty_ = true;
	}
	bool FormatValueAt(char *buf, size_t bufSize, int x, int y) const override;
	void DeviceLost();
	float GetHistogramValue(int idx) const override { return 0; }
	int GetHistogramSize() const override { return 0; }

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
	bool Draw(GPUCommon *gpuDebug, Draw::DrawContext *draw, float zoom);
	void Snapshot() {
		dirty_ = true;
	}
	bool FormatValueAt(char *buf, size_t bufSize, int x, int y) const override;
	void DeviceLost();

	VirtualFramebuffer *GetVFB(FramebufferManagerCommon *fbMan) const;

	// We need to re-fetch the vfb each frame from these parameters.
	u32 fbAddr = 0;
	u32 fbStride = 0;
	GEBufferFormat fbFormat = GE_FORMAT_INVALID;

	// This specifies what to show
	Draw::Aspect aspect_{};
	bool showAlpha_ = false;
	float scale = 1.0f;  // Scales depth values.

	const int *Histogram() const { return histogram_; }
	float GetHistogramValue(int idx) const override {
		return histogram_[idx];
	}
	float GetHistogramMaxValue() const { return histogramMax_; }
	int GetHistogramSize() const override { return aspect_ == Draw::Aspect::STENCIL_BIT ? 256 : (aspect_ == Draw::Aspect::COLOR_BIT && showAlpha_ ? 256 : 0); }
private:
	uint8_t *data_ = nullptr;
	int histogram_[256 * 3] = {};
	int histogramMax_ = 0;
	Draw::DataFormat readbackFmt_;
	Draw::Texture *texture_ = nullptr;
	bool dirty_ = true;
};

class ImGePixelViewerWindow {
public:
	void Draw(ImConfig &cfg, ImControl &control, GPUCommon *gpuDebug, Draw::DrawContext *draw);
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
	void DeviceLost() {
		viewer_.DeviceLost();
	}

private:
	ImGePixelViewer viewer_;
};

class ImGeDebuggerWindow {
public:
	ImGeDebuggerWindow();
	void Draw(ImConfig &cfg, ImControl &control, GPUCommon *gpuDebug, Draw::DrawContext *draw);
	ImGeDisasmView &View() {
		return disasmView_;
	}
	const char *Title() const {
		return "GE Debugger";
	}
	void NotifyStep();
	void DeviceLost();

private:
	ImGeDisasmView disasmView_;
	ImGeReadbackViewer rbViewer_;
	ImGePixelViewer swViewer_;
	int showBannerInFrames_ = 0;
	bool reloadPreview_ = false;
	GECommand previewCmd_{};
	GEPrimitiveType previewPrim_ = GEPrimitiveType::GE_PRIM_TRIANGLES;
	std::vector<u16> previewIndices_;
	std::vector<GPUDebugVertex> previewVertices_;
	int previewIndexOffset_ = 0;
	bool previewTransformed_ = true;
	Draw::Aspect selectedAspect_;
	float previewZoom_ = 1.0f;
};
