#include <cstring>
#include <memory>
#include <set>
#include <sstream>

#include "Common/Profiler/Profiler.h"

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Core/Config.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/ShaderId.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GPU/Vulkan/VulkanQueueRunner.h"

using namespace PPSSPP_VK;

u32 VulkanPipeline::GetVariantsBitmask() const {
	return pipeline->GetVariantsBitmask();
}

PipelineManagerVulkan::PipelineManagerVulkan(VulkanContext *vulkan) : pipelines_(256), vulkan_(vulkan) {
	// The pipeline cache is created on demand (or explicitly through Load).
}

PipelineManagerVulkan::~PipelineManagerVulkan() {
	// Block on all pipelines to make sure any background compiles are done.
	// This is very important to do before we start trying to tear down the shaders - otherwise, we might
	// be deleting shaders before queued pipeline creations that use them are performed.
	pipelines_.Iterate([&](const VulkanPipelineKey &key, VulkanPipeline *value) {
		if (value->pipeline) {
			value->pipeline->BlockUntilCompiled();
		}
	});

	Clear();
	if (pipelineCache_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeletePipelineCache(pipelineCache_);
	vulkan_ = nullptr;
}

void PipelineManagerVulkan::Clear() {
	pipelines_.Iterate([&](const VulkanPipelineKey &key, VulkanPipeline *value) {
		if (!value->pipeline) {
			// Something went wrong.
			ERROR_LOG(Log::G3D, "Null pipeline found in PipelineManagerVulkan::Clear - didn't wait for asyncs?");
		} else {
			value->pipeline->QueueForDeletion(vulkan_);
		}
		delete value;
	});

	pipelines_.Clear();
}

void PipelineManagerVulkan::InvalidateMSAAPipelines() {
	pipelines_.Iterate([&](const VulkanPipelineKey &key, VulkanPipeline *value) {
		value->pipeline->DestroyVariants(vulkan_, true);
	});
}

void PipelineManagerVulkan::DeviceLost() {
	Clear();
	if (pipelineCache_ != VK_NULL_HANDLE)
		vulkan_->Delete().QueueDeletePipelineCache(pipelineCache_);
	vulkan_ = nullptr;
}

void PipelineManagerVulkan::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;
	// The pipeline cache is created on demand.
}

struct DeclTypeInfo {
	VkFormat type;
	const char *name;
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
};

static void VertexAttribSetup(VkVertexInputAttributeDescription *attr, int fmt, int offset, PspAttributeLocation location) {
	_assert_(fmt != DEC_NONE);
	_assert_(fmt < ARRAY_SIZE(VComp));
	attr->location = (uint32_t)location;
	attr->binding = 0;
	attr->format = VComp[fmt].type;
	attr->offset = offset;
}

// Returns the number of attributes that were set.
// We could cache these AttributeDescription arrays (with pspFmt as the key), but hardly worth bothering
// as we will only call this code when we need to create a new VkPipeline.
static int SetupVertexAttribs(VkVertexInputAttributeDescription attrs[], const DecVtxFormat &decFmt) {
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
	VertexAttribSetup(&attrs[count++], DecVtxFormat::PosFmt(), decFmt.posoff, PspAttributeLocation::POSITION);
	return count;
}

static int SetupVertexAttribsPretransformed(VkVertexInputAttributeDescription attrs[], bool needsUV, bool needsColor1, bool needsFog) {
	int count = 0;
	VertexAttribSetup(&attrs[count++], DEC_FLOAT_4, offsetof(TransformedVertex, pos), PspAttributeLocation::POSITION);
	if (needsUV) {
		VertexAttribSetup(&attrs[count++], DEC_FLOAT_3, offsetof(TransformedVertex, uv), PspAttributeLocation::TEXCOORD);
	}
	VertexAttribSetup(&attrs[count++], DEC_U8_4, offsetof(TransformedVertex, color0), PspAttributeLocation::COLOR0);
	if (needsColor1) {
		VertexAttribSetup(&attrs[count++], DEC_U8_4, offsetof(TransformedVertex, color1), PspAttributeLocation::COLOR1);
	}
	if (needsFog) {
		VertexAttribSetup(&attrs[count++], DEC_FLOAT_1, offsetof(TransformedVertex, fog), PspAttributeLocation::NORMAL);
	}
	return count;
}

static bool UsesBlendConstant(int factor) {
	switch (factor) {
	case VK_BLEND_FACTOR_CONSTANT_ALPHA:
	case VK_BLEND_FACTOR_CONSTANT_COLOR:
	case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
	case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
		return true;
	default:
		return false;
	}
}

static std::string CutFromMain(const std::string &str) {
	std::vector<std::string> lines;
	SplitString(str, '\n', lines);

	std::string rebuilt;
	bool foundStart = false;
	int c = 0;
	for (const std::string &str : lines) {
		if (startsWith(str, "void main")) {
			foundStart = true;
			rebuilt += StringFromFormat("... (cut %d lines)\n", c);
		}
		if (foundStart) {
			rebuilt += str + "\n";
		}
		c++;
	}
	return rebuilt;
}

static VulkanPipeline *CreateVulkanPipeline(VulkanRenderManager *renderManager, VkPipelineCache pipelineCache,
	VKRPipelineLayout *layout, PipelineFlags pipelineFlags, VkSampleCountFlagBits sampleCount, const VulkanPipelineRasterStateKey &key,
	const DecVtxFormat *decFmt, VulkanVertexShader *vs, VulkanFragmentShader *fs, VulkanGeometryShader *gs, bool useHwTransform, u32 variantBitmask, bool cacheLoad) {
	_assert_(fs && vs);

	if (!fs || !fs->GetModule()) {
		ERROR_LOG(Log::G3D, "Fragment shader missing in CreateVulkanPipeline");
		return nullptr;
	}
	if (!vs || !vs->GetModule()) {
		ERROR_LOG(Log::G3D, "Vertex shader missing in CreateVulkanPipeline");
		return nullptr;
	}
	if (gs && !gs->GetModule()) {
		ERROR_LOG(Log::G3D, "Geometry shader missing in CreateVulkanPipeline");
		return nullptr;
	}

	VulkanPipeline *vulkanPipeline = new VulkanPipeline();
	vulkanPipeline->desc = new VKRGraphicsPipelineDesc();
	VKRGraphicsPipelineDesc *desc = vulkanPipeline->desc;
	desc->pipelineCache = pipelineCache;

	desc->fragmentShader = fs->GetModule();
	desc->vertexShader = vs->GetModule();
	desc->geometryShader = gs ? gs->GetModule() : nullptr;

	PROFILE_THIS_SCOPE("pipelinebuild");
	bool useBlendConstant = false;

	VkPipelineColorBlendAttachmentState &blend0 = desc->blend0;
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

	VkPipelineColorBlendStateCreateInfo &cbs = desc->cbs;
	cbs.flags = 0;
	cbs.pAttachments = &blend0;
	cbs.attachmentCount = 1;
	cbs.logicOpEnable = key.logicOpEnable;
	if (key.logicOpEnable)
		cbs.logicOp = (VkLogicOp)key.logicOp;
	else
		cbs.logicOp = VK_LOGIC_OP_COPY;

	VkPipelineDepthStencilStateCreateInfo &dss = desc->dss;
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

	VkDynamicState *dynamicStates = &desc->dynamicStates[0];
	int numDyn = 0;
	if (key.blendEnable &&
		(UsesBlendConstant(key.srcAlpha) || UsesBlendConstant(key.srcColor) || UsesBlendConstant(key.destAlpha) || UsesBlendConstant(key.destColor))) {
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
		useBlendConstant = true;
	}
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_SCISSOR;
	dynamicStates[numDyn++] = VK_DYNAMIC_STATE_VIEWPORT;
	if (key.stencilTestEnable) {
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
		dynamicStates[numDyn++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
	}

	VkPipelineDynamicStateCreateInfo &ds = desc->ds;
	ds.flags = 0;
	ds.pDynamicStates = dynamicStates;
	ds.dynamicStateCount = numDyn;

	VkPipelineRasterizationStateCreateInfo &rs = desc->rs;
	rs.flags = 0;
	rs.depthBiasEnable = false;
	rs.cullMode = key.cullMode;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.lineWidth = 1.0f;
	rs.rasterizerDiscardEnable = false;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.depthClampEnable = key.depthClampEnable;

	if (renderManager->GetVulkanContext()->GetDeviceFeatures().enabled.provokingVertex.provokingVertexLast) {
		ChainStruct(rs, &desc->rs_provoking);
		desc->rs_provoking.provokingVertexMode = VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;
	}

	desc->fragmentShaderSource = fs->GetShaderString(SHADER_STRING_SOURCE_CODE);
	desc->vertexShaderSource = vs->GetShaderString(SHADER_STRING_SOURCE_CODE);
	if (gs) {
		desc->geometryShaderSource = gs->GetShaderString(SHADER_STRING_SOURCE_CODE);
	}

	_dbg_assert_(key.topology != VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
	_dbg_assert_(key.topology != VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
	desc->topology = (VkPrimitiveTopology)key.topology;

	int vertexStride = 0;
	VkVertexInputAttributeDescription *attrs = &desc->attrs[0];

	int attributeCount;
	if (useHwTransform) {
		attributeCount = SetupVertexAttribs(attrs, *decFmt);
		vertexStride = decFmt->stride;
	} else {
		bool needsUV = true;
		bool needsColor1 = vs->GetID().Bit(VS_BIT_LMODE);
		attributeCount = SetupVertexAttribsPretransformed(attrs, needsUV, needsColor1, true);
		vertexStride = (int)sizeof(TransformedVertex);
	}

	VkVertexInputBindingDescription &ibd = desc->ibd;
	ibd.binding = 0;
	ibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	ibd.stride = vertexStride;

	VkPipelineVertexInputStateCreateInfo &vis = desc->vis;
	vis.flags = 0;
	vis.vertexBindingDescriptionCount = 1;
	vis.pVertexBindingDescriptions = &desc->ibd;
	vis.vertexAttributeDescriptionCount = attributeCount;
	vis.pVertexAttributeDescriptions = attrs;

	VkPipelineViewportStateCreateInfo &views = desc->views;
	views.flags = 0;
	views.viewportCount = 1;
	views.scissorCount = 1;
	views.pViewports = nullptr;  // dynamic
	views.pScissors = nullptr;  // dynamic

	desc->pipelineLayout = layout;

	std::string tag = "game";
#ifdef _DEBUG
	tag = FragmentShaderDesc(fs->GetID()) + " VS " + VertexShaderDesc(vs->GetID());
#endif

	VKRGraphicsPipeline *pipeline = renderManager->CreateGraphicsPipeline(desc, pipelineFlags, variantBitmask, sampleCount, cacheLoad, tag.c_str());

	vulkanPipeline->pipeline = pipeline;
	if (useBlendConstant) {
		pipelineFlags |= PipelineFlags::USES_BLEND_CONSTANT;
	}
	if (gs) {
		pipelineFlags |= PipelineFlags::USES_GEOMETRY_SHADER;
	}
	if (dss.depthTestEnable || dss.stencilTestEnable) {
		pipelineFlags |= PipelineFlags::USES_DEPTH_STENCIL;
	}
	vulkanPipeline->pipelineFlags = pipelineFlags;
	return vulkanPipeline;
}

VulkanPipeline *PipelineManagerVulkan::GetOrCreatePipeline(VulkanRenderManager *renderManager, VKRPipelineLayout *layout, const VulkanPipelineRasterStateKey &rasterKey, const DecVtxFormat *decFmt, VulkanVertexShader *vs, VulkanFragmentShader *fs, VulkanGeometryShader *gs, bool useHwTransform, u32 variantBitmask, int multiSampleLevel, bool cacheLoad) {
	if (!pipelineCache_) {
		VkPipelineCacheCreateInfo pc{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		VkResult res = vkCreatePipelineCache(vulkan_->GetDevice(), &pc, nullptr, &pipelineCache_);
		_assert_(VK_SUCCESS == res);
	}

	VulkanPipelineKey key{};

	key.raster = rasterKey;
	key.useHWTransform = useHwTransform;
	key.vShader = vs->GetModule();
	key.fShader = fs->GetModule();
	key.gShader = gs ? gs->GetModule() : VK_NULL_HANDLE;
	key.vtxFmtId = useHwTransform ? decFmt->id : 0;

	VulkanPipeline *pipeline;
	if (pipelines_.Get(key, &pipeline)) {
		return pipeline;
	}

	PipelineFlags pipelineFlags = (PipelineFlags)0;
	if (fs->Flags() & FragmentShaderFlags::USES_DISCARD) {
		pipelineFlags |= PipelineFlags::USES_DISCARD;
	}
	if (fs->Flags() & FragmentShaderFlags::USES_FLAT_SHADING) {
		pipelineFlags |= PipelineFlags::USES_FLAT_SHADING;
	}
	if (vs->Flags() & VertexShaderFlags::MULTI_VIEW) {
		pipelineFlags |= PipelineFlags::USES_MULTIVIEW;
	}

	VkSampleCountFlagBits sampleCount = MultiSampleLevelToFlagBits(multiSampleLevel);

	pipeline = CreateVulkanPipeline(
		renderManager, pipelineCache_, layout, pipelineFlags, sampleCount,
		rasterKey, decFmt, vs, fs, gs, useHwTransform, variantBitmask, cacheLoad);

	// If the above failed, we got a null pipeline. We still insert it to keep track.
	pipelines_.Insert(key, pipeline);

	// Don't return placeholder null pipelines.
	if (pipeline && pipeline->pipeline) {
		return pipeline;
	} else {
		return nullptr;
	}
}

std::vector<std::string> PipelineManagerVulkan::DebugGetObjectIDs(DebugShaderType type) const {
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_PIPELINE:
	{
		ids.reserve(pipelines_.size());
		pipelines_.Iterate([&](const VulkanPipelineKey &key, VulkanPipeline *value) {
			std::string id;
			key.ToString(&id);
			ids.push_back(id);
		});
	}
	break;
	default:
		break;
	}
	return ids;
}

static const char *const topologies[8] = {
	"POINTS",
	"LINES",
	"LINESTRIP",
	"TRIS",
	"TRISTRIP",
	"TRIFAN",
};

static const char *const blendOps[8] = {
	"ADD",
	"SUB",
	"RSUB",
	"MIN",
	"MAX",
};

static const char *const compareOps[8] = {
	"NEVER",
	"<",
	"==",
	"<=",
	">",
	">=",
	"!=",
	"ALWAYS",
};

static const char *const logicOps[] = {
	"CLEAR",
	"AND",
	"AND_REV",
	"COPY",
	"AND_INV",
	"NOOP",
	"XOR",
	"OR",
	"NOR",
	"EQUIV",
	"INVERT",
	"OR_REV",
	"COPY_INV",
	"OR_INV",
	"NAND",
	"SET",
};

static const char *const stencilOps[8] = {
	"KEEP",
	"ZERO",
	"REPL",
	"INC_SAT",
	"DEC_SAT",
	"INVERT",
	"INC_WRAP",
	"DEC_WRAP",
};

static const char *const blendFactors[19] = {
	"ZERO",
	"ONE",
	"SRC_COL",
	"INV_SRC_COL",
	"DST_COL",
	"INV_DST_COL",
	"SRC_A",
	"INV_SRC_A",
	"DST_A",
	"INV_DST_A",
	"CONSTANT_COL",
	"INV_CONST_COL",
	"CONSTANT_A",
	"INV_CONST_A",
	"SRC_A_SAT",
	"SRC1_COL",
	"INV_SRC1_COL",
	"SRC1_A",
	"INV_SRC1_A",
};

std::string PipelineManagerVulkan::DebugGetObjectString(const std::string &id, DebugShaderType type, DebugShaderStringType stringType, ShaderManagerVulkan *shaderManager) {
	if (type != SHADER_TYPE_PIPELINE)
		return "N/A";

	VulkanPipelineKey pipelineKey;
	pipelineKey.FromString(id);

	VulkanPipeline *pipeline;
	if (!pipelines_.Get(pipelineKey, &pipeline)) {
		return "N/A (missing)";
	}
	_assert_(pipeline != nullptr);
	u32 variants = pipeline->GetVariantsBitmask();

	std::string keyDescription = pipelineKey.GetDescription(stringType, shaderManager);
	return StringFromFormat("%s. v: %08x", keyDescription.c_str(), variants);
}

std::string VulkanPipelineKey::GetRasterStateDesc(bool lineBreaks) const {
	std::stringstream str;
	str << topologies[raster.topology] << " ";
	if (useHWTransform) {
		str << "HWX ";
	}
	if (vtxFmtId) {
		str << "Vfmt(" << StringFromFormat("%08x", vtxFmtId) << ") ";  // TODO: Format nicer.
	} else {
		str << "SWX ";
	}
	if (lineBreaks) str << std::endl;
	if (raster.blendEnable) {
		str << "Blend(C:" << blendOps[raster.blendOpColor] << "/"
			<< blendFactors[raster.srcColor] << ":" << blendFactors[raster.destColor] << " ";
		if (raster.blendOpAlpha != VK_BLEND_OP_ADD ||
			raster.srcAlpha != VK_BLEND_FACTOR_ONE ||
			raster.destAlpha != VK_BLEND_FACTOR_ZERO) {
			str << "A:" << blendOps[raster.blendOpAlpha] << "/"
				<< blendFactors[raster.srcColor] << ":" << blendFactors[raster.destColor] << " ";
		}
		str << ") ";
		if (lineBreaks) str << std::endl;
	}
	if (raster.colorWriteMask != 0xF) {
		str << "Mask(";
		for (int i = 0; i < 4; i++) {
			if (raster.colorWriteMask & (1 << i)) {
				str << "RGBA"[i];
			} else {
				str << "_";
			}
		}
		str << ") ";
		if (lineBreaks) str << std::endl;
	}
	if (raster.depthTestEnable) {
		str << "Z(";
		if (raster.depthWriteEnable)
			str << "W, ";
		if (raster.depthCompareOp)
			str << compareOps[raster.depthCompareOp & 7];
		str << ") ";
		if (lineBreaks) str << std::endl;
	}
	if (raster.stencilTestEnable) {
		str << "Stenc(";
		str << compareOps[raster.stencilCompareOp & 7] << " ";
		str << stencilOps[raster.stencilPassOp & 7] << "/";
		str << stencilOps[raster.stencilFailOp & 7] << "/";
		str << stencilOps[raster.stencilDepthFailOp & 7];
		str << ") ";
		if (lineBreaks) str << std::endl;
	}
	if (raster.logicOpEnable) {
		str << "Logic(" << logicOps[raster.logicOp & 15] << ") ";
		if (lineBreaks) str << std::endl;
	}
	return str.str();
}

std::string VulkanPipelineKey::GetDescription(DebugShaderStringType stringType, ShaderManagerVulkan *shaderManager) const {
	switch (stringType) {
	case SHADER_STRING_SHORT_DESC:
		// Just show the raster state. Also show brief VS/FS IDs?
		return GetRasterStateDesc(false);

	case SHADER_STRING_SOURCE_CODE:
	{
		// More detailed description of all the parts of the pipeline.
		VkShaderModule fsModule = this->fShader->BlockUntilReady();
		VkShaderModule vsModule = this->vShader->BlockUntilReady();
		VkShaderModule gsModule = this->gShader ? this->gShader->BlockUntilReady() : VK_NULL_HANDLE;

		std::stringstream str;
		str << "VS: " << VertexShaderDesc(shaderManager->GetVertexShaderFromModule(vsModule)->GetID()) << std::endl;
		str << "FS: " << FragmentShaderDesc(shaderManager->GetFragmentShaderFromModule(fsModule)->GetID()) << std::endl;
		if (gsModule) {
			str << "GS: " << GeometryShaderDesc(shaderManager->GetGeometryShaderFromModule(gsModule)->GetID()) << std::endl;
		}
		str << GetRasterStateDesc(true);
		return str.str();
	}

	default:
		return "N/A";
	}
}

// For some reason this struct is only defined in the spec, not in the headers.
struct VkPipelineCacheHeader {
	uint32_t headerSize;
	VkPipelineCacheHeaderVersion version;
	uint32_t vendorId;
	uint32_t deviceId;
	uint8_t uuid[VK_UUID_SIZE];
};

struct StoredVulkanPipelineKey {
	VulkanPipelineRasterStateKey raster;
	VShaderID vShaderID;
	FShaderID fShaderID;
	GShaderID gShaderID;
	uint32_t vtxFmtId;
	uint32_t variants;
	bool useHWTransform;  // TODO: Still needed?

	// For std::set. Better zero-initialize the struct properly for this to work.
	bool operator < (const StoredVulkanPipelineKey &other) const {
		return memcmp(this, &other, sizeof(*this)) < 0;
	}
};

// If you're looking for how to invalidate the cache, it's done in ShaderManagerVulkan, look for CACHE_VERSION and increment it.
// (Header of the same file this is stored in).
void PipelineManagerVulkan::SavePipelineCache(FILE *file, bool saveRawPipelineCache, ShaderManagerVulkan *shaderManager, Draw::DrawContext *drawContext) {
	VulkanRenderManager *rm = (VulkanRenderManager *)drawContext->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	VulkanQueueRunner *queueRunner = rm->GetQueueRunner();

	size_t dataSize = 0;
	uint32_t size;

	if (saveRawPipelineCache) {
		// WARNING: See comment in LoadCache before using this path.
		VkResult result = vkGetPipelineCacheData(vulkan_->GetDevice(), pipelineCache_, &dataSize, nullptr);
		uint32_t size = (uint32_t)dataSize;
		if (result != VK_SUCCESS) {
			size = 0;
			fwrite(&size, sizeof(size), 1, file);
			return;
		}
		auto buffer = std::make_unique<uint8_t[]>(dataSize);
		vkGetPipelineCacheData(vulkan_->GetDevice(), pipelineCache_, &dataSize, buffer.get());
		size = (uint32_t)dataSize;
		fwrite(&size, sizeof(size), 1, file);
		fwrite(buffer.get(), 1, size, file);
		NOTICE_LOG(Log::G3D, "Saved Vulkan pipeline cache (%d bytes).", (int)size);
	}

	size_t seekPosOnFailure = ftell(file);

	bool failed = false;
	bool writeFailed = false;
	// Since we don't include the full pipeline key, there can be duplicates,
	// caused by things like switching from buffered to non-buffered rendering.
	// Make sure the set of pipelines we write is "unique".
	std::set<StoredVulkanPipelineKey> keys;

	pipelines_.Iterate([&](const VulkanPipelineKey &pkey, VulkanPipeline *value) {
		if (failed)
			return;
		VulkanVertexShader *vshader = shaderManager->GetVertexShaderFromModule(pkey.vShader->BlockUntilReady());
		VulkanFragmentShader *fshader = shaderManager->GetFragmentShaderFromModule(pkey.fShader->BlockUntilReady());
		VulkanGeometryShader *gshader = nullptr;
		if (pkey.gShader) {
			gshader = shaderManager->GetGeometryShaderFromModule(pkey.gShader->BlockUntilReady());
			if (!gshader)
				failed = true;
		}
		if (!vshader || !fshader || failed) {
			failed = true;
			return;
		}
		_dbg_assert_(pkey.raster.topology != VK_PRIMITIVE_TOPOLOGY_POINT_LIST && pkey.raster.topology != VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
		StoredVulkanPipelineKey key{};
		key.raster = pkey.raster;
		key.useHWTransform = pkey.useHWTransform;
		key.fShaderID = fshader->GetID();
		key.vShaderID = vshader->GetID();
		key.gShaderID = gshader ? gshader->GetID() : GShaderID();
		key.variants = value->GetVariantsBitmask();
		if (key.useHWTransform) {
			// NOTE: This is not a vtype, but a decoded vertex format.
			key.vtxFmtId = pkey.vtxFmtId;
		}
		keys.insert(key);
	});

	// Write the number of pipelines.
	size = (uint32_t)keys.size();
	writeFailed = writeFailed || fwrite(&size, sizeof(size), 1, file) != 1;

	// Write the pipelines.
	for (auto &key : keys) {
		writeFailed = writeFailed || fwrite(&key, sizeof(key), 1, file) != 1;
	}

	if (failed) {
		ERROR_LOG(Log::G3D, "Failed to write pipeline cache, some shader was missing");
		// Write a zero in the right place so it doesn't try to load the pipelines next time.
		size = 0;
		fseek(file, (long)seekPosOnFailure, SEEK_SET);
		writeFailed = fwrite(&size, sizeof(size), 1, file) != 1;
		if (writeFailed) {
			ERROR_LOG(Log::G3D, "Failed to write pipeline cache, disk full?");
		}
		return;
	}
	if (writeFailed) {
		ERROR_LOG(Log::G3D, "Failed to write pipeline cache, disk full?");
	} else {
		NOTICE_LOG(Log::G3D, "Saved Vulkan pipeline ID cache (%d unique pipelines/%d).", (int)keys.size(), (int)pipelines_.size());
	}
}

bool PipelineManagerVulkan::LoadPipelineCache(FILE *file, bool loadRawPipelineCache, ShaderManagerVulkan *shaderManager, Draw::DrawContext *drawContext, VKRPipelineLayout *layout, int multiSampleLevel) {
	VulkanRenderManager *rm = (VulkanRenderManager *)drawContext->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	VulkanQueueRunner *queueRunner = rm->GetQueueRunner();

	uint32_t size = 0;
	if (loadRawPipelineCache) {
		NOTICE_LOG(Log::G3D, "WARNING: Using the badly tested raw pipeline cache path!!!!");
		// WARNING: Do not use this path until after reading and implementing https://zeux.io/2019/07/17/serializing-pipeline-cache/ !
		bool success = fread(&size, sizeof(size), 1, file) == 1;
		if (!size || !success) {
			WARN_LOG(Log::G3D, "Zero-sized Vulkan pipeline cache.");
			return true;
		}
		auto buffer = std::make_unique<uint8_t[]>(size);
		success = fread(buffer.get(), 1, size, file) == size;
		// Verify header.
		VkPipelineCacheHeader *header = (VkPipelineCacheHeader *)buffer.get();
		if (!success || header->version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
			// Bad header, don't do anything.
			WARN_LOG(Log::G3D, "Bad Vulkan pipeline cache header - ignoring");
			return false;
		}
		if (0 != memcmp(header->uuid, vulkan_->GetPhysicalDeviceProperties().properties.pipelineCacheUUID, VK_UUID_SIZE)) {
			// Wrong hardware/driver/etc.
			WARN_LOG(Log::G3D, "Bad Vulkan pipeline cache UUID - ignoring");
			return false;
		}

		VkPipelineCacheCreateInfo pc{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		pc.pInitialData = buffer.get();
		pc.initialDataSize = size;
		pc.flags = 0;
		VkPipelineCache cache;
		VkResult res = vkCreatePipelineCache(vulkan_->GetDevice(), &pc, nullptr, &cache);
		if (res != VK_SUCCESS) {
			return false;
		}
		if (!pipelineCache_) {
			pipelineCache_ = cache;
		} else {
			vkMergePipelineCaches(vulkan_->GetDevice(), pipelineCache_, 1, &cache);
		}
		NOTICE_LOG(Log::G3D, "Loaded Vulkan binary pipeline cache (%d bytes).", (int)size);
		// Note that after loading the cache, it's still a good idea to pre-create the various pipelines.
	} else {
		if (!pipelineCache_) {
			VkPipelineCacheCreateInfo pc{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
			VkResult res = vkCreatePipelineCache(vulkan_->GetDevice(), &pc, nullptr, &pipelineCache_);
			if (res != VK_SUCCESS) {
				WARN_LOG(Log::G3D, "vkCreatePipelineCache failed (%08x), highly unexpected", (u32)res);
				return false;
			}
		}
	}

	// Read the number of pipelines.
	bool failed = fread(&size, sizeof(size), 1, file) != 1;

	NOTICE_LOG(Log::G3D, "Creating %d pipelines from cache (%dx MSAA)...", size, (1 << multiSampleLevel));
	int pipelineCreateFailCount = 0;
	int shaderFailCount = 0;
	for (uint32_t i = 0; i < size; i++) {
		if (failed) {
			break;
		}
		StoredVulkanPipelineKey key;
		failed = failed || fread(&key, sizeof(key), 1, file) != 1;
		if (failed) {
			ERROR_LOG(Log::G3D, "Truncated Vulkan pipeline cache file, stopping.");
			break;
		}

		if (key.raster.topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST || key.raster.topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST) {
			WARN_LOG(Log::G3D, "Bad raster key in cache, ignoring");
			continue;
		}

		VulkanVertexShader *vs = shaderManager->GetVertexShaderFromID(key.vShaderID);
		VulkanFragmentShader *fs = shaderManager->GetFragmentShaderFromID(key.fShaderID);
		VulkanGeometryShader *gs = shaderManager->GetGeometryShaderFromID(key.gShaderID);
		if (!vs || !fs || (!gs && key.gShaderID.Bit(GS_BIT_ENABLED))) {
			// We just ignore this one, it'll get created later if needed.
			// Probably some useFlags mismatch.
			WARN_LOG(Log::G3D, "Failed to find vs or fs in pipeline %d in cache, skipping pipeline", (int)i);
			continue;
		}

		// Avoid creating multisampled shaders if it's not enabled, as that results in an invalid combination.
		// Note that variantsToBuild is NOT directly a RenderPassType! instead, it's a collection of (1 << RenderPassType).
		u32 variantsToBuild = key.variants;
		if (multiSampleLevel == 0) {
			for (u32 i = 0; i < (int)RenderPassType::TYPE_COUNT; i++) {
				if (RenderPassTypeHasMultisample((RenderPassType)i)) {
					variantsToBuild &= ~(1 << i);
				}
			}
		}

		DecVtxFormat fmt;
		fmt.InitializeFromID(key.vtxFmtId);
		VulkanPipeline *pipeline = GetOrCreatePipeline(
			rm, layout, key.raster, key.useHWTransform ? &fmt : 0, vs, fs, gs, key.useHWTransform, variantsToBuild, multiSampleLevel, true);
		if (!pipeline) {
			pipelineCreateFailCount += 1;
		}
	}

	rm->NudgeCompilerThread();

	NOTICE_LOG(Log::G3D, "Recreated Vulkan pipeline cache (%d pipelines, %d failed).", (int)size, pipelineCreateFailCount);
	// We just ignore any failures.
	return true;
}
