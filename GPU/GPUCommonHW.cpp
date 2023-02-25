#include "Common/GPU/thin3d.h"
#include "Common/Serialize/Serializer.h"
#include "Common/System/System.h"

#include "Core/System.h"

#include "GPU/GPUCommonHW.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"

GPUCommonHW::GPUCommonHW(GraphicsContext *gfxCtx, Draw::DrawContext *draw) : GPUCommon(gfxCtx, draw) {}

GPUCommonHW::~GPUCommonHW() {
	// Clear features so they're not visible in system info.
	gstate_c.SetUseFlags(0);

	// Delete the various common managers.
	framebufferManager_->DestroyAllFBOs();
	delete framebufferManager_;
	delete textureCache_;
	shaderManager_->ClearShaders();
	delete shaderManager_;
}

void GPUCommonHW::DeviceLost() {
	textureCache_->Clear(false);
	framebufferManager_->DeviceLost();
	textureCache_->DeviceLost();
	shaderManager_->DeviceLost();
}

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

void GPUCommonHW::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCache_->Clear(true);
		drawEngineCommon_->ClearTrackedVertexArrays();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManager_->DestroyAllFBOs();
	}
}

void GPUCommonHW::ClearCacheNextFrame() {
	textureCache_->ClearNextFrame();
}

// Needs to be called on GPU thread, not reporting thread.
void GPUCommonHW::BuildReportingInfo() {
	using namespace Draw;

	reportingPrimaryInfo_ = draw_->GetInfoString(InfoField::VENDORSTRING);
	reportingFullInfo_ = reportingPrimaryInfo_ + " - " + System_GetProperty(SYSPROP_GPUDRIVER_VERSION) + " - " + draw_->GetInfoString(InfoField::SHADELANGVERSION);
}
