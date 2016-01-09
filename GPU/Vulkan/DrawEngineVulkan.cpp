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

#include <cassert>

#include "base/logging.h"
#include "base/timeutil.h"

#include "Common/MemoryUtil.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"

enum {
	DRAW_BINDING_TEXTURE = 0,
	DRAW_BINDING_2ND_TEXTURE = 1,
	DRAW_BINDING_DYNUBO_BASE = 2,
	DRAW_BINDING_DYNUBO_LIGHT = 3,
	DRAW_BINDING_DYNUBO_BONE = 4,
};

const VkPrimitiveTopology prim[8] = {
	VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  // Vulkan doesn't do quads. We could do strips with restart-index though. We could also do RECT primitives in the geometry shader.
};

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

DrawEngineVulkan::DrawEngineVulkan(VulkanContext *vulkan)
	:
	vulkan_(vulkan), 
	decodedVerts_(0),
	prevPrim_(GE_PRIM_INVALID),
	lastVType_(-1),
	pipelineManager_(nullptr),
	textureCache_(nullptr),
	framebufferManager_(nullptr),
	numDrawCalls(0),
	vertexCountInDrawCalls(0),
	decodeCounter_(0),
	fboTexNeedBind_(false),
	fboTexBound_(false),
	curFrame_(0) {

	memset(&decOptions_, 0, sizeof(decOptions_));
	decOptions_.expandAllUVtoFloat = true;
	decOptions_.expandAllWeightsToFloat = true;
	decOptions_.expand8BitNormalsToFloat = true;

	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE);
	splineBuffer = (u8 *)AllocateMemoryPages(SPLINE_BUFFER_SIZE);
	transformed = (TransformedVertex *)AllocateMemoryPages(TRANSFORMED_VERTEX_BUFFER_SIZE);
	transformedExpanded = (TransformedVertex *)AllocateMemoryPages(3 * TRANSFORMED_VERTEX_BUFFER_SIZE);

	indexGen.Setup(decIndex);

	// All resources we need for PSP drawing. Usually only bindings 0 and 2-4 are populated.
	VkDescriptorSetLayoutBinding bindings[5];
	bindings[0].descriptorCount = 1;
	bindings[0].pImmutableSamplers = nullptr;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[0].binding = DRAW_BINDING_TEXTURE;
	bindings[1].descriptorCount = 1;
	bindings[1].pImmutableSamplers = nullptr;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = DRAW_BINDING_2ND_TEXTURE;
	bindings[2].descriptorCount = 1;
	bindings[2].pImmutableSamplers = nullptr;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[2].binding = DRAW_BINDING_DYNUBO_BASE;
	bindings[3].descriptorCount = 1;
	bindings[3].pImmutableSamplers = nullptr;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[3].binding = DRAW_BINDING_DYNUBO_LIGHT;
	bindings[4].descriptorCount = 1;
	bindings[4].pImmutableSamplers = nullptr;
	bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[4].binding = DRAW_BINDING_DYNUBO_BONE;

	VkDevice device = vulkan_->GetDevice();

	VkDescriptorSetLayoutCreateInfo dsl;
	dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dsl.pNext = nullptr;
	dsl.bindingCount = 5;
	dsl.pBindings = bindings;
	VkResult res = vkCreateDescriptorSetLayout(device, &dsl, nullptr, &descriptorSetLayout_);

	VkDescriptorPoolSize dpTypes[2];
	dpTypes[0].descriptorCount = 800;
	dpTypes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	dpTypes[1].descriptorCount = 200;
	dpTypes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo dp;
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.pNext = nullptr;
	dp.flags = 0;   // Don't want to mess around with individually freeing these, let's go fixed each frame and zap the whole array. Might try the dynamic approach later.
	dp.maxSets = 1000;
	dp.pPoolSizes = dpTypes;
	dp.poolSizeCount = ARRAY_SIZE(dpTypes);
	res = vkCreateDescriptorPool(device, &dp, nullptr, &frame_[0].descPool);
	assert(VK_SUCCESS == res);
	res = vkCreateDescriptorPool(device, &dp, nullptr, &frame_[1].descPool);
	assert(VK_SUCCESS == res);

	// We are going to use one-shot descriptors in the initial implementation. Might look into caching them
	// if creating and updating them turns out to be expensive.
	for (int i = 0; i < 2; i++) {
		VkResult res = vkCreateDescriptorPool(vulkan_->GetDevice(), &dp, nullptr, &frame_[i].descPool);
		assert(VK_SUCCESS == res);
		frame_[i].pushData = new VulkanPushBuffer(vulkan_, 4 * 1024 * 1024);  // TODO: Do something more dynamic
	}

	VkPipelineLayoutCreateInfo pl;
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.pNext = nullptr;
	pl.pPushConstantRanges = nullptr;
	pl.pushConstantRangeCount = 0;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &descriptorSetLayout_;
	res = vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout_);
	assert(VK_SUCCESS == res);

	VkSamplerCreateInfo samp = {};
	samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samp.pNext = nullptr;
	samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samp.flags = 0;
	samp.magFilter = VK_FILTER_LINEAR;
	samp.minFilter = VK_FILTER_LINEAR;
	res = vkCreateSampler(device, &samp, nullptr, &depalSampler_);
	assert(VK_SUCCESS == res);
}

void DrawEngineVulkan::BeginFrame() {
	FrameData *frame = &frame_[curFrame_ & 1];
	vkResetDescriptorPool(vulkan_->GetDevice(), frame->descPool, 0);
	frame->pushData->Begin(vulkan_->GetDevice());
	frame->pushData->Reset();
}

void DrawEngineVulkan::EndFrame() {
	FrameData *frame = &frame_[curFrame_ & 1];
	frame->pushData->End(vulkan_->GetDevice());
	curFrame_++;
}

DrawEngineVulkan::~DrawEngineVulkan() {
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(splineBuffer, SPLINE_BUFFER_SIZE);
	FreeMemoryPages(transformed, TRANSFORMED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(transformedExpanded, 3 * TRANSFORMED_VERTEX_BUFFER_SIZE);

	for (int i = 0; i < 2; i++) {
		vulkan_->QueueDelete(frame_[i].descPool);
		delete frame_[i].pushData;
	}
	vulkan_->QueueDelete(depalSampler_);
}

VertexDecoder *DrawEngineVulkan::GetVertexDecoder(u32 vtype) {
	auto iter = decoderMap_.find(vtype);
	if (iter != decoderMap_.end())
		return iter->second;
	VertexDecoder *dec = new VertexDecoder();
	dec->SetVertexType(vtype, decOptions_, decJitCache_);
	decoderMap_[vtype] = dec;
	return dec;
}

void DrawEngineVulkan::SetupVertexDecoder(u32 vertType) {
	SetupVertexDecoderInternal(vertType);
}

inline void DrawEngineVulkan::SetupVertexDecoderInternal(u32 vertType) {
	// As the decoder depends on the UVGenMode when we use UV prescale, we simply mash it
	// into the top of the verttype where there are unused bits.
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);

	// If vtype has changed, setup the vertex decoder.
	if (vertTypeID != lastVType_) {
		dec_ = GetVertexDecoder(vertTypeID);
		lastVType_ = vertTypeID;
	}
}

void DrawEngineVulkan::SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) {
	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawCalls >= MAX_DEFERRED_DRAW_CALLS || vertexCountInDrawCalls + vertexCount > VERTEX_BUFFER_MAX)
		Flush(cmd_);

	// TODO: Is this the right thing to do?
	if (prim == GE_PRIM_KEEP_PREVIOUS) {
		prim = prevPrim_ != GE_PRIM_INVALID ? prevPrim_ : GE_PRIM_POINTS;
	} else {
		prevPrim_ = prim;
	}

	SetupVertexDecoderInternal(vertType);

	*bytesRead = vertexCount * dec_->VertexSize();

	if ((vertexCount < 2 && prim > 0) || (vertexCount < 3 && prim > 2 && prim != GE_PRIM_RECTANGLES))
		return;

	DeferredDrawCall &dc = drawCalls[numDrawCalls];
	dc.verts = verts;
	dc.inds = inds;
	dc.vertType = vertType;
	dc.indexType = (vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT;
	dc.prim = prim;
	dc.vertexCount = vertexCount;

	if (inds) {
		GetIndexBounds(inds, vertexCount, vertType, &dc.indexLowerBound, &dc.indexUpperBound);
	} else {
		dc.indexLowerBound = 0;
		dc.indexUpperBound = vertexCount - 1;
	}

	numDrawCalls++;
	vertexCountInDrawCalls += vertexCount;

	if (g_Config.bSoftwareSkinning && (vertType & GE_VTYPE_WEIGHT_MASK)) {
		DecodeVertsStep();
		decodeCounter_++;
	}

	if (prim == GE_PRIM_RECTANGLES && (gstate.getTextureAddress(0) & 0x3FFFFFFF) == (gstate.getFrameBufAddress() & 0x3FFFFFFF)) {
		// Rendertarget == texture?
		if (!g_Config.bDisableSlowFramebufEffects) {
			gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
			Flush(cmd_);
		}
	}
}

void DrawEngineVulkan::DecodeVerts() {
	for (; decodeCounter_ < numDrawCalls; decodeCounter_++) {
		DecodeVertsStep();
	}
	// Sanity check
	if (indexGen.Prim() < 0) {
		ERROR_LOG_REPORT(G3D, "DecodeVerts: Failed to deduce prim: %i", indexGen.Prim());
		// Force to points (0)
		indexGen.AddPrim(GE_PRIM_POINTS, 0);
	}
}

void DrawEngineVulkan::DecodeVertsStep() {
	const int i = decodeCounter_;

	const DeferredDrawCall &dc = drawCalls[i];

	indexGen.SetIndex(decodedVerts_);
	int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;

	u32 indexType = dc.indexType;
	void *inds = dc.inds;
	if (indexType == GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT) {
		// Decode the verts and apply morphing. Simple.
		dec_->DecodeVerts(decoded + decodedVerts_ * (int)dec_->GetDecVtxFmt().stride,
			dc.verts, indexLowerBound, indexUpperBound);
		decodedVerts_ += indexUpperBound - indexLowerBound + 1;
		indexGen.AddPrim(dc.prim, dc.vertexCount);
	} else {
		// It's fairly common that games issue long sequences of PRIM calls, with differing
		// inds pointer but the same base vertex pointer. We'd like to reuse vertices between
		// these as much as possible, so we make sure here to combine as many as possible
		// into one nice big drawcall, sharing data.

		// 1. Look ahead to find the max index, only looking as "matching" drawcalls.
		//    Expand the lower and upper bounds as we go.
		int lastMatch = i;
		const int total = numDrawCalls;
		for (int j = i + 1; j < total; ++j) {
			if (drawCalls[j].verts != dc.verts)
				break;

			indexLowerBound = std::min(indexLowerBound, (int)drawCalls[j].indexLowerBound);
			indexUpperBound = std::max(indexUpperBound, (int)drawCalls[j].indexUpperBound);
			lastMatch = j;
		}

		// 2. Loop through the drawcalls, translating indices as we go.
		switch (indexType) {
		case GE_VTYPE_IDX_8BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u8 *)drawCalls[j].inds, indexLowerBound);
			}
			break;
		case GE_VTYPE_IDX_16BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u16 *)drawCalls[j].inds, indexLowerBound);
			}
			break;
		}

		const int vertexCount = indexUpperBound - indexLowerBound + 1;

		// This check is a workaround for Pangya Fantasy Golf, which sends bogus index data when switching items in "My Room" sometimes.
		if (decodedVerts_ + vertexCount > VERTEX_BUFFER_MAX) {
			return;
		}

		// 3. Decode that range of vertex data.
		dec_->DecodeVerts(decoded + decodedVerts_ * (int)dec_->GetDecVtxFmt().stride,
			dc.verts, indexLowerBound, indexUpperBound);
		decodedVerts_ += vertexCount;

		// 4. Advance indexgen vertex counter.
		indexGen.Advance(vertexCount);
		decodeCounter_ = lastMatch;
	}
}

inline u32 ComputeMiniHashRange(const void *ptr, size_t sz) {
	// Switch to u32 units.
	const u32 *p = (const u32 *)ptr;
	sz >>= 2;

	if (sz > 100) {
		size_t step = sz / 4;
		u32 hash = 0;
		for (size_t i = 0; i < sz; i += step) {
			hash += DoReliableHash32(p + i, 100, 0x3A44B9C4);
		}
		return hash;
	} else {
		return p[0] + p[sz - 1];
	}
}

VkDescriptorSet DrawEngineVulkan::GetDescriptorSet(CachedTextureVulkan *texture, VkSampler sampler, VkBuffer dynamicUbo) {
	DescriptorSetKey key;
	key.texture_ = texture;
	key.sampler_ = sampler;
	key.secondaryTexture_ = nullptr;
	key.buffer_ = dynamicUbo;

	FrameData *frame = &frame_[curFrame_ & 1];
	auto iter = frame->descSets.find(key);
	if (iter != frame->descSets.end()) {
		return iter->second;
	}

	// Didn't find one in the frame cache, let's make a new one.

	VkDescriptorSet desc;
	VkDescriptorSetAllocateInfo descAlloc;
	descAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descAlloc.pNext = nullptr;
	descAlloc.pSetLayouts = &descriptorSetLayout_;
	descAlloc.descriptorPool = frame->descPool;
	descAlloc.descriptorSetCount = 1;
	vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, &desc);

	// We just don't write to the slots we don't care about.
	VkWriteDescriptorSet writes[4];
	memset(writes, 0, sizeof(writes));
	// Main texture
	int n = 0;
	if (texture) {
		VkDescriptorImageInfo tex;
		tex.imageLayout = texture->imageLayout;
		tex.imageView = texture->imageView;
		tex.sampler = sampler;
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_TEXTURE;
		writes[n].pImageInfo = &tex;
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[n].dstSet = desc;
		n++;
	}

  // Skipping 2nd texture for now.
	// Uniform buffer objects
	VkDescriptorBufferInfo buf[3];
	buf[0].buffer = dynamicUbo;
	buf[0].offset = 0;
	buf[0].range = sizeof(UB_VS_FS_Base);
	buf[1].buffer = dynamicUbo;
	buf[1].offset = 0;
	buf[1].range = sizeof(UB_VS_Lights);
	buf[2].buffer = dynamicUbo;
	buf[2].offset = 0;
	buf[2].range = sizeof(UB_VS_Bones);
	writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[n].pNext = nullptr;
	writes[n].dstBinding = DRAW_BINDING_DYNUBO_BASE;
	writes[n].pBufferInfo = &buf[0];
	writes[n].dstSet = desc;
	writes[n].descriptorCount = 3;
	writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	n++;

	vkUpdateDescriptorSets(vulkan_->GetDevice(), n, writes, 0, nullptr);

	frame->descSets[key] = desc;
	return desc;
}

// The inline wrapper in the header checks for numDrawCalls == 0d
void DrawEngineVulkan::DoFlush(VkCommandBuffer cmd) {
	gpuStats.numFlushes++;

	FrameData *frame = &frame_[curFrame_ & 1];

	// Note than when we implement overflow in pushbuffer, we need to make sure to overflow here, not between
	// the three ubo pushes. The reason is that the three UBOs must be in the same buffer as that's how we
	// designed the descriptor set.

	//	CachedTextureVulkan *tex = textureCache_->ApplyTexture();
	VkDescriptorSet ds = GetDescriptorSet(nullptr, nullptr, frame->pushData->GetVkBuffer());

	GEPrimitiveType prim = prevPrim_;

	bool useHWTransform = CanUseHardwareTransform(prim);

	VulkanVertexShader *vshader;
	VulkanFragmentShader *fshader;

	uint32_t baseUBOOffset = 0;
	uint32_t lightUBOOffset = 0;
	uint32_t boneUBOOffset = 0;

	uint32_t ibOffset = 0;
	uint32_t vbOffset = 0;
	
	if (useHWTransform) {
		int vertexCount = 0;
		int maxIndex = 0;
		bool useElements = true;

		DecodeVerts();
		gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
		useElements = !indexGen.SeenOnlyPurePrims();
		vertexCount = indexGen.VertexCount();
		maxIndex = indexGen.MaxIndex();
		if (!useElements && indexGen.PureCount()) {
			vertexCount = indexGen.PureCount();
		}
		prim = indexGen.Prim();

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		VulkanPipelineRasterStateKey pipelineKey;
		VulkanDynamicState dynState;
		ConvertStateToVulkanKey(*framebufferManager_, prim, pipelineKey, dynState);
		// TODO: Dirty-flag these.
		vkCmdSetScissor(cmd_, 0, 1, &dynState.scissor);
		vkCmdSetViewport(cmd_, 0, 1, &dynState.viewport);
		vkCmdSetStencilReference(cmd_, VK_STENCIL_FRONT_AND_BACK, dynState.stencilRef);
		vkCmdSetStencilWriteMask(cmd_, VK_STENCIL_FRONT_AND_BACK, dynState.stencilWriteMask);
		vkCmdSetStencilCompareMask(cmd_, VK_STENCIL_FRONT_AND_BACK, dynState.stencilCompareMask);
		// vkCmdSetBlendConstants(cmd_, dynState.blendColor);
		shaderManager_->UpdateUniforms();
		shaderManager_->GetShaders(prim, lastVType_, &vshader, &fshader, useHWTransform);
		VulkanPipeline *pipeline = pipelineManager_->GetOrCreatePipeline(pipelineLayout_, pipelineKey, dec_, vshader->GetModule(), fshader->GetModule(), true);
		vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);  // TODO: Avoid if same as last draw.

		if (pipeline->uniformBlocks & UB_VS_FS_BASE) {
			baseUBOOffset = shaderManager_->PushBaseBuffer(frame->pushData);
		}
		if (pipeline->uniformBlocks & UB_VS_LIGHTS) {
			lightUBOOffset = shaderManager_->PushLightBuffer(frame->pushData);
		}
		if (pipeline->uniformBlocks & UB_VS_BONES) {
			boneUBOOffset = shaderManager_->PushBoneBuffer(frame->pushData);
		}

		VkBuffer buf[1] = {frame->pushData->GetVkBuffer()};
		const uint32_t dynamicUBOOffsets[3] = {
			baseUBOOffset, lightUBOOffset, boneUBOOffset,
		};
		vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &ds, 3, dynamicUBOOffsets);

		vbOffset = (uint32_t)frame->pushData->Push(decoded, vertexCount * dec_->GetDecVtxFmt().stride);

		VkDeviceSize offsets[1] = { vbOffset };
		if (useElements) {
			ibOffset = (uint32_t)frame->pushData->Push(decIndex, 2 * indexGen.VertexCount());
			// TODO: Avoid rebinding vertex/index buffers if the vertex size stays the same by using the offset arguments
			// Might want to separate vertices out into a different push buffer in that case.
			vkCmdBindVertexBuffers(cmd_, 0, 1, buf, offsets);
			vkCmdBindIndexBuffer(cmd_, buf[0], ibOffset, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(cmd_, maxIndex, 1, 0, 0, 0);
		} else {
			vkCmdBindVertexBuffers(cmd_, 0, 1, buf, offsets);
			vkCmdDraw(cmd_, vertexCount, 1, 0, 0);
		}
	} else {
		DecodeVerts();
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
		prim = indexGen.Prim();
		// Undo the strip optimization, not supported by the SW code yet.
		if (prim == GE_PRIM_TRIANGLE_STRIP)
			prim = GE_PRIM_TRIANGLES;
		VERBOSE_LOG(G3D, "Flush prim %i SW! %i verts in one go", prim, indexGen.VertexCount());

		int numTrans = 0;
		bool drawIndexed = false;
		u16 *inds = decIndex;
		TransformedVertex *drawBuffer = NULL;
		SoftwareTransformResult result;
		memset(&result, 0, sizeof(result));

		int maxIndex = indexGen.MaxIndex();
		SoftwareTransform(
			prim, decoded, indexGen.VertexCount(),
			dec_->VertexType(), inds, GE_VTYPE_IDX_16BIT, dec_->GetDecVtxFmt(),
			maxIndex, framebufferManager_, textureCache_, transformed, transformedExpanded, drawBuffer, numTrans, drawIndexed, &result, 1.0f);

		if (result.action == SW_DRAW_PRIMITIVES) {
			VulkanPipelineRasterStateKey pipelineKey;
			VulkanDynamicState dynState;
			ConvertStateToVulkanKey(*framebufferManager_, prim, pipelineKey, dynState);
			// TODO: Dirty-flag these.
			vkCmdSetScissor(cmd_, 0, 1, &dynState.scissor);
			vkCmdSetViewport(cmd_, 0, 1, &dynState.viewport);
			if (dynState.useStencil) {
				vkCmdSetStencilWriteMask(cmd_, VK_STENCIL_FRONT_AND_BACK, dynState.stencilWriteMask);
				vkCmdSetStencilCompareMask(cmd_, VK_STENCIL_FRONT_AND_BACK, dynState.stencilCompareMask);
			}
			if (result.setStencil) {
				// hey, dynamic state!
				vkCmdSetStencilReference(cmd_, VK_STENCIL_FRONT_AND_BACK, result.stencilValue);
			} else if (dynState.useStencil) {
				vkCmdSetStencilReference(cmd_, VK_STENCIL_FRONT_AND_BACK, dynState.stencilRef);
			}

			// vkCmdSetBlendConstants(cmd_, dynState.blendColor);
			shaderManager_->UpdateUniforms();
			shaderManager_->GetShaders(prim, lastVType_, &vshader, &fshader, useHWTransform);
			VulkanPipeline *pipeline = pipelineManager_->GetOrCreatePipeline(pipelineLayout_, pipelineKey, dec_, vshader->GetModule(), fshader->GetModule(), false);
			vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);  // TODO: Avoid if same as last draw.

			if (pipeline->uniformBlocks & UB_VS_FS_BASE) {
				baseUBOOffset = shaderManager_->PushBaseBuffer(frame->pushData);
			}

			const uint32_t dynamicUBOOffsets[3] = {
				baseUBOOffset, lightUBOOffset, boneUBOOffset,
			};
			vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &ds, 3, dynamicUBOOffsets);

			vbOffset = (uint32_t)frame->pushData->Push(decoded, numTrans * dec_->GetDecVtxFmt().stride);

			VkBuffer buf[1] = { frame->pushData->GetVkBuffer() };
			VkDeviceSize offsets[1] = { vbOffset };
			if (drawIndexed) {
				ibOffset = (uint32_t)frame->pushData->Push(decIndex, 2 * indexGen.VertexCount());
				// TODO: Have a buffer per frame, use a walking buffer pointer
				// TODO: Avoid rebinding if the vertex size stays the same by using the offset arguments
				vkCmdBindVertexBuffers(cmd_, 0, 1, buf, offsets);
				vkCmdBindIndexBuffer(cmd_, buf[0], ibOffset, VK_INDEX_TYPE_UINT16);
				vkCmdDrawIndexed(cmd_, numTrans, 1, 0, 0, 0);
			} else {
				// TODO: Avoid rebinding if the vertex size stays the same by using the offset arguments
				vkCmdBindVertexBuffers(cmd_, 0, 1, buf, offsets);
				vkCmdDraw(cmd_, numTrans, 1, 0, 0);
			}
		} else if (result.action == SW_CLEAR) {
			// TODO: Support clearing only color and not alpha, or vice versa. This is not supported (probably for good reason) by vkCmdClearColorAttachment
			// so we will have to simply draw a rectangle instead.

			int mask = gstate.isClearModeColorMask() ? 1 : 0;
			if (gstate.isClearModeAlphaMask()) mask |= 2;
			if (gstate.isClearModeDepthMask()) mask |= 4;

			VkClearValue colorValue, depthValue;
			colorValue.color.float32[0] = (result.color & 0xFF) * (1.0f / 255.0f);
			colorValue.color.float32[1] = ((result.color >> 8) & 0xFF) * (1.0f / 255.0f);
			colorValue.color.float32[2] = ((result.color >> 16) & 0xFF) * (1.0f / 255.0f);
			colorValue.color.float32[3] = ((result.color >> 24) & 0xFF) * (1.0f / 255.0f);
			depthValue.depthStencil.depth = result.depth;
			depthValue.depthStencil.stencil = (result.color >> 24) & 0xFF;

			VkClearRect rect;
			rect.baseArrayLayer = 0;
			rect.layerCount = 1;
			rect.rect.offset.x = 0;
			rect.rect.offset.y = 0;
			rect.rect.extent.width = gstate_c.curRTRenderWidth;
			rect.rect.extent.height = gstate_c.curRTRenderHeight;

			int count = 0;
			VkClearAttachment attach[2];
			if (mask & 3) {
				attach[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				attach[count].clearValue = colorValue;
				attach[count].colorAttachment = 0;
				count++;
			}
			if (mask & 4) {
				attach[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				attach[count].clearValue = depthValue;
				attach[count].colorAttachment = 0;
			}
			vkCmdClearAttachments(cmd_, count, attach, 1, &rect);

			if (mask & 1) {
				framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
			}
			if (mask & 4) {
				framebufferManager_->SetDepthUpdated();
			}
		}
	}

	gpuStats.numDrawCalls += numDrawCalls;
	gpuStats.numVertsSubmitted += vertexCountInDrawCalls;

	indexGen.Reset();
	decodedVerts_ = 0;
	numDrawCalls = 0;
	vertexCountInDrawCalls = 0;
	decodeCounter_ = 0;
	prevPrim_ = GE_PRIM_INVALID;
	gstate_c.vertexFullAlpha = true;
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

	// Now seems as good a time as any to reset the min/max coords, which we may examine later.
	gstate_c.vertBounds.minU = 512;
	gstate_c.vertBounds.minV = 512;
	gstate_c.vertBounds.maxU = 0;
	gstate_c.vertBounds.maxV = 0;

	host->GPUNotifyDraw();
}

void DrawEngineVulkan::Resized() {
	decJitCache_->Clear();
	lastVType_ = -1;
	dec_ = NULL;
	for (auto iter = decoderMap_.begin(); iter != decoderMap_.end(); iter++) {
		delete iter->second;
	}
	decoderMap_.clear();
}

bool DrawEngineVulkan::IsCodePtrVertexDecoder(const u8 *ptr) const {
	return decJitCache_->IsInSpace(ptr);
}
