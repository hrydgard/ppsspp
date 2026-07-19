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
#include <mutex>

#include "Common/Thread/Promise.h"
#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "Common/Math/lin/matrix4x4.h"
#include "GPU/Common/ShaderUniforms.h"

#include "Common/File/FileUtil.h"

class VulkanContext;
class DrawEngineVulkan;
class VulkanPushPool;

class VulkanFragmentShader {
public:
	VulkanFragmentShader(VulkanContext *vulkan, FShaderID id, FragmentShaderFlags flags, const char *code);
	~VulkanFragmentShader();

	const std::string &source() const { return source_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	Promise<VkShaderModule> *GetModule() const { return module_; }
	const FShaderID &GetID() const { return id_; }

	FragmentShaderFlags Flags() const { return flags_;  }

protected:	
	Promise<VkShaderModule> *module_ = nullptr;

	VulkanContext *vulkan_;
	std::string source_;
	bool failed_ = false;
	FShaderID id_;
	FragmentShaderFlags flags_;
};

class VulkanVertexShader {
public:
	VulkanVertexShader(VulkanContext *vulkan, VShaderID id, VertexShaderFlags flags, const char *code, bool useHWTransform);
	~VulkanVertexShader();

	const std::string &source() const { return source_; }

	bool UseHWTransform() const { return useHWTransform_; }  // TODO: Roll into flags
	VertexShaderFlags Flags() const { return flags_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	Promise<VkShaderModule> *GetModule() const { return module_; }
	const VShaderID &GetID() const { return id_; }

protected:
	Promise<VkShaderModule> *module_ = nullptr;

	VulkanContext *vulkan_;
	std::string source_;
	bool useHWTransform_;
	VShaderID id_;
	VertexShaderFlags flags_;
};

struct Uniforms {
	// Uniform block scratchpad. These (the relevant ones) are copied to the current pushbuffer at draw time.
	UB_VS_FS_Base ub_base{};
	UB_VS_Lights ub_lights{};
};

enum class ClipInfoFlags;

class ShaderManagerVulkan : public ShaderManagerCommon {
public:
	ShaderManagerVulkan(Draw::DrawContext *draw);
	~ShaderManagerVulkan();

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	void GetShaderIDs(int prim, u32 vertexType, VShaderID *vshader, FShaderID *fshader, const ComputedPipelineState &pipelineState, bool useHWTransform, ClipInfoFlags clipInfoFlags);
	void ClearShaders() override;

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	const VulkanVertexShader *GetVertexShaderFromID(VShaderID VSID);
	const VulkanFragmentShader *GetFragmentShaderFromID(FShaderID FSID);

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) override;

	uint64_t UpdateUniforms(bool useBufferedRendering, bool pixelMapped);

	// TODO: Avoid copying these buffers if same as last draw, can still point to it assuming we're still in the same pushbuffer.
	// Applies dirty changes and copies the buffer.
	bool IsBaseDirty() { return true; }
	bool IsLightDirty() { return true; }
	bool IsBoneDirty() { return true; }

	uint32_t PushBaseBuffer(VulkanPushPool *dest, VkBuffer *buf) const {
		return dest->Push(&uniforms_->ub_base, sizeof(uniforms_->ub_base), uboAlignment_, buf);
	}
	uint32_t PushLightBuffer(VulkanPushPool *dest, VkBuffer *buf) const {
		return dest->Push(&uniforms_->ub_lights, sizeof(uniforms_->ub_lights), uboAlignment_, buf);
	}

	static bool LoadCacheFlags(FILE *f, DrawEngineVulkan *drawEngine);
	bool LoadCache(FILE *f);
	void SaveCache(FILE *f, DrawEngineVulkan *drawEngine);

private:
	void Clear();

	ShaderLanguageDesc compat_;

	typedef DenseHashMap<FShaderID, VulkanFragmentShader *> FSCache;
	FSCache fsCache_;

	typedef DenseHashMap<VShaderID, VulkanVertexShader *> VSCache;
	VSCache vsCache_;

	char *codeBuffer_;

	uint64_t uboAlignment_;

	Uniforms *uniforms_;
	FShaderID lastFSID_;
	VShaderID lastVSID_;
};
