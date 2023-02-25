#include "Common/GPU/thin3d.h"
#include "Common/Serialize/Serializer.h"
#include "Common/System/System.h"

#include "Core/System.h"
#include "Core/Config.h"

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

u32 GPUCommonHW::CheckGPUFeatures() const {
	u32 features = 0;
	if (draw_->GetDeviceCaps().logicOpSupported) {
		features |= GPU_USE_LOGIC_OP;
	}
	if (draw_->GetDeviceCaps().anisoSupported) {
		features |= GPU_USE_ANISOTROPY;
	}
	if (draw_->GetDeviceCaps().textureNPOTFullySupported) {
		features |= GPU_USE_TEXTURE_NPOT;
	}
	if (draw_->GetDeviceCaps().dualSourceBlend) {
		if (!g_Config.bVendorBugChecksEnabled || !draw_->GetBugs().Has(Draw::Bugs::DUAL_SOURCE_BLENDING_BROKEN)) {
			features |= GPU_USE_DUALSOURCE_BLEND;
		}
	}
	if (draw_->GetDeviceCaps().blendMinMaxSupported) {
		features |= GPU_USE_BLEND_MINMAX;
	}

	if (draw_->GetDeviceCaps().clipDistanceSupported) {
		features |= GPU_USE_CLIP_DISTANCE;
	}

	if (draw_->GetDeviceCaps().cullDistanceSupported) {
		features |= GPU_USE_CULL_DISTANCE;
	}

	if (draw_->GetDeviceCaps().textureDepthSupported) {
		features |= GPU_USE_DEPTH_TEXTURE;
	}

	if (draw_->GetDeviceCaps().depthClampSupported) {
		// Some backends always do GPU_USE_ACCURATE_DEPTH, but it's required for depth clamp.
		features |= GPU_USE_DEPTH_CLAMP | GPU_USE_ACCURATE_DEPTH;
	}

	bool canClipOrCull = draw_->GetDeviceCaps().clipDistanceSupported || draw_->GetDeviceCaps().cullDistanceSupported;
	bool canDiscardVertex = !draw_->GetBugs().Has(Draw::Bugs::BROKEN_NAN_IN_CONDITIONAL);
	if (canClipOrCull || canDiscardVertex) {
		// We'll dynamically use the parts that are supported, to reduce artifacts as much as possible.
		features |= GPU_USE_VS_RANGE_CULLING;
	}

	if (draw_->GetDeviceCaps().framebufferFetchSupported) {
		features |= GPU_USE_FRAMEBUFFER_FETCH;
	}

	if (draw_->GetShaderLanguageDesc().bitwiseOps) {
		features |= GPU_USE_LIGHT_UBERSHADER;
	}

	if (PSP_CoreParameter().compat.flags().ClearToRAM) {
		features |= GPU_USE_CLEAR_RAM_HACK;
	}

	// Even without depth clamp, force accurate depth on for some games that break without it.
	if (PSP_CoreParameter().compat.flags().DepthRangeHack) {
		features |= GPU_USE_ACCURATE_DEPTH;
	}

	return features;
}
