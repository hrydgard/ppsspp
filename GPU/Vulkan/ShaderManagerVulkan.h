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

#include <map>

#include "base/basictypes.h"
#include "Globals.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "math/lin/matrix4x4.h"

void ConvertProjMatrixToVulkan(Matrix4x4 & in);

// Pretty much full. Will need more bits for more fine grained dirty tracking for lights.
enum {
	DIRTY_PROJMATRIX = (1 << 0),
	DIRTY_PROJTHROUGHMATRIX = (1 << 1),
	DIRTY_FOGCOLOR = (1 << 2),
	DIRTY_FOGCOEF = (1 << 3),
	DIRTY_TEXENV = (1 << 4),
	DIRTY_ALPHACOLORREF = (1 << 5),
	DIRTY_STENCILREPLACEVALUE = (1 << 6),

	DIRTY_ALPHACOLORMASK = (1 << 7),
	DIRTY_LIGHT0 = (1 << 8),
	DIRTY_LIGHT1 = (1 << 9),
	DIRTY_LIGHT2 = (1 << 10),
	DIRTY_LIGHT3 = (1 << 11),

	DIRTY_MATDIFFUSE = (1 << 12),
	DIRTY_MATSPECULAR = (1 << 13),
	DIRTY_MATEMISSIVE = (1 << 14),
	DIRTY_AMBIENT = (1 << 15),
	DIRTY_MATAMBIENTALPHA = (1 << 16),
	DIRTY_SHADERBLEND = (1 << 17),  // Used only for in-shader blending.
	DIRTY_UVSCALEOFFSET = (1 << 18),  // this will be dirtied ALL THE TIME... maybe we'll need to do "last value with this shader compares"
	DIRTY_TEXCLAMP = (1 << 19),

	DIRTY_DEPTHRANGE = (1 << 20),

	DIRTY_WORLDMATRIX = (1 << 21),
	DIRTY_VIEWMATRIX = (1 << 22),
	DIRTY_TEXMATRIX = (1 << 23),
	DIRTY_BONEMATRIX0 = (1 << 24),
	DIRTY_BONEMATRIX1 = (1 << 25),
	DIRTY_BONEMATRIX2 = (1 << 26),
	DIRTY_BONEMATRIX3 = (1 << 27),
	DIRTY_BONEMATRIX4 = (1 << 28),
	DIRTY_BONEMATRIX5 = (1 << 29),
	DIRTY_BONEMATRIX6 = (1 << 30),
	DIRTY_BONEMATRIX7 = (1 << 31),

	DIRTY_BASE_UNIFORMS = 
		DIRTY_WORLDMATRIX | DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWMATRIX | DIRTY_TEXMATRIX | DIRTY_ALPHACOLORREF |
		DIRTY_PROJMATRIX | DIRTY_FOGCOLOR | DIRTY_FOGCOEF | DIRTY_TEXENV | DIRTY_STENCILREPLACEVALUE | 
		DIRTY_ALPHACOLORMASK | DIRTY_SHADERBLEND | DIRTY_UVSCALEOFFSET | DIRTY_TEXCLAMP | DIRTY_DEPTHRANGE | DIRTY_MATAMBIENTALPHA,
	DIRTY_LIGHT_UNIFORMS =
		DIRTY_LIGHT0 | DIRTY_LIGHT1 | DIRTY_LIGHT2 | DIRTY_LIGHT3 |
		DIRTY_MATDIFFUSE | DIRTY_MATSPECULAR | DIRTY_MATEMISSIVE | DIRTY_AMBIENT,
	DIRTY_BONE_UNIFORMS = 0xFF000000,

	DIRTY_ALL = 0xFFFFFFFF
};

// TODO: Split into two structs, one for software transform and one for hardware transform, to save space.
// 512 bytes. Probably can't get to 256 (nVidia's UBO alignment).
struct UB_VS_FS_Base {
	float proj[16];
	float proj_through[16];
	float view[16];
	float world[16];
	float tex[16];  // not that common, may want to break out
	float uvScaleOffset[4];
	float depthRange[4];
	float fogCoef_stencil[4];
	float matAmbient[4];
	// Fragment data
	float fogColor[4];
	float texEnvColor[4];
	int alphaColorRef[4];
	int colorTestMask[4];
	float blendFixA[4];
	float blendFixB[4];
	float texClamp[4];
	float texClampOffset[4];
};

static const char *ub_baseStr =
R"(  mat4 proj_mtx;
	mat4 proj_through_mtx;
  mat4 view_mtx;
  mat4 world_mtx;
  mat4 tex_mtx;
  vec4 uvscaleoffset;
  vec4 depthRange;
  vec3 fogcoef_stencilreplace;
  vec4 matambientalpha;
  vec3 fogcolor;
  vec3 texenv;
  ivec4 alphacolorref;
  ivec4 alphacolormask;
  vec3 blendFixA;
  vec3 blendFixB;
  vec4 texclamp;
  vec2 texclampoff;
)";

// 576 bytes. Can we get down to 512?
struct UB_VS_Lights {
	float ambientColor[4];
	float materialDiffuse[4];
	float materialSpecular[4];
	float materialEmissive[4];
	float lpos[4][4];
	float ldir[4][4];
	float latt[4][4];
	float lightAngle[4][4];   // TODO: Merge with lightSpotCoef, use .xy
	float lightSpotCoef[4][4];
	float lightAmbient[4][4];
	float lightDiffuse[4][4];
	float lightSpecular[4][4];
};

static const char *ub_vs_lightsStr =
R"(	vec4 globalAmbient;
	vec3 matdiffuse;
	vec4 matspecular;
	vec3 matemissive;
	vec3 pos[4];
	vec3 dir[4];
	vec3 att[4];
	float angle[4];
	float spotCoef[4];
	vec3 ambient[4];
	vec3 diffuse[4];
	vec3 specular[4];
)";

// With some cleverness, we could get away with uploading just half this when only the four first
// bones are being used. This is 512b, 256b would be great.
// Could also move to 4x3 matrices - would let us fit 5 bones into 256b.
struct UB_VS_Bones {
	float bones[8][16];
};

static const char *ub_vs_bonesStr =
R"(	mat4 m[8];
)";

class VulkanContext;
class VulkanPushBuffer;

class VulkanFragmentShader {
public:
	VulkanFragmentShader(VulkanContext *vulkan, ShaderID id, const char *code, bool useHWTransform);
	~VulkanFragmentShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	VkShaderModule GetModule() const { return module_; }

protected:	
	VkShaderModule module_;

	VulkanContext *vulkan_;
	std::string source_;
	bool failed_;
	bool useHWTransform_;
	ShaderID id_;
};

class VulkanVertexShader {
public:
	VulkanVertexShader(VulkanContext *vulkan, ShaderID id, const char *code, int vertType, bool useHWTransform, bool usesLighting);
	~VulkanVertexShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }
	bool HasBones() const {
		return id_.Bit(VS_BIT_ENABLE_BONES);
	}
	bool HasLights() const {
		return usesLighting_;
	}

	std::string GetShaderString(DebugShaderStringType type) const;
	VkShaderModule GetModule() const { return module_; }

protected:
	VkShaderModule module_;

	VulkanContext *vulkan_;
	std::string source_;
	bool failed_;
	bool useHWTransform_;
	bool usesLighting_;
	ShaderID id_;
};

class VulkanPushBuffer;

class ShaderManagerVulkan {
public:
	ShaderManagerVulkan(VulkanContext *vulkan);
	~ShaderManagerVulkan();

	void GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader, bool useHWTransform);
	void ClearShaders();
	void DirtyShader();
	void DirtyLastShader();

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	uint32_t UpdateUniforms();

	void DirtyUniform(u32 what) {
		globalDirty_ |= what;
	}

	// TODO: Avoid copying these buffers if same as last draw, can still point to it assuming we're still in the same pushbuffer.
	// Applies dirty changes and copies the buffer.
	bool IsBaseDirty() { return true; }
	bool IsLightDirty() { return true; }
	bool IsBoneDirty() { return true; }

	uint32_t PushBaseBuffer(VulkanPushBuffer *dest, VkBuffer *buf);
	uint32_t PushLightBuffer(VulkanPushBuffer *dest, VkBuffer *buf);
	uint32_t PushBoneBuffer(VulkanPushBuffer *dest, VkBuffer *buf);

private:
	void BaseUpdateUniforms(int dirtyUniforms);
	void LightUpdateUniforms(int dirtyUniforms);
	void BoneUpdateUniforms(int dirtyUniforms);

	void Clear();

	VulkanContext *vulkan_;

	typedef std::map<ShaderID, VulkanFragmentShader *> FSCache;
	FSCache fsCache_;

	typedef std::map<ShaderID, VulkanVertexShader *> VSCache;
	VSCache vsCache_;

	char *codeBuffer_;

	uint32_t globalDirty_;
	uint32_t uboAlignment_;
	// Uniform block scratchpad. These (the relevant ones) are copied to the current pushbuffer at draw time.
	UB_VS_FS_Base ub_base;
	UB_VS_Lights ub_lights;
	UB_VS_Bones ub_bones;

	VulkanFragmentShader *lastFShader_;
	VulkanVertexShader *lastVShader_;

	ShaderID lastFSID_;
	ShaderID lastVSID_;
};
