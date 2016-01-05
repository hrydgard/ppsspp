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
#include "thin3d/vulkan_utils.h"

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
#include "UI/OnScreenDisplay.h"

VulkanFragmentShader::VulkanFragmentShader(VulkanContext *vulkan, ShaderID id, const char *code, bool useHWTransform)
	: vulkan_(vulkan), id_(id), failed_(false), useHWTransform_(useHWTransform), module_(nullptr) {
	source_ = code;

	std::string errorMessage;
	std::vector<uint32_t> spirv;

	bool success = GLSLtoSPV(VK_SHADER_STAGE_FRAGMENT_BIT, code, spirv, &errorMessage);
	if (!errorMessage.empty()) {
		if (success) {
			ERROR_LOG(G3D, "Warnings in shader compilation!");
		} else {
			ERROR_LOG(G3D, "Error in shader compilation!");
		}
		ERROR_LOG(G3D, "Messages: %s", errorMessage.c_str());
		ERROR_LOG(G3D, "Shader source:\n%s", code);
		OutputDebugStringA("Messages:\n");
		OutputDebugStringA(errorMessage.c_str());
		OutputDebugStringA(code);
		Reporting::ReportMessage("Vulkan error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	} else {
		success = vulkan_->CreateShaderModule(spirv, &module_);
#ifdef SHADERLOG
		OutputDebugStringA(code);
		OutputDebugStringA("OK");
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
	if (module_) {
		vulkan_->QueueDelete(module_);
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

VulkanVertexShader::VulkanVertexShader(VulkanContext *vulkan, ShaderID id, const char *code, int vertType, bool useHWTransform) 
	: vulkan_(vulkan), id_(id), failed_(false), useHWTransform_(useHWTransform), module_(nullptr) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
	std::string errorMessage;
	std::vector<uint32_t> spirv;
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
	}

	if (!success) {
		failed_ = true;
		module_ = nullptr;
		return;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
}

VulkanVertexShader::~VulkanVertexShader() {
	if (module_) {
		vulkan_->QueueDelete(module_);
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

// Depth in ogl is between -1;1 we need between 0;1 and optionally reverse it
static void ConvertProjMatrixToVulkan(Matrix4x4 &in, bool invertedX, bool invertedY, bool invertedZ) {
	// Half pixel offset hack
	float xoff = 0.5f / gstate_c.curRTRenderWidth;
	xoff = gstate_c.vpXOffset + (invertedX ? xoff : -xoff);
	float yoff = 0.5f / gstate_c.curRTRenderHeight;
	yoff = gstate_c.vpYOffset + (invertedY ? yoff : -yoff);

	if (invertedX)
		xoff = -xoff;
	if (invertedY)
		yoff = -yoff;

	in.translateAndScale(Vec3(xoff, yoff, 0.5f), Vec3(gstate_c.vpWidthScale, gstate_c.vpHeightScale, invertedZ ? -0.5 : 0.5f));
}

static void ConvertProjMatrixToVulkanThrough(Matrix4x4 &in) {
	in.translateAndScale(Vec3(0.0f, 0.0f, 0.5f), Vec3(1.0f, 1.0f, 0.5f));
}

void ShaderManagerVulkan::PSUpdateUniforms(int dirtyUniforms) {
	if (dirtyUniforms & DIRTY_TEXENV) {
		Uint8x3ToFloat4(ub_base.texEnvColor, gstate.texenvcolor);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORREF) {
		Uint8x3ToFloat4_Alpha(ub_base.alphaColorRef, gstate.getColorTestRef(), (float)gstate.getAlphaTestRef());
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORMASK) {
		Uint8x3ToFloat4(ub_base.colorTestMask, gstate.colortestmask);
	}
	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		Uint8x3ToFloat4(ub_base.fogColor, gstate.fogcolor);
	}
	if (dirtyUniforms & DIRTY_STENCILREPLACEVALUE) {
		Uint8x1ToFloat4(ub_base.stencilReplace, gstate.getStencilTestRef());
	}
	if (dirtyUniforms & DIRTY_SHADERBLEND) {
		Uint8x3ToFloat4(ub_base.blendFixA, gstate.getFixA());
		Uint8x3ToFloat4(ub_base.blendFixB, gstate.getFixB());
	}
	if (dirtyUniforms & DIRTY_TEXCLAMP) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		// First wrap xy, then half texel xy (for clamp.)
		const float texclamp[4] = {
			widthFactor,
			heightFactor,
			invW * 0.5f,
			invH * 0.5f,
		};
		const float texclampoff[2] = {
			gstate_c.curTextureXOffset * invW,
			gstate_c.curTextureYOffset * invH,
		};
		CopyFloat4(ub_base.texClamp, texclamp);
		CopyFloat2(ub_base.texClampOffset, texclampoff);
	}
}

void ShaderManagerVulkan::VSUpdateUniforms(int dirtyUniforms) {
	// Update any dirty uniforms before we draw
	if (dirtyUniforms & DIRTY_PROJMATRIX) {
		Matrix4x4 flippedMatrix;
		memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

		const bool invertedY = gstate_c.vpHeight < 0;
		if (!invertedY) {
			flippedMatrix[1] = -flippedMatrix[1];
			flippedMatrix[5] = -flippedMatrix[5];
			flippedMatrix[9] = -flippedMatrix[9];
			flippedMatrix[13] = -flippedMatrix[13];
		}
		const bool invertedX = gstate_c.vpWidth < 0;
		if (invertedX) {
			flippedMatrix[0] = -flippedMatrix[0];
			flippedMatrix[4] = -flippedMatrix[4];
			flippedMatrix[8] = -flippedMatrix[8];
			flippedMatrix[12] = -flippedMatrix[12];
		}
		
		const bool invertedZ = gstate_c.vpDepthScale < 0;
		ConvertProjMatrixToVulkan(flippedMatrix, invertedX, invertedY, invertedZ);

		CopyMatrix4x4(ub_base.proj, flippedMatrix.getReadPtr());
	}
	if (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX) {
		Matrix4x4 proj_through;
		proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);
		ConvertProjMatrixToVulkanThrough(proj_through);
		CopyMatrix4x4(ub_base.proj, proj_through.getReadPtr());
	}

	// Transform
	if (dirtyUniforms & DIRTY_WORLDMATRIX) {
		ConvertMatrix4x3To4x4(ub_base.world, gstate.worldMatrix);
	}
	if (dirtyUniforms & DIRTY_VIEWMATRIX) {
		ConvertMatrix4x3To4x4(ub_base.view, gstate.viewMatrix);
	}
	if (dirtyUniforms & DIRTY_TEXMATRIX) {
		ConvertMatrix4x3To4x4(ub_base.tex, gstate.tgenMatrix);
	}
	if (dirtyUniforms & DIRTY_FOGCOEF) {
		float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		if (my_isinf(fogcoef[1])) {
			// not really sure what a sensible value might be.
			fogcoef[1] = fogcoef[1] < 0.0f ? -10000.0f : 10000.0f;
		} else if (my_isnan(fogcoef[1])) {
			// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
			// Just put the fog far away at a large finite distance.
			// Infinities and NaNs are rather unpredictable in shaders on many GPUs
			// so it's best to just make it a sane calculation.
			fogcoef[0] = 100000.0f;
			fogcoef[1] = 1.0f;
		}
#ifndef MOBILE_DEVICE
		else if (my_isnanorinf(fogcoef[1]) || my_isnanorinf(fogcoef[0])) {
			ERROR_LOG_REPORT_ONCE(fognan, G3D, "Unhandled fog NaN/INF combo: %f %f", fogcoef[0], fogcoef[1]);
		}
#endif
		CopyFloat2(ub_base.fogCoef, fogcoef);
	}
	// TODO: Could even set all bones in one go if they're all dirty.
#ifdef USE_BONE_ARRAY
	if (u_bone != 0) {
		float allBones[8 * 16];

		bool allDirty = true;
		for (int i = 0; i < numBones; i++) {
			if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
				ConvertMatrix4x3To4x4(allBones + 16 * i, gstate.boneMatrix + 12 * i);
			} else {
				allDirty = false;
			}
		}
		if (allDirty) {
			// Set them all with one call
			//glUniformMatrix4fv(u_bone, numBones, GL_FALSE, allBones);
		} else {
			// Set them one by one. Could try to coalesce two in a row etc but too lazy.
			for (int i = 0; i < numBones; i++) {
				if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
					//glUniformMatrix4fv(u_bone + i, 1, GL_FALSE, allBones + 16 * i);
				}
			}
		}
	}
#else
	for (int i = 0; i < 8; i++) {
		if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4(ub_bones.bones[i], gstate.boneMatrix + 12 * i);
		}
	}
#endif

	// Texturing
	if (dirtyUniforms & DIRTY_UVSCALEOFFSET) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		float uvscaleoff[4];
		switch (gstate.getUVGenMode()) {
		case GE_TEXMAP_TEXTURE_COORDS:
			// Not sure what GE_TEXMAP_UNKNOWN is, but seen in Riviera.  Treating the same as GE_TEXMAP_TEXTURE_COORDS works.
		case GE_TEXMAP_UNKNOWN:
			uvscaleoff[0] = gstate_c.uv.uScale * widthFactor;
			uvscaleoff[1] = gstate_c.uv.vScale * heightFactor;
			uvscaleoff[2] = gstate_c.uv.uOff * widthFactor;
			uvscaleoff[3] = gstate_c.uv.vOff * heightFactor;
			break;

		// These two work the same whether or not we prescale UV.

		case GE_TEXMAP_TEXTURE_MATRIX:
			// We cannot bake the UV coord scale factor in here, as we apply a matrix multiplication
			// before this is applied, and the matrix multiplication may contain translation. In this case
			// the translation will be scaled which breaks faces in Hexyz Force for example.
			// So I've gone back to applying the scale factor in the shader.
			uvscaleoff[0] = widthFactor;
			uvscaleoff[1] = heightFactor;
			uvscaleoff[2] = 0.0f;
			uvscaleoff[3] = 0.0f;
			break;

		case GE_TEXMAP_ENVIRONMENT_MAP:
			// In this mode we only use uvscaleoff to scale to the texture size.
			uvscaleoff[0] = widthFactor;
			uvscaleoff[1] = heightFactor;
			uvscaleoff[2] = 0.0f;
			uvscaleoff[3] = 0.0f;
			break;

		default:
			ERROR_LOG_REPORT(G3D, "Unexpected UV gen mode: %d", gstate.getUVGenMode());
		}
		CopyFloat4(ub_base.uvScaleOffset, uvscaleoff);
	}

	if (dirtyUniforms & DIRTY_DEPTHRANGE)	{
		float viewZScale = gstate.getViewportZScale();
		float viewZCenter = gstate.getViewportZCenter();

		// Given the way we do the rounding, the integer part of the offset is probably mostly irrelevant as we cancel
		// it afterwards anyway.
		// It seems that we should adjust for D3D projection matrix. We got squashed up to only 0-1, so we divide
		// the scale factor by 2, and add an offset. But, this doesn't work! I get near-perfect results not doing it.
		// viewZScale *= 2.0f;

		// Need to take the possibly inverted proj matrix into account.
		if (gstate_c.vpDepthScale < 0.0)
			viewZScale *= -1.0f;
		viewZCenter -= 32767.5f;
		float viewZInvScale;
		if (viewZScale != 0.0) {
			viewZInvScale = 1.0f / viewZScale;
		} else {
			viewZInvScale = 0.0;
		}

		float data[4] = { viewZScale, viewZCenter, viewZCenter, viewZInvScale };
		CopyFloat4(ub_base.depthRange, data);
	}

	// Lighting
	if (dirtyUniforms & DIRTY_AMBIENT) {
		Uint8x3ToFloat4_AlphaUint8(ub_lights.ambientColor, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (dirtyUniforms & DIRTY_MATAMBIENTALPHA) {
		// Note - this one is not in lighting but in transformCommon as it has uses beyond lighting
		Uint8x3ToFloat4_AlphaUint8(ub_base.matAmbient, gstate.materialambient, gstate.getMaterialAmbientA());
	}
	if (dirtyUniforms & DIRTY_MATDIFFUSE) {
		Uint8x3ToFloat4(ub_lights.materialDiffuse, gstate.materialdiffuse);
	}
	if (dirtyUniforms & DIRTY_MATEMISSIVE) {
		Uint8x3ToFloat4(ub_lights.materialEmissive, gstate.materialemissive);
	}
	if (dirtyUniforms & DIRTY_MATSPECULAR) {
		Uint8x3ToFloat4_Alpha(ub_lights.materialEmissive, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
	}

	for (int i = 0; i < 4; i++) {
		if (dirtyUniforms & (DIRTY_LIGHT0 << i)) {
			if (gstate.isDirectionalLight(i)) {
				// Prenormalize
				float x = getFloat24(gstate.lpos[i * 3 + 0]);
				float y = getFloat24(gstate.lpos[i * 3 + 1]);
				float z = getFloat24(gstate.lpos[i * 3 + 2]);
				float len = sqrtf(x*x + y*y + z*z);
				if (len == 0.0f)
					len = 1.0f;
				else
					len = 1.0f / len;
				float vec[3] = { x * len, y * len, z * len };
				CopyFloat3To4(ub_lights.lpos[i], vec);
			} else {
				ExpandFloat24x3ToFloat4(ub_lights.lpos[i], &gstate.lpos[i * 3]);
			}
			ExpandFloat24x3ToFloat4(ub_lights.ldir[i], &gstate.ldir[i * 3]);
			ExpandFloat24x3ToFloat4(ub_lights.latt[i], &gstate.latt[i * 3]);
			CopyFloat1To4(ub_lights.lightAngle[i], getFloat24(gstate.lcutoff[i]));
			CopyFloat1To4(ub_lights.lightSpotCoef[i], getFloat24(gstate.lconv[i]));
			Uint8x3ToFloat4(ub_lights.lightAmbient[i], gstate.lcolor[i * 3]);
			Uint8x3ToFloat4(ub_lights.lightDiffuse[i], gstate.lcolor[i * 3 + 1]);
			Uint8x3ToFloat4(ub_lights.lightSpecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}
}

ShaderManagerVulkan::ShaderManagerVulkan(VulkanContext *vulkan) 
	: vulkan_(vulkan), lastVShader_(nullptr), lastFShader_(nullptr), globalDirty_(0xFFFFFFFF) {
	codeBuffer_ = new char[16384];
}

ShaderManagerVulkan::~ShaderManagerVulkan() {
	delete [] codeBuffer_;
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
	globalDirty_ = 0xFFFFFFFF;
	lastFSID_.clear();
	lastVSID_.clear();
	DirtyShader();
}

void ShaderManagerVulkan::ClearCache(bool deleteThem) {
	Clear();
}


void ShaderManagerVulkan::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.clear();
	lastVSID_.clear();
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
	globalDirty_ = 0xFFFFFFFF;
}

void ShaderManagerVulkan::DirtyLastShader() { // disables vertex arrays
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
}


void ShaderManagerVulkan::GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader) {
	bool useHWTransform = CanUseHardwareTransform(prim);

	ShaderID VSID;
	ComputeVertexShaderID(&VSID, vertType, useHWTransform);
	ShaderID FSID;
	ComputeFragmentShaderID(&FSID);

	// Just update uniforms if this is the same shader as last time.
	if (lastVShader_ != nullptr && lastFShader_ != nullptr && VSID == lastVSID_ && FSID == lastFSID_) {
		if (globalDirty_) {
			PSUpdateUniforms(globalDirty_);
			VSUpdateUniforms(globalDirty_);
			globalDirty_ = 0;
		}
		*vshader = lastVShader_;
		*fshader = lastFShader_;
		// Already all set, no need to look up in shader maps.
		return;
	}

	VSCache::iterator vsIter = vsCache_.find(VSID);
	VulkanVertexShader *vs;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		GenerateVulkanGLSLVertexShader(VSID, codeBuffer_);
		vs = new VulkanVertexShader(vulkan_, VSID, codeBuffer_, vertType, useHWTransform);
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

	if (globalDirty_) {
		PSUpdateUniforms(globalDirty_);
		VSUpdateUniforms(globalDirty_);
		globalDirty_ = 0;
	}
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
	}
	break;
	case SHADER_TYPE_FRAGMENT:
	{
		for (auto iter : fsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
	}
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
