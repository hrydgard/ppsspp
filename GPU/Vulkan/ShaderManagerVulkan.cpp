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

#include "Common/StringUtils.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/Log.h"
#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"

VkShaderModule CompileShaderModule(VulkanContext *vulkan, VkShaderStageFlagBits stage, const char *code) {
	PROFILE_THIS_SCOPE("shadercomp");

	std::string errorMessage;
	std::vector<uint32_t> spirv;

	bool success = GLSLtoSPV(stage, code, GLSLVariant::VULKAN, spirv, &errorMessage);

	VkShaderModule shaderModule = VK_NULL_HANDLE;

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
		OutputDebugStringA("Error messages:\n");
		OutputDebugStringA(errorMessage.c_str());
#endif
		Reporting::ReportMessage("Vulkan error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	} else {
#ifdef SHADERLOG
		OutputDebugStringA(LineNumberString(code).c_str());
#endif
#ifdef SHADERLOG
		OutputDebugStringA("OK\n");
#endif
	}

	if (success) {
		vulkan->CreateShaderModule(spirv, &shaderModule);
	}

	return shaderModule;
}


VulkanFragmentShader::VulkanFragmentShader(VulkanContext *vulkan, FShaderID id, const char *code)
	: vulkan_(vulkan), id_(id) {
	source_ = code;
	module_ = CompileShaderModule(vulkan, VK_SHADER_STAGE_FRAGMENT_BIT, source_.c_str());
	if (!module_) {
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
	: vulkan_(vulkan), useHWTransform_(useHWTransform), id_(id) {
	source_ = code;
	module_ = CompileShaderModule(vulkan, VK_SHADER_STAGE_VERTEX_BIT, source_.c_str());
	if (!module_) {
		failed_ = true;
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

ShaderManagerVulkan::ShaderManagerVulkan(Draw::DrawContext *draw)
	: ShaderManagerCommon(draw), compat_(GLSL_VULKAN), fsCache_(16), vsCache_(16) {
	codeBuffer_ = new char[16384];
	VulkanContext *vulkan = (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	uboAlignment_ = vulkan->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment;
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));
	memset(&ub_bones, 0, sizeof(ub_bones));

	static_assert(sizeof(ub_base) <= 512, "ub_base grew too big");
	static_assert(sizeof(ub_lights) <= 512, "ub_lights grew too big");
	static_assert(sizeof(ub_bones) <= 384, "ub_bones grew too big");
}

ShaderManagerVulkan::~ShaderManagerVulkan() {
	ClearShaders();
	delete[] codeBuffer_;
}

void ShaderManagerVulkan::DeviceRestore(Draw::DrawContext *draw) {
	VulkanContext *vulkan = (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	draw_ = draw;
	uboAlignment_ = vulkan->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment;
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

void ShaderManagerVulkan::GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader, bool useHWTransform, bool useHWTessellation, bool weightsAsFloat) {
	VShaderID VSID;
	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, vertType, useHWTransform, useHWTessellation, weightsAsFloat);
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

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	VulkanVertexShader *vs = vsCache_.Get(VSID);
	if (!vs)	{
		// Vertex shader not in cache. Let's compile it.
		std::string genErrorString;
		uint64_t uniformMask = 0;  // Not used
		uint32_t attributeMask = 0;  // Not used
		bool success = GenerateVertexShader(VSID, codeBuffer_, compat_, draw_->GetBugs(), &attributeMask, &uniformMask, &genErrorString);
		_assert_msg_(success, "VS gen error: %s", genErrorString.c_str());
		vs = new VulkanVertexShader(vulkan, VSID, codeBuffer_, useHWTransform);
		vsCache_.Insert(VSID, vs);
	}
	lastVSID_ = VSID;

	VulkanFragmentShader *fs = fsCache_.Get(FSID);
	if (!fs) {
		// uint32_t vendorID = vulkan->GetPhysicalDeviceProperties().properties.vendorID;
		// Fragment shader not in cache. Let's compile it.
		std::string genErrorString;
		uint64_t uniformMask = 0;  // Not used
		bool success = GenerateFragmentShader(FSID, codeBuffer_, compat_, draw_->GetBugs(), &uniformMask, &genErrorString);
		_assert_msg_(success, "FS gen error: %s", genErrorString.c_str());
		fs = new VulkanFragmentShader(vulkan, FSID, codeBuffer_);
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
#define CACHE_VERSION 19
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

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	for (int i = 0; i < header.numVertexShaders; i++) {
		VShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			ERROR_LOG(G3D, "Vulkan shader cache truncated");
			break;
		}
		bool useHWTransform = id.Bit(VS_BIT_USE_HW_TRANSFORM);
		std::string genErrorString;
		uint32_t attributeMask = 0;
		uint64_t uniformMask = 0;
		if (!GenerateVertexShader(id, codeBuffer_, compat_, draw_->GetBugs(), &attributeMask, &uniformMask, &genErrorString)) {
			return false;
		}
		VulkanVertexShader *vs = new VulkanVertexShader(vulkan, id, codeBuffer_, useHWTransform);
		vsCache_.Insert(id, vs);
	}
	uint32_t vendorID = vulkan->GetPhysicalDeviceProperties().properties.vendorID;

	for (int i = 0; i < header.numFragmentShaders; i++) {
		FShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			ERROR_LOG(G3D, "Vulkan shader cache truncated");
			break;
		}
		std::string genErrorString;
		uint64_t uniformMask = 0;
		if (!GenerateFragmentShader(id, codeBuffer_, compat_, draw_->GetBugs(), &uniformMask, &genErrorString)) {
			return false;
		}
		VulkanFragmentShader *fs = new VulkanFragmentShader(vulkan, id, codeBuffer_);
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
