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

// For matrices convertions
#include <xnamath.h>

namespace DX9 {

PSShader::PSShader(const char *code, bool useHWTransform) : failed_(false), useHWTransform_(useHWTransform) {
	source_ = code;
#ifdef SHADERLOG
#ifdef _XBOX
	OutputDebugString(code);
#else
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
#endif
	bool success;

	success = CompilePixelShader(code, &shader, &constant);

	if (!success) {
		failed_ = true;
		shader = NULL;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
}

PSShader::~PSShader() {
	pD3Ddevice->SetPixelShader(NULL);
	if (shader)
		shader->Release();
}

VSShader::VSShader(const char *code, bool useHWTransform) : failed_(false), useHWTransform_(useHWTransform) {
	source_ = code;
#ifdef SHADERLOG
#ifdef _XBOX
	OutputDebugString(code);
#else
	OutputDebugString(ConvertUTF8ToWString(code).c_str());
#endif
#endif
	bool success;

	success = CompileVertexShader(code, &shader, &constant);

	if (!success) {
		failed_ = true;
		shader = NULL;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
}

VSShader::~VSShader() {
	pD3Ddevice->SetVertexShader(NULL);
	if (shader)
		shader->Release();
}


// Helper
D3DXHANDLE LinkedShaderDX9::GetConstantByName(LPCSTR pName) {
	D3DXHANDLE ret = NULL;
	if ((ret = m_fs->constant->GetConstantByName(NULL, pName)) != NULL)  {
	} else if ((ret = m_vs->constant->GetConstantByName(NULL, pName)) != NULL)  {}
	return ret;
}

LinkedShaderDX9::LinkedShaderDX9(VSShader *vs, PSShader *fs, u32 vertType, bool useHWTransform)
		:dirtyUniforms(0), useHWTransform_(useHWTransform) {
	
	INFO_LOG(G3D, "Linked shader: vs %i fs %i", (int)vs->shader, (int)fs->shader);

	m_vs = vs;
	m_fs = fs;

	u_tex = 			GetConstantByName("tex");
	u_proj = 			GetConstantByName("u_proj");
	u_proj_through = 	GetConstantByName("u_proj_through");
	u_texenv = 			GetConstantByName("u_texenv");
	u_fogcolor = 		GetConstantByName("u_fogcolor");
	u_fogcoef = 		GetConstantByName("u_fogcoef");
	u_alphacolorref = 	GetConstantByName("u_alphacolorref");
	u_alphacolormask = 	GetConstantByName("u_alphacolormask");

	// Transform
	u_view = 	GetConstantByName("u_view");
	u_world = 	GetConstantByName("u_world");
	u_texmtx = 	GetConstantByName("u_texmtx");

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
	u_ambient =			GetConstantByName("u_ambient");
	u_matambientalpha = GetConstantByName("u_matambientalpha");
	u_matdiffuse =		GetConstantByName("u_matdiffuse");
	u_matspecular =		GetConstantByName("u_matspecular");
	u_matemissive =		GetConstantByName("u_matemissive");
	u_uvscaleoffset =	GetConstantByName("u_uvscaleoffset");

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

	/*
	a_position = glGetAttribLocation(program, "a_position");
	a_color0 = glGetAttribLocation(program, "a_color0");
	a_color1 = glGetAttribLocation(program, "a_color1");
	a_texcoord = glGetAttribLocation(program, "a_texcoord");
	a_normal = glGetAttribLocation(program, "a_normal");
	a_weight0123 = glGetAttribLocation(program, "a_w1");
	a_weight4567 = glGetAttribLocation(program, "a_w2");
	*/

	//glUseProgram(program);

	pD3Ddevice->SetPixelShader(fs->shader);
	pD3Ddevice->SetVertexShader(vs->shader);

	// Default uniform values
	//glUniform1i(u_tex, 0);
	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL;
	use();
}

LinkedShaderDX9::~LinkedShaderDX9() {
//	glDeleteProgram(program);
}

void LinkedShaderDX9::SetFloatArray(D3DXHANDLE uniform, const float* pArray, int len) {
	if (m_fs->constant->SetFloatArray(pD3Ddevice, uniform, pArray, len) == D3D_OK); 
	else
		m_vs->constant->SetFloatArray(pD3Ddevice, uniform, pArray, len);
}

void LinkedShaderDX9::SetFloat(D3DXHANDLE uniform, float value) {
	if (m_fs->constant->SetFloat(pD3Ddevice, uniform, value) == D3D_OK); 
	else
		m_vs->constant->SetFloat(pD3Ddevice, uniform, value);
}


// Utility
void LinkedShaderDX9::SetColorUniform3(D3DXHANDLE uniform, u32 color) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f
	};
	SetFloatArray(uniform, col, 4);
}

void LinkedShaderDX9::SetFloat24Uniform3(D3DXHANDLE uniform, const u32 data[3]) {
	const u32 col[4] = {
		data[0] >> 8, data[1] >> 8, data[2] >> 8,
	};
	SetFloatArray(uniform, (const float *)&col[0], 4);
}

void LinkedShaderDX9::SetColorUniform3Alpha(D3DXHANDLE uniform, u32 color, u8 alpha) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		alpha/255.0f
	};
	SetFloatArray(uniform, col, 4);
}

void LinkedShaderDX9::SetColorUniform3Alpha255(D3DXHANDLE uniform, u32 color, u8 alpha) {
	if (1) {
		const float col[4] = {
			(float)((color & 0xFF)) * (1.0f / 255.0f),
			(float)((color & 0xFF00) >> 8) * (1.0f / 255.0f),
			(float)((color & 0xFF0000) >> 16) * (1.0f / 255.0f),
			(float)alpha * (1.0f / 255.0f)
		};
		SetFloatArray(uniform, col, 4);
	} else {
		const float col[4] = {
			(float)((color & 0xFF)) ,
			(float)((color & 0xFF00) >> 8) ,
			(float)((color & 0xFF0000) >> 16) ,
			(float)alpha 
		};
		SetFloatArray(uniform, col, 4);
	}
}


void LinkedShaderDX9::SetColorUniform3ExtraFloat(D3DXHANDLE uniform, u32 color, float extra) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		extra
	};
	SetFloatArray(uniform, col, 4);
}

// Utility
void LinkedShaderDX9::SetMatrix4x3(D3DXHANDLE uniform, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4(m4x4, m4x3);

	if (m_vs->constant->SetMatrix(pD3Ddevice, uniform, (D3DXMATRIX*)m4x4) == D3D_OK); 
	else
		m_fs->constant->SetMatrix(pD3Ddevice, uniform, (D3DXMATRIX*)m4x4);
}

void LinkedShaderDX9::SetMatrix(D3DXHANDLE uniform, const float* pMatrix) {
	D3DXMATRIX * pDxMat = (D3DXMATRIX*)pMatrix;

	if (m_vs->constant->SetMatrix(pD3Ddevice, uniform, pDxMat) == D3D_OK); 
	else
		m_fs->constant->SetMatrix(pD3Ddevice, uniform, pDxMat);
}

// Depth in ogl is between -1;1 we need between 0;1
static void ConvertMatrices(Matrix4x4 & in) {
	/*
	in.zz *= 0.5f;
	in.wz += 1.f;
	*/
	Matrix4x4 s;
	Matrix4x4 t;
	s.setScaling(Vec3(1, 1, 0.5f));
	t.setTranslation(Vec3(0, 0, 0.5f));
	in = in * s;
	in = in * t;
}

void LinkedShaderDX9::use() {
	
	updateUniforms();

	pD3Ddevice->SetPixelShader(m_fs->shader);
	pD3Ddevice->SetVertexShader(m_vs->shader);
}

void LinkedShaderDX9::stop() {

}

void LinkedShaderDX9::updateUniforms() {
	if (!dirtyUniforms)
		return;

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
		// Convert matrices !
		ConvertMatrices(flippedMatrix);

		SetMatrix(u_proj, flippedMatrix.getReadPtr());
	}
	if (u_proj_through != 0 && (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX))
	{
		Matrix4x4 proj_through;
		proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);

		// Convert matrices !
		ConvertMatrices(proj_through);

		SetMatrix(u_proj_through, proj_through.getReadPtr());
	}
	if (u_texenv != 0 && (dirtyUniforms & DIRTY_TEXENV)) {
		SetColorUniform3(u_texenv, gstate.texenvcolor);
	}
	if (u_alphacolorref != 0 && (dirtyUniforms & DIRTY_ALPHACOLORREF)) {
		SetColorUniform3Alpha255(u_alphacolorref, gstate.getColorTestRef(), gstate.getAlphaTestRef());
	}
	if (u_alphacolormask != 0 && (dirtyUniforms & DIRTY_ALPHACOLORMASK)) {
		SetColorUniform3(u_alphacolormask, gstate.colortestmask);
	}
	if (u_fogcolor != 0 && (dirtyUniforms & DIRTY_FOGCOLOR)) {
		SetColorUniform3(u_fogcolor, gstate.fogcolor);
	}
	if (u_fogcoef != 0 && (dirtyUniforms & DIRTY_FOGCOEF)) {
		const float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		SetFloatArray(u_fogcoef, fogcoef, 2);
	}

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
				static const float rescale[4] = {1.0f, 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
				float factor = rescale[(gstate.vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT];
				uvscaleoff[0] = gstate_c.uv.uScale * factor * widthFactor;
				uvscaleoff[1] = gstate_c.uv.vScale * factor * heightFactor;
				uvscaleoff[2] = gstate_c.uv.uOff * widthFactor;
				uvscaleoff[3] = gstate_c.uv.vOff * heightFactor;
			} else {
				uvscaleoff[0] = widthFactor;
				uvscaleoff[1] = heightFactor;
				uvscaleoff[2] = 0.0f;
				uvscaleoff[3] = 0.0f;
			}
		}		
		SetFloatArray(u_uvscaleoffset, uvscaleoff, 4);
	}

	// Transform
	if (u_world != 0 && (dirtyUniforms & DIRTY_WORLDMATRIX)) {
		SetMatrix4x3(u_world, gstate.worldMatrix);
	}
	if (u_view != 0 && (dirtyUniforms & DIRTY_VIEWMATRIX)) {
		SetMatrix4x3(u_view, gstate.viewMatrix);
	}
	if (u_texmtx != 0 && (dirtyUniforms & DIRTY_TEXMATRIX)) {
		SetMatrix4x3(u_texmtx, gstate.tgenMatrix);
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
				SetMatrix(u_bone[i], bonetemp);
		}
	}
#endif

	// Lighting
	if (u_ambient != 0 && (dirtyUniforms & DIRTY_AMBIENT)) {
		SetColorUniform3Alpha(u_ambient, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (u_matambientalpha != 0 && (dirtyUniforms & DIRTY_MATAMBIENTALPHA)) {
		SetColorUniform3Alpha(u_matambientalpha, gstate.materialambient, gstate.getMaterialAmbientA());
	}
	if (u_matdiffuse != 0 && (dirtyUniforms & DIRTY_MATDIFFUSE)) {
		SetColorUniform3(u_matdiffuse, gstate.materialdiffuse);
	}
	if (u_matemissive != 0 && (dirtyUniforms & DIRTY_MATEMISSIVE)) {
		SetColorUniform3(u_matemissive, gstate.materialemissive);
	}
	if (u_matspecular != 0 && (dirtyUniforms & DIRTY_MATSPECULAR)) {
		SetColorUniform3ExtraFloat(u_matspecular, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
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
					SetFloatArray(u_lightpos[i], vec, 3);
				} else {
					SetFloat24Uniform3(u_lightpos[i], &gstate.lpos[i * 3]);
				}
			}
			if (u_lightdir[i] != 0) SetFloat24Uniform3(u_lightdir[i], &gstate.ldir[i * 3]);
			if (u_lightatt[i] != 0) SetFloat24Uniform3(u_lightatt[i], &gstate.latt[i * 3]);
			if (u_lightangle[i] != 0) SetFloat(u_lightangle[i], getFloat24(gstate.lcutoff[i]));
			if (u_lightspotCoef[i] != 0) SetFloat(u_lightspotCoef[i], getFloat24(gstate.lconv[i]));
			if (u_lightambient[i] != 0) SetColorUniform3(u_lightambient[i], gstate.lcolor[i * 3]);
			if (u_lightdiffuse[i] != 0) SetColorUniform3(u_lightdiffuse[i], gstate.lcolor[i * 3 + 1]);
			if (u_lightspecular[i] != 0) SetColorUniform3(u_lightspecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}

	dirtyUniforms = 0;
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

void ShaderManagerDX9::EndFrame() { // disables vertex arrays
	if (lastShader_)
		lastShader_->stop();
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
	ComputeVertexShaderIDDX9(&VSID, prim, useHWTransform);
	ComputeFragmentShaderIDDX9(&FSID);

	// Just update uniforms if this is the same shader as last time.
	if (lastShader_ != 0 && VSID == lastVSID_ && FSID == lastFSID_) {
		lastShader_->updateUniforms();
		return lastShader_;	// Already all set.
	}

	if (lastShader_ != 0) {
		// There was a previous shader and we're switching.
		lastShader_->stop();
	}

	lastVSID_ = VSID;
	lastFSID_ = FSID;

	VSCache::iterator vsIter = vsCache_.find(VSID);
	VSShader *vs;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		GenerateVertexShaderDX9(prim, codeBuffer_, useHWTransform);
		vs = new VSShader(codeBuffer_, useHWTransform);

		if (vs->Failed()) {
			ERROR_LOG(HLE, "Shader compilation failed, falling back to software transform");
			osm.Show("hardware transform error - falling back to software", 2.5f, 0xFF3030FF, -1, true);
			delete vs;

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			GenerateVertexShaderDX9(prim, codeBuffer_, false);
			vs = new VSShader(codeBuffer_, false);
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

};
