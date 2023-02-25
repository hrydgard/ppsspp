#pragma once

#include "GPUCommon.h"

// Shared GPUCommon implementation for the HW backends.
// Things that are irrelevant for SoftGPU should live here.
class GPUCommonHW : public GPUCommon {
public:
	GPUCommonHW(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPUCommonHW();

	void CopyDisplayToOutput(bool reallyDirty) override;
	void DoState(PointerWrap &p) override;
	void DeviceLost() override;

	void BeginFrame() override;

	u32 CheckGPUFeatures() const override;

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

	void Execute_VertexType(u32 op, u32 diff);
	void Execute_VertexTypeSkinning(u32 op, u32 diff);

	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_BlockTransferStart(u32 op, u32 diff);

	void Execute_TexSize0(u32 op, u32 diff);
	void Execute_TexLevel(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);

	typedef void (GPUCommonHW::*CmdFunc)(u32 op, u32 diff);

	void FastRunLoop(DisplayList &list) override;
	void ExecuteOp(u32 op, u32 diff) override;

protected:
	void UpdateCmdInfo() override;

	void PreExecuteOp(u32 op, u32 diff) override;
	void ClearCacheNextFrame() override;

	// Needs to be called on GPU thread, not reporting thread.
	void BuildReportingInfo() override;
	void UpdateMSAALevel(Draw::DrawContext *draw) override;

	void CheckRenderResized() override;
	u32 CheckGPUFeaturesLate(u32 features) const;

	int msaaLevel_ = 0;
	bool sawExactEqualDepth_ = false;

private:
	void CheckDepthUsage(VirtualFramebuffer *vfb);
	void CheckFlushOp(int cmd, u32 diff);
};
