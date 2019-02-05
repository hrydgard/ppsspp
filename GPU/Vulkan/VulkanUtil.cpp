// Copyright (c) 2016- PPSSPP Project.

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

#include "base/basictypes.h"
#include "Common/Log.h"
#include "Common/Vulkan/VulkanContext.h"
#include "GPU/Vulkan/VulkanUtil.h"

Vulkan2D::Vulkan2D(VulkanContext *vulkan) : vulkan_(vulkan) {
	InitDeviceObjects();
}

Vulkan2D::~Vulkan2D() {
	DestroyDeviceObjects();
}

void Vulkan2D::Shutdown() {
	DestroyDeviceObjects();
}

void Vulkan2D::DestroyDeviceObjects() {
	for (int i = 0; i < vulkan_->GetInflightFrames(); i++) {
		if (frameData_[i].descPool != VK_NULL_HANDLE) {
			vulkan_->Delete().QueueDeleteDescriptorPool(frameData_[i].descPool);
			frameData_[i].descPool = VK_NULL_HANDLE;
		}
	}
	for (auto it : pipelines_) {
		vulkan_->Delete().QueueDeletePipeline(it.second);
	}
	pipelines_.clear();

	VkDevice device = vulkan_->GetDevice();
	if (descriptorSetLayout_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteDescriptorSetLayout(descriptorSetLayout_);
		descriptorSetLayout_ = VK_NULL_HANDLE;
	}
	if (pipelineLayout_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeletePipelineLayout(pipelineLayout_);
		pipelineLayout_ = VK_NULL_HANDLE;
	}

	// pipelineBasicTex_ and pipelineBasicTex_ come from vulkan2D_.
	if (pipelineCache_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeletePipelineCache(pipelineCache_);
		pipelineCache_ = VK_NULL_HANDLE;
	}
}

void Vulkan2D::InitDeviceObjects() {
	VkPipelineCacheCreateInfo pc{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	VkResult res = vkCreatePipelineCache(vulkan_->GetDevice(), &pc, nullptr, &pipelineCache_);
	assert(VK_SUCCESS == res);

	VkDescriptorSetLayoutBinding bindings[2] = {};
	// Texture.
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[0].binding = 0;
	// In depal, this second texture is used for the palette.
	bindings[1].descriptorCount = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;

	VkDevice device = vulkan_->GetDevice();

	VkDescriptorSetLayoutCreateInfo dsl = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dsl.bindingCount = 2;
	dsl.pBindings = bindings;
	res = vkCreateDescriptorSetLayout(device, &dsl, nullptr, &descriptorSetLayout_);
	assert(VK_SUCCESS == res);

	VkDescriptorPoolSize dpTypes[1];
	dpTypes[0].descriptorCount = 3000;
	dpTypes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo dp = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dp.flags = 0;   // Don't want to mess around with individually freeing these, let's go fixed each frame and zap the whole array. Might try the dynamic approach later.
	dp.maxSets = 3000;
	dp.pPoolSizes = dpTypes;
	dp.poolSizeCount = ARRAY_SIZE(dpTypes);
	for (int i = 0; i < ARRAY_SIZE(frameData_); i++) {
		VkResult res = vkCreateDescriptorPool(vulkan_->GetDevice(), &dp, nullptr, &frameData_[i].descPool);
		assert(VK_SUCCESS == res);
	}

	VkPushConstantRange push = {};
	push.offset = 0;
	push.size = 48;
	push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;

	VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pl.pPushConstantRanges = &push;
	pl.pushConstantRangeCount = 1;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &descriptorSetLayout_;
	pl.flags = 0;
	res = vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout_);
	assert(VK_SUCCESS == res);
}

void Vulkan2D::DeviceLost() {
	DestroyDeviceObjects();
}

void Vulkan2D::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;
	InitDeviceObjects();
}

void Vulkan2D::BeginFrame() {
	int curFrame = vulkan_->GetCurFrame();
	FrameData &frame = frameData_[curFrame];
	frame.descSets.clear();
	vkResetDescriptorPool(vulkan_->GetDevice(), frame.descPool, 0);
}

void Vulkan2D::EndFrame() {
}

VkDescriptorSet Vulkan2D::GetDescriptorSet(VkImageView tex1, VkSampler sampler1, VkImageView tex2, VkSampler sampler2) {
	DescriptorSetKey key;
	key.imageView[0] = tex1;
	key.imageView[1] = tex2;
	key.sampler[0] = sampler1;
	key.sampler[1] = sampler2;

	int curFrame = vulkan_->GetCurFrame();
	FrameData *frame = &frameData_[curFrame];
	auto iter = frame->descSets.find(key);
	if (iter != frame->descSets.end()) {
		return iter->second;
	}

	// Didn't find one in the frame descriptor set cache, let's make a new one.
	// We wipe the cache on every frame.

	VkDescriptorSet desc;
	VkDescriptorSetAllocateInfo descAlloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	descAlloc.pSetLayouts = &descriptorSetLayout_;
	descAlloc.descriptorPool = frame->descPool;
	descAlloc.descriptorSetCount = 1;
	VkResult result = vkAllocateDescriptorSets(vulkan_->GetDevice(), &descAlloc, &desc);
	assert(result == VK_SUCCESS);

	// We just don't write to the slots we don't care about.
	VkWriteDescriptorSet writes[2]{};
	// Main and sub textures
	int n = 0;
	VkDescriptorImageInfo image1{};
	VkDescriptorImageInfo image2{};
	if (tex1) {
#ifdef VULKAN_USE_GENERAL_LAYOUT_FOR_COLOR
		image1.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
#else
		image1.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#endif
		image1.imageView = tex1;
		image1.sampler = sampler1;
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].dstBinding = 0;
		writes[n].pImageInfo = &image1;
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[n].dstSet = desc;
		n++;
	}
	if (tex2) {
		// TODO: Also support LAYOUT_GENERAL to be able to texture from framebuffers without transitioning them?
#ifdef VULKAN_USE_GENERAL_LAYOUT_FOR_COLOR
		image2.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
#else
		image2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
#endif
		image2.imageView = tex2;
		image2.sampler = sampler2;
		writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[n].dstBinding = 1;
		writes[n].pImageInfo = &image2;
		writes[n].descriptorCount = 1;
		writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[n].dstSet = desc;
		n++;
	}

	vkUpdateDescriptorSets(vulkan_->GetDevice(), n, writes, 0, nullptr);

	frame->descSets[key] = desc;
	return desc;
}

VkPipeline Vulkan2D::GetPipeline(VkRenderPass rp, VkShaderModule vs, VkShaderModule fs, bool readVertices, VK2DDepthStencilMode depthStencilMode) {
	PipelineKey key;
	key.vs = vs;
	key.fs = fs;
	key.rp = rp;
	key.depthStencilMode = depthStencilMode;
	key.readVertices = readVertices;

	auto iter = pipelines_.find(key);
	if (iter != pipelines_.end()) {
		return iter->second;
	}

	VkPipelineColorBlendAttachmentState blend0 = {};
	blend0.blendEnable = false;
	blend0.colorWriteMask = depthStencilMode == VK2DDepthStencilMode::STENCIL_REPLACE_ALWAYS ? VK_COLOR_COMPONENT_A_BIT : 0xF;

	VkPipelineColorBlendStateCreateInfo cbs = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	cbs.pAttachments = &blend0;
	cbs.attachmentCount = 1;
	cbs.logicOpEnable = false;

	VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	dss.depthBoundsTestEnable = false;
	dss.depthTestEnable = false;
	dss.stencilTestEnable = false;
	switch (depthStencilMode) {
	case VK2DDepthStencilMode::NONE:
		break;
	case VK2DDepthStencilMode::STENCIL_REPLACE_ALWAYS:
		dss.stencilTestEnable = true;
		dss.front.reference = 0xFF;
		dss.front.compareMask = 0xFF;
		dss.front.compareOp = VK_COMPARE_OP_ALWAYS;
		dss.front.depthFailOp = VK_STENCIL_OP_REPLACE;
		dss.front.failOp = VK_STENCIL_OP_REPLACE;
		dss.front.passOp = VK_STENCIL_OP_REPLACE;
		dss.back = dss.front;
		break;
	}

	VkDynamicState dynamicStates[5];
	int numDyn = 0;
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_SCISSOR;
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_VIEWPORT;
	if (depthStencilMode == VK2DDepthStencilMode::STENCIL_REPLACE_ALWAYS) {
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
	}

	VkPipelineDynamicStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	ds.pDynamicStates = dynamicStates;
	ds.dynamicStateCount = numDyn;

	VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineShaderStageCreateInfo ss[2] = {};
	ss[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	ss[0].module = vs;
	ss[0].pName = "main";
	ss[0].flags = 0;
	ss[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	ss[1].module = fs;
	ss[1].pName = "main";
	ss[1].flags = 0;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.flags = 0;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	inputAssembly.primitiveRestartEnable = true;

	VkVertexInputAttributeDescription attrs[2];
	int attributeCount = 2;
	attrs[0].binding = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[0].location = 0;
	attrs[0].offset = 0;
	attrs[1].binding = 0;
	attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[1].location = 1;
	attrs[1].offset = 12;
	int vertexStride = 12 + 8;

	VkVertexInputBindingDescription ibd = {};
	ibd.binding = 0;
	ibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	ibd.stride = vertexStride;

	VkPipelineVertexInputStateCreateInfo vis = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vis.vertexBindingDescriptionCount = readVertices ? 1 : 0;
	vis.pVertexBindingDescriptions = readVertices ? &ibd : nullptr;
	vis.vertexAttributeDescriptionCount = readVertices ? attributeCount : 0;
	vis.pVertexAttributeDescriptions = readVertices ? attrs : nullptr;

	VkPipelineViewportStateCreateInfo views = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	views.viewportCount = 1;
	views.scissorCount = 1;
	views.pViewports = nullptr;  // dynamic
	views.pScissors = nullptr;  // dynamic

	VkGraphicsPipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipe.flags = 0;
	pipe.stageCount = 2;
	pipe.pStages = ss;
	pipe.basePipelineIndex = 0;

	pipe.pColorBlendState = &cbs;
	pipe.pDepthStencilState = &dss;
	pipe.pRasterizationState = &rs;

	pipe.pVertexInputState = &vis;
	pipe.pViewportState = &views;
	pipe.pTessellationState = nullptr;
	pipe.pDynamicState = &ds;
	pipe.pInputAssemblyState = &inputAssembly;
	pipe.pMultisampleState = &ms;
	pipe.layout = pipelineLayout_;
	pipe.basePipelineHandle = VK_NULL_HANDLE;
	pipe.basePipelineIndex = 0;
	pipe.renderPass = rp;
	pipe.subpass = 0;

	VkPipeline pipeline;
	VkResult result = vkCreateGraphicsPipelines(vulkan_->GetDevice(), pipelineCache_, 1, &pipe, nullptr, &pipeline);
	if (result == VK_SUCCESS) {
		pipelines_[key] = pipeline;
		return pipeline;
	} else {
		return VK_NULL_HANDLE;
	}
}

VkShaderModule CompileShaderModule(VulkanContext *vulkan, VkShaderStageFlagBits stage, const char *code, std::string *error) {
	std::vector<uint32_t> spirv;
	bool success = GLSLtoSPV(stage, code, spirv, error);
	if (!error->empty()) {
		if (success) {
			ERROR_LOG(G3D, "Warnings in shader compilation!");
		} else {
			ERROR_LOG(G3D, "Error in shader compilation!");
		}
		ERROR_LOG(G3D, "Messages: %s", error->c_str());
		ERROR_LOG(G3D, "Shader source:\n%s", code);
		OutputDebugStringUTF8("Messages:\n");
		OutputDebugStringUTF8(error->c_str());
		return VK_NULL_HANDLE;
	} else {
		VkShaderModule module;
		if (vulkan->CreateShaderModule(spirv, &module)) {
			return module;
		} else {
			return VK_NULL_HANDLE;
		}
	}
}
