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
	if (module_) {
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
	if (module_) {
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

static void ConvertProjMatrixToVulkan(Matrix4x4 &in, bool invertedX, bool invertedY) {
	const Vec3 trans(0, 0, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

ShaderManagerVulkan::ShaderManagerVulkan(VulkanContext *vulkan)
	: vulkan_(vulkan), lastVShader_(nullptr), lastFShader_(nullptr), globalDirty_(0xFFFFFFFF) {
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

void ShaderManagerVulkan::BaseUpdateUniforms(int dirtyUniforms) {
	if (dirtyUniforms & DIRTY_TEXENV) {
		Uint8x3ToFloat4(ub_base.texEnvColor, gstate.texenvcolor);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORREF) {
		Uint8x3ToInt4_Alpha(ub_base.alphaColorRef, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORMASK) {
		Uint8x3ToInt4_Alpha(ub_base.colorTestMask, gstate.getColorTestMask(), gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		Uint8x3ToFloat4(ub_base.fogColor, gstate.fogcolor);
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

	if (dirtyUniforms & DIRTY_PROJMATRIX) {
		Matrix4x4 flippedMatrix;
		memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

		const bool invertedY = gstate_c.vpHeight < 0;
		if (invertedY) {
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
		ConvertProjMatrixToVulkan(flippedMatrix, invertedX, invertedY);
		CopyMatrix4x4(ub_base.proj, flippedMatrix.getReadPtr());
	}

	if (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX) {
		Matrix4x4 proj_through;
		proj_through.setOrthoVulkan(0.0f, gstate_c.curRTWidth, 0, gstate_c.curRTHeight, 0, 1);
		CopyMatrix4x4(ub_base.proj_through, proj_through.getReadPtr());
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

	// Combined two small uniforms
	if (dirtyUniforms & (DIRTY_FOGCOEF | DIRTY_STENCILREPLACEVALUE)) {
		float fogcoef_stencil[3] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
			(float)gstate.getStencilTestRef()
		};
		if (my_isinf(fogcoef_stencil[1])) {
			// not really sure what a sensible value might be.
			fogcoef_stencil[1] = fogcoef_stencil[1] < 0.0f ? -10000.0f : 10000.0f;
		} else if (my_isnan(fogcoef_stencil[1])) {
			// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
			// Just put the fog far away at a large finite distance.
			// Infinities and NaNs are rather unpredictable in shaders on many GPUs
			// so it's best to just make it a sane calculation.
			fogcoef_stencil[0] = 100000.0f;
			fogcoef_stencil[1] = 1.0f;
		}
#ifndef MOBILE_DEVICE
		else if (my_isnanorinf(fogcoef_stencil[1]) || my_isnanorinf(fogcoef_stencil[0])) {
			ERROR_LOG_REPORT_ONCE(fognan, G3D, "Unhandled fog NaN/INF combo: %f %f", fogcoef_stencil[0], fogcoef_stencil[1]);
		}
#endif
		CopyFloat3(ub_base.fogCoef_stencil, fogcoef_stencil);
	}

	// Texturing
	if (dirtyUniforms & DIRTY_UVSCALEOFFSET) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		static const float rescale[4] = { 1.0f, 2 * 127.5f / 128.f, 2 * 32767.5f / 32768.f, 1.0f };
		const float factor = rescale[(gstate.vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT];

		float uvscaleoff[4];

		switch (gstate.getUVGenMode()) {
		case GE_TEXMAP_TEXTURE_COORDS:
			// Not sure what GE_TEXMAP_UNKNOWN is, but seen in Riviera.  Treating the same as GE_TEXMAP_TEXTURE_COORDS works.
		case GE_TEXMAP_UNKNOWN:
			if (g_Config.bPrescaleUV) {
				// We are here but are prescaling UV in the decoder? Let's do the same as in the other case
				// except consider *Scale and *Off to be 1 and 0.
				uvscaleoff[0] = widthFactor;
				uvscaleoff[1] = heightFactor;
				uvscaleoff[2] = 0.0f;
				uvscaleoff[3] = 0.0f;
			} else {
				uvscaleoff[0] = gstate_c.uv.uScale * factor * widthFactor;
				uvscaleoff[1] = gstate_c.uv.vScale * factor * heightFactor;
				uvscaleoff[2] = gstate_c.uv.uOff * widthFactor;
				uvscaleoff[3] = gstate_c.uv.vOff * heightFactor;
			}
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

	if (dirtyUniforms & DIRTY_DEPTHRANGE) {
		float viewZScale = gstate.getViewportZScale();
		float viewZCenter = gstate.getViewportZCenter();
		float viewZInvScale;

		// We had to scale and translate Z to account for our clamped Z range.
		// Therefore, we also need to reverse this to round properly.
		//
		// Example: scale = 65535.0, center = 0.0
		// Resulting range = -65535 to 65535, clamped to [0, 65535]
		// gstate_c.vpDepthScale = 2.0f
		// gstate_c.vpZOffset = -1.0f
		//
		// The projection already accounts for those, so we need to reverse them.
		//
		// Additionally, D3D9 uses a range from [0, 1].  We double and move the center.
		viewZScale *= (1.0f / gstate_c.vpDepthScale) * 2.0f;
		viewZCenter -= 65535.0f * gstate_c.vpZOffset + 32768.5f;

		if (viewZScale != 0.0) {
			viewZInvScale = 1.0f / viewZScale;
		} else {
			viewZInvScale = 0.0;
		}

		float data[4] = { viewZScale, viewZCenter, viewZCenter, viewZInvScale };
		CopyFloat4(ub_base.depthRange, data);
	}
}

void ShaderManagerVulkan::LightUpdateUniforms(int dirtyUniforms) {
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
		Uint8x3ToFloat4_Alpha(ub_lights.materialSpecular, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
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

void ShaderManagerVulkan::BoneUpdateUniforms(int dirtyUniforms) {
	for (int i = 0; i < 8; i++) {
		if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4(ub_bones.bones[i], gstate.boneMatrix + 12 * i);
		}
	}
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
	DirtyUniform(0xFFFFFFFF);
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

uint32_t ShaderManagerVulkan::UpdateUniforms() {
	uint32_t dirty = globalDirty_;
	if (globalDirty_) {
		BaseUpdateUniforms(dirty);
		LightUpdateUniforms(dirty);
		BoneUpdateUniforms(dirty);
	}
	globalDirty_ = 0;
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
