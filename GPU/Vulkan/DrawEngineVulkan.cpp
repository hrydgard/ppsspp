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
	VERTEX_CACHE_SIZE = 8192 * 1024
};

#define VERTEXCACHE_DECIMATION_INTERVAL 17
#define DESCRIPTORSET_DECIMATION_INTERVAL 1  // Temporarily cut to 1. Handle reuse breaks this when textures get deleted.

enum { VAI_KILL_AGE = 120, VAI_UNRELIABLE_KILL_AGE = 240, VAI_UNRELIABLE_KILL_MAX = 4 };

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

DrawEngineVulkan::DrawEngineVulkan(Draw::DrawContext *draw)
	: draw_(draw), vai_(1024) {
	decOptions_.expandAllWeightsToFloat = false;
	decOptions_.expand8BitNormalsToFloat = false;
#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	decOptions_.alignOutputToWord = true;
#endif

	indexGen.Setup(decIndex_);
}

void DrawEngineVulkan::InitDeviceObjects() {
	// All resources we need for PSP drawing. Usually only bindings 0 and 2-4 are populated.

	// TODO: Make things more flexible, so we at least have specialized layouts for input attachments and tess.
	// Note that it becomes a support matrix..
	VkDescriptorSetLayoutBinding bindings[DRAW_BINDING_COUNT]{};
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[0].binding = DRAW_BINDING_TEXTURE;
	bindings[1].descriptorCount = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = DRAW_BINDING_2ND_TEXTURE;
	bindings[2].descriptorCount = 1;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;  // sampler is ignored though.
	bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[2].binding = DRAW_BINDING_DEPAL_TEXTURE;
	bindings[3].descriptorCount = 1;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	if (gstate_c.Use(GPU_USE_GS_CULLING))
		bindings[3].stageFlags |= VK_SHADER_STAGE_GEOMETRY_BIT;
	bindings[3].binding = DRAW_BINDING_DYNUBO_BASE;
	bindings[4].descriptorCount = 1;
	bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[4].binding = DRAW_BINDING_DYNUBO_LIGHT;
	bindings[5].descriptorCount = 1;
	bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	bindings[5].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[5].binding = DRAW_BINDING_DYNUBO_BONE;
	// Used only for hardware tessellation.
	bindings[6].descriptorCount = 1;
	bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[6].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[6].binding = DRAW_BINDING_TESS_STORAGE_BUF;
	bindings[7].descriptorCount = 1;
	bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[7].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[7].binding = DRAW_BINDING_TESS_STORAGE_BUF_WU;
	bindings[8].descriptorCount = 1;
	bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[8].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[8].binding = DRAW_BINDING_TESS_STORAGE_BUF_WV;

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	VkDevice device = vulkan->GetDevice();

	VkDescriptorSetLayoutCreateInfo dsl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dsl.bindingCount = ARRAY_SIZE(bindings);
	dsl.pBindings = bindings;
	VkResult res = vkCreateDescriptorSetLayout(device, &dsl, nullptr, &descriptorSetLayout_);
	_dbg_assert_(VK_SUCCESS == res);
	vulkan->SetDebugName(descriptorSetLayout_, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "drawengine_d_layout");

	static constexpr int DEFAULT_DESC_POOL_SIZE = 512;
	std::vector<VkDescriptorPoolSize> dpTypes;
	dpTypes.resize(4);
	dpTypes[0].descriptorCount = DEFAULT_DESC_POOL_SIZE * 3;
	dpTypes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	dpTypes[1].descriptorCount = DEFAULT_DESC_POOL_SIZE * 3;  // Don't use these for tess anymore, need max three per set.
	dpTypes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	dpTypes[2].descriptorCount = DEFAULT_DESC_POOL_SIZE * 3;  // TODO: Use a separate layout when no spline stuff is needed to reduce the need for these.
	dpTypes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	dpTypes[3].descriptorCount = DEFAULT_DESC_POOL_SIZE;  // For the frame global uniform buffer. Might need to allocate multiple times.
	dpTypes[3].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

	VkDescriptorPoolCreateInfo dp{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	// Don't want to mess around with individually freeing these.
	// We zap the whole pool every few frames.
	dp.flags = 0;
	dp.maxSets = DEFAULT_DESC_POOL_SIZE;

	// We are going to use one-shot descriptors in the initial implementation. Might look into caching them
	// if creating and updating them turns out to be expensive.
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		frame_[i].descPool.Create(vulkan, dp, dpTypes);

		// Note that pushUBO_ is also used for tessellation data (search for SetPushBuffer), and to upload
		// the null texture. This should be cleaned up...
	}

	pushUBO_ = (VulkanPushPool *)draw_->GetNativeObject(Draw::NativeObject::PUSH_POOL);
	pushVertex_ = new VulkanPushPool(vulkan, "pushVertex", 4 * 1024 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
	pushIndex_ = new VulkanPushPool(vulkan, "pushIndex", 1 * 512 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

	VkPipelineLayoutCreateInfo pl{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pl.pPushConstantRanges = nullptr;
	pl.pushConstantRangeCount = 0;
	VkDescriptorSetLayout layouts[1] = { descriptorSetLayout_};
	pl.setLayoutCount = ARRAY_SIZE(layouts);
	pl.pSetLayouts = layouts;
	pl.flags = 0;

	res = vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout_);
	_dbg_assert_(VK_SUCCESS == res);

	vulkan->SetDebugName(pipelineLayout_, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "drawengine_p_layout");

	VkSamplerCreateInfo samp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samp.magFilter = VK_FILTER_LINEAR;
	samp.minFilter = VK_FILTER_LINEAR;
	samp.maxLod = VK_LOD_CLAMP_NONE;  // recommended by best practices, has no effect since we don't use mipmaps.
	res = vkCreateSampler(device, &samp, nullptr, &samplerSecondaryLinear_);
	samp.magFilter = VK_FILTER_NEAREST;
	samp.minFilter = VK_FILTER_NEAREST;
	res = vkCreateSampler(device, &samp, nullptr, &samplerSecondaryNearest_);
	_dbg_assert_(VK_SUCCESS == res);
	res = vkCreateSampler(device, &samp, nullptr, &nullSampler_);
	_dbg_assert_(VK_SUCCESS == res);

	vertexCache_ = new VulkanPushBuffer(vulkan, "pushVertexCache", VERTEX_CACHE_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

	tessDataTransferVulkan = new TessellationDataTransferVulkan(vulkan);
	tessDataTransfer = tessDataTransferVulkan;

	draw_->SetInvalidationCallback(std::bind(&DrawEngineVulkan::Invalidate, this, std::placeholders::_1));
}

DrawEngineVulkan::~DrawEngineVulkan() {
	DestroyDeviceObjects();
}

void DrawEngineVulkan::FrameData::Destroy(VulkanContext *vulkan) {
	descPool.Destroy();
}

void DrawEngineVulkan::DestroyDeviceObjects() {
	if (!draw_) {
		// We've already done this from LostDevice.
		return;
	}

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);

	draw_->SetInvalidationCallback(InvalidationCallback());

	delete tessDataTransferVulkan;
	tessDataTransfer = nullptr;
	tessDataTransferVulkan = nullptr;

	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		frame_[i].Destroy(vulkan);
	}

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
	if (pipelineLayout_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeletePipelineLayout(pipelineLayout_);
	if (descriptorSetLayout_ != VK_NULL_HANDLE)
		vulkan->Delete().QueueDeleteDescriptorSetLayout(descriptorSetLayout_);
	if (vertexCache_) {
		vertexCache_->Destroy(vulkan);
		delete vertexCache_;
		vertexCache_ = nullptr;
	}

	// Need to clear this to get rid of all remaining references to the dead buffers.
	vai_.Iterate([](uint32_t hash, VertexArrayInfoVulkan *vai) {
		delete vai;
	});
	vai_.Clear();
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
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	lastPipeline_ = nullptr;

	// pushUBO is the thin3d push pool, don't need to BeginFrame again.
	pushVertex_->BeginFrame();
	pushIndex_->BeginFrame();

	tessDataTransferVulkan->SetPushPool(pushUBO_);

	DirtyAllUBOs();

	FrameData *frame = &GetCurFrame();

	// First reset all buffers, then begin. This is so that Reset can free memory and Begin can allocate it,
	// if growing the buffer is needed. Doing it this way will reduce fragmentation if more than one buffer
	// needs to grow in the same frame. The state where many buffers are reset can also be used to 
	// defragment memory.
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);

	// Wipe the vertex cache if it's grown too large.
	if (vertexCache_->GetTotalSize() > VERTEX_CACHE_SIZE) {
		vertexCache_->Destroy(vulkan);
		delete vertexCache_;  // orphans the buffers, they'll get deleted once no longer used by an in-flight frame.
		vertexCache_ = new VulkanPushBuffer(vulkan, "vertexCacheR", VERTEX_CACHE_SIZE, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		vai_.Iterate([&](uint32_t hash, VertexArrayInfoVulkan *vai) {
			delete vai;
		});
		vai_.Clear();
	}

	vertexCache_->BeginNoReset();

	if (--descDecimationCounter_ <= 0) {
		frame->descPool.Reset();
		descDecimationCounter_ = DESCRIPTORSET_DECIMATION_INTERVAL;
	}

	if (--decimationCounter_ <= 0) {
		decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;

		const int threshold = gpuStats.numFlips - VAI_KILL_AGE;
		const int unreliableThreshold = gpuStats.numFlips - VAI_UNRELIABLE_KILL_AGE;
		int unreliableLeft = VAI_UNRELIABLE_KILL_MAX;
		vai_.Iterate([&](uint32_t hash, VertexArrayInfoVulkan *vai) {
			bool kill;
			if (vai->status == VertexArrayInfoVulkan::VAI_UNRELIABLE) {
				// We limit killing unreliable so we don't rehash too often.
				kill = vai->lastFrame < unreliableThreshold && --unreliableLeft >= 0;
			} else {
				kill = vai->lastFrame < threshold;
			}
			if (kill) {
				// This is actually quite safe.
				vai_.Remove(hash);
				delete vai;
			}
		});
	}
	vai_.Maintain();
}

void DrawEngineVulkan::EndFrame() {
	FrameData *frame = &GetCurFrame();
	stats_.pushVertexSpaceUsed = (int)pushVertex_->GetUsedThisFrame();
	stats_.pushIndexSpaceUsed = (int)pushIndex_->GetUsedThisFrame();
	vertexCache_->End();
}

void DrawEngineVulkan::DecodeVertsToPushBuffer(VulkanPushBuffer *push, uint32_t *bindOffset, VkBuffer *vkbuf) {
	u8 *dest = decoded_;

	// Figure out how much pushbuffer space we need to allocate.
	if (push) {
		int vertsToDecode = ComputeNumVertsToDecode();
		dest = (u8 *)push->Allocate(vertsToDecode * dec_->GetDecVtxFmt().stride, 4, vkbuf, bindOffset);
	}
	DecodeVerts(dest);
}

void DrawEngineVulkan::DecodeVertsToPushPool(VulkanPushPool *push, uint32_t *bindOffset, VkBuffer *vkbuf) {
	u8 *dest = decoded_;

	// Figure out how much pushbuffer space we need to allocate.
	if (push) {
		int vertsToDecode = ComputeNumVertsToDecode();
		dest = push->Allocate(vertsToDecode * dec_->GetDecVtxFmt().stride, 4, vkbuf, bindOffset);
	}
	DecodeVerts(dest);
}

VkDescriptorSet DrawEngineVulkan::GetOrCreateDescriptorSet(VkImageView imageView, VkSampler sampler, VkBuffer base, VkBuffer light, VkBuffer bone, bool tess) {
	_dbg_assert_(base != VK_NULL_HANDLE);
	_dbg_assert_(light != VK_NULL_HANDLE);
	_dbg_assert_(bone != VK_NULL_HANDLE);

	DescriptorSetKey key{};
	key.imageView_ = imageView;
	key.sampler_ = sampler;
	key.secondaryImageView_ = boundSecondary_;
	key.depalImageView_ = boundDepal_;
	key.base_ = base;
	key.light_ = light;
	key.bone_ = bone;

	FrameData &frame = GetCurFrame();
	// See if we already have this descriptor set cached.
	if (!tess) { // Don't cache descriptors for HW tessellation.
		VkDescriptorSet d = frame.descSets.Get(key);
		if (d != VK_NULL_HANDLE)
			return d;
	}

	// Didn't find one in the frame descriptor set cache, let's make a new one.
	// We wipe the cache on every frame.
	VkDescriptorSet desc = frame.descPool.Allocate(1, &descriptorSetLayout_, "game_descset");

	// Even in release mode, this is bad.
	_assert_msg_(desc != VK_NULL_HANDLE, "Ran out of descriptor space in pool. sz=%d", (int)frame.descSets.size());

	// We just don't write to the slots we don't care about, which is fine.
	VkWriteDescriptorSet writes[DRAW_BINDING_COUNT]{};
	// Main texture
	int n = 0;
	VkDescriptorImageInfo tex[3]{};
	if (imageView) {
		_dbg_assert_(sampler != VK_NULL_HANDLE);

		tex[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		tex[0].imageView = imageView;
		tex[0].sampler = sampler;
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_TEXTURE;
		writes[n].pImageInfo = &tex[0];
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[n].dstSet = desc;
		n++;
	}

	if (boundSecondary_) {
		tex[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		tex[1].imageView = boundSecondary_;
		tex[1].sampler = samplerSecondaryNearest_;
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_2ND_TEXTURE;
		writes[n].pImageInfo = &tex[1];
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[n].dstSet = desc;
		n++;
	}

	if (boundDepal_) {
		tex[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		tex[2].imageView = boundDepal_;
		tex[2].sampler = boundDepalSmoothed_ ? samplerSecondaryLinear_ : samplerSecondaryNearest_;
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_DEPAL_TEXTURE;
		writes[n].pImageInfo = &tex[2];
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[n].dstSet = desc;
		n++;
	}

	// Tessellation data buffer.
	if (tess) {
		const VkDescriptorBufferInfo *bufInfo = tessDataTransferVulkan->GetBufferInfo();
		// Control Points
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_TESS_STORAGE_BUF;
		writes[n].pBufferInfo = &bufInfo[0];
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[n].dstSet = desc;
		n++;
		// Weights U
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_TESS_STORAGE_BUF_WU;
		writes[n].pBufferInfo = &bufInfo[1];
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[n].dstSet = desc;
		n++;
		// Weights V
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_TESS_STORAGE_BUF_WV;
		writes[n].pBufferInfo = &bufInfo[2];
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[n].dstSet = desc;
		n++;
	}

	// Uniform buffer objects
	VkDescriptorBufferInfo buf[3]{};
	int count = 0;
	buf[count].buffer = base;
	buf[count].offset = 0;
	buf[count].range = sizeof(UB_VS_FS_Base);
	count++;
	buf[count].buffer = light;
	buf[count].offset = 0;
	buf[count].range = sizeof(UB_VS_Lights);
	count++;
	buf[count].buffer = bone;
	buf[count].offset = 0;
	buf[count].range = sizeof(UB_VS_Bones);
	count++;
	for (int i = 0; i < count; i++) {
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].pNext = nullptr;
		writes[n].dstBinding = DRAW_BINDING_DYNUBO_BASE + i;
		writes[n].dstArrayElement = 0;
		writes[n].pBufferInfo = &buf[i];
		writes[n].dstSet = desc;
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		n++;
	}

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	vkUpdateDescriptorSets(vulkan->GetDevice(), n, writes, 0, nullptr);

	if (!tess) // Again, avoid caching when HW tessellation.
		frame.descSets.Insert(key, desc);
	return desc;
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

void MarkUnreliable(VertexArrayInfoVulkan *vai) {
	vai->status = VertexArrayInfoVulkan::VAI_UNRELIABLE;
	// TODO: If we change to a real allocator, free the data here.
	// For now we just leave it in the pushbuffer.
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

bool DrawEngineVulkan::VertexCacheLookup(int &vertexCount, GEPrimitiveType &prim, VkBuffer &vbuf, uint32_t &vbOffset, VkBuffer &ibuf, uint32_t &ibOffset, bool &useElements, bool forceIndexed) {
	// getUVGenMode can have an effect on which UV decoder we need to use! And hence what the decoded data will look like. See #9263
	// u32 dcid = (u32)XXH3_64bits(&drawCalls_, sizeof(DeferredDrawCall) * numDrawCalls_) ^ gstate.getUVGenMode();
	u32 dcid = ComputeDrawcallsHash() ^ gstate.getUVGenMode();

	PROFILE_THIS_SCOPE("vcache");
	VertexArrayInfoVulkan *vai = vai_.Get(dcid);
	if (!vai) {
		vai = new VertexArrayInfoVulkan();
		vai_.Insert(dcid, vai);
	}

	switch (vai->status) {
	case VertexArrayInfoVulkan::VAI_NEW:
	{
		// Haven't seen this one before. We don't actually upload the vertex data yet.
		uint64_t dataHash = ComputeHash();
		vai->hash = dataHash;
		vai->minihash = ComputeMiniHash();
		vai->status = VertexArrayInfoVulkan::VAI_HASHING;
		vai->drawsUntilNextFullHash = 0;
		DecodeVertsToPushPool(pushVertex_, &vbOffset, &vbuf);  // writes to indexGen
		vai->numVerts = indexGen.VertexCount();
		vai->prim = indexGen.Prim();
		vai->maxIndex = indexGen.MaxIndex();
		vai->flags = gstate_c.vertexFullAlpha ? VAIVULKAN_FLAG_VERTEXFULLALPHA : 0;
		return true;
	}

	// Hashing - still gaining confidence about the buffer.
	// But if we get this far it's likely to be worth uploading the data.
	case VertexArrayInfoVulkan::VAI_HASHING:
	{
		PROFILE_THIS_SCOPE("vcachehash");
		vai->numDraws++;
		if (vai->lastFrame != gpuStats.numFlips) {
			vai->numFrames++;
		}
		if (vai->drawsUntilNextFullHash == 0) {
			// Let's try to skip a full hash if mini would fail.
			const u32 newMiniHash = ComputeMiniHash();
			uint64_t newHash = vai->hash;
			if (newMiniHash == vai->minihash) {
				newHash = ComputeHash();
			}
			if (newMiniHash != vai->minihash || newHash != vai->hash) {
				MarkUnreliable(vai);
				DecodeVertsToPushPool(pushVertex_, &vbOffset, &vbuf);
				return true;
			}
			if (vai->numVerts > 64) {
				// exponential backoff up to 16 draws, then every 24
				vai->drawsUntilNextFullHash = std::min(24, vai->numFrames);
			} else {
				// Lower numbers seem much more likely to change.
				vai->drawsUntilNextFullHash = 0;
			}
			// TODO: tweak
			//if (vai->numFrames > 1000) {
			//	vai->status = VertexArrayInfo::VAI_RELIABLE;
			//}
		} else {
			vai->drawsUntilNextFullHash--;
			u32 newMiniHash = ComputeMiniHash();
			if (newMiniHash != vai->minihash) {
				MarkUnreliable(vai);
				DecodeVertsToPushPool(pushVertex_, &vbOffset, &vbuf);
				return true;
			}
		}

		if (!vai->vb) {
			// Directly push to the vertex cache.
			DecodeVertsToPushBuffer(vertexCache_, &vai->vbOffset, &vai->vb);
			_dbg_assert_msg_(gstate_c.vertBounds.minV >= gstate_c.vertBounds.maxV, "Should not have checked UVs when caching.");
			vai->numVerts = indexGen.VertexCount();
			vai->maxIndex = indexGen.MaxIndex();
			vai->flags = gstate_c.vertexFullAlpha ? VAIVULKAN_FLAG_VERTEXFULLALPHA : 0;
			if (forceIndexed) {
				vai->prim = indexGen.GeneralPrim();
				useElements = true;
			} else {
				vai->prim = indexGen.Prim();
				useElements = !indexGen.SeenOnlyPurePrims();
				if (!useElements && indexGen.PureCount()) {
					vai->numVerts = indexGen.PureCount();
				}
			}

			if (useElements) {
				u32 size = sizeof(uint16_t) * indexGen.VertexCount();
				void *dest = vertexCache_->Allocate(size, 4, &vai->ib, &vai->ibOffset);
				memcpy(dest, decIndex_, size);
			} else {
				vai->ib = VK_NULL_HANDLE;
				vai->ibOffset = 0;
			}
		} else {
			gpuStats.numCachedDrawCalls++;
			useElements = vai->ib ? true : false;
			gpuStats.numCachedVertsDrawn += vai->numVerts;
			gstate_c.vertexFullAlpha = vai->flags & VAIVULKAN_FLAG_VERTEXFULLALPHA;
		}
		vbuf = vai->vb;
		ibuf = vai->ib;
		vbOffset = vai->vbOffset;
		ibOffset = vai->ibOffset;
		vertexCount = vai->numVerts;
		prim = static_cast<GEPrimitiveType>(vai->prim);
		break;
	}

	// Reliable - we don't even bother hashing anymore. Right now we don't go here until after a very long time.
	case VertexArrayInfoVulkan::VAI_RELIABLE:
	{
		vai->numDraws++;
		if (vai->lastFrame != gpuStats.numFlips) {
			vai->numFrames++;
		}
		gpuStats.numCachedDrawCalls++;
		gpuStats.numCachedVertsDrawn += vai->numVerts;
		vbuf = vai->vb;
		ibuf = vai->ib;
		vbOffset = vai->vbOffset;
		ibOffset = vai->ibOffset;
		vertexCount = vai->numVerts;
		prim = static_cast<GEPrimitiveType>(vai->prim);

		gstate_c.vertexFullAlpha = vai->flags & VAIVULKAN_FLAG_VERTEXFULLALPHA;
		break;
	}

	case VertexArrayInfoVulkan::VAI_UNRELIABLE:
	{
		vai->numDraws++;
		if (vai->lastFrame != gpuStats.numFlips) {
			vai->numFrames++;
		}
		DecodeVertsToPushPool(pushVertex_, &vbOffset, &vbuf);
		return true;
	}
	default:
		break;
	}
	return false;
}

// The inline wrapper in the header checks for numDrawCalls_ == 0
void DrawEngineVulkan::DoFlush() {
	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	PROFILE_THIS_SCOPE("Flush");
	FrameData &frameData = GetCurFrame();

	bool tess = gstate_c.submitType == SubmitType::HW_BEZIER || gstate_c.submitType == SubmitType::HW_SPLINE;

	bool textureNeedsApply = false;
	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
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
		int vertexCount = 0;
		bool useElements = true;

		// Cannot cache vertex data with morph enabled.
		bool useCache = g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK);
		// Also avoid caching when software skinning.
		VkBuffer vbuf = VK_NULL_HANDLE;
		VkBuffer ibuf = VK_NULL_HANDLE;
		if (decOptions_.applySkinInDecode && (lastVType_ & GE_VTYPE_WEIGHT_MASK)) {
			useCache = false;
		}
		bool useIndexGen = true;
		if (useCache) {
			useIndexGen = VertexCacheLookup(vertexCount, prim, vbuf, vbOffset, ibuf, ibOffset, useElements, forceIndexed);
		} else {
			if (decOptions_.applySkinInDecode && (lastVType_ & GE_VTYPE_WEIGHT_MASK)) {
				// If software skinning, we've already predecoded into "decoded". So push that content.
				VkDeviceSize size = decodedVerts_ * dec_->GetDecVtxFmt().stride;
				u8 *dest = (u8 *)pushVertex_->Allocate(size, 4, &vbuf, &vbOffset);
				memcpy(dest, decoded_, size);
			} else {
				// Decode directly into the pushbuffer
				DecodeVertsToPushPool(pushVertex_, &vbOffset, &vbuf);
			}
			gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
		}

		if (useIndexGen) {
			vertexCount = indexGen.VertexCount();
			if (forceIndexed) {
				useElements = true;
				prim = indexGen.GeneralPrim();
			} else {
				useElements = !indexGen.SeenOnlyPurePrims();
				if (!useElements && indexGen.PureCount()) {
					vertexCount = indexGen.PureCount();
				}
				prim = indexGen.Prim();
			}
		}

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
			if (!vshader) {
				// We're screwed.
				return;
			}
			_dbg_assert_msg_(vshader->UseHWTransform(), "Bad vshader");

			VulkanPipeline *pipeline = pipelineManager_->GetOrCreatePipeline(renderManager, pipelineLayout_, pipelineKey_, &dec_->decFmt, vshader, fshader, gshader, true, 0, framebufferManager_->GetMSAALevel(), false);
			if (!pipeline || !pipeline->pipeline) {
				// Already logged, let's bail out.
				return;
			}
			BindShaderBlendTex();  // This might cause copies so important to do before BindPipeline.

			renderManager->BindPipeline(pipeline->pipeline, pipeline->pipelineFlags, pipelineLayout_);
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
		UpdateUBOs(&frameData);

		VkDescriptorSet ds = GetOrCreateDescriptorSet(imageView, sampler, baseBuf, lightBuf, boneBuf, tess);

		// TODO: Can we avoid binding all three when not needed? Same below for hardware transform.
		// Think this will require different descriptor set layouts.
		const uint32_t dynamicUBOOffsets[3] = {
			baseUBOOffset, lightUBOOffset, boneUBOOffset,
		};
		if (useElements) {
			if (!ibuf) {
				ibOffset = (uint32_t)pushIndex_->Push(decIndex_, sizeof(uint16_t) * indexGen.VertexCount(), 4, &ibuf);
			}
			renderManager->DrawIndexed(ds, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, ibuf, ibOffset, vertexCount, 1);
		} else {
			renderManager->Draw(ds, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, vertexCount);
		}
	} else {
		PROFILE_THIS_SCOPE("soft");
		if (!decOptions_.applySkinInDecode) {
			decOptions_.applySkinInDecode = true;
			lastVType_ |= (1 << 26);
			dec_ = GetVertexDecoder(lastVType_);
		}
		DecodeVerts(decoded_);
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

		int maxIndex = indexGen.MaxIndex();
		SoftwareTransform swTransform(params);

		const Lin::Vec3 trans(gstate_c.vpXOffset, gstate_c.vpYOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
		const Lin::Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
		swTransform.SetProjMatrix(gstate.projMatrix, gstate_c.vpWidth < 0, gstate_c.vpHeight < 0, trans, scale);

		swTransform.Decode(prim, dec_->VertexType(), dec_->GetDecVtxFmt(), maxIndex, &result);
		// Non-zero depth clears are unusual, but some drivers don't match drawn depth values to cleared values.
		// Games sometimes expect exact matches (see #12626, for example) for equal comparisons.
		if (result.action == SW_CLEAR && everUsedEqualDepth_ && gstate.isClearModeDepthMask() && result.depth > 0.0f && result.depth < 1.0f)
			result.action = SW_NOT_READY;
		if (result.action == SW_NOT_READY) {
			swTransform.DetectOffsetTexture(maxIndex);
			swTransform.BuildDrawingParams(prim, indexGen.VertexCount(), dec_->VertexType(), inds, maxIndex, &result);
		}

		if (result.setSafeSize)
			framebufferManager_->SetSafeSize(result.safeWidth, result.safeHeight);

		// Only here, where we know whether to clear or to draw primitives, should we actually set the current framebuffer! Because that gives use the opportunity
		// to use a "pre-clear" render pass, for high efficiency on tilers.
		if (result.action == SW_DRAW_PRIMITIVES) {
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

				shaderManager_->GetShaders(prim, dec_, &vshader, &fshader, &gshader, pipelineState_, false, false, decOptions_.expandAllWeightsToFloat, true);
				_dbg_assert_msg_(!vshader->UseHWTransform(), "Bad vshader");
				VulkanPipeline *pipeline = pipelineManager_->GetOrCreatePipeline(renderManager, pipelineLayout_, pipelineKey_, &dec_->decFmt, vshader, fshader, gshader, false, 0, framebufferManager_->GetMSAALevel(), false);
				if (!pipeline || !pipeline->pipeline) {
					// Already logged, let's bail out.
					decodedVerts_ = 0;
					numDrawCalls_ = 0;
					decodeCounter_ = 0;
					decOptions_.applySkinInDecode = g_Config.bSoftwareSkinning;
					return;
				}
				BindShaderBlendTex();  // This might cause copies so super important to do before BindPipeline.

				renderManager->BindPipeline(pipeline->pipeline, pipeline->pipelineFlags, pipelineLayout_);
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
			UpdateUBOs(&frameData);

			VkDescriptorSet ds = GetOrCreateDescriptorSet(imageView, sampler, baseBuf, lightBuf, boneBuf, tess);
			const uint32_t dynamicUBOOffsets[3] = {
				baseUBOOffset, lightUBOOffset, boneUBOOffset,
			};

			PROFILE_THIS_SCOPE("renderman_q");

			if (result.drawIndexed) {
				VkBuffer vbuf, ibuf;
				vbOffset = (uint32_t)pushVertex_->Push(result.drawBuffer, maxIndex * sizeof(TransformedVertex), 4, &vbuf);
				ibOffset = (uint32_t)pushIndex_->Push(inds, sizeof(short) * result.drawNumTrans, 4, &ibuf);
				renderManager->DrawIndexed(ds, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, ibuf, ibOffset, result.drawNumTrans, 1);
			} else {
				VkBuffer vbuf;
				vbOffset = (uint32_t)pushVertex_->Push(result.drawBuffer, result.drawNumTrans * sizeof(TransformedVertex), 4, &vbuf);
				renderManager->Draw(ds, ARRAY_SIZE(dynamicUBOOffsets), dynamicUBOOffsets, vbuf, vbOffset, result.drawNumTrans);
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

	gpuStats.numFlushes++;
	gpuStats.numDrawCalls += numDrawCalls_;
	gpuStats.numVertsSubmitted += vertexCountInDrawCalls_;

	indexGen.Reset();
	decodedVerts_ = 0;
	numDrawCalls_ = 0;
	vertexCountInDrawCalls_ = 0;
	decodeCounter_ = 0;
	gstate_c.vertexFullAlpha = true;
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

	// Now seems as good a time as any to reset the min/max coords, which we may examine later.
	gstate_c.vertBounds.minU = 512;
	gstate_c.vertBounds.minV = 512;
	gstate_c.vertBounds.maxU = 0;
	gstate_c.vertBounds.maxV = 0;

	GPUDebug::NotifyDraw();
}

void DrawEngineVulkan::UpdateUBOs(FrameData *frame) {
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

DrawEngineVulkan::FrameData &DrawEngineVulkan::GetCurFrame() {
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	return frame_[vulkan->GetCurFrame()];
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
