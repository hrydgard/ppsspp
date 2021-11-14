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

#pragma once

#include <cstdio>
#include <cstdint>

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "Common/Math/lin/matrix4x4.h"
#include "GPU/Common/ShaderUniforms.h"

class VulkanContext;
class VulkanPushBuffer;

class VulkanFragmentShader {
public:
	VulkanFragmentShader(VulkanContext *vulkan, FShaderID id, const char *code);
	~VulkanFragmentShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	VkShaderModule GetModule() const { return module_; }
	const FShaderID &GetID() { return id_; }

protected:	
	VkShaderModule module_ = VK_NULL_HANDLE;

	VulkanContext *vulkan_;
	std::string source_;
	bool failed_ = false;
	FShaderID id_;
};

class VulkanVertexShader {
public:
	VulkanVertexShader(VulkanContext *vulkan, VShaderID id, const char *code, bool useHWTransform);
	~VulkanVertexShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	VkShaderModule GetModule() const { return module_; }
	const VShaderID &GetID() { return id_; }

protected:
	VkShaderModule module_ = VK_NULL_HANDLE;

	VulkanContext *vulkan_;
	std::string source_;
	bool failed_ = false;
	bool useHWTransform_;
	VShaderID id_;
};

class VulkanPushBuffer;

class ShaderManagerVulkan : public ShaderManagerCommon {
public:
	ShaderManagerVulkan(Draw::DrawContext *draw);
	~ShaderManagerVulkan();

	void DeviceRestore(Draw::DrawContext *draw);

	void GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader, bool useHWTransform, bool useHWTessellation, bool weightsAsFloat);
	void ClearShaders();
	void DirtyShader();
	void DirtyLastShader() override;

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	// Used for saving/loading the cache. Don't need to be particularly fast.
	VulkanVertexShader *GetVertexShaderFromID(VShaderID id) { return vsCache_.Get(id); }
	VulkanFragmentShader *GetFragmentShaderFromID(FShaderID id) { return fsCache_.Get(id); }
	VulkanVertexShader *GetVertexShaderFromModule(VkShaderModule module);
	VulkanFragmentShader *GetFragmentShaderFromModule(VkShaderModule module);

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	uint64_t UpdateUniforms(bool useBufferedRendering);

	// TODO: Avoid copying these buffers if same as last draw, can still point to it assuming we're still in the same pushbuffer.
	// Applies dirty changes and copies the buffer.
	bool IsBaseDirty() { return true; }
	bool IsLightDirty() { return true; }
	bool IsBoneDirty() { return true; }

	uint32_t PushBaseBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
		return dest->PushAligned(&ub_base, sizeof(ub_base), uboAlignment_, buf);
	}
	uint32_t PushLightBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
		return dest->PushAligned(&ub_lights, sizeof(ub_lights), uboAlignment_, buf);
	}
	// TODO: Only push half the bone buffer if we only have four bones.
	uint32_t PushBoneBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
		return dest->PushAligned(&ub_bones, sizeof(ub_bones), uboAlignment_, buf);
	}

	bool LoadCache(FILE *f);
	void SaveCache(FILE *f);

private:
	void Clear();

	ShaderLanguageDesc compat_;

	typedef DenseHashMap<FShaderID, VulkanFragmentShader *, nullptr> FSCache;
	FSCache fsCache_;

	typedef DenseHashMap<VShaderID, VulkanVertexShader *, nullptr> VSCache;
	VSCache vsCache_;

	char *codeBuffer_;

	uint64_t uboAlignment_;
	// Uniform block scratchpad. These (the relevant ones) are copied to the current pushbuffer at draw time.
	UB_VS_FS_Base ub_base;
	UB_VS_Lights ub_lights;
	UB_VS_Bones ub_bones;

	VulkanFragmentShader *lastFShader_ = nullptr;
	VulkanVertexShader *lastVShader_ = nullptr;

	FShaderID lastFSID_;
	VShaderID lastVSID_;
};
