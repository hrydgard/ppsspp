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

//#define USE_UBERSHADER

#include <cstdio>
#include <cstdint>

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "Common/Math/lin/matrix4x4.h"
#include "GPU/Common/ShaderUniforms.h"

#ifdef USE_UBERSHADER
struct light_t {
	u32 COMP;
	u32 TYPE;
	u32 ENABLE;
	u32 pad;
};

struct UB_VSID {
	light_t VS_BIT_LIGHT[4];
	u32 VS_BIT_LIGHTING_ENABLE;
	u32 VS_BIT_MATERIAL_UPDATE;
	u32 VS_BIT_LMODE;
	u32 VS_BIT_LS0;
	u32 VS_BIT_LS1;
	u32 VS_BIT_IS_THROUGH;
	u32 VS_BIT_USE_HW_TRANSFORM;
	u32 VS_BIT_DO_TEXTURE;
	u32 VS_BIT_DO_TEXTURE_TRANSFORM;
	u32 VS_BIT_UVGEN_MODE;
	u32 VS_BIT_UVPROJ_MODE;
	u32 VS_BIT_HAS_TEXCOORD;
	u32 VS_BIT_HAS_TEXCOORD_TESS;
	u32 VS_BIT_HAS_COLOR;
	u32 VS_BIT_HAS_COLOR_TESS;
	u32 VS_BIT_HAS_NORMAL;
	u32 VS_BIT_HAS_NORMAL_TESS;
	u32 VS_BIT_NORM_REVERSE;
	u32 VS_BIT_NORM_REVERSE_TESS;
	u32 VS_BIT_ENABLE_BONES;
	u32 VS_BIT_BONES;
	u32 VS_BIT_WEIGHT_FMTSCALE;
	u32 VS_BIT_SPLINE;
	u32 VS_BIT_BEZIER;
	u32 GPU_ROUND_DEPTH_TO_16BIT;
	u32 GPU_SUPPORTS_VS_RANGE_CULLING;
};


struct UB_FSID {
	u32 FS_BIT_CLEARMODE;
	u32 FS_BIT_DO_TEXTURE;
	u32 FS_BIT_TEXFUNC;
	u32 FS_BIT_TEXALPHA;
	u32 FS_BIT_SHADER_DEPAL;
	u32 FS_BIT_SHADER_TEX_CLAMP;
	u32 FS_BIT_CLAMP_S;
	u32 FS_BIT_CLAMP_T;
	u32 FS_BIT_TEXTURE_AT_OFFSET;
	u32 FS_BIT_LMODE;
	u32 FS_BIT_ALPHA_TEST;
	u32 FS_BIT_ALPHA_TEST_FUNC;
	u32 FS_BIT_ALPHA_AGAINST_ZERO;
	u32 FS_BIT_COLOR_TEST;
	u32 FS_BIT_COLOR_TEST_FUNC;
	u32 FS_BIT_COLOR_AGAINST_ZERO;
	u32 FS_BIT_ENABLE_FOG;
	u32 FS_BIT_DO_TEXTURE_PROJ;
	u32 FS_BIT_COLOR_DOUBLE;
	u32 FS_BIT_STENCIL_TO_ALPHA;
	u32 FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE;
	u32 FS_BIT_REPLACE_LOGIC_OP_TYPE;
	u32 FS_BIT_REPLACE_BLEND;
	u32 FS_BIT_BLENDEQ;
	u32 FS_BIT_BLENDFUNC_A;
	u32 FS_BIT_BLENDFUNC_B;
	u32 FS_BIT_FLATSHADE;
	u32 FS_BIT_BGRA_TEXTURE;
	u32 FS_BIT_TEST_DISCARD_TO_ZERO;
	u32 GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
	u32 GPU_SUPPORTS_DEPTH_CLAMP;
	u32 GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
	u32 GPU_SUPPORTS_ACCURATE_DEPTH;
};
#endif

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
#ifdef USE_UBERSHADER
	UB_FSID ub_id = {};
#endif
protected:	
	VkShaderModule module_;

	VulkanContext *vulkan_;
	std::string source_;
	bool failed_;
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
#ifdef USE_UBERSHADER
	UB_VSID ub_id = {};
#endif
protected:
	VkShaderModule module_;

	VulkanContext *vulkan_;
	std::string source_;
	bool failed_;
	bool useHWTransform_;
	VShaderID id_;
};

class VulkanPushBuffer;

class ShaderManagerVulkan : public ShaderManagerCommon {
public:
	ShaderManagerVulkan(Draw::DrawContext *draw, VulkanContext *vulkan);
	~ShaderManagerVulkan();

	void DeviceRestore(VulkanContext *vulkan, Draw::DrawContext *draw);

	void GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader, bool useHWTransform, bool useHWTessellation);
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
#ifdef USE_UBERSHADER
	uint32_t PushVSIDBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
		return dest->PushAligned(&lastVShader_->ub_id, sizeof(lastVShader_->ub_id), uboAlignment_, buf);
	}
	uint32_t PushFSIDBuffer(VulkanPushBuffer *dest, VkBuffer *buf) {
		return dest->PushAligned(&lastFShader_->ub_id, sizeof(lastFShader_->ub_id), uboAlignment_, buf);
	}
#endif

	bool LoadCache(FILE *f);
	void SaveCache(FILE *f);

private:
	void Clear();

	VulkanContext *vulkan_;

	typedef DenseHashMap<FShaderID, VulkanFragmentShader *, nullptr> FSCache;
	FSCache fsCache_;

	typedef DenseHashMap<VShaderID, VulkanVertexShader *, nullptr> VSCache;
	VSCache vsCache_;

	char *codeBuffer_;
#ifdef USE_UBERSHADER
	char *ubershader_vs_;
	char *ubershader_fs_;
#endif
	uint64_t uboAlignment_;
	// Uniform block scratchpad. These (the relevant ones) are copied to the current pushbuffer at draw time.
	UB_VS_FS_Base ub_base;
	UB_VS_Lights ub_lights;
	UB_VS_Bones ub_bones;

	VulkanFragmentShader *lastFShader_;
	VulkanVertexShader *lastVShader_;

	FShaderID lastFSID_;
	VShaderID lastVSID_;
};
