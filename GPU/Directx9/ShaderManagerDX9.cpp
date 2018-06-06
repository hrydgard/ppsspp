// Copyright (c) 2012- PPSSPP Project.

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
#include "gfx/d3d9_shader.h"
#include "base/logging.h"
#include "i18n/i18n.h"
#include "math/lin/matrix4x4.h"
#include "math/math_util.h"
#include "math/dataconv.h"
#include "util/text/utf8.h"

#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/FramebufferDX9.h"

namespace DX9 {

PSShader::PSShader(LPDIRECT3DDEVICE9 device, FShaderID id, const char *code) : id_(id), shader(nullptr), failed_(false) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
	bool success;
	std::string errorMessage;

	success = CompilePixelShader(device, code, &shader, NULL, errorMessage);

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
		Reporting::ReportMessage("D3D error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	}

	if (!success) {
		failed_ = true;
		if (shader)
			shader->Release();
		shader = NULL;
		return;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
}

PSShader::~PSShader() {
	if (shader)
		shader->Release();
}

std::string PSShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return FragmentShaderDesc(id_);
	default:
		return "N/A";
	}
}

VSShader::VSShader(LPDIRECT3DDEVICE9 device, VShaderID id, const char *code, bool useHWTransform) : id_(id), shader(nullptr), failed_(false), useHWTransform_(useHWTransform) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
	bool success;
	std::string errorMessage;

	success = CompileVertexShader(device, code, &shader, NULL, errorMessage);
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
		Reporting::ReportMessage("D3D error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	}

	if (!success) {
		failed_ = true;
		if (shader)
			shader->Release();
		shader = NULL;
		return;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
}

VSShader::~VSShader() {
	if (shader)
		shader->Release();
}

std::string VSShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return VertexShaderDesc(id_);
	default:
		return "N/A";
	}
}

void ShaderManagerDX9::PSSetColorUniform3(int creg, u32 color) {
	float f[4];
	Uint8x3ToFloat4(f, color);
	device_->SetPixelShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::PSSetColorUniform3Alpha255(int creg, u32 color, u8 alpha) {
	const float col[4] = {
		(float)((color & 0xFF)),
		(float)((color & 0xFF00) >> 8),
		(float)((color & 0xFF0000) >> 16),
		(float)alpha,
	};
	device_->SetPixelShaderConstantF(creg, col, 1);
}

void ShaderManagerDX9::PSSetFloat(int creg, float value) {
	const float f[4] = { value, 0.0f, 0.0f, 0.0f };
	device_->SetPixelShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::PSSetFloatArray(int creg, const float *value, int count) {
	float f[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < count; i++) {
		f[i] = value[i];
	}
	device_->SetPixelShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::VSSetFloat(int creg, float value) {
	const float f[4] = { value, 0.0f, 0.0f, 0.0f };
	device_->SetVertexShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::VSSetFloatArray(int creg, const float *value, int count) {
	float f[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < count; i++) {
		f[i] = value[i];
	}
	device_->SetVertexShaderConstantF(creg, f, 1);
}

// Utility
void ShaderManagerDX9::VSSetColorUniform3(int creg, u32 color) {
	float f[4];
	Uint8x3ToFloat4(f, color);
	device_->SetVertexShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::VSSetFloatUniform4(int creg, float data[4]) {
	device_->SetVertexShaderConstantF(creg, data, 1);
}

void ShaderManagerDX9::VSSetFloat24Uniform3(int creg, const u32 data[3]) {
	float f[4];
	ExpandFloat24x3ToFloat4(f, data);
	device_->SetVertexShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::VSSetColorUniform3Alpha(int creg, u32 color, u8 alpha) {
	float f[4];
	Uint8x3ToFloat4_AlphaUint8(f, color, alpha);
	device_->SetVertexShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::VSSetColorUniform3ExtraFloat(int creg, u32 color, float extra) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		extra
	};
	device_->SetVertexShaderConstantF(creg, col, 1);
}

// Utility
void ShaderManagerDX9::VSSetMatrix4x3(int creg, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4Transposed(m4x4, m4x3);
	device_->SetVertexShaderConstantF(creg, m4x4, 4);
}

void ShaderManagerDX9::VSSetMatrix4x3_3(int creg, const float *m4x3) {
	float m3x4[12];
	ConvertMatrix4x3To3x4Transposed(m3x4, m4x3);
	device_->SetVertexShaderConstantF(creg, m3x4, 3);
}

void ShaderManagerDX9::VSSetMatrix(int creg, const float* pMatrix) {
	float transp[16];
	Transpose4x4(transp, pMatrix);
	device_->SetVertexShaderConstantF(creg, transp, 4);
}

// Depth in ogl is between -1;1 we need between 0;1 and optionally reverse it
static void ConvertProjMatrixToD3D(Matrix4x4 &in, bool invertedX, bool invertedY) {
	// Half pixel offset hack
	float xoff = 1.0f / gstate_c.curRTRenderWidth;
	xoff = gstate_c.vpXOffset + (invertedX ? xoff : -xoff);
	float yoff = -1.0f / gstate_c.curRTRenderHeight;
	yoff = gstate_c.vpYOffset + (invertedY ? yoff : -yoff);

	if (invertedX)
		xoff = -xoff;
	if (invertedY)
		yoff = -yoff;

	const Vec3 trans(xoff, yoff, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

static void ConvertProjMatrixToD3DThrough(Matrix4x4 &in) {
	float xoff = -1.0f / gstate_c.curRTRenderWidth;
	float yoff = 1.0f / gstate_c.curRTRenderHeight;
	in.translateAndScale(Vec3(xoff, yoff, 0.5f), Vec3(1.0f, 1.0f, 0.5f));
}

const uint64_t psUniforms = DIRTY_TEXENV | DIRTY_ALPHACOLORREF | DIRTY_ALPHACOLORMASK | DIRTY_FOGCOLOR | DIRTY_STENCILREPLACEVALUE | DIRTY_SHADERBLEND | DIRTY_TEXCLAMP;

void ShaderManagerDX9::PSUpdateUniforms(u64 dirtyUniforms) {
	if (dirtyUniforms & DIRTY_TEXENV) {
		PSSetColorUniform3(CONST_PS_TEXENV, gstate.texenvcolor);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORREF) {
		PSSetColorUniform3Alpha255(CONST_PS_ALPHACOLORREF, gstate.getColorTestRef(), gstate.getAlphaTestRef());
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORMASK) {
		PSSetColorUniform3(CONST_PS_ALPHACOLORMASK, gstate.colortestmask);
	}
	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		PSSetColorUniform3(CONST_PS_FOGCOLOR, gstate.fogcolor);
	}
	if (dirtyUniforms & DIRTY_STENCILREPLACEVALUE) {
		PSSetFloat(CONST_PS_STENCILREPLACE, (float)gstate.getStencilTestRef() * (1.0f / 255.0f));
	}

	if (dirtyUniforms & DIRTY_SHADERBLEND) {
		PSSetColorUniform3(CONST_PS_BLENDFIXA, gstate.getFixA());
		PSSetColorUniform3(CONST_PS_BLENDFIXB, gstate.getFixB());

		const float fbotexSize[2] = {
			1.0f / (float)gstate_c.curRTRenderWidth,
			1.0f / (float)gstate_c.curRTRenderHeight,
		};
		PSSetFloatArray(CONST_PS_FBOTEXSIZE, fbotexSize, 2);
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
		PSSetFloatArray(CONST_PS_TEXCLAMP, texclamp, 4);
		PSSetFloatArray(CONST_PS_TEXCLAMPOFF, texclampoff, 2);
	}
}

const uint64_t vsUniforms = DIRTY_PROJMATRIX | DIRTY_PROJTHROUGHMATRIX | DIRTY_WORLDMATRIX | DIRTY_VIEWMATRIX | DIRTY_TEXMATRIX |
DIRTY_FOGCOEF | DIRTY_BONE_UNIFORMS | DIRTY_UVSCALEOFFSET | DIRTY_DEPTHRANGE |
DIRTY_AMBIENT | DIRTY_MATAMBIENTALPHA | DIRTY_MATSPECULAR | DIRTY_MATDIFFUSE | DIRTY_MATEMISSIVE | DIRTY_LIGHT0 | DIRTY_LIGHT1 | DIRTY_LIGHT2 | DIRTY_LIGHT3;

void ShaderManagerDX9::VSUpdateUniforms(u64 dirtyUniforms) {
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

		ConvertProjMatrixToD3D(flippedMatrix, invertedX, invertedY);

		VSSetMatrix(CONST_VS_PROJ, flippedMatrix.getReadPtr());
	}
	if (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX) {
		Matrix4x4 proj_through;
		proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);

		ConvertProjMatrixToD3DThrough(proj_through);

		VSSetMatrix(CONST_VS_PROJ_THROUGH, proj_through.getReadPtr());
	}
	// Transform
	if (dirtyUniforms & DIRTY_WORLDMATRIX) {
		VSSetMatrix4x3_3(CONST_VS_WORLD, gstate.worldMatrix);
	}
	if (dirtyUniforms & DIRTY_VIEWMATRIX) {
		VSSetMatrix4x3_3(CONST_VS_VIEW, gstate.viewMatrix);
	}
	if (dirtyUniforms & DIRTY_TEXMATRIX) {
		VSSetMatrix4x3_3(CONST_VS_TEXMTX, gstate.tgenMatrix);
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
		VSSetFloatArray(CONST_VS_FOGCOEF, fogcoef, 2);
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
			VSSetMatrix4x3_3(CONST_VS_BONE0 + 3 * i, gstate.boneMatrix + 12 * i);
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
		uvscaleoff[0] = widthFactor;
		uvscaleoff[1] = heightFactor;
		uvscaleoff[2] = 0.0f;
		uvscaleoff[3] = 0.0f;
		VSSetFloatArray(CONST_VS_UVSCALEOFFSET, uvscaleoff, 4);
	}

	if (dirtyUniforms & DIRTY_DEPTHRANGE)	{
		// Depth is [0, 1] mapping to [minz, maxz], not too hard.
		float vpZScale = gstate.getViewportZScale();
		float vpZCenter = gstate.getViewportZCenter();

		// These are just the reverse of the formulas in GPUStateUtils.
		float halfActualZRange = vpZScale / gstate_c.vpDepthScale;
		float minz = -((gstate_c.vpZOffset * halfActualZRange) - vpZCenter) - halfActualZRange;
		float viewZScale = halfActualZRange * 2.0f;
		// Account for the half pixel offset.
		float viewZCenter = minz + (DepthSliceFactor() / 256.0f) * 0.5f;
		float viewZInvScale;

		if (viewZScale != 0.0) {
			viewZInvScale = 1.0f / viewZScale;
		} else {
			viewZInvScale = 0.0;
		}

		float data[4] = { viewZScale, viewZCenter, viewZCenter, viewZInvScale };
		VSSetFloatUniform4(CONST_VS_DEPTHRANGE, data);
	}
	// Lighting
	if (dirtyUniforms & DIRTY_AMBIENT) {
		VSSetColorUniform3Alpha(CONST_VS_AMBIENT, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (dirtyUniforms & DIRTY_MATAMBIENTALPHA) {
		VSSetColorUniform3Alpha(CONST_VS_MATAMBIENTALPHA, gstate.materialambient, gstate.getMaterialAmbientA());
	}
	if (dirtyUniforms & DIRTY_MATDIFFUSE) {
		VSSetColorUniform3(CONST_VS_MATDIFFUSE, gstate.materialdiffuse);
	}
	if (dirtyUniforms & DIRTY_MATEMISSIVE) {
		VSSetColorUniform3(CONST_VS_MATEMISSIVE, gstate.materialemissive);
	}
	if (dirtyUniforms & DIRTY_MATSPECULAR) {
		VSSetColorUniform3ExtraFloat(CONST_VS_MATSPECULAR, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
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
				VSSetFloatArray(CONST_VS_LIGHTPOS + i, vec, 3);
			} else {
				VSSetFloat24Uniform3(CONST_VS_LIGHTPOS + i, &gstate.lpos[i * 3]);
			}
			VSSetFloat24Uniform3(CONST_VS_LIGHTDIR + i, &gstate.ldir[i * 3]);
			VSSetFloat24Uniform3(CONST_VS_LIGHTATT + i, &gstate.latt[i * 3]);
			float angle_spotCoef[4] = { getFloat24(gstate.lcutoff[i]), getFloat24(gstate.lconv[i]) };
			VSSetFloatUniform4(CONST_VS_LIGHTANGLE_SPOTCOEF + i, angle_spotCoef);
			VSSetColorUniform3(CONST_VS_LIGHTAMBIENT + i, gstate.lcolor[i * 3]);
			VSSetColorUniform3(CONST_VS_LIGHTDIFFUSE + i, gstate.lcolor[i * 3 + 1]);
			VSSetColorUniform3(CONST_VS_LIGHTSPECULAR + i, gstate.lcolor[i * 3 + 2]);
		}
	}
}

ShaderManagerDX9::ShaderManagerDX9(LPDIRECT3DDEVICE9 device) : device_(device), lastVShader_(nullptr), lastPShader_(nullptr) {
	codeBuffer_ = new char[16384];
}

ShaderManagerDX9::~ShaderManagerDX9() {
	delete [] codeBuffer_;
}

void ShaderManagerDX9::Clear() {
	for (auto iter = fsCache_.begin(); iter != fsCache_.end(); ++iter)	{
		delete iter->second;
	}
	for (auto iter = vsCache_.begin(); iter != vsCache_.end(); ++iter)	{
		delete iter->second;
	}
	fsCache_.clear();
	vsCache_.clear();
	DirtyShader();
}

void ShaderManagerDX9::ClearCache(bool deleteThem) {
	Clear();
}


void ShaderManagerDX9::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	lastVShader_ = nullptr;
	lastPShader_ = nullptr;
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

void ShaderManagerDX9::DirtyLastShader() { // disables vertex arrays
	lastVShader_ = nullptr;
	lastPShader_ = nullptr;
}

VSShader *ShaderManagerDX9::ApplyShader(int prim, u32 vertType) {
	// Always use software for flat shading to fix the provoking index.
	bool useHWTransform = CanUseHardwareTransform(prim) && gstate.getShadeMode() != GE_SHADE_FLAT;

	VShaderID VSID;
	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, vertType, useHWTransform);
	} else {
		VSID = lastVSID_;
	}

	FShaderID FSID;
	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID);
	} else {
		FSID = lastFSID_;
	}

	// Just update uniforms if this is the same shader as last time.
	if (lastVShader_ != nullptr && lastPShader_ != nullptr && VSID == lastVSID_ && FSID == lastFSID_) {
		uint64_t dirtyUniforms = gstate_c.GetDirtyUniforms();
		if (dirtyUniforms) {
			if (dirtyUniforms & psUniforms)
				PSUpdateUniforms(dirtyUniforms);
			if (dirtyUniforms & vsUniforms)
				VSUpdateUniforms(dirtyUniforms);
			gstate_c.CleanUniforms();
		}
		return lastVShader_;	// Already all set.
	}

	VSCache::iterator vsIter = vsCache_.find(VSID);
	VSShader *vs;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		GenerateVertexShaderHLSL(VSID, codeBuffer_);
		vs = new VSShader(device_, VSID, codeBuffer_, useHWTransform);

		if (vs->Failed()) {
			I18NCategory *gr = GetI18NCategory("Graphics");
			ERROR_LOG(G3D, "Shader compilation failed, falling back to software transform");
			if (!g_Config.bHideSlowWarnings) {
				host->NotifyUserMessage(gr->T("hardware transform error - falling back to software"), 2.5f, 0xFF3030FF);
			}
			delete vs;

			ComputeVertexShaderID(&VSID, vertType, false);

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			GenerateVertexShaderHLSL(VSID, codeBuffer_);
			vs = new VSShader(device_, VSID, codeBuffer_, false);
		}

		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	lastVSID_ = VSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	PSShader *fs;
	if (fsIter == fsCache_.end())	{
		// Fragment shader not in cache. Let's compile it.
		GenerateFragmentShaderHLSL(FSID, codeBuffer_);
		fs = new PSShader(device_, FSID, codeBuffer_);
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	lastFSID_ = FSID;

	uint64_t dirtyUniforms = gstate_c.GetDirtyUniforms();
	if (dirtyUniforms) {
		if (dirtyUniforms & psUniforms)
			PSUpdateUniforms(dirtyUniforms);
		if (dirtyUniforms & vsUniforms)
			VSUpdateUniforms(dirtyUniforms);
		gstate_c.CleanUniforms();
	}

	device_->SetPixelShader(fs->shader);
	device_->SetVertexShader(vs->shader);

	lastPShader_ = fs;
	lastVShader_ = vs;
	return vs;
}

std::vector<std::string> ShaderManagerDX9::DebugGetShaderIDs(DebugShaderType type) {
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

std::string ShaderManagerDX9::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		auto iter = vsCache_.find(VShaderID(shaderId));
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}

	case SHADER_TYPE_FRAGMENT:
	{
		auto iter = fsCache_.find(FShaderID(shaderId));
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}
	default:
		return "N/A";
	}
}

}  // namespace
