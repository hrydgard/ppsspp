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
#define SHADERLOG
#endif

#include <map>

#include "base/logging.h"
#include "math/lin/matrix4x4.h"
#include "math/math_util.h"
#include "math/dataconv.h"
#include "util/text/utf8.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"

VulkanFragmentShader::VulkanFragmentShader(VulkanContext *vulkan, ShaderID id, const char *code, bool useHWTransform)
	: vulkan_(vulkan), id_(id), failed_(false), useHWTransform_(useHWTransform), module_(0) {
	source_ = code;

	std::string errorMessage;
	std::vector<uint32_t> spirv;
#ifdef SHADERLOG
	OutputDebugStringA(code);
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
		OutputDebugStringA("Messages:\n");
		OutputDebugStringA(errorMessage.c_str());
		OutputDebugStringA(code);
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
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
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

VulkanVertexShader::VulkanVertexShader(VulkanContext *vulkan, ShaderID id, const char *code, int vertType, bool useHWTransform, bool usesLighting)
	: vulkan_(vulkan), id_(id), failed_(false), useHWTransform_(useHWTransform), module_(VK_NULL_HANDLE), usesLighting_(usesLighting) {
	source_ = code;
	std::string errorMessage;
	std::vector<uint32_t> spirv;
#ifdef SHADERLOG
	OutputDebugStringA(code);
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
		OutputDebugStringUTF8("Messages:\n");
		OutputDebugStringUTF8(errorMessage.c_str());
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
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
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

ShaderManagerVulkan::ShaderManagerVulkan(VulkanContext *vulkan)
	: vulkan_(vulkan), lastVShader_(nullptr), lastFShader_(nullptr) {
	codeBuffer_ = new char[16384];
	uboAlignment_ = vulkan_->GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));
	memset(&ub_bones, 0, sizeof(ub_bones));

	ILOG("sizeof(ub_base): %d", (int)sizeof(ub_base));
	ILOG("sizeof(ub_lights): %d", (int)sizeof(ub_lights));
	ILOG("sizeof(ub_bones): %d", (int)sizeof(ub_bones));
}

ShaderManagerVulkan::~ShaderManagerVulkan() {
	ClearShaders();
	delete[] codeBuffer_;
}

uint32_t ShaderManagerVulkan::PushBaseBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
	return dest->PushAligned(&ub_base, sizeof(ub_base), uboAlignment_, buf);
}

uint32_t ShaderManagerVulkan::PushLightBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
	return dest->PushAligned(&ub_lights, sizeof(ub_lights), uboAlignment_, buf);
}

// TODO: Only push half the bone buffer if we only have four bones.
uint32_t ShaderManagerVulkan::PushBoneBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
	return dest->PushAligned(&ub_bones, sizeof(ub_bones), uboAlignment_, buf);
}

void ShaderManagerVulkan::DeviceRestore(VulkanContext *vulkan) {
	vulkan_ = vulkan;
	uboAlignment_ = vulkan_->GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;
}

void ShaderManagerVulkan::Clear() {
	for (auto iter = fsCache_.begin(); iter != fsCache_.end(); ++iter)	{
		delete iter->second;
	}
	for (auto iter = vsCache_.begin(); iter != vsCache_.end(); ++iter)	{
		delete iter->second;
	}
	fsCache_.clear();
	vsCache_.clear();
	lastFSID_.clear();
	lastVSID_.clear();
}

void ShaderManagerVulkan::ClearShaders() {
	Clear();
	DirtyShader();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);
}

void ShaderManagerVulkan::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.clear();
	lastVSID_.clear();
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
}

void ShaderManagerVulkan::DirtyLastShader() { // disables vertex arrays
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
}

uint64_t ShaderManagerVulkan::UpdateUniforms() {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		if (dirty & DIRTY_BASE_UNIFORMS)
			BaseUpdateUniforms(&ub_base, dirty, false);
		if (dirty & DIRTY_LIGHT_UNIFORMS)
			LightUpdateUniforms(&ub_lights, dirty);
		if (dirty & DIRTY_BONE_UNIFORMS)
			BoneUpdateUniforms(&ub_bones, dirty);
	}
	gstate_c.CleanUniforms();
	return dirty;
}

void ShaderManagerVulkan::GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader, bool useHWTransform) {
	ShaderID VSID;
	ShaderID FSID;
	ComputeVertexShaderID(&VSID, vertType, useHWTransform);
	ComputeFragmentShaderID(&FSID);

	// Just update uniforms if this is the same shader as last time.
	if (lastVShader_ != nullptr && lastFShader_ != nullptr && VSID == lastVSID_ && FSID == lastFSID_) {
		*vshader = lastVShader_;
		*fshader = lastFShader_;
		// Already all set, no need to look up in shader maps.
		return;
	}

	VSCache::iterator vsIter = vsCache_.find(VSID);
	VulkanVertexShader *vs;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		bool usesLighting;
		GenerateVulkanGLSLVertexShader(VSID, codeBuffer_, &usesLighting);
		vs = new VulkanVertexShader(vulkan_, VSID, codeBuffer_, vertType, useHWTransform, usesLighting);
		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	lastVSID_ = VSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	VulkanFragmentShader *fs;
	if (fsIter == fsCache_.end())	{
		// Fragment shader not in cache. Let's compile it.
		GenerateVulkanGLSLFragmentShader(FSID, codeBuffer_);
		fs = new VulkanFragmentShader(vulkan_, FSID, codeBuffer_, useHWTransform);
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	lastFSID_ = FSID;

	lastVShader_ = vs;
	lastFShader_ = fs;

	*vshader = vs;
	*fshader = fs;
}

std::vector<std::string> ShaderManagerVulkan::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		for (auto iter : vsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
		break;
	}
	case SHADER_TYPE_FRAGMENT:
	{
		for (auto iter : fsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
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
		auto iter = vsCache_.find(shaderId);
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}

	case SHADER_TYPE_FRAGMENT:
	{
		auto iter = fsCache_.find(shaderId);
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}
	default:
		return "N/A";
	}
}
