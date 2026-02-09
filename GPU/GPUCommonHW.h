#pragma once

#include "GPUCommon.h"

// Shared GPUCommon implementation for the HW backends.
// Things that are irrelevant for SoftGPU should live here.
class GPUCommonHW : public GPUCommon {
public:
	GPUCommonHW(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPUCommonHW() override;

	// This can fail, and if so no render pass is active.
	void SetCurFramebufferDirty(bool dirty) override { curFramebufferDirty_ = dirty; }
	void PrepareCopyDisplayToOutput(const DisplayLayoutConfig &config) override;
	void CopyDisplayToOutput(const DisplayLayoutConfig &config) override;
	void DoState(PointerWrap &p) override;
	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	void BeginHostFrame(const DisplayLayoutConfig &config) override;

	u32 CheckGPUFeatures() const override;

	// From GPUDebugInterface.
	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) override;
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer) override;
	bool GetOutputFramebuffer(GPUDebugBuffer &buffer) override;
	std::vector<const VirtualFramebuffer *> GetFramebufferList() const override;
	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level, bool *isFramebuffer) override;
	bool GetCurrentClut(GPUDebugBuffer &buffer) override;

	FramebufferManagerCommon *GetFramebufferManagerCommon() override {
		return framebufferManager_;
	}
	TextureCacheCommon *GetTextureCacheCommon() override {
		return textureCache_;
	}

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;
	void InvalidateCache(u32 addr, int size, GPUInvalidationType type) override;

	u32 DrawSync(int mode) override;
	int ListSync(int listid, int mode) override;

	bool FramebufferDirty() override;
	bool FramebufferReallyDirty() override;

	void Execute_VertexType(u32 op, u32 diff);
	void Execute_VertexTypeSkinning(u32 op, u32 diff);

	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_BlockTransferStart(u32 op, u32 diff);

	void Execute_TexSize0(u32 op, u32 diff);
	void Execute_TexLevel(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);

	void Execute_WorldMtxNum(u32 op, u32 diff);
	void Execute_WorldMtxData(u32 op, u32 diff);
	void Execute_ViewMtxNum(u32 op, u32 diff);
	void Execute_ViewMtxData(u32 op, u32 diff);
	void Execute_ProjMtxNum(u32 op, u32 diff);
	void Execute_ProjMtxData(u32 op, u32 diff);
	void Execute_TgenMtxNum(u32 op, u32 diff);
	void Execute_TgenMtxData(u32 op, u32 diff);
	void Execute_BoneMtxNum(u32 op, u32 diff);
	void Execute_BoneMtxData(u32 op, u32 diff);

	void Execute_TexFlush(u32 op, u32 diff);

	// TODO: Have these return an error code if they jump to a bad address. If bad, stop the FastRunLoop.
	typedef void (GPUCommonHW::*CmdFunc)(u32 op, u32 diff);

	void FastRunLoop(DisplayList &list) override;
	void ExecuteOp(u32 op, u32 diff) override;

	bool PresentedThisFrame() const override;

private:
	void CheckDepthUsage(VirtualFramebuffer *vfb) override;
	void CheckFlushOp(int cmd, u32 diff);

protected:
	size_t FormatGPUStatsCommon(char *buf, size_t size);
	void UpdateCmdInfo() override;

	void PreExecuteOp(u32 op, u32 diff) override;
	void ClearCacheNextFrame() override;

	// Needs to be called on GPU thread, not reporting thread.
	void BuildReportingInfo() override;
	void UpdateMSAALevel(Draw::DrawContext *draw) override;

	void CheckDisplayResized() override;
	void CheckRenderResized(const DisplayLayoutConfig &config) override;
	void CheckConfigChanged(const DisplayLayoutConfig &config) override;

	u32 CheckGPUFeaturesLate(u32 features) const;

	int msaaLevel_ = 0;
	bool sawExactEqualDepth_ = false;
	ShaderManagerCommon *shaderManager_ = nullptr;
	bool curFramebufferDirty_ = false;
};
