#include "Common/GPU/thin3d.h"
#include "GPU/GPUCommonHW.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"


GPUCommonHW::GPUCommonHW(GraphicsContext *gfxCtx, Draw::DrawContext *draw) : GPUCommon(gfxCtx, draw) {

}

GPUCommonHW::~GPUCommonHW() {}

void GPUCommonHW::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPUCommonHW::CopyDisplayToOutput(bool reallyDirty) {
	// Flush anything left over.
	drawEngineCommon_->DispatchFlush();

	shaderManager_->DirtyLastShader();

	framebufferManager_->CopyDisplayToOutput(reallyDirty);

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}
