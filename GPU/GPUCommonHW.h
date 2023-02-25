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

	u32 CheckGPUFeatures() const override;

protected:
	void UpdateCmdInfo() override;

	void PreExecuteOp(u32 op, u32 diff);
	void ClearCacheNextFrame() override;

	// Needs to be called on GPU thread, not reporting thread.
	void BuildReportingInfo() override;
	void UpdateMSAALevel(Draw::DrawContext *draw) override;

	void CheckRenderResized() override;

	int msaaLevel_ = 0;
};

