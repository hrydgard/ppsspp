#include <cstring>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
// #include "GPU/Vulkan/"
#include "thin3d/VulkanContext.h"

PipelineManagerVulkan::PipelineManagerVulkan(VulkanContext *vulkan) : vulkan_(vulkan) {
	pipelineCache_ = vulkan->CreatePipelineCache();
}

PipelineManagerVulkan::~PipelineManagerVulkan() {
	Clear();
	vulkan_->QueueDelete(pipelineCache_);
}

void PipelineManagerVulkan::Clear() {
	// This should kill off all the shaders at once.
	// This could also be an opportunity to store the whole cache to disk. Will need to also
	// store the keys.
	for (auto iter : pipelines_) {
		delete iter.second;
	}
	pipelines_.clear();
}

struct DeclTypeInfo {
	VkFormat type;
	const char * name;
};

static const DeclTypeInfo VComp[] = {
	{ VK_FORMAT_UNDEFINED, "NULL" }, // DEC_NONE,
	{ VK_FORMAT_R32_SFLOAT, "R32_SFLOAT " },  // DEC_FLOAT_1,
	{ VK_FORMAT_R32G32_SFLOAT, "R32G32_SFLOAT " },  // DEC_FLOAT_2,
	{ VK_FORMAT_R32G32B32_SFLOAT, "R32G32B32_SFLOAT " },  // DEC_FLOAT_3,
	{ VK_FORMAT_R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT " },  // DEC_FLOAT_4,

	{ VK_FORMAT_R8G8B8A8_SNORM, "R8G8B8A8_SNORM" }, // DEC_S8_3,
	{ VK_FORMAT_R16G16B16A16_SNORM, "R16G16B16A16_SNORM	" },	// DEC_S16_3,

	{ VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM	" },	// DEC_U8_1,
	{ VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM	" },	// DEC_U8_2,
	{ VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM	" },	// DEC_U8_3,
	{ VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM	" },	// DEC_U8_4,
	{ VK_FORMAT_R16G16_UNORM, "R16G16_UNORM" },	// 	DEC_U16_1,
	{ VK_FORMAT_R16G16_UNORM, "R16G16_UNORM" },	// 	DEC_U16_2,
	{ VK_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM " }, // DEC_U16_3,
	{ VK_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16_UNORM " }, // DEC_U16_4,
																											// Not supported in regular DX9 so faking, will cause graphics bugs until worked around
	{ VK_FORMAT_R8G8_UINT, "R8G8_UINT" },   // DEC_U8A_2,
	{ VK_FORMAT_R16G16_UINT, "R16G16_UINT" }, // DEC_U16A_2,
};

void VertexAttribSetup(VkVertexInputAttributeDescription *attr, int fmt, int offset, PspAttributeLocation location) {
	attr->location = (uint32_t)location;
	attr->binding = 0;
	attr->format = VComp[fmt].type;
	attr->offset = offset;
}

// Returns the number of attributes that were set.
// We could cache these AttributeDescription arrays (with pspFmt as the key), but hardly worth bothering
// as we will only call this code when we need to create a new VkPipeline.
int SetupVertexAttribs(VkVertexInputAttributeDescription attrs[], const DecVtxFormat &decFmt) {
	int count = 0;
	if (decFmt.w0fmt != 0) {
		VertexAttribSetup(&attrs[count++], decFmt.w0fmt, decFmt.w0off, PspAttributeLocation::W1);
	}
	if (decFmt.w1fmt != 0) {
		VertexAttribSetup(&attrs[count++], decFmt.w1fmt, decFmt.w1off, PspAttributeLocation::W2);
	}
	if (decFmt.uvfmt != 0) {
		VertexAttribSetup(&attrs[count++], decFmt.uvfmt, decFmt.uvoff, PspAttributeLocation::TEXCOORD);
	}
	if (decFmt.c0fmt != 0) {
		VertexAttribSetup(&attrs[count++], decFmt.c0fmt, decFmt.c0off, PspAttributeLocation::COLOR0);
	}
	if (decFmt.c1fmt != 0) {
		VertexAttribSetup(&attrs[count++], decFmt.c1fmt, decFmt.c1off, PspAttributeLocation::COLOR1);
	}
	if (decFmt.nrmfmt != 0) {
		VertexAttribSetup(&attrs[count++], decFmt.nrmfmt, decFmt.nrmoff, PspAttributeLocation::NORMAL);
	}
	// Position is always there.
	VertexAttribSetup(&attrs[count++], decFmt.posfmt, decFmt.posoff, PspAttributeLocation::POSITION);
	return count;
}

int SetupVertexAttribsPretransformed(VkVertexInputAttributeDescription attrs[]) {
	VertexAttribSetup(&attrs[0], DEC_FLOAT_4, 0, PspAttributeLocation::POSITION);
	VertexAttribSetup(&attrs[1], DEC_FLOAT_3, 16, PspAttributeLocation::TEXCOORD);
	VertexAttribSetup(&attrs[2], DEC_U8_4, 28, PspAttributeLocation::COLOR0);
	VertexAttribSetup(&attrs[3], DEC_U8_4, 32, PspAttributeLocation::COLOR1);
	return 4;
}

static VulkanPipeline *CreateVulkanPipeline(VkDevice device, VkPipelineCache pipelineCache, VkPipelineLayout layout, VkRenderPass renderPass, const VulkanPipelineRasterStateKey &key, const VertexDecoder *vtxDec, VkShaderModule vshader, VkShaderModule fshader, bool useHwTransform) {
	VkPipelineColorBlendAttachmentState blend0;
	blend0.blendEnable = key.blendEnable;
	if (key.blendEnable) {
		blend0.colorBlendOp = (VkBlendOp)key.blendOpColor;
		blend0.alphaBlendOp = (VkBlendOp)key.blendOpAlpha;
		blend0.srcColorBlendFactor = (VkBlendFactor)key.srcColor;
		blend0.srcAlphaBlendFactor = (VkBlendFactor)key.srcAlpha;
		blend0.dstColorBlendFactor = (VkBlendFactor)key.destColor;
		blend0.dstAlphaBlendFactor = (VkBlendFactor)key.destAlpha;
	}
	blend0.colorWriteMask = key.colorWriteMask;
	
	VkPipelineColorBlendStateCreateInfo cbs;
	cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cbs.pNext = nullptr;
	cbs.flags = 0;
	cbs.pAttachments = &blend0;
	cbs.attachmentCount = 1;
	cbs.logicOpEnable = key.logicOpEnable;
	if (key.logicOpEnable)
		cbs.logicOp = (VkLogicOp)key.logicOp;
	else
		cbs.logicOp = VK_LOGIC_OP_COPY;

	VkPipelineDepthStencilStateCreateInfo dss = { };
	dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	dss.pNext = nullptr;
	dss.depthBoundsTestEnable = false;
	dss.stencilTestEnable = key.stencilTestEnable;
	if (key.stencilTestEnable) {
		dss.front.compareOp = (VkCompareOp)key.stencilCompareOp;
		dss.front.passOp = (VkStencilOp)key.stencilPassOp;
		dss.front.failOp = (VkStencilOp)key.stencilFailOp;
		dss.front.depthFailOp = (VkStencilOp)key.stencilDepthFailOp;
		// Back stencil is always the same as front on PSP.
		memcpy(&dss.back, &dss.front, sizeof(dss.front));
	}
	dss.depthTestEnable = key.depthTestEnable;
	if (key.depthTestEnable) {
		dss.depthCompareOp = (VkCompareOp)key.depthCompareOp;
		dss.depthWriteEnable = key.depthWriteEnable;
	}

	VkDynamicState dynamicStates[8];
	int numDyn = 0;
	if (key.blendEnable) {
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
	}
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_SCISSOR;
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_VIEWPORT;
	if (key.stencilTestEnable) {
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
	}
	
	VkPipelineDynamicStateCreateInfo ds;
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	ds.pNext = nullptr;
	ds.flags = 0;
	ds.pDynamicStates = dynamicStates;
	ds.dynamicStateCount = numDyn;
	
	VkPipelineRasterizationStateCreateInfo rs;
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.pNext = nullptr;
	rs.flags = 0;
	rs.depthBiasEnable = false;
	rs.cullMode = key.cullMode;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.lineWidth = 1.0f;
	rs.rasterizerDiscardEnable = false;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.depthClampEnable = false;

	VkPipelineMultisampleStateCreateInfo ms;
	memset(&ms, 0, sizeof(ms));
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.pNext = nullptr;
	ms.pSampleMask = nullptr;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineShaderStageCreateInfo ss[2];
	ss[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[0].pNext = nullptr;
	ss[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	ss[0].pSpecializationInfo = nullptr;
	ss[0].module = vshader;
	ss[0].pName = "main";
	ss[0].flags = 0;
	ss[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ss[1].pNext = nullptr;
	ss[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	ss[1].pSpecializationInfo = nullptr;
	ss[1].module = fshader;
	ss[1].pName = "main";
	ss[1].flags = 0;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.pNext = nullptr;
	inputAssembly.flags = 0;
	inputAssembly.topology = (VkPrimitiveTopology)key.topology;
	inputAssembly.primitiveRestartEnable = false;

	int vertexStride = 0;

	int offset = 0;
	VkVertexInputAttributeDescription attrs[8];
	int attributeCount;
	if (useHwTransform) {
		attributeCount = SetupVertexAttribs(attrs, vtxDec->decFmt);
		vertexStride = vtxDec->decFmt.stride;
	} else {
		attributeCount = SetupVertexAttribsPretransformed(attrs);
		vertexStride = 36;
	}

	VkVertexInputBindingDescription ibd;
	ibd.binding = 0;
	ibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	ibd.stride = vertexStride;

	VkPipelineVertexInputStateCreateInfo vis;
	vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vis.pNext = nullptr;
	vis.flags = 0;
	vis.vertexBindingDescriptionCount = 1;
	vis.pVertexBindingDescriptions = &ibd;
	vis.vertexAttributeDescriptionCount = attributeCount;
	vis.pVertexAttributeDescriptions = attrs;

	VkPipelineViewportStateCreateInfo vs;
	vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vs.pNext = nullptr;
	vs.flags = 0;
	vs.viewportCount = 1;
	vs.scissorCount = 1;
	vs.pViewports = nullptr;  // dynamic
	vs.pScissors = nullptr;  // dynamic

	VkGraphicsPipelineCreateInfo pipe;
	pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipe.pNext = nullptr;
	pipe.flags = 0;
	pipe.stageCount = 2;
	pipe.pStages = ss;
	pipe.basePipelineIndex = 0;

	pipe.pColorBlendState = &cbs;
	if (key.depthTestEnable || key.stencilTestEnable) {
		pipe.pDepthStencilState = &dss;
	} else {
		pipe.pDepthStencilState = nullptr;
	}
	pipe.pRasterizationState = &rs;

	// We will use dynamic viewport state.
	pipe.pVertexInputState = &vis;
	pipe.pViewportState = &vs;
	pipe.pTessellationState = nullptr;
	pipe.pDynamicState = &ds;
	pipe.pInputAssemblyState = &inputAssembly;
	pipe.pMultisampleState = &ms;
	pipe.layout = layout;
	pipe.basePipelineHandle = nullptr;
	pipe.basePipelineIndex = 0;
	pipe.renderPass = renderPass;
	pipe.subpass = 0;

	VkPipeline pipeline;
	VkResult result = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipe, nullptr, &pipeline);
	if (result != VK_SUCCESS) {
		ERROR_LOG(G3D, "Failed creating graphics pipeline!");
		return nullptr;
	}

	VulkanPipeline *vulkanPipeline = new VulkanPipeline();
	vulkanPipeline->pipeline = pipeline;
	vulkanPipeline->uniformBlocks = UB_VS_FS_BASE;
	if (useHwTransform) {
		// TODO: Remove BONES and LIGHTS when those aren't used.
		vulkanPipeline->uniformBlocks |= UB_VS_BONES | UB_VS_LIGHTS;
	}
	return vulkanPipeline;
}

VulkanPipeline *PipelineManagerVulkan::GetOrCreatePipeline(VkPipelineLayout layout, const VulkanPipelineRasterStateKey &rasterKey, const VertexDecoder *vtxDec, VulkanVertexShader *vs, VulkanFragmentShader *fs, bool useHwTransform) {
	VulkanPipelineKey key;
	key.raster = rasterKey;
	key.useHWTransform = useHwTransform;
	key.vShader = vs->GetModule();
	key.fShader = fs->GetModule();
	key.vtxDec = vtxDec;
	auto iter = pipelines_.find(key);
	if (iter != pipelines_.end()) {
		return iter->second;
	}
	
	VulkanPipeline *pipeline = CreateVulkanPipeline(
		vulkan_->GetDevice(), pipelineCache_, layout, vulkan_->GetSurfaceRenderPass(), 
		rasterKey, vtxDec, key.vShader, key.fShader, useHwTransform);
	pipelines_[key] = pipeline;
	return pipeline;
}

std::vector<std::string> PipelineManagerVulkan::DebugGetObjectIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_PIPELINE:
	{
		for (auto iter : pipelines_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
	}
	break;
	}
	return ids;
}

std::string PipelineManagerVulkan::DebugGetObjectString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	if (type != SHADER_TYPE_PIPELINE)
		return "N/A";

	VulkanPipelineKey shaderId;
	shaderId.FromString(id);

	auto iter = pipelines_.find(shaderId);
	if (iter == pipelines_.end()) {
		return "";
	}

	switch (stringType) {
	case SHADER_STRING_SHORT_DESC:
	{
		return StringFromFormat("%p", &iter->second);
	}

	case SHADER_STRING_SOURCE_CODE:
	{
		return "N/A";
	}
	default:
		return "N/A";
	}
}
