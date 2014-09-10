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
#include "helper/global.h"
#include "base/logging.h"
#include "math/lin/matrix4x4.h"
#include "util/text/utf8.h"

#include "Common/Common.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TransformPipelineDX9.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "UI/OnScreenDisplay.h"

namespace DX9 {

PSShader::PSShader(const char *code, bool useHWTransform) : failed_(false), useHWTransform_(useHWTransform) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
	bool success;
	std::string errorMessage;

	success = CompilePixelShader(code, &shader, &constant, errorMessage);

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

	u_texenv = GetConstantByName("u_texenv");
	u_fogcolor = GetConstantByName("u_fogcolor");
	u_alphacolorref = GetConstantByName("u_alphacolorref");
	u_alphacolormask = GetConstantByName("u_alphacolormask");
}

PSShader::~PSShader() {
	pD3Ddevice->SetPixelShader(NULL);
	if (constant)
		constant->Release();
	if (shader)
		shader->Release();
}

VSShader::VSShader(const char *code, int vertType, bool useHWTransform) : failed_(false), useHWTransform_(useHWTransform) {
	source_ = code;
#ifdef SHADERLOG
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
	bool success;
	std::string errorMessage;

	success = CompileVertexShader(code, &shader, &constant, errorMessage);

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

	u_proj = GetConstantByName("u_proj");
	u_proj_through = GetConstantByName("u_proj_through");

	// Transform
	u_view = GetConstantByName("u_view");
	u_world = GetConstantByName("u_world");
	u_texmtx = GetConstantByName("u_texmtx");
	u_fogcoef = GetConstantByName("u_fogcoef");

	if (vertTypeGetWeightMask(vertType) != 0)
		numBones = TranslateNumBonesDX9(vertTypeGetNumBoneWeights(vertType));
	else
		numBones = 0;

#ifdef USE_BONE_ARRAY
	u_bone = glGetUniformLocation(program, "u_bone");
#else
	for (int i = 0; i < 8; i++) {
		char name[10];
		sprintf(name, "u_bone%i", i);
		u_bone[i] = GetConstantByName(name);
	}
#endif

	// Lighting, texturing
	u_ambient = GetConstantByName("u_ambient");
	u_matambientalpha = GetConstantByName("u_matambientalpha");
	u_matdiffuse = GetConstantByName("u_matdiffuse");
	u_matspecular = GetConstantByName("u_matspecular");
	u_matemissive = GetConstantByName("u_matemissive");
	u_uvscaleoffset = GetConstantByName("u_uvscaleoffset");

	for (int i = 0; i < 4; i++) {
		char temp[64];
		sprintf(temp, "u_lightpos%i", i);
		u_lightpos[i] = GetConstantByName(temp);
		sprintf(temp, "u_lightdir%i", i);
		u_lightdir[i] = GetConstantByName(temp);
		sprintf(temp, "u_lightatt%i", i);
		u_lightatt[i] = GetConstantByName(temp);
		sprintf(temp, "u_lightangle%i", i);
		u_lightangle[i] = GetConstantByName(temp);
		sprintf(temp, "u_lightspotCoef%i", i);
		u_lightspotCoef[i] = GetConstantByName(temp);
		sprintf(temp, "u_lightambient%i", i);
		u_lightambient[i] = GetConstantByName(temp);
		sprintf(temp, "u_lightdiffuse%i", i);
		u_lightdiffuse[i] = GetConstantByName(temp);
		sprintf(temp, "u_lightspecular%i", i);
		u_lightspecular[i] = GetConstantByName(temp);
	}
}

VSShader::~VSShader() {
	pD3Ddevice->SetVertexShader(NULL);
	if (constant)
		constant->Release();
	if (shader)
		shader->Release();
}


// Helper
D3DXHANDLE PSShader::GetConstantByName(LPCSTR pName) {
	return constant->GetConstantByName(NULL, pName);
}

// Helper
D3DXHANDLE VSShader::GetConstantByName(LPCSTR pName) {
	return constant->GetConstantByName(NULL, pName);
}

LinkedShaderDX9::LinkedShaderDX9(VSShader *vs, PSShader *fs, u32 vertType, bool useHWTransform)
		:dirtyUniforms(0), useHWTransform_(useHWTransform) {
	
	INFO_LOG(G3D, "Linked shader: vs %i fs %i", (int)vs->shader, (int)fs->shader);

	m_vs = vs;
	m_fs = fs;

	pD3Ddevice->SetPixelShader(fs->shader);
	pD3Ddevice->SetVertexShader(vs->shader);

	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL;
	use();
}

LinkedShaderDX9::~LinkedShaderDX9() {
//	glDeleteProgram(program);
}

void PSShader::SetColorUniform3(int creg, u32 color) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		0.0f
	};
	pD3Ddevice->SetPixelShaderConstantF(creg, col, 1);
}

void PSShader::SetColorUniform3Alpha255(int creg, u32 color, u8 alpha) {
	const float col[4] = {
		(float)((color & 0xFF)),
		(float)((color & 0xFF00) >> 8),
		(float)((color & 0xFF0000) >> 16),
		(float)alpha,
	};
	pD3Ddevice->SetPixelShaderConstantF(creg, col, 1);
}

void VSShader::SetFloat(int creg, float value) {
	const float f[4] = { value, 0.0f, 0.0f, 0.0f };
	pD3Ddevice->SetVertexShaderConstantF(creg, f, 1);
}

void VSShader::SetFloatArray(int creg, const float *value, int count) {
	float f[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < count; i++) {
		f[i] = value[i];
	}
	pD3Ddevice->SetVertexShaderConstantF(creg, f, 1);
}

// Utility
void VSShader::SetColorUniform3(int creg, u32 color) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		0.0f
	};
	pD3Ddevice->SetVertexShaderConstantF(creg, col, 1);
}

void VSShader::SetFloat24Uniform3(int creg, const u32 data[3]) {
	const u32 col[4] = {
		data[0] >> 8, data[1] >> 8, data[2] >> 8, 0
	};
	pD3Ddevice->SetVertexShaderConstantF(creg, (const float *)&col[0], 1);
}

void VSShader::SetColorUniform3Alpha(int creg, u32 color, u8 alpha) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		alpha/255.0f
	};
	pD3Ddevice->SetVertexShaderConstantF(creg, col, 1);
}

void VSShader::SetColorUniform3ExtraFloat(int creg, u32 color, float extra) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		extra
	};
	pD3Ddevice->SetVertexShaderConstantF(creg, col, 1);
}

// Utility
void VSShader::SetMatrix4x3(int creg, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4Transposed(m4x4, m4x3);
	pD3Ddevice->SetVertexShaderConstantF(creg, m4x4, 4);
}

void VSShader::SetMatrix(int creg, const float* pMatrix) {
	float transp[16];
	Transpose4x4(transp, pMatrix);
	pD3Ddevice->SetVertexShaderConstantF(creg, transp, 4);
}

// Depth in ogl is between -1;1 we need between 0;1 and optionally reverse it
void ConvertProjMatrixToD3D(Matrix4x4 & in, bool invert) {
	Matrix4x4 s;
	Matrix4x4 t;
	s.setScaling(Vec3(1, 1, invert ? -0.5 : 0.5f));
	t.setTranslation(Vec3(0, 0, 0.5f));
	in = in * s * t;
}

void LinkedShaderDX9::updateUniforms() {
	if (dirtyUniforms) {
		m_fs->updateUniforms(dirtyUniforms);
		m_vs->updateUniforms(dirtyUniforms);
		dirtyUniforms = 0;
	}
}

void LinkedShaderDX9::use() {
	updateUniforms();

	pD3Ddevice->SetPixelShader(m_fs->shader);
	pD3Ddevice->SetVertexShader(m_vs->shader);
}

void PSShader::updateUniforms(int dirtyUniforms) {
	if (u_texenv != 0 && (dirtyUniforms & DIRTY_TEXENV)) {
		SetColorUniform3(CONST_PS_TEXENV, gstate.texenvcolor);
	}
	if (u_alphacolorref != 0 && (dirtyUniforms & DIRTY_ALPHACOLORREF)) {
		SetColorUniform3Alpha255(CONST_PS_ALPHACOLORREF, gstate.getColorTestRef(), gstate.getAlphaTestRef());
	}
	if (u_alphacolormask != 0 && (dirtyUniforms & DIRTY_ALPHACOLORMASK)) {
		SetColorUniform3(CONST_PS_ALPHACOLORMASK, gstate.colortestmask);
	}
	if (u_fogcolor != 0 && (dirtyUniforms & DIRTY_FOGCOLOR)) {
		SetColorUniform3(CONST_PS_FOGCOLOR, gstate.fogcolor);
	}
}

void VSShader::updateUniforms(int dirtyUniforms) {
	// Update any dirty uniforms before we draw
	if (u_proj != 0 && (dirtyUniforms & DIRTY_PROJMATRIX)) {
		Matrix4x4 flippedMatrix;
		memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));
		if (gstate_c.vpHeight < 0) {
			flippedMatrix[5] = -flippedMatrix[5];
			flippedMatrix[13] = -flippedMatrix[13];
		}
		if (gstate_c.vpWidth < 0) {
			flippedMatrix[0] = -flippedMatrix[0];
			flippedMatrix[12] = -flippedMatrix[12];
		}

		bool invert = gstate_c.vpDepth < 0;
		ConvertProjMatrixToD3D(flippedMatrix, invert);

		SetMatrix(CONST_VS_PROJ, flippedMatrix.getReadPtr());
	}
	if (u_proj_through != 0 && (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX)) {
		Matrix4x4 proj_through;
		proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);

		ConvertProjMatrixToD3D(proj_through, false);

		SetMatrix(CONST_VS_PROJ_THROUGH, proj_through.getReadPtr());
	}
	// Transform
	if (u_world != 0 && (dirtyUniforms & DIRTY_WORLDMATRIX)) {
		SetMatrix4x3(CONST_VS_WORLD, gstate.worldMatrix);
	}
	if (u_view != 0 && (dirtyUniforms & DIRTY_VIEWMATRIX)) {
		SetMatrix4x3(CONST_VS_VIEW, gstate.viewMatrix);
	}
	if (u_texmtx != 0 && (dirtyUniforms & DIRTY_TEXMATRIX)) {
		SetMatrix4x3(CONST_VS_TEXMTX, gstate.tgenMatrix);
	}
	if (u_fogcoef != 0 && (dirtyUniforms & DIRTY_FOGCOEF)) {
		const float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		SetFloatArray(CONST_VS_FOGCOEF, fogcoef, 2);
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
			glUniformMatrix4fv(u_bone, numBones, GL_FALSE, allBones);
		} else {
			// Set them one by one. Could try to coalesce two in a row etc but too lazy.
			for (int i = 0; i < numBones; i++) {
				if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
					glUniformMatrix4fv(u_bone + i, 1, GL_FALSE, allBones + 16 * i);
				}
			}
		}
	}
#else
	float bonetemp[16];
	for (int i = 0; i < numBones; i++) {
		if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4(bonetemp, gstate.boneMatrix + 12 * i);
			if (u_bone[i] != 0)
				SetMatrix(CONST_VS_BONE0 + 4 * i, bonetemp);
		}
	}
#endif

	// Texturing
	if (u_uvscaleoffset != 0 && (dirtyUniforms & DIRTY_UVSCALEOFFSET)) {
		float uvscaleoff[4];
		if (gstate.isModeThrough()) {
			// We never get here because we don't use HW transform with through mode.
			// Although - why don't we?
			uvscaleoff[0] = gstate_c.uv.uScale / gstate_c.curTextureWidth;
			uvscaleoff[1] = gstate_c.uv.vScale / gstate_c.curTextureHeight;
			uvscaleoff[2] = gstate_c.uv.uOff / gstate_c.curTextureWidth;
			uvscaleoff[3] = gstate_c.uv.vOff / gstate_c.curTextureHeight;
		} else {
			int w = gstate.getTextureWidth(0);
			int h = gstate.getTextureHeight(0);
			float widthFactor = (float)w / (float)gstate_c.curTextureWidth;
			float heightFactor = (float)h / (float)gstate_c.curTextureHeight;
			// Not sure what GE_TEXMAP_UNKNOWN is, but seen in Riviera.  Treating the same as GE_TEXMAP_TEXTURE_COORDS works.
			if (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_COORDS || gstate.getUVGenMode() == GE_TEXMAP_UNKNOWN) {
				uvscaleoff[0] = gstate_c.uv.uScale * widthFactor;
				uvscaleoff[1] = gstate_c.uv.vScale * heightFactor;
				uvscaleoff[2] = gstate_c.uv.uOff * widthFactor;
				uvscaleoff[3] = gstate_c.uv.vOff * heightFactor;
			} else {
				uvscaleoff[0] = widthFactor;
				uvscaleoff[1] = heightFactor;
				uvscaleoff[2] = 0.0f;
				uvscaleoff[3] = 0.0f;
			}
		}
		SetFloatArray(CONST_VS_UVSCALEOFFSET, uvscaleoff, 4);
	}

	// Lighting
	if (u_ambient != 0 && (dirtyUniforms & DIRTY_AMBIENT)) {
		SetColorUniform3Alpha(CONST_VS_AMBIENT, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (u_matambientalpha != 0 && (dirtyUniforms & DIRTY_MATAMBIENTALPHA)) {
		SetColorUniform3Alpha(CONST_VS_MATAMBIENTALPHA, gstate.materialambient, gstate.getMaterialAmbientA());
	}
	if (u_matdiffuse != 0 && (dirtyUniforms & DIRTY_MATDIFFUSE)) {
		SetColorUniform3(CONST_VS_MATDIFFUSE, gstate.materialdiffuse);
	}
	if (u_matemissive != 0 && (dirtyUniforms & DIRTY_MATEMISSIVE)) {
		SetColorUniform3(CONST_VS_MATEMISSIVE, gstate.materialemissive);
	}
	if (u_matspecular != 0 && (dirtyUniforms & DIRTY_MATSPECULAR)) {
		SetColorUniform3ExtraFloat(CONST_VS_MATSPECULAR, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
	}
	for (int i = 0; i < 4; i++) {
		if (dirtyUniforms & (DIRTY_LIGHT0 << i)) {
			if (u_lightpos[i] != 0) {
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
					SetFloatArray(CONST_VS_LIGHTPOS + i, vec, 3);
				} else {
					SetFloat24Uniform3(CONST_VS_LIGHTPOS + i, &gstate.lpos[i * 3]);
				}
			}
			if (u_lightdir[i] != 0) SetFloat24Uniform3(CONST_VS_LIGHTDIR + i, &gstate.ldir[i * 3]);
			if (u_lightatt[i] != 0) SetFloat24Uniform3(CONST_VS_LIGHTATT + i, &gstate.latt[i * 3]);
			if (u_lightangle[i] != 0) SetFloat(CONST_VS_LIGHTANGLE + i, getFloat24(gstate.lcutoff[i]));
			if (u_lightspotCoef[i] != 0) SetFloat(CONST_VS_LIGHTSPOTCOEF + i, getFloat24(gstate.lconv[i]));
			if (u_lightambient[i] != 0) SetColorUniform3(CONST_VS_LIGHTAMBIENT + i, gstate.lcolor[i * 3]);
			if (u_lightdiffuse[i] != 0) SetColorUniform3(CONST_VS_LIGHTDIFFUSE + i, gstate.lcolor[i * 3 + 1]);
			if (u_lightspecular[i] != 0) SetColorUniform3(CONST_VS_LIGHTSPECULAR + i, gstate.lcolor[i * 3 + 2]);
		}
	}
}

ShaderManagerDX9::ShaderManagerDX9() : lastShader_(NULL), globalDirty_(0xFFFFFFFF), shaderSwitchDirty_(0) {
	codeBuffer_ = new char[16384];
}

ShaderManagerDX9::~ShaderManagerDX9() {
	delete [] codeBuffer_;
}

void ShaderManagerDX9::Clear() {
	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		delete iter->ls;
	}
	for (auto iter = fsCache_.begin(); iter != fsCache_.end(); ++iter)	{
		delete iter->second;
	}
	for (auto iter = vsCache_.begin(); iter != vsCache_.end(); ++iter)	{
		delete iter->second;
	}
	linkedShaderCache_.clear();
	fsCache_.clear();
	vsCache_.clear();
	globalDirty_ = 0xFFFFFFFF;
	lastFSID_.clear();
	lastVSID_.clear();
	DirtyShader();
}

void ShaderManagerDX9::ClearCache(bool deleteThem) {
	Clear();
}


void ShaderManagerDX9::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.clear();
	lastVSID_.clear();
	lastShader_ = 0;
	globalDirty_ = 0xFFFFFFFF;
	shaderSwitchDirty_ = 0;
}

void ShaderManagerDX9::DirtyLastShader() { // disables vertex arrays
	lastShader_ = 0;
}


LinkedShaderDX9 *ShaderManagerDX9::ApplyShader(int prim, u32 vertType) {
	if (globalDirty_) {
		if (lastShader_)
			lastShader_->dirtyUniforms |= globalDirty_;
		shaderSwitchDirty_ |= globalDirty_;
		globalDirty_ = 0;
	}

	bool useHWTransform = CanUseHardwareTransformDX9(prim);

	VertexShaderIDDX9 VSID;
	FragmentShaderIDDX9 FSID;
	ComputeVertexShaderIDDX9(&VSID, vertType, prim, useHWTransform);
	ComputeFragmentShaderIDDX9(&FSID);

	// Just update uniforms if this is the same shader as last time.
	if (lastShader_ != 0 && VSID == lastVSID_ && FSID == lastFSID_) {
		lastShader_->updateUniforms();
		return lastShader_;	// Already all set.
	}

	lastVSID_ = VSID;
	lastFSID_ = FSID;

	VSCache::iterator vsIter = vsCache_.find(VSID);
	VSShader *vs;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		GenerateVertexShaderDX9(prim, codeBuffer_, useHWTransform);
		vs = new VSShader(codeBuffer_, vertType, useHWTransform);

		if (vs->Failed()) {
			ERROR_LOG(HLE, "Shader compilation failed, falling back to software transform");
			osm.Show("hardware transform error - falling back to software", 2.5f, 0xFF3030FF, -1, true);
			delete vs;

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			GenerateVertexShaderDX9(prim, codeBuffer_, false);
			vs = new VSShader(codeBuffer_, vertType, false);
		}

		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}

	FSCache::iterator fsIter = fsCache_.find(FSID);
	PSShader *fs;
	if (fsIter == fsCache_.end())	{
		// Fragment shader not in cache. Let's compile it.
		GenerateFragmentShaderDX9(codeBuffer_);
		fs = new PSShader(codeBuffer_, useHWTransform);
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	// Okay, we have both shaders. Let's see if there's a linked one.
	LinkedShaderDX9 *ls = NULL;

	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		// Deferred dirtying! Let's see if we can make this even more clever later.
		iter->ls->dirtyUniforms |= shaderSwitchDirty_;

		if (iter->vs == vs && iter->fs == fs) {
			ls = iter->ls;
		}
	}
	shaderSwitchDirty_ = 0;

	if (ls == NULL) {
		ls = new LinkedShaderDX9(vs, fs, vertType, vs->UseHWTransform());	// This does "use" automatically
		const LinkedShaderCacheEntry entry(vs, fs, ls);
		linkedShaderCache_.push_back(entry);
	} else {
		// If shader changed we need to update all uniforms
		if (lastShader_ != ls) {
			ls->dirtyUniforms = DIRTY_ALL;
		}

		ls->use();
	}

	lastShader_ = ls;
	return ls;
}

}  // namespace
