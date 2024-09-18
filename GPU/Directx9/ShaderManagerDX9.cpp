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
//#define SHADERLOG
#endif

#include <cmath>
#include <map>

#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/D3D9/D3D9ShaderCompiler.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/OSD.h"
#include "Common/System/Display.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/FramebufferManagerDX9.h"

using namespace Lin;

PSShader::PSShader(LPDIRECT3DDEVICE9 device, FShaderID id, const char *code) : id_(id) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
	bool success;
	std::string errorMessage;

	success = CompilePixelShaderD3D9(device, code, &shader, &errorMessage);

	if (!errorMessage.empty()) {
		if (success) {
			ERROR_LOG(Log::G3D, "Warnings in shader compilation!");
		} else {
			ERROR_LOG(Log::G3D, "Error in shader compilation!");
		}
		ERROR_LOG(Log::G3D, "Messages: %s", errorMessage.c_str());
		ERROR_LOG(Log::G3D, "Shader source:\n%s", LineNumberString(code).c_str());
		OutputDebugStringUTF8("Messages:\n");
		OutputDebugStringUTF8(errorMessage.c_str());
		Reporting::ReportMessage("D3D error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	}

	if (!success) {
		failed_ = true;
		shader = nullptr;
		return;
	} else {
		VERBOSE_LOG(Log::G3D, "Compiled pixel shader:\n%s\n", (const char *)code);
	}
}

PSShader::~PSShader() {
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

VSShader::VSShader(LPDIRECT3DDEVICE9 device, VShaderID id, const char *code, bool useHWTransform) : useHWTransform_(useHWTransform), id_(id) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
	bool success;
	std::string errorMessage;

	success = CompileVertexShaderD3D9(device, code, &shader, &errorMessage);
	if (!errorMessage.empty()) {
		if (success) {
			ERROR_LOG(Log::G3D, "Warnings in shader compilation!");
		} else {
			ERROR_LOG(Log::G3D, "Error in shader compilation!");
		}
		ERROR_LOG(Log::G3D, "Messages: %s", errorMessage.c_str());
		ERROR_LOG(Log::G3D, "Shader source:\n%s", code);
		OutputDebugStringUTF8("Messages:\n");
		OutputDebugStringUTF8(errorMessage.c_str());
		Reporting::ReportMessage("D3D error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
	}

	if (!success) {
		failed_ = true;
		shader = nullptr;
		return;
	} else {
		VERBOSE_LOG(Log::G3D, "Compiled vertex shader:\n%s\n", (const char *)code);
	}
}

VSShader::~VSShader() {
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

void ShaderManagerDX9::VSSetFloatUniform4(int creg, const float data[4]) {
	device_->SetVertexShaderConstantF(creg, data, 1);
}

void ShaderManagerDX9::VSSetFloat24Uniform3(int creg, const u32 data[3]) {
	float f[4];
	ExpandFloat24x3ToFloat4(f, data);
	device_->SetVertexShaderConstantF(creg, f, 1);
}

void ShaderManagerDX9::VSSetFloat24Uniform3Normalized(int creg, const u32 data[3]) {
	float f[4];
	ExpandFloat24x3ToFloat4AndNormalize(f, data);
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

void ShaderManagerDX9::VSSetMatrix4x3_3(int creg, const float *m4x3) {
	float m3x4[12];
	ConvertMatrix4x3To3x4Transposed(m3x4, m4x3);
	device_->SetVertexShaderConstantF(creg, m3x4, 3);
}

void ShaderManagerDX9::VSSetMatrix(int creg, const float* pMatrix) {
	device_->SetVertexShaderConstantF(creg, pMatrix, 4);
}

// Depth in ogl is between -1;1 we need between 0;1 and optionally reverse it
static void ConvertProjMatrixToD3D(Matrix4x4 &in, bool invertedX, bool invertedY) {
	// Half pixel offset hack
	float xoff = 1.0f / gstate_c.curRTRenderWidth;
	if (invertedX) {
		xoff = -gstate_c.vpXOffset - xoff;
	} else {
		xoff = gstate_c.vpXOffset - xoff;
	}

	float yoff = -1.0f / gstate_c.curRTRenderHeight;
	if (invertedY) {
		yoff = -gstate_c.vpYOffset - yoff;
	} else {
		yoff = gstate_c.vpYOffset - yoff;
	}

	const Vec3 trans(xoff, yoff, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

static void ConvertProjMatrixToD3DThrough(Matrix4x4 &in) {
	float xoff = -1.0f / gstate_c.curRTRenderWidth;
	float yoff = 1.0f / gstate_c.curRTRenderHeight;
	in.translateAndScale(Vec3(xoff, yoff, 0.5f), Vec3(1.0f, 1.0f, 0.5f));
}

const uint64_t psUniforms = DIRTY_TEXENV | DIRTY_TEX_ALPHA_MUL | DIRTY_ALPHACOLORREF | DIRTY_ALPHACOLORMASK | DIRTY_FOGCOLOR | DIRTY_STENCILREPLACEVALUE | DIRTY_SHADERBLEND | DIRTY_TEXCLAMP | DIRTY_MIPBIAS;

void ShaderManagerDX9::PSUpdateUniforms(u64 dirtyUniforms) {
	if (dirtyUniforms & DIRTY_TEXENV) {
		PSSetColorUniform3(CONST_PS_TEXENV, gstate.texenvcolor);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORREF) {
		PSSetColorUniform3Alpha255(CONST_PS_ALPHACOLORREF, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORMASK) {
		PSSetColorUniform3Alpha255(CONST_PS_ALPHACOLORMASK, gstate.colortestmask, gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		PSSetColorUniform3(CONST_PS_FOGCOLOR, gstate.fogcolor);
	}
	if (dirtyUniforms & DIRTY_STENCILREPLACEVALUE) {
		PSSetFloat(CONST_PS_STENCILREPLACE, (float)gstate.getStencilTestRef() * (1.0f / 255.0f));
	}
	if (dirtyUniforms & DIRTY_TEX_ALPHA_MUL) {
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE) {
			doTextureAlpha = false;
		}
		// NOTE: Reversed value, more efficient in shader.
		float noAlphaMul[2] = { doTextureAlpha ? 0.0f : 1.0f, gstate.isColorDoublingEnabled() ? 2.0f : 1.0f };
		PSSetFloatArray(CONST_PS_TEX_NO_ALPHA_MUL, noAlphaMul, 2);
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

	if (dirtyUniforms & DIRTY_MIPBIAS) {
		float mipBias = (float)gstate.getTexLevelOffset16() * (1.0 / 16.0f);

		// NOTE: This equation needs some adjustment in D3D9. Can't get it to look completely smooth :(
		mipBias = (mipBias + 0.25f) / (float)(gstate.getTextureMaxLevel() + 1);
		PSSetFloatArray(CONST_PS_MIPBIAS, &mipBias, 1);
	}
}

const uint64_t vsUniforms = DIRTY_PROJMATRIX | DIRTY_PROJTHROUGHMATRIX | DIRTY_WORLDMATRIX | DIRTY_VIEWMATRIX | DIRTY_TEXMATRIX |
DIRTY_FOGCOEF | DIRTY_BONE_UNIFORMS | DIRTY_UVSCALEOFFSET | DIRTY_DEPTHRANGE | DIRTY_CULLRANGE |
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
		VSSetFloat(CONST_VS_ROTATION, 0);  // We don't use this on any platform in D3D9.
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
		// The PSP just ignores infnan here (ignoring IEEE), so take it down to a valid float.
		// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
		if (my_isnanorinf(fogcoef[0])) {
			// Not really sure what a sensible value might be, but let's try 64k.
			fogcoef[0] = std::signbit(fogcoef[0]) ? -65535.0f : 65535.0f;
		}
		if (my_isnanorinf(fogcoef[1])) {
			fogcoef[1] = std::signbit(fogcoef[1]) ? -65535.0f : 65535.0f;
		}
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
		float widthFactor = 1.0f;
		float heightFactor = 1.0f;
		if (gstate_c.textureIsFramebuffer) {
			const float invW = 1.0f / (float)gstate_c.curTextureWidth;
			const float invH = 1.0f / (float)gstate_c.curTextureHeight;
			const int w = gstate.getTextureWidth(0);
			const int h = gstate.getTextureHeight(0);
			widthFactor = (float)w * invW;
			heightFactor = (float)h * invH;
		}
		float uvscaleoff[4];
		uvscaleoff[0] = widthFactor;
		uvscaleoff[1] = heightFactor;
		uvscaleoff[2] = 0.0f;
		uvscaleoff[3] = 0.0f;
		VSSetFloatArray(CONST_VS_UVSCALEOFFSET, uvscaleoff, 4);
	}

	if (dirtyUniforms & DIRTY_DEPTHRANGE) {
		// Depth is [0, 1] mapping to [minz, maxz], not too hard.
		float vpZScale = gstate.getViewportZScale();
		float vpZCenter = gstate.getViewportZCenter();

		// These are just the reverse of the formulas in GPUStateUtils.
		float halfActualZRange = InfToZero(gstate_c.vpDepthScale != 0.0f ? vpZScale / gstate_c.vpDepthScale : 0.0f);
		float minz = -((gstate_c.vpZOffset * halfActualZRange) - vpZCenter) - halfActualZRange;
		float viewZScale = halfActualZRange * 2.0f;
		float viewZCenter = minz;
		float reverseScale = InfToZero(gstate_c.vpDepthScale != 0.0f ? 2.0f * (1.0f / gstate_c.vpDepthScale) : 0.0f);
		float reverseTranslate = gstate_c.vpZOffset * 0.5f + 0.5f;

		float data[4] = { viewZScale, viewZCenter, reverseTranslate, reverseScale };
		VSSetFloatUniform4(CONST_VS_DEPTHRANGE, data);

		if (draw_->GetDeviceCaps().clipPlanesSupported >= 1) {
			float clip[4] = { 0.0f, 0.0f, reverseScale, 1.0f - reverseTranslate * reverseScale };
			// Well, not a uniform, but we treat it as one like other backends.
			device_->SetClipPlane(0, clip);
		}
	}
	if (dirtyUniforms & DIRTY_CULLRANGE) {
		float minValues[4], maxValues[4];
		CalcCullRange(minValues, maxValues, false, false);
		VSSetFloatUniform4(CONST_VS_CULLRANGEMIN, minValues);
		VSSetFloatUniform4(CONST_VS_CULLRANGEMAX, maxValues);
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
				VSSetFloat24Uniform3Normalized(CONST_VS_LIGHTPOS + i, &gstate.lpos[i * 3]);
			} else {
				VSSetFloat24Uniform3(CONST_VS_LIGHTPOS + i, &gstate.lpos[i * 3]);
			}
			VSSetFloat24Uniform3Normalized(CONST_VS_LIGHTDIR + i, &gstate.ldir[i * 3]);
			VSSetFloat24Uniform3(CONST_VS_LIGHTATT + i, &gstate.latt[i * 3]);
			float angle_spotCoef[4] = { getFloat24(gstate.lcutoff[i]), getFloat24(gstate.lconv[i]) };
			VSSetFloatUniform4(CONST_VS_LIGHTANGLE_SPOTCOEF + i, angle_spotCoef);
			VSSetColorUniform3(CONST_VS_LIGHTAMBIENT + i, gstate.lcolor[i * 3]);
			VSSetColorUniform3(CONST_VS_LIGHTDIFFUSE + i, gstate.lcolor[i * 3 + 1]);
			VSSetColorUniform3(CONST_VS_LIGHTSPECULAR + i, gstate.lcolor[i * 3 + 2]);
		}
	}
}

ShaderManagerDX9::ShaderManagerDX9(Draw::DrawContext *draw, LPDIRECT3DDEVICE9 device)
	: ShaderManagerCommon(draw), device_(device) {
	codeBuffer_ = new char[32768];
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
	DirtyLastShader();
}

void ShaderManagerDX9::ClearShaders() {
	Clear();
}

void ShaderManagerDX9::DirtyLastShader() {
	// Forget the last shader ID
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	lastVShader_ = nullptr;
	lastPShader_ = nullptr;
	// TODO: Probably not necessary to dirty uniforms here on DX9.
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

VSShader *ShaderManagerDX9::ApplyShader(bool useHWTransform, bool useHWTessellation, VertexDecoder *decoder, bool weightsAsFloat, bool useSkinInDecode, const ComputedPipelineState &pipelineState) {
	VShaderID VSID;
	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, decoder, useHWTransform, useHWTessellation, weightsAsFloat, useSkinInDecode);
	} else {
		VSID = lastVSID_;
	}

	FShaderID FSID;
	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID, pipelineState, draw_->GetBugs());
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
	VSShader *vs = nullptr;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		std::string genErrorString;
		uint32_t attrMask;
		uint64_t uniformMask;
		VertexShaderFlags flags;
		if (GenerateVertexShader(VSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &attrMask, &uniformMask, &flags, &genErrorString)) {
			vs = new VSShader(device_, VSID, codeBuffer_, useHWTransform);
		}
		if (!vs || vs->Failed()) {
			if (!vs) {
				// TODO: Report this?
				ERROR_LOG(Log::G3D, "Shader generation failed, falling back to software transform");
			} else {
				ERROR_LOG(Log::G3D, "Shader compilation failed, falling back to software transform");
			}
			if (!g_Config.bHideSlowWarnings) {
				auto gr = GetI18NCategory(I18NCat::GRAPHICS);
				g_OSD.Show(OSDType::MESSAGE_ERROR, gr->T("hardware transform error - falling back to software"), 2.5f);
			}
			delete vs;

			ComputeVertexShaderID(&VSID, decoder, false, false, weightsAsFloat, useSkinInDecode);

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			uint32_t attrMask;
			uint64_t uniformMask;
			bool success = GenerateVertexShader(VSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &attrMask, &uniformMask, &flags, &genErrorString);
			_assert_(success);
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
		std::string errorString;
		uint64_t uniformMask;
		FragmentShaderFlags flags;
		bool success = GenerateFragmentShader(FSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &uniformMask, &flags, &errorString);
		// We're supposed to handle all possible cases.
		_assert_(success);
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

	device_->SetPixelShader(fs->shader.Get());
	device_->SetVertexShader(vs->shader.Get());

	lastPShader_ = fs;
	lastVShader_ = vs;
	return vs;
}

std::vector<std::string> ShaderManagerDX9::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
		for (auto iter : vsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
		break;
	case SHADER_TYPE_FRAGMENT:
		for (auto iter : fsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
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
