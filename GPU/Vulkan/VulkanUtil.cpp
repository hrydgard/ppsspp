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

VulkanFBO::VulkanFBO() : color_(nullptr), depthStencil_(nullptr) {}

VulkanFBO::~VulkanFBO() {
	delete color_;
	delete depthStencil_;
}

void VulkanFBO::Create(VulkanContext *vulkan, VkRenderPass rp_compatible, int width, int height, VkFormat color_Format) {
	color_ = new VulkanTexture(vulkan);
	VkImageCreateFlags flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	color_->CreateDirect(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, flags | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, nullptr);
	depthStencil_->CreateDirect(width, height, 1, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, flags | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, nullptr);

	VkImageView views[2] = { color_->GetImageView(), depthStencil_->GetImageView() };

	VkFramebufferCreateInfo fb = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fb.pAttachments = views;
	fb.attachmentCount = 2;
	fb.flags = 0;
	fb.renderPass = rp_compatible;
	fb.width = width;
	fb.height = height;
	fb.layers = 1;

	vkCreateFramebuffer(vulkan->GetDevice(), &fb, nullptr, &framebuffer_);
}

Vulkan2D::Vulkan2D(VulkanContext *vulkan) : vulkan_(vulkan) {
	// All resources we need for PSP drawing. Usually only bindings 0 and 2-4 are populated.
	VkDescriptorSetLayoutBinding bindings[2] = {};
	bindings[0].descriptorCount = 1;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[0].binding = 0;
	bindings[1].descriptorCount = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings[1].binding = 1;

	VkDevice device = vulkan_->GetDevice();

	VkDescriptorSetLayoutCreateInfo dsl = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dsl.bindingCount = 2;
	dsl.pBindings = bindings;
	VkResult res = vkCreateDescriptorSetLayout(device, &dsl, nullptr, &descriptorSetLayout_);
	assert(VK_SUCCESS == res);

	VkDescriptorPoolSize dpTypes[1];
	dpTypes[0].descriptorCount = 200;
	dpTypes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo dp = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dp.flags = 0;   // Don't want to mess around with individually freeing these, let's go fixed each frame and zap the whole array. Might try the dynamic approach later.
	dp.maxSets = 200;
	dp.pPoolSizes = dpTypes;
	dp.poolSizeCount = ARRAY_SIZE(dpTypes);
	for (int i = 0; i < 2; i++) {
		VkResult res = vkCreateDescriptorPool(vulkan_->GetDevice(), &dp, nullptr, &frameData_[i].descPool);
		assert(VK_SUCCESS == res);
	}

	VkPushConstantRange push = {};
	push.offset = 0;
	push.size = 32;
	push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pl.pPushConstantRanges = &push;
	pl.pushConstantRangeCount = 1;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &descriptorSetLayout_;
	pl.flags = 0;
	res = vkCreatePipelineLayout(device, &pl, nullptr, &pipelineLayout_);
	assert(VK_SUCCESS == res);
}

Vulkan2D::~Vulkan2D() {
	VkDevice device = vulkan_->GetDevice();
	for (int i = 0; i < 2; i++) {
		vulkan_->Delete().QueueDeleteDescriptorPool(frameData_[i].descPool);
	}
	vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
}

void Vulkan2D::BeginFrame() {
	FrameData &frame = frameData_[curFrame_];
	frame.descSets.clear();
	vkResetDescriptorPool(vulkan_->GetDevice(), frame.descPool, 0);
}

void Vulkan2D::EndFrame() {
	curFrame_ = (curFrame_ + 1) & 1;
}

VkDescriptorSet Vulkan2D::GetDescriptorSet(VkImageView tex1, VkSampler sampler1, VkImageView tex2, VkSampler sampler2) {
	DescriptorSetKey key;
	key.imageView[0] = tex1;
	key.imageView[1] = tex2;
	key.sampler[0] = sampler1;
	key.sampler[1] = sampler2;

	FrameData *frame = &frameData_[curFrame_ & 1];
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
	VkWriteDescriptorSet writes[2];
	memset(writes, 0, sizeof(writes));
	// Main and sub textures
	int n = 0;
	VkDescriptorImageInfo image1 = {};
	VkDescriptorImageInfo image2 = {};
	if (tex1) {
		// TODO: Also support LAYOUT_GENERAL to be able to texture from framebuffers without transitioning them?
		image1.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
		image2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

VkPipeline Vulkan2D::GetPipeline(VkPipelineCache cache, VkRenderPass rp, VkShaderModule vs, VkShaderModule fs) {
	PipelineKey key;
	key.vs = vs;
	key.fs = fs;

	auto iter = pipelines_.find(key);
	if (iter != pipelines_.end()) {
		return iter->second;
	}

	VkPipelineColorBlendAttachmentState blend0 = {};
	blend0.blendEnable = false;
	blend0.colorWriteMask = 0xF;

	VkPipelineColorBlendStateCreateInfo cbs = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	cbs.pAttachments = &blend0;
	cbs.attachmentCount = 1;
	cbs.logicOpEnable = false;

	VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	dss.depthBoundsTestEnable = false;
	dss.stencilTestEnable = false;
	dss.depthTestEnable = false;

	VkDynamicState dynamicStates[2];
	int numDyn = 0;
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_SCISSOR;
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_VIEWPORT;

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
	vis.vertexBindingDescriptionCount = 1;
	vis.pVertexBindingDescriptions = &ibd;
	vis.vertexAttributeDescriptionCount = attributeCount;
	vis.pVertexAttributeDescriptions = attrs;

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

	// We will use dynamic viewport state.
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
	VkResult result = vkCreateGraphicsPipelines(vulkan_->GetDevice(), cache, 1, &pipe, nullptr, &pipeline);
	if (result == VK_SUCCESS) {
		pipelines_[key] = pipeline;
		return pipeline;
	} else {
		return VK_NULL_HANDLE;
	}
}

void Vulkan2D::BindDescriptorSet(VkCommandBuffer cmd, VkImageView tex1, VkSampler sampler1) {
	VkDescriptorSet descSet = GetDescriptorSet(tex1, sampler1, VK_NULL_HANDLE, VK_NULL_HANDLE);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet, 0, nullptr);
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