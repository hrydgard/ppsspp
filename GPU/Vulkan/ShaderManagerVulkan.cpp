// Copyright (c) 2015- PPSSPP Project.

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

#ifdef _WIN32
//#define SHADERLOG
#endif

#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/VFS/VFS.h"

#include "Common/StringUtils.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/Log.h"
#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"

VulkanFragmentShader::VulkanFragmentShader(VulkanContext *vulkan, FShaderID id, const char *code)
	: vulkan_(vulkan), id_(id), failed_(false), module_(0) {
	PROFILE_THIS_SCOPE("shadercomp");
#ifdef USE_UBERSHADER
	ub_id.FS_BIT_CLEARMODE = id.Bit(FS_BIT_CLEARMODE);
	ub_id.FS_BIT_DO_TEXTURE = id.Bit(FS_BIT_DO_TEXTURE);
	ub_id.FS_BIT_TEXFUNC = id.Bits(FS_BIT_TEXFUNC, 3);
	ub_id.FS_BIT_TEXALPHA = id.Bit(FS_BIT_TEXALPHA);
	ub_id.FS_BIT_SHADER_DEPAL = id.Bit(FS_BIT_SHADER_DEPAL);
	ub_id.FS_BIT_SHADER_TEX_CLAMP = id.Bit(FS_BIT_SHADER_TEX_CLAMP);
	ub_id.FS_BIT_CLAMP_S = id.Bit(FS_BIT_CLAMP_S);
	ub_id.FS_BIT_CLAMP_T = id.Bit(FS_BIT_CLAMP_T);
	ub_id.FS_BIT_TEXTURE_AT_OFFSET = id.Bit(FS_BIT_TEXTURE_AT_OFFSET);
	ub_id.FS_BIT_LMODE = id.Bit(FS_BIT_LMODE);
	ub_id.FS_BIT_ALPHA_TEST = id.Bit(FS_BIT_ALPHA_TEST);
	ub_id.FS_BIT_ALPHA_TEST_FUNC = id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3);
	ub_id.FS_BIT_ALPHA_AGAINST_ZERO = id.Bit(FS_BIT_ALPHA_AGAINST_ZERO);
	ub_id.FS_BIT_COLOR_TEST = id.Bit(FS_BIT_COLOR_TEST);
	ub_id.FS_BIT_COLOR_TEST_FUNC = id.Bits(FS_BIT_COLOR_TEST_FUNC, 2);
	ub_id.FS_BIT_COLOR_AGAINST_ZERO = id.Bit(FS_BIT_COLOR_AGAINST_ZERO);
	ub_id.FS_BIT_ENABLE_FOG = id.Bit(FS_BIT_ENABLE_FOG);
	ub_id.FS_BIT_DO_TEXTURE_PROJ = id.Bit(FS_BIT_DO_TEXTURE_PROJ);
	ub_id.FS_BIT_COLOR_DOUBLE = id.Bit(FS_BIT_COLOR_DOUBLE);
	ub_id.FS_BIT_STENCIL_TO_ALPHA = id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2);
	ub_id.FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE = id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);
	ub_id.FS_BIT_REPLACE_LOGIC_OP_TYPE = id.Bits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2);
	ub_id.FS_BIT_REPLACE_BLEND = id.Bits(FS_BIT_REPLACE_BLEND, 3);
	ub_id.FS_BIT_BLENDEQ = id.Bits(FS_BIT_BLENDEQ, 3);
	ub_id.FS_BIT_BLENDFUNC_A = id.Bits(FS_BIT_BLENDFUNC_A, 4);
	ub_id.FS_BIT_BLENDFUNC_B = id.Bits(FS_BIT_BLENDFUNC_B, 4);
	ub_id.FS_BIT_FLATSHADE = id.Bit(FS_BIT_FLATSHADE);
	ub_id.FS_BIT_BGRA_TEXTURE = id.Bit(FS_BIT_BGRA_TEXTURE);
	ub_id.FS_BIT_TEST_DISCARD_TO_ZERO = id.Bit(FS_BIT_TEST_DISCARD_TO_ZERO);
	ub_id.GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT = gstate_c.Supports(GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT);
	ub_id.GPU_SUPPORTS_DEPTH_CLAMP = gstate_c.Supports(GPU_SUPPORTS_DEPTH_CLAMP);
	ub_id.GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT = gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT);
	ub_id.GPU_SUPPORTS_ACCURATE_DEPTH = gstate_c.Supports(GPU_SUPPORTS_ACCURATE_DEPTH);
#endif
	source_ = code;

	std::string errorMessage;
	std::vector<uint32_t> spirv;
#ifdef SHADERLOG
	OutputDebugStringA(LineNumberString(code).c_str());
#endif

	bool success = GLSLtoSPV(VK_SHADER_STAGE_FRAGMENT_BIT, code, spirv, &errorMessage);
	if (!errorMessage.empty()) {
		if (success) {
			ERROR_LOG(G3D, "Warnings in shader compilation!");
		} else {
			ERROR_LOG(G3D, "Error in shader compilation!");
		}
		ERROR_LOG(G3D, "Messages: %s", errorMessage.c_str());
		ERROR_LOG(G3D, "Shader source:\n%s", code);
#ifdef SHADERLOG
		OutputDebugStringA(LineNumberString(code).c_str());
		OutputDebugStringA("Messages:\n");
		OutputDebugStringA(errorMessage.c_str());
#endif
		Reporting::ReportMessage("Vulkan error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	} else {
		success = vulkan_->CreateShaderModule(spirv, &module_);
#ifdef SHADERLOG
		OutputDebugStringA("OK\n");
#endif
	}

	if (!success) {
		failed_ = true;
		return;
	} else {
		VERBOSE_LOG(G3D, "Compiled fragment shader:\n%s\n", (const char *)code);
	}
}

VulkanFragmentShader::~VulkanFragmentShader() {
	if (module_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteShaderModule(module_);
	}
}

std::string VulkanFragmentShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return FragmentShaderDesc(id_);
	default:
		return "N/A";
	}
}

VulkanVertexShader::VulkanVertexShader(VulkanContext *vulkan, VShaderID id, const char *code, bool useHWTransform)
	: vulkan_(vulkan), id_(id), failed_(false), useHWTransform_(useHWTransform), module_(VK_NULL_HANDLE) {
	PROFILE_THIS_SCOPE("shadercomp");
#ifdef USE_UBERSHADER
	ub_id.VS_BIT_LIGHT[0].COMP = id.Bits(VS_BIT_LIGHT0_COMP, 2);
	ub_id.VS_BIT_LIGHT[0].TYPE = id.Bits(VS_BIT_LIGHT0_TYPE, 2);
	ub_id.VS_BIT_LIGHT[1].COMP = id.Bits(VS_BIT_LIGHT1_COMP, 2);
	ub_id.VS_BIT_LIGHT[1].TYPE = id.Bits(VS_BIT_LIGHT1_TYPE, 2);
	ub_id.VS_BIT_LIGHT[2].COMP = id.Bits(VS_BIT_LIGHT2_COMP, 2);
	ub_id.VS_BIT_LIGHT[2].TYPE = id.Bits(VS_BIT_LIGHT2_TYPE, 2);
	ub_id.VS_BIT_LIGHT[3].COMP = id.Bits(VS_BIT_LIGHT3_COMP, 2);
	ub_id.VS_BIT_LIGHT[3].TYPE = id.Bits(VS_BIT_LIGHT3_TYPE, 2);
	ub_id.VS_BIT_LIGHT[0].ENABLE = id.Bit(VS_BIT_LIGHT0_ENABLE);
	ub_id.VS_BIT_LIGHT[1].ENABLE = id.Bit(VS_BIT_LIGHT1_ENABLE);
	ub_id.VS_BIT_LIGHT[2].ENABLE = id.Bit(VS_BIT_LIGHT2_ENABLE);
	ub_id.VS_BIT_LIGHT[3].ENABLE = id.Bit(VS_BIT_LIGHT3_ENABLE);
	ub_id.VS_BIT_LIGHTING_ENABLE = id.Bit(VS_BIT_LIGHTING_ENABLE);
	ub_id.VS_BIT_MATERIAL_UPDATE = id.Bits(VS_BIT_MATERIAL_UPDATE, 3);
	ub_id.VS_BIT_LMODE = id.Bit(VS_BIT_LMODE);
	ub_id.VS_BIT_LS0 = id.Bits(VS_BIT_LS0, 2);
	ub_id.VS_BIT_LS1 = id.Bits(VS_BIT_LS1, 2);
	ub_id.VS_BIT_IS_THROUGH = id.Bit(VS_BIT_IS_THROUGH);
	ub_id.VS_BIT_USE_HW_TRANSFORM = id.Bit(VS_BIT_USE_HW_TRANSFORM);
	ub_id.VS_BIT_DO_TEXTURE = id.Bit(VS_BIT_DO_TEXTURE);
	ub_id.VS_BIT_DO_TEXTURE_TRANSFORM = id.Bit(VS_BIT_DO_TEXTURE_TRANSFORM);
	ub_id.VS_BIT_UVGEN_MODE = id.Bit(VS_BIT_UVGEN_MODE);
	ub_id.VS_BIT_UVPROJ_MODE = id.Bits(VS_BIT_UVPROJ_MODE, 2);
	ub_id.VS_BIT_HAS_TEXCOORD = id.Bit(VS_BIT_HAS_TEXCOORD);
	ub_id.VS_BIT_HAS_TEXCOORD_TESS = id.Bit(VS_BIT_HAS_TEXCOORD_TESS);
	ub_id.VS_BIT_HAS_COLOR = id.Bit(VS_BIT_HAS_COLOR);
	ub_id.VS_BIT_HAS_COLOR_TESS = id.Bit(VS_BIT_HAS_COLOR_TESS);
	ub_id.VS_BIT_HAS_NORMAL = id.Bit(VS_BIT_HAS_NORMAL);
	ub_id.VS_BIT_HAS_NORMAL_TESS = id.Bit(VS_BIT_HAS_NORMAL_TESS);
	ub_id.VS_BIT_NORM_REVERSE = id.Bit(VS_BIT_NORM_REVERSE);
	ub_id.VS_BIT_NORM_REVERSE_TESS = id.Bit(VS_BIT_NORM_REVERSE_TESS);
	ub_id.VS_BIT_ENABLE_BONES = id.Bit(VS_BIT_ENABLE_BONES);
	ub_id.VS_BIT_BONES = id.Bits(VS_BIT_BONES, 3);
	ub_id.VS_BIT_WEIGHT_FMTSCALE = id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2);
	ub_id.VS_BIT_SPLINE = id.Bit(VS_BIT_SPLINE);
	ub_id.VS_BIT_BEZIER = id.Bit(VS_BIT_BEZIER);
	ub_id.GPU_ROUND_DEPTH_TO_16BIT = gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT);
	ub_id.GPU_SUPPORTS_VS_RANGE_CULLING = gstate_c.Supports(GPU_SUPPORTS_VS_RANGE_CULLING);
#endif
	source_ = code;
	std::string errorMessage;
	std::vector<uint32_t> spirv;
#ifdef SHADERLOG
	OutputDebugStringA(LineNumberString(code).c_str());
#endif
	bool success = GLSLtoSPV(VK_SHADER_STAGE_VERTEX_BIT, code, spirv, &errorMessage);
	if (!errorMessage.empty()) {
		if (success) {
			ERROR_LOG(G3D, "Warnings in shader compilation!");
		} else {
			ERROR_LOG(G3D, "Error in shader compilation!");
		}
		ERROR_LOG(G3D, "Messages: %s", errorMessage.c_str());
		ERROR_LOG(G3D, "Shader source:\n%s", code);
#ifdef SHADERLOG
		OutputDebugStringA(LineNumberString(code).c_str());
		OutputDebugStringUTF8("Messages:\n");
		OutputDebugStringUTF8(errorMessage.c_str());
#endif
		Reporting::ReportMessage("Vulkan error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	} else {
		success = vulkan_->CreateShaderModule(spirv, &module_);
#ifdef SHADERLOG
		OutputDebugStringA("OK\n");
#endif
	}

	if (!success) {
		failed_ = true;
		module_ = VK_NULL_HANDLE;
		return;
	} else {
		VERBOSE_LOG(G3D, "Compiled vertex shader:\n%s\n", (const char *)code);
	}
}

VulkanVertexShader::~VulkanVertexShader() {
	if (module_ != VK_NULL_HANDLE) {
		vulkan_->Delete().QueueDeleteShaderModule(module_);
	}
}

std::string VulkanVertexShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return VertexShaderDesc(id_);
	default:
		return "N/A";
	}
}

ShaderManagerVulkan::ShaderManagerVulkan(Draw::DrawContext *draw, VulkanContext *vulkan)
	: ShaderManagerCommon(draw), vulkan_(vulkan), lastVShader_(nullptr), lastFShader_(nullptr), fsCache_(16), vsCache_(16) {
	codeBuffer_ = new char[16384];
	uboAlignment_ = vulkan_->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment;
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));
	memset(&ub_bones, 0, sizeof(ub_bones));

	static_assert(sizeof(ub_base) <= 512, "ub_base grew too big");
	static_assert(sizeof(ub_lights) <= 512, "ub_lights grew too big");
	static_assert(sizeof(ub_bones) <= 384, "ub_bones grew too big");
#ifdef USE_UBERSHADER
	size_t sz;
	ubershader_vs_ = (char*)VFSReadFile("shaders/ubershader.vsh", &sz);
	ubershader_fs_ = (char*)VFSReadFile("shaders/ubershader.fsh", &sz);
#endif
}

ShaderManagerVulkan::~ShaderManagerVulkan() {
	ClearShaders();
	delete[] codeBuffer_;
#ifdef USE_UBERSHADER
	delete ubershader_vs_;
	delete ubershader_fs_;
#endif
}

void ShaderManagerVulkan::DeviceRestore(VulkanContext *vulkan, Draw::DrawContext *draw) {
	vulkan_ = vulkan;
	draw_ = draw;
	uboAlignment_ = vulkan_->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment;
}

void ShaderManagerVulkan::Clear() {
	fsCache_.Iterate([&](const FShaderID &key, VulkanFragmentShader *shader) {
		delete shader;
	});
	vsCache_.Iterate([&](const VShaderID &key, VulkanVertexShader *shader) {
		delete shader;
	});
	fsCache_.Clear();
	vsCache_.Clear();
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

void ShaderManagerVulkan::ClearShaders() {
	Clear();
	DirtyShader();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

void ShaderManagerVulkan::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	DirtyLastShader();
}

void ShaderManagerVulkan::DirtyLastShader() {
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

uint64_t ShaderManagerVulkan::UpdateUniforms(bool useBufferedRendering) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		if (dirty & DIRTY_BASE_UNIFORMS)
			BaseUpdateUniforms(&ub_base, dirty, false, useBufferedRendering);
		if (dirty & DIRTY_LIGHT_UNIFORMS)
			LightUpdateUniforms(&ub_lights, dirty);
		if (dirty & DIRTY_BONE_UNIFORMS)
			BoneUpdateUniforms(&ub_bones, dirty);
	}
	gstate_c.CleanUniforms();
	return dirty;
}

void ShaderManagerVulkan::GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader, bool useHWTransform, bool useHWTessellation) {
	VShaderID VSID;
	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, vertType, useHWTransform, useHWTessellation);
	} else {
		VSID = lastVSID_;
	}

	FShaderID FSID;
	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID, draw_->GetBugs());
	} else {
		FSID = lastFSID_;
	}

	_dbg_assert_(FSID.Bit(FS_BIT_LMODE) == VSID.Bit(VS_BIT_LMODE));
	_dbg_assert_(FSID.Bit(FS_BIT_DO_TEXTURE) == VSID.Bit(VS_BIT_DO_TEXTURE));
	_dbg_assert_(FSID.Bit(FS_BIT_ENABLE_FOG) == VSID.Bit(VS_BIT_ENABLE_FOG));
	_dbg_assert_(FSID.Bit(FS_BIT_FLATSHADE) == VSID.Bit(VS_BIT_FLATSHADE));

	// Just update uniforms if this is the same shader as last time.
	if (lastVShader_ != nullptr && lastFShader_ != nullptr && VSID == lastVSID_ && FSID == lastFSID_) {
		*vshader = lastVShader_;
		*fshader = lastFShader_;
		_dbg_assert_msg_((*vshader)->UseHWTransform() == useHWTransform, "Bad vshader was cached");
		// Already all set, no need to look up in shader maps.
		return;
	}

	VulkanVertexShader *vs = vsCache_.Get(VSID);
	if (!vs)	{
		// Vertex shader not in cache. Let's compile it.
#ifdef USE_UBERSHADER
		vs = new VulkanVertexShader(vulkan_, VSID, ubershader_vs_, useHWTransform);
#else
		GenerateVulkanGLSLVertexShader(VSID, codeBuffer_);
		vs = new VulkanVertexShader(vulkan_, VSID, codeBuffer_, useHWTransform);
#endif
		vsCache_.Insert(VSID, vs);
	}
	lastVSID_ = VSID;

	VulkanFragmentShader *fs = fsCache_.Get(FSID);
	if (!fs) {
#ifdef USE_UBERSHADER
		fs = new VulkanFragmentShader(vulkan_, FSID, ubershader_fs_);
#else
		uint32_t vendorID = vulkan_->GetPhysicalDeviceProperties().properties.vendorID;
		// Fragment shader not in cache. Let's compile it.
		GenerateVulkanGLSLFragmentShader(FSID, codeBuffer_, vendorID);
		fs = new VulkanFragmentShader(vulkan_, FSID, codeBuffer_);
#endif
		fsCache_.Insert(FSID, fs);
	}

	lastFSID_ = FSID;

	lastVShader_ = vs;
	lastFShader_ = fs;

	*vshader = vs;
	*fshader = fs;
	_dbg_assert_msg_((*vshader)->UseHWTransform() == useHWTransform, "Bad vshader was computed");
}

std::vector<std::string> ShaderManagerVulkan::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		vsCache_.Iterate([&](const VShaderID &id, VulkanVertexShader *shader) {
			std::string idstr;
			id.ToString(&idstr);
			ids.push_back(idstr);
		});
		break;
	}
	case SHADER_TYPE_FRAGMENT:
	{
		fsCache_.Iterate([&](const FShaderID &id, VulkanFragmentShader *shader) {
			std::string idstr;
			id.ToString(&idstr);
			ids.push_back(idstr);
		});
		break;
	}
	default:
		break;
	}
	return ids;
}

std::string ShaderManagerVulkan::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		VulkanVertexShader *vs = vsCache_.Get(VShaderID(shaderId));
		return vs ? vs->GetShaderString(stringType) : "";
	}

	case SHADER_TYPE_FRAGMENT:
	{
		VulkanFragmentShader *fs = fsCache_.Get(FShaderID(shaderId));
		return fs ? fs->GetShaderString(stringType) : "";
	}
	default:
		return "N/A";
	}
}

VulkanVertexShader *ShaderManagerVulkan::GetVertexShaderFromModule(VkShaderModule module) {
	VulkanVertexShader *vs = nullptr;
	vsCache_.Iterate([&](const VShaderID &id, VulkanVertexShader *shader) {
		if (shader->GetModule() == module)
			vs = shader;
	});
	return vs;
}

VulkanFragmentShader *ShaderManagerVulkan::GetFragmentShaderFromModule(VkShaderModule module) {
	VulkanFragmentShader *fs = nullptr;
	fsCache_.Iterate([&](const FShaderID &id, VulkanFragmentShader *shader) {
		if (shader->GetModule() == module)
			fs = shader;
	});
	return fs;
}

// Shader cache.
//
// We simply store the IDs of the shaders used during gameplay. On next startup of
// the same game, we simply compile all the shaders from the start, so we don't have to
// compile them on the fly later. We also store the Vulkan pipeline cache, so if it contains
// pipelines compiled from SPIR-V matching these shaders, pipeline creation will be practically
// instantaneous.

#define CACHE_HEADER_MAGIC 0xff51f420 
#define CACHE_VERSION 18
struct VulkanCacheHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t featureFlags;
	uint32_t reserved;
	int numVertexShaders;
	int numFragmentShaders;
};

bool ShaderManagerVulkan::LoadCache(FILE *f) {
	VulkanCacheHeader header{};
	bool success = fread(&header, sizeof(header), 1, f) == 1;
	if (!success || header.magic != CACHE_HEADER_MAGIC)
		return false;
	if (header.version != CACHE_VERSION)
		return false;
	if (header.featureFlags != gstate_c.featureFlags)
		return false;

	for (int i = 0; i < header.numVertexShaders; i++) {
		VShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			ERROR_LOG(G3D, "Vulkan shader cache truncated");
			break;
		}
		bool useHWTransform = id.Bit(VS_BIT_USE_HW_TRANSFORM);
#ifdef USE_UBERSHADER
		VulkanVertexShader *vs = new VulkanVertexShader(vulkan_, id, ubershader_vs_, useHWTransform);
#else
		GenerateVulkanGLSLVertexShader(id, codeBuffer_);
		VulkanVertexShader *vs = new VulkanVertexShader(vulkan_, id, codeBuffer_, useHWTransform);
#endif
		vsCache_.Insert(id, vs);
	}
	uint32_t vendorID = vulkan_->GetPhysicalDeviceProperties().properties.vendorID;
	for (int i = 0; i < header.numFragmentShaders; i++) {
		FShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			ERROR_LOG(G3D, "Vulkan shader cache truncated");
			break;
		}
#ifdef USE_UBERSHADER
		VulkanFragmentShader *fs = new VulkanFragmentShader(vulkan_, id, ubershader_fs_);
#else
		GenerateVulkanGLSLFragmentShader(id, codeBuffer_, vendorID);
		VulkanFragmentShader *fs = new VulkanFragmentShader(vulkan_, id, codeBuffer_);
#endif
		fsCache_.Insert(id, fs);
	}

	NOTICE_LOG(G3D, "Loaded %d vertex and %d fragment shaders", header.numVertexShaders, header.numFragmentShaders);
	return true;
}

void ShaderManagerVulkan::SaveCache(FILE *f) {
	VulkanCacheHeader header{};
	header.magic = CACHE_HEADER_MAGIC;
	header.version = CACHE_VERSION;
	header.featureFlags = gstate_c.featureFlags;
	header.reserved = 0;
	header.numVertexShaders = (int)vsCache_.size();
	header.numFragmentShaders = (int)fsCache_.size();
	bool writeFailed = fwrite(&header, sizeof(header), 1, f) != 1;
	vsCache_.Iterate([&](const VShaderID &id, VulkanVertexShader *vs) {
		writeFailed = writeFailed || fwrite(&id, sizeof(id), 1, f) != 1;
	});
	fsCache_.Iterate([&](const FShaderID &id, VulkanFragmentShader *fs) {
		writeFailed = writeFailed || fwrite(&id, sizeof(id), 1, f) != 1;
	});
	if (writeFailed) {
		ERROR_LOG(G3D, "Failed to write Vulkan shader cache, disk full?");
	} else {
		NOTICE_LOG(G3D, "Saved %d vertex and %d fragment shaders", header.numVertexShaders, header.numFragmentShaders);
	}
}
