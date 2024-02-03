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

#include "ppsspp_config.h"
#include <algorithm>
#include <functional>

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"

#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/TimeUtil.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"

#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"

using namespace PPSSPP_VK;

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

DrawEngineVulkan::DrawEngineVulkan(Draw::DrawContext *draw)
	: draw_(draw) {
	decOptions_.expandAllWeightsToFloat = false;
	decOptions_.expand8BitNormalsToFloat = false;
#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	decOptions_.alignOutputToWord = true;
#endif

	indexGen.Setup(decIndex_);
}

void DrawEngineVulkan::InitDeviceObjects() {
	// All resources we need for PSP drawing. Usually only bindings 0 and 2-4 are populated.

	BindingType bindingTypes[VKRPipelineLayout::MAX_DESC_SET_BINDINGS] = {
		BindingType::COMBINED_IMAGE_SAMPLER,  // main
		BindingType::COMBINED_IMAGE_SAMPLER,  // framebuffer-read
		BindingType::COMBINED_IMAGE_SAMPLER,  // palette
		BindingType::UNIFORM_BUFFER_DYNAMIC_ALL,  // uniforms
		BindingType::UNIFORM_BUFFER_DYNAMIC_VERTEX,  // lights
		BindingType::UNIFORM_BUFFER_DYNAMIC_VERTEX,  // bones
		BindingType::STORAGE_BUFFER_VERTEX,  // tess
		BindingType::STORAGE_BUFFER_VERTEX,
		BindingType::STORAGE_BUFFER_VERTEX,
	};

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	VkDevice device = vulkan->GetDevice();

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	pipelineLayout_ = renderManager->CreatePipelineLayout(bindingTypes, ARRAY_SIZE(bindingTypes), draw_->GetDeviceCaps().geometryShaderSupported, "drawengine_layout");

	pushUBO_ = (VulkanPushPool *)draw_->GetNativeObject(Draw::NativeObject::PUSH_POOL);
	pushVertex_ = new VulkanPushPool(vulkan, "pushVertex", 4 * 1024 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	pushIndex_ = new VulkanPushPool(vulkan, "pushIndex", 1 * 512 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

	VkSamplerCreateInfo samp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.magFilter = VK_FILTER_LINEAR;
	samp.minFilter = VK_FILTER_LINEAR;
	samp.maxLod = VK_LOD_CLAMP_NONE;  // recommended by best practices, has no effect since we don't use mipmaps.
	VkResult res = vkCreateSampler(device, &samp, nullptr, &samplerSecondaryLinear_);
	samp.magFilter = VK_FILTER_NEAREST;
	samp.minFilter = VK_FILTER_NEAREST;
	res = vkCreateSampler(device, &samp, nullptr, &samplerSecondaryNearest_);
	_dbg_assert_(VK_SUCCESS == res);
	res = vkCreateSampler(device, &samp, nullptr, &nullSampler_);
	_dbg_assert_(VK_SUCCESS == res);

	tessDataTransferVulkan = new TessellationDataTransferVulkan(vulkan);
	tessDataTransfer = tessDataTransferVulkan;

	draw_->SetInvalidationCallback(std::bind(&DrawEngineVulkan::Invalidate, this, std::placeholders::_1));
}

DrawEngineVulkan::~DrawEngineVulkan() {
	DestroyDeviceObjects();
}

void DrawEngineVulkan::DestroyDeviceObjects() {
	if (!draw_) {
		// We've already done this from LostDevice.
		return;
	}

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	draw_->SetInvalidationCallback(InvalidationCallback());

	delete tessDataTransferVulkan;
	tessDataTransfer = nullptr;
	tessDataTransferVulkan = nullptr;

	pushUBO_ = nullptr;

	if (pushVertex_) {
		pushVertex_->Destroy();
		delete pushVertex_;
		pushVertex_ = nullptr;
	}
	if (pushIndex_) {
		pushIndex_->Destroy();
		delete pushIndex_;
		pushIndex_ = nullptr;
	}

	if (samplerSecondaryNearest_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeleteSampler(samplerSecondaryNearest_);
	if (samplerSecondaryLinear_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeleteSampler(samplerSecondaryLinear_);
	if (nullSampler_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeleteSampler(nullSampler_);

	renderManager->DestroyPipelineLayout(pipelineLayout_);
}

void DrawEngineVulkan::DeviceLost() {
	DestroyDeviceObjects();
	DirtyAllUBOs();
	draw_ = nullptr;
}

void DrawEngineVulkan::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	InitDeviceObjects();
}

void DrawEngineVulkan::BeginFrame() {
	lastPipeline_ = nullptr;

	// pushUBO is the thin3d push pool, don't need to BeginFrame again.
	pushVertex_->BeginFrame();
	pushIndex_->BeginFrame();

	tessDataTransferVulkan->SetPushPool(pushUBO_);

	DirtyAllUBOs();
}

void DrawEngineVulkan::EndFrame() {
	stats_.pushVertexSpaceUsed = (int)pushVertex_->GetUsedThisFrame();
	stats_.pushIndexSpaceUsed = (int)pushIndex_->GetUsedThisFrame();
}

void DrawEngineVulkan::DirtyAllUBOs() {
	baseUBOOffset = 0;
	lightUBOOffset = 0;
	boneUBOOffset = 0;
	baseBuf = VK_NULL_HANDLE;
	lightBuf = VK_NULL_HANDLE;
	boneBuf = VK_NULL_HANDLE;
	dirtyUniforms_ = DIRTY_BASE_UNIFORMS | DIRTY_LIGHT_UNIFORMS | DIRTY_BONE_UNIFORMS;
	imageView = VK_NULL_HANDLE;
	sampler = VK_NULL_HANDLE;
	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

void DrawEngineVulkan::Invalidate(InvalidationCallbackFlags flags) {
	if (flags & InvalidationCallbackFlags::COMMAND_BUFFER_STATE) {
		// Nothing here anymore (removed the "frame descriptor set"
		// If we add back "seldomly-changing" descriptors, we might use this again.
	}
	if (flags & InvalidationCallbackFlags::RENDER_PASS_STATE) {
		// If have a new render pass, dirty our dynamic state so it gets re-set.
		//
		// Dirty everything that has dynamic state that will need re-recording.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_BLEND_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		lastPipeline_ = nullptr;
	}
}

// The inline wrapper in the header checks for numDrawCalls_ == 0
void DrawEngineVulkan::DoFlush() {
	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	PROFILE_THIS_SCOPE("Flush");

	bool tess = gstate_c.submitType == SubmitType::HW_BEZIER || gstate_c.submitType == SubmitType::HW_SPLINE;

	bool textureNeedsApply = false;
	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		// NOTE: After this is set, we MUST call ApplyTexture before returning.
		textureNeedsApply = true;
	} else if (gstate.getTextureAddress(0) == (gstate.getFrameBufRawAddress() | 0x04000000)) {
		// This catches the case of clearing a texture.
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}

	GEPrimitiveType prim = prevPrim_;

	// Always use software for flat shading to fix the provoking index.
	bool useHWTransform = CanUseHardwareTransform(prim) && (tess || gstate.getShadeMode() != GE_SHADE_FLAT);

	uint32_t ibOffset;
	uint32_t vbOffset;
	
	// The optimization to avoid indexing isn't really worth it on Vulkan since it means creating more pipelines.
	// This could be avoided with the new dynamic state extensions, but not available enough on mobile.
	const bool forceIndexed = draw_->GetDeviceCaps().verySlowShaderCompiler;

	if (useHWTransform) {
		VkBuffer vbuf = VK_NULL_HANDLE;
		VkBuffer ibuf = VK_NULL_HANDLE;
		if (decOptions_.applySkinInDecode && (lastVType_ & GE_VTYPE_WEIGHT_MASK)) {
			// If software skinning, we're predecoding into "decoded". So make sure we're done, then push that content.
			DecodeVerts(decoded_);
			VkDeviceSize size = numDecodedVerts_ * dec_->GetDecVtxFmt().stride;
			u8 *dest = (u8 *)pushVertex_->Allocate(size, 4, &vbuf, &vbOffset);
			memcpy(dest, decoded_, size);
		} else {
			// Figure out how much pushbuffer space we need to allocate.
			int vertsToDecode = ComputeNumVertsToDecode();
			// Decode directly into the pushbuffer
			u8 *dest = pushVertex_->Allocate(vertsToDecode * dec_->GetDecVtxFmt().stride, 4, &vbuf, &vbOffset);
			DecodeVerts(dest);
		}

		int vertexCount;
		int maxIndex;
		bool useElements;
		DecodeIndsAndGetData(&prim, &vertexCount, &maxIndex, &useElements, false);

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		if (textureNeedsApply) {
			textureCache_->ApplyTexture();
			textureCache_->GetVulkanHandles(imageView, sampler);
			if (imageView == VK_NULL_HANDLE)
				imageView = (VkImageView)draw_->GetNativeObject(gstate_c.textureIsArray ? Draw::NativeObject::NULL_IMAGEVIEW_ARRAY : Draw::NativeObject::NULL_IMAGEVIEW);
			if (sampler == VK_NULL_HANDLE)
				sampler = nullSampler_;
		}

		if (!lastPipeline_ || gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE) || prim != lastPrim_) {
			if (prim != lastPrim_ || gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE)) {
				ConvertStateToVulkanKey(*framebufferManager_, shaderManager_, prim, pipelineKey_, dynState_);
			}

			VulkanVertexShader *vshader = nullptr;
			VulkanFragmentShader *fshader = nullptr;
			VulkanGeometryShader *gshader = nullptr;

			shaderManager_->GetShaders(prim, dec_, &vshader, &fshader, &gshader, pipelineState_, true, useHWTessellation_, decOptions_.expandAllWeightsToFloat, decOptions_.applySkinInDecode);
			_dbg_assert_msg_(vshader->UseHWTransform(), "Bad vshader");
			VulkanPipeline *pipeline = pipelineManager_->GetOrCreatePipeline(renderManager, pipelineLayout_, pipelineKey_, &dec_->decFmt, vshader, fshader, gshader, true, 0, framebufferManager_->GetMSAALevel(), false);
			if (!pipeline || !pipeline->pipeline) {
				// Already logged, let's bail out.
				ResetAfterDraw();
				return;
			}
			BindShaderBlendTex();  // This might cause copies so important to do before BindPipeline.

			if (!renderManager->BindPipeline(pipeline->pipeline, pipeline->pipelineFlags, pipelineLayout_)) {
				renderManager->ReportBadStateForDraw();
				ResetAfterDraw();
				return;
			}
			if (pipeline != lastPipeline_) {
				if (lastPipeline_ && !(lastPipeline_->UsesBlendConstant() && pipeline->UsesBlendConstant())) {
					gstate_c.Dirty(DIRTY_BLEND_STATE);
				}
				lastPipeline_ = pipeline;
			}
			ApplyDrawStateLate(renderManager, false, 0, pipeline->UsesBlendConstant());
			gstate_c.Clean(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
			gstate_c.Dirty(dirtyRequiresRecheck_);
			dirtyRequiresRecheck_ = 0;
			lastPipeline_ = pipeline;
		}
		lastPrim_ = prim;

		dirtyUniforms_ |= shaderManager_->UpdateUniforms(framebufferManager_->UseBufferedRendering());
		UpdateUBOs();

		int descCount = 6;
		if (tess)
			descCount = 9;
		int descSetIndex;
		PackedDescriptor *descriptors = renderManager->PushDescriptorSet(descCount, &descSetIndex);
		descriptors[0].image.view = imageView;
		descriptors[0].image.sampler = sampler;

		descriptors[1].image.view = boundSecondary_;
		descriptors[1].image.sampler = samplerSecondaryNearest_;

		descriptors[2].image.view = boundDepal_;
		descriptors[2].image.sampler = (boundDepal_ && boundDepalSmoothed_) ? samplerSecondaryLinear_ : samplerSecondaryNearest_;

		descriptors[3].buffer.buffer = baseBuf;
		descriptors[3].buffer.range = sizeof(UB_VS_FS_Base);
		descriptors[3].buffer.offset = 0;

		descriptors[4].buffer.buffer = lightBuf;
		descriptors[4].buffer.range = sizeof(UB_VS_Lights);
		descriptors[4].buffer.offset = 0;

		descriptors[5].buffer.buffer = boneBuf;
		descriptors[5].buffer.range = sizeof(UB_VS_Bones);
		descriptors[5].buffer.offset = 0;
		if (tess) {
			const VkDescriptorBufferInfo *bufInfo = tessDataTransferVulkan->GetBufferInfo();
			for (int j = 0; j < 3; j++) {
				descriptors[j + 6].buffer.buffer = bufInfo[j].buffer;
				descriptors[j + 6].buffer.range = bufInfo[j].range;
				descriptors[j + 6].buffer.offset = bufInfo[j].offset;
			}
		}
		// TODO: Can we avoid binding all three when not needed? Same below for hardware transform.
		// Think this will require different descriptor set layouts.
		const uint32_t dynamicUBOOffsets[3] = {
			baseUBOOffset, lightUBOOffset, boneUBOOffset,
		};
		if (useElements) {
			if (!ibuf) {
				ibOffset = (uint32_t)pushIndex_->Push(decIndex_, sizeof(uint16_t) * vertexCount, 4, &ibuf);
			}
			renderManager->DrawIndexed(descSetIndex, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, ibuf, ibOffset, vertexCount, 1);
		} else {
			renderManager->Draw(descSetIndex, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, vertexCount);
		}
	} else {
		PROFILE_THIS_SCOPE("soft");
		if (!decOptions_.applySkinInDecode) {
			decOptions_.applySkinInDecode = true;
			lastVType_ |= (1 << 26);
			dec_ = GetVertexDecoder(lastVType_);
		}
		int prevDecodedVerts = numDecodedVerts_;

		DecodeVerts(decoded_);
		int vertexCount = DecodeInds();

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		gpuStats.numUncachedVertsDrawn += vertexCount;
		prim = IndexGenerator::GeneralPrim((GEPrimitiveType)drawInds_[0].prim);

		u16 *inds = decIndex_;
		SoftwareTransformResult result{};
		SoftwareTransformParams params{};
		params.decoded = decoded_;
		params.transformed = transformed_;
		params.transformedExpanded = transformedExpanded_;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		// In Vulkan, we have to force drawing of primitives if !framebufferManager_->UseBufferedRendering() because Vulkan clears
		// do not respect scissor rects.
		params.allowClear = framebufferManager_->UseBufferedRendering();
		params.allowSeparateAlphaClear = false;
		params.provokeFlatFirst = true;
		params.flippedY = true;
		params.usesHalfZ = true;

		// We need to update the viewport early because it's checked for flipping in SoftwareTransform.
		// We don't have a "DrawStateEarly" in vulkan, so...
		// TODO: Probably should eventually refactor this and feed the vp size into SoftwareTransform directly (Unknown's idea).
		if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
			ViewportAndScissor vpAndScissor;
			ConvertViewportAndScissor(framebufferManager_->UseBufferedRendering(),
				framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
				framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
				vpAndScissor);
			UpdateCachedViewportState(vpAndScissor);
		}

		SoftwareTransform swTransform(params);

		const Lin::Vec3 trans(gstate_c.vpXOffset, gstate_c.vpYOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
		const Lin::Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
		swTransform.SetProjMatrix(gstate.projMatrix, gstate_c.vpWidth < 0, gstate_c.vpHeight < 0, trans, scale);

		swTransform.Transform(prim, dec_->VertexType(), dec_->GetDecVtxFmt(), numDecodedVerts_, &result);
		// Non-zero depth clears are unusual, but some drivers don't match drawn depth values to cleared values.
		// Games sometimes expect exact matches (see #12626, for example) for equal comparisons.
		if (result.action == SW_CLEAR && everUsedEqualDepth_ && gstate.isClearModeDepthMask() && result.depth > 0.0f && result.depth < 1.0f)
			result.action = SW_NOT_READY;

		if (result.action == SW_NOT_READY) {
			// decIndex_ here is always equal to inds currently, but it may not be in the future.
			swTransform.BuildDrawingParams(prim, vertexCount, dec_->VertexType(), inds, RemainingIndices(inds), numDecodedVerts_, VERTEX_BUFFER_MAX, &result);
		}

		if (result.setSafeSize)
			framebufferManager_->SetSafeSize(result.safeWidth, result.safeHeight);

		// Only here, where we know whether to clear or to draw primitives, should we actually set the current framebuffer! Because that gives use the opportunity
		// to use a "pre-clear" render pass, for high efficiency on tilers.
		if (result.action == SW_DRAW_PRIMITIVES) {
			if (textureNeedsApply) {
				gstate_c.pixelMapped = result.pixelMapped;
				textureCache_->ApplyTexture();
				gstate_c.pixelMapped = false;
				textureCache_->GetVulkanHandles(imageView, sampler);
				if (imageView == VK_NULL_HANDLE)
					imageView = (VkImageView)draw_->GetNativeObject(gstate_c.textureIsArray ? Draw::NativeObject::NULL_IMAGEVIEW_ARRAY : Draw::NativeObject::NULL_IMAGEVIEW);
				if (sampler == VK_NULL_HANDLE)
					sampler = nullSampler_;
			}
			if (!lastPipeline_ || gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE) || prim != lastPrim_) {
				if (prim != lastPrim_ || gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE)) {
					ConvertStateToVulkanKey(*framebufferManager_, shaderManager_, prim, pipelineKey_, dynState_);
				}

				VulkanVertexShader *vshader = nullptr;
				VulkanFragmentShader *fshader = nullptr;
				VulkanGeometryShader *gshader = nullptr;

				shaderManager_->GetShaders(prim, dec_, &vshader, &fshader, &gshader, pipelineState_, false, false, decOptions_.expandAllWeightsToFloat, true);
				_dbg_assert_msg_(!vshader->UseHWTransform(), "Bad vshader");
				VulkanPipeline *pipeline = pipelineManager_->GetOrCreatePipeline(renderManager, pipelineLayout_, pipelineKey_, &dec_->decFmt, vshader, fshader, gshader, false, 0, framebufferManager_->GetMSAALevel(), false);
				if (!pipeline || !pipeline->pipeline) {
					// Already logged, let's bail out.
					ResetAfterDraw();
					return;
				}
				BindShaderBlendTex();  // This might cause copies so super important to do before BindPipeline.

				if (!renderManager->BindPipeline(pipeline->pipeline, pipeline->pipelineFlags, pipelineLayout_)) {
					renderManager->ReportBadStateForDraw();
					ResetAfterDraw();
					return;
				}
				if (pipeline != lastPipeline_) {
					if (lastPipeline_ && !lastPipeline_->UsesBlendConstant() && pipeline->UsesBlendConstant()) {
						gstate_c.Dirty(DIRTY_BLEND_STATE);
					}
					lastPipeline_ = pipeline;
				}
				ApplyDrawStateLate(renderManager, result.setStencil, result.stencilValue, pipeline->UsesBlendConstant());
				gstate_c.Clean(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
				gstate_c.Dirty(dirtyRequiresRecheck_);
				dirtyRequiresRecheck_ = 0;
				lastPipeline_ = pipeline;
			}

			lastPrim_ = prim;

			dirtyUniforms_ |= shaderManager_->UpdateUniforms(framebufferManager_->UseBufferedRendering());

			// Even if the first draw is through-mode, make sure we at least have one copy of these uniforms buffered
			UpdateUBOs();

			int descCount = 6;
			int descSetIndex;
			PackedDescriptor *descriptors = renderManager->PushDescriptorSet(descCount, &descSetIndex);
			descriptors[0].image.view = imageView;
			descriptors[0].image.sampler = sampler;
			descriptors[1].image.view = boundSecondary_;
			descriptors[1].image.sampler = samplerSecondaryNearest_;
			descriptors[2].image.view = boundDepal_;
			descriptors[2].image.sampler = (boundDepal_ && boundDepalSmoothed_) ? samplerSecondaryLinear_ : samplerSecondaryNearest_;
			descriptors[3].buffer.buffer = baseBuf;
			descriptors[3].buffer.range = sizeof(UB_VS_FS_Base);
			descriptors[4].buffer.buffer = lightBuf;
			descriptors[4].buffer.range = sizeof(UB_VS_Lights);
			descriptors[5].buffer.buffer = boneBuf;
			descriptors[5].buffer.range = sizeof(UB_VS_Bones);

			const uint32_t dynamicUBOOffsets[3] = {
				baseUBOOffset, lightUBOOffset, boneUBOOffset,
			};

			PROFILE_THIS_SCOPE("renderman_q");

			if (result.drawIndexed) {
				VkBuffer vbuf, ibuf;
				vbOffset = (uint32_t)pushVertex_->Push(result.drawBuffer, numDecodedVerts_ * sizeof(TransformedVertex), 4, &vbuf);
				ibOffset = (uint32_t)pushIndex_->Push(inds, sizeof(short) * result.drawNumTrans, 4, &ibuf);
				renderManager->DrawIndexed(descSetIndex, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, ibuf, ibOffset, result.drawNumTrans, 1);
			} else {
				VkBuffer vbuf;
				vbOffset = (uint32_t)pushVertex_->Push(result.drawBuffer, result.drawNumTrans * sizeof(TransformedVertex), 4, &vbuf);
				renderManager->Draw(descSetIndex, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, result.drawNumTrans);
			}
		} else if (result.action == SW_CLEAR) {
			// Note: we won't get here if the clear is alpha but not color, or color but not alpha.
			bool clearColor = gstate.isClearModeColorMask();
			bool clearAlpha = gstate.isClearModeAlphaMask();  // and stencil
			bool clearDepth = gstate.isClearModeDepthMask();
			int mask = 0;
			// The Clear detection takes care of doing a regular draw instead if separate masking
			// of color and alpha is needed, so we can just treat them as the same.
			if (clearColor || clearAlpha) mask |= Draw::FBChannel::FB_COLOR_BIT;
			if (clearDepth) mask |= Draw::FBChannel::FB_DEPTH_BIT;
			if (clearAlpha) mask |= Draw::FBChannel::FB_STENCIL_BIT;
			// Note that since the alpha channel and the stencil channel are shared on the PSP,
			// when we clear alpha, we also clear stencil to the same value.
			draw_->Clear(mask, result.color, result.depth, result.color >> 24);
			if (clearColor || clearAlpha) {
				framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
			}
			if (gstate_c.Use(GPU_USE_CLEAR_RAM_HACK) && gstate.isClearModeColorMask() && (gstate.isClearModeAlphaMask() || gstate.FrameBufFormat() == GE_FORMAT_565)) {
				int scissorX1 = gstate.getScissorX1();
				int scissorY1 = gstate.getScissorY1();
				int scissorX2 = gstate.getScissorX2() + 1;
				int scissorY2 = gstate.getScissorY2() + 1;
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, result.color);
			}
		}
		decOptions_.applySkinInDecode = g_Config.bSoftwareSkinning;
	}

	ResetAfterDrawInline();

	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

	GPUDebug::NotifyDraw();
}

void DrawEngineVulkan::ResetAfterDraw() {
	indexGen.Reset();
	numDecodedVerts_ = 0;
	numDrawVerts_ = 0;
	numDrawInds_ = 0;
	vertexCountInDrawCalls_ = 0;
	decodeIndsCounter_ = 0;
	decodeVertsCounter_ = 0;
	decOptions_.applySkinInDecode = g_Config.bSoftwareSkinning;
	gstate_c.vertexFullAlpha = true;
}

void DrawEngineVulkan::UpdateUBOs() {
	if ((dirtyUniforms_ & DIRTY_BASE_UNIFORMS) || baseBuf == VK_NULL_HANDLE) {
		baseUBOOffset = shaderManager_->PushBaseBuffer(pushUBO_, &baseBuf);
		dirtyUniforms_ &= ~DIRTY_BASE_UNIFORMS;
	}
	if ((dirtyUniforms_ & DIRTY_LIGHT_UNIFORMS) || lightBuf == VK_NULL_HANDLE) {
		lightUBOOffset = shaderManager_->PushLightBuffer(pushUBO_, &lightBuf);
		dirtyUniforms_ &= ~DIRTY_LIGHT_UNIFORMS;
	}
	if ((dirtyUniforms_ & DIRTY_BONE_UNIFORMS) || boneBuf == VK_NULL_HANDLE) {
		boneUBOOffset = shaderManager_->PushBoneBuffer(pushUBO_, &boneBuf);
		dirtyUniforms_ &= ~DIRTY_BONE_UNIFORMS;
	}
}

void TessellationDataTransferVulkan::SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) {
	// SSBOs that are not simply float1 or float2 need to be padded up to a float4 size. vec3 members
	// also need to be 16-byte aligned, hence the padding.
	struct TessData {
		float pos[3]; float pad1;
		float uv[2]; float pad2[2];
		float color[4];
	};

	int size = size_u * size_v;

	int ssboAlignment = vulkan_->GetPhysicalDeviceProperties().properties.limits.minStorageBufferOffsetAlignment;
	uint8_t *data = (uint8_t *)push_->Allocate(size * sizeof(TessData), ssboAlignment, &bufInfo_[0].buffer, (uint32_t *)&bufInfo_[0].offset);
	bufInfo_[0].range = size * sizeof(TessData);

	float *pos = (float *)(data);
	float *tex = (float *)(data + offsetof(TessData, uv));
	float *col = (float *)(data + offsetof(TessData, color));
	int stride = sizeof(TessData) / sizeof(float);

	CopyControlPoints(pos, tex, col, stride, stride, stride, points, size, vertType);

	using Spline::Weight;

	// Weights U
	data = (uint8_t *)push_->Allocate(weights.size_u * sizeof(Weight), ssboAlignment, &bufInfo_[1].buffer, (uint32_t *)&bufInfo_[1].offset);
	memcpy(data, weights.u, weights.size_u * sizeof(Weight));
	bufInfo_[1].range = weights.size_u * sizeof(Weight);

	// Weights V
	data = (uint8_t *)push_->Allocate(weights.size_v * sizeof(Weight), ssboAlignment, &bufInfo_[2].buffer, (uint32_t *)&bufInfo_[2].offset);
	memcpy(data, weights.v, weights.size_v * sizeof(Weight));
	bufInfo_[2].range = weights.size_v * sizeof(Weight);
}
