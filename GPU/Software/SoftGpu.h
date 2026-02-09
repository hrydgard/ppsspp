// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <cstdint>

#include "GPU/GPUCommon.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "Common/GPU/thin3d.h"

struct FormatBuffer {
	FormatBuffer() { data = nullptr; }
	union {
		u8 *data;
		u16 *as16;
		u32 *as32;
	};

	inline void Set16(int x, int y, int stride, u16 v) const {
		as16[x + y * stride] = v;
	}

	inline void Set32(int x, int y, int stride, u32 v) const {
		as32[x + y * stride] = v;
	}

	inline u16 Get16(int x, int y, int stride) const {
		return as16[x + y * stride];
	}

	inline u32 Get32(int x, int y, int stride) const {
		return as32[x + y * stride];
	}

	inline u16 *Get16Ptr(int x, int y, int stride) const {
		return &as16[x + y * stride];
	}

	inline u32 *Get32Ptr(int x, int y, int stride) const {
		return &as32[x + y * stride];
	}
};

enum class SoftDirty : uint64_t {
	NONE = 0,

	PIXEL_BASIC = 1ULL << 0,
	PIXEL_STENCIL = 1ULL << 1,
	PIXEL_ALPHA = 1ULL << 2,
	PIXEL_DITHER = 1ULL << 3,
	PIXEL_WRITEMASK = 1ULL << 4,
	PIXEL_CACHED = 1ULL << 5,
	PIXEL_ALL = 0b111111ULL << 0,

	SAMPLER_BASIC = 1ULL << 6,
	SAMPLER_TEXLIST = 1ULL << 7,
	SAMPLER_CLUT = 1ULL << 8,
	SAMPLER_ALL = 0b111ULL << 6,

	RAST_BASIC = 1ULL << 9,
	RAST_TEX = 1ULL << 10,
	RAST_OFFSET = 1ULL << 11,
	RAST_ALL = 0b111ULL << 9,

	LIGHT_BASIC = 1ULL << 12,
	LIGHT_MATERIAL = 1ULL << 13,
	LIGHT_0 = 1ULL << 14,
	LIGHT_1 = 1ULL << 15,
	LIGHT_2 = 1ULL << 16,
	LIGHT_3 = 1ULL << 17,
	LIGHT_ALL = 0b111111ULL << 12,

	TRANSFORM_BASIC = 1ULL << 18,
	TRANSFORM_MATRIX = 1ULL << 19,
	TRANSFORM_VIEWPORT = 1ULL << 20,
	TRANSFORM_FOG = 1ULL << 21,
	TRANSFORM_ALL = 0b1111ULL << 18,

	BINNER_RANGE = 1ULL << 22,
	BINNER_OVERLAP = 1ULL << 23,
};
static inline SoftDirty operator |(const SoftDirty &lhs, const SoftDirty &rhs) {
	return SoftDirty((uint64_t)lhs | (uint64_t)rhs);
}
static inline SoftDirty &operator |=(SoftDirty &lhs, const SoftDirty &rhs) {
	lhs = lhs | rhs;
	return lhs;
}
static inline bool operator &(const SoftDirty &lhs, const SoftDirty &rhs) {
	return ((uint64_t)lhs & (uint64_t)rhs) != 0;
}
static inline SoftDirty &operator &=(SoftDirty &lhs, const SoftDirty &rhs) {
	lhs = SoftDirty((uint64_t)lhs & (uint64_t)rhs);
	return lhs;
}
static inline SoftDirty operator ~(const SoftDirty &v) {
	return SoftDirty(~(uint64_t)v);
}

class PresentationCommon;
class SoftwareDrawEngine;

enum class SoftGPUVRAMDirty : uint8_t {
	CLEAR = 0,
	DIRTY = 1,
	REALLY_DIRTY = 2,
};

ENUM_CLASS_BITOPS(SoftGPUVRAMDirty);

class SoftGPU : public GPUCommon {
public:
	SoftGPU(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~SoftGPU();

	u32 CheckGPUFeatures() const override { return 0; }
	bool IsStarted() override;
	void ExecuteOp(u32 op, u32 diff) override;
	void FinishDeferred() override;
	int ListSync(int listid, int mode) override;
	u32 DrawSync(int mode) override;
	void UpdateCmdInfo() override {}

	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;
	void SetCurFramebufferDirty(bool dirty) override {}
	void PrepareCopyDisplayToOutput(const DisplayLayoutConfig &config) override;
	void CopyDisplayToOutput(const DisplayLayoutConfig &config) override;
	void GetStats(char *buffer, size_t bufsize) override;
	std::vector<const VirtualFramebuffer *> GetFramebufferList() const override { return std::vector<const VirtualFramebuffer *>(); }
	void InvalidateCache(u32 addr, int size, GPUInvalidationType type) override;
	void PerformWriteFormattedFromMemory(u32 addr, int size, int width, GEBufferFormat format) override;
	bool PerformMemoryCopy(u32 dest, u32 src, int size, GPUCopyFlag flags = GPUCopyFlag::NONE) override;
	bool PerformMemorySet(u32 dest, u8 v, int size) override;
	bool PerformReadbackToMemory(u32 dest, int size) override;
	bool PerformWriteColorFromMemory(u32 dest, int size) override;
	bool PerformWriteStencilFromMemory(u32 dest, int size, WriteStencil flags) override;

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	void NotifyRenderResized(const DisplayLayoutConfig &config) override;
	void NotifyDisplayResized() override;

	void CheckDisplayResized() override;
	void CheckConfigChanged(const DisplayLayoutConfig &config) override;

	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) const override {
		primaryInfo = "Software";
		fullInfo = "Software";
	}

	bool FramebufferDirty() override;
	bool FramebufferReallyDirty() override;

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes = -1) override;
	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) override;
	bool GetCurrentClut(GPUDebugBuffer &buffer) override;
	bool GetCurrentDrawAsDebugVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

	void Execute_BlockTransferStart(u32 op, u32 diff);
	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);
	void Execute_FramebufPtr(u32 op, u32 diff);
	void Execute_FramebufFormat(u32 op, u32 diff);
	void Execute_ZbufPtr(u32 op, u32 diff);
	void Execute_VertexType(u32 op, u32 diff);

	// Overridden to change flushing behavior.
	void Execute_Call(u32 op, u32 diff);

	// Overridden for a dirty flag change.
	void Execute_BoundingBox(u32 op, u32 diff);

	void Execute_WorldMtxNum(u32 op, u32 diff);
	void Execute_ViewMtxNum(u32 op, u32 diff);
	void Execute_ProjMtxNum(u32 op, u32 diff);
	void Execute_TgenMtxNum(u32 op, u32 diff);
	void Execute_BoneMtxNum(u32 op, u32 diff);

	void Execute_WorldMtxData(u32 op, u32 diff);
	void Execute_ViewMtxData(u32 op, u32 diff);
	void Execute_ProjMtxData(u32 op, u32 diff);
	void Execute_TgenMtxData(u32 op, u32 diff);
	void Execute_BoneMtxData(u32 op, u32 diff);

	bool GetMatrix24(GEMatrixType type, u32_le *result, u32 cmdbits) override;
	void ResetMatrices() override;

	void Execute_ImmVertexAlphaPrim(u32 op, u32 diff);

	typedef void (SoftGPU::*CmdFunc)(u32 op, u32 diff);

	void BeginHostFrame(const DisplayLayoutConfig &config) override;
	bool PresentedThisFrame() const override;

protected:
	void FastRunLoop(DisplayList &list) override;
	void CopyToCurrentFboFromDisplayRam(const DisplayLayoutConfig &config, int srcwidth, int srcheight);
	void ConvertTextureDescFrom16(Draw::TextureDesc &desc, int srcwidth, int srcheight, const uint16_t *overrideData = nullptr);

	void BuildReportingInfo() override {}

private:
	void MarkDirty(uint32_t addr, uint32_t stride, uint32_t height, GEBufferFormat fmt, SoftGPUVRAMDirty value);
	void MarkDirty(uint32_t addr, uint32_t bytes, SoftGPUVRAMDirty value);
	bool ClearDirty(uint32_t addr, uint32_t stride, uint32_t height, GEBufferFormat fmt, SoftGPUVRAMDirty value);
	bool ClearDirty(uint32_t addr, uint32_t bytes, SoftGPUVRAMDirty value);

	uint8_t vramDirty_[2048];
	uint32_t lastDirtyAddr_ = 0;
	uint32_t lastDirtySize_ = 0;
	SoftGPUVRAMDirty lastDirtyValue_ = SoftGPUVRAMDirty::CLEAR;

	u32 displayFramebuf_;
	u32 displayStride_;
	GEBufferFormat displayFormat_;
	SoftDirty dirtyFlags_ = SoftDirty(-1);

	PresentationCommon *presentation_ = nullptr;
	SoftwareDrawEngine *drawEngine_ = nullptr;

	Draw::Texture *fbTex = nullptr;
	std::vector<u32> fbTexBuffer_;
};

// TODO: These shouldn't be global.
extern uint8_t clut[1024];
extern FormatBuffer fb;
extern FormatBuffer depthbuf;

// Type for the DarkStalkers stretch replacement.
enum class DSStretch {
	Off = 0,
	Normal,
	Wide,
};
