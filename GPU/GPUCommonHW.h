#pragma once

#include "GPUCommon.h"

// Shared GPUCommon implementation for the HW backends.
// Things that are irrelevant for SoftGPU should live here.
class GPUCommonHW : public GPUCommon {
public:
	GPUCommonHW(GraphicsContext *gfxCtx, Draw::DrawContext *draw);
	~GPUCommonHW();

	void CopyDisplayToOutput(bool reallyDirty) override;

protected:
	void PreExecuteOp(u32 op, u32 diff);
};

