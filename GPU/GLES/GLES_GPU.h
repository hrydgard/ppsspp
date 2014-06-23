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

#pragma once

#include <list>
#include <deque>

#include "gfx_es2/fbo.h"

#include "GPU/GPUCommon.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/TransformPipeline.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/DepalettizeShader.h"

class ShaderManager;
class LinkedShader;

class GLES_GPU : public GPUCommon {
public:
	GLES_GPU();
	~GLES_GPU();
	virtual void InitClear();
	virtual void PreExecuteOp(u32 op, u32 diff);
	void ExecuteOpInternal(u32 op, u32 diff);
	virtual void ExecuteOp(u32 op, u32 diff);

	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format);
	virtual void CopyDisplayToOutput();
	virtual void BeginFrame();
	virtual void UpdateStats();
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type);
	virtual bool PerformMemoryCopy(u32 dest, u32 src, int size);
	virtual bool PerformMemorySet(u32 dest, u8 v, int size);
	virtual bool PerformMemoryDownload(u32 dest, int size);
	virtual bool PerformMemoryUpload(u32 dest, int size);
	virtual bool PerformStencilUpload(u32 dest, int size);
	virtual void ClearCacheNextFrame();
	virtual void DeviceLost();  // Only happens on Android. Drop all textures and shaders.

	virtual void DumpNextFrame();
	virtual void DoState(PointerWrap &p);

	// Called by the window system if the window size changed. This will be reflected in PSPCoreParam.pixel*.
	virtual void Resized();
	virtual void ClearShaderCache();
	virtual void CleanupBeforeUI();
	virtual bool DecodeTexture(u8* dest, GPUgstate state) {
		return textureCache_.DecodeTexture(dest, state);
	}
	virtual bool FramebufferDirty();
	virtual bool FramebufferReallyDirty();

	virtual void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) {
		primaryInfo = reportingPrimaryInfo_;
		fullInfo = reportingFullInfo_;
	}
	std::vector<FramebufferInfo> GetFramebufferList();

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level);
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	virtual bool DescribeCodePtr(const u8 *ptr, std::string &name);

	typedef void (GLES_GPU::*CmdFunc)(u32 op, u32 diff);
	struct CommandInfo {
		u8 flags;
		GLES_GPU::CmdFunc func;
	};

	void Execute_Vaddr(u32 op, u32 diff);
	void Execute_Iaddr(u32 op, u32 diff);
	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_BoundingBox(u32 op, u32 diff);
	void Execute_VertexType(u32 op, u32 diff);
	void Execute_Region(u32 op, u32 diff);
	void Execute_Scissor(u32 op, u32 diff);
	void Execute_FramebufType(u32 op, u32 diff);
	void Execute_ViewportType(u32 op, u32 diff);
	void Execute_TexScaleU(u32 op, u32 diff);
	void Execute_TexScaleV(u32 op, u32 diff);
	void Execute_TexOffsetU(u32 op, u32 diff);
	void Execute_TexOffsetV(u32 op, u32 diff);
	void Execute_TexAddr0(u32 op, u32 diff);
	void Execute_TexAddrN(u32 op, u32 diff);
	void Execute_TexBufw0(u32 op, u32 diff);
	void Execute_TexBufwN(u32 op, u32 diff);
	void Execute_TexSize0(u32 op, u32 diff);
	void Execute_TexSizeN(u32 op, u32 diff);
	void Execute_TexFormat(u32 op, u32 diff);
	void Execute_TexMapMode(u32 op, u32 diff);
	void Execute_TexParamType(u32 op, u32 diff);
	void Execute_TexEnvColor(u32 op, u32 diff);
	void Execute_TexLevel(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);
	void Execute_ClutFormat(u32 op, u32 diff);
	void Execute_Ambient(u32 op, u32 diff);
	void Execute_MaterialDiffuse(u32 op, u32 diff);
	void Execute_MaterialEmissive(u32 op, u32 diff);
	void Execute_MaterialAmbient(u32 op, u32 diff);
	void Execute_MaterialSpecular(u32 op, u32 diff);
	void Execute_Light0Param(u32 op, u32 diff);
	void Execute_Light1Param(u32 op, u32 diff);
	void Execute_Light2Param(u32 op, u32 diff);
	void Execute_Light3Param(u32 op, u32 diff);
	void Execute_FogColor(u32 op, u32 diff);
	void Execute_FogCoef(u32 op, u32 diff);
	void Execute_ColorTestMask(u32 op, u32 diff);
	void Execute_AlphaTest(u32 op, u32 diff);
	void Execute_StencilTest(u32 op, u32 diff);
	void Execute_ColorRef(u32 op, u32 diff);
	void Execute_WorldMtxNum(u32 op, u32 diff);
	void Execute_WorldMtxData(u32 op, u32 diff);
	void Execute_ViewMtxNum(u32 op, u32 diff);
	void Execute_ViewMtxData(u32 op, u32 diff);
	void Execute_ProjMtxNum(u32 op, u32 diff);
	void Execute_ProjMtxData(u32 op, u32 diff);
	void Execute_TgenMtxNum(u32 op, u32 diff);
	void Execute_TgenMtxData(u32 op, u32 diff);
	void Execute_BoneMtxNum(u32 op, u32 diff);
	void Execute_BoneMtxData(u32 op, u32 diff);
	void Execute_BlockTransferStart(u32 op, u32 diff);

protected:
	virtual void FastRunLoop(DisplayList &list);
	virtual void ProcessEvent(GPUEvent ev);
	virtual void FastLoadBoneMatrix(u32 target);

private:
	void Flush() {
		transformDraw_.Flush();
	}
	void DoBlockTransfer();
	void ApplyDrawState(int prim);
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();
	void InitClearInternal();
	void BeginFrameInternal();
	void CopyDisplayToOutputInternal();
	void PerformMemoryCopyInternal(u32 dest, u32 src, int size);
	void PerformMemorySetInternal(u32 dest, u8 v, int size);
	void PerformStencilUploadInternal(u32 dest, int size);
	void InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type);
	inline void UpdateVsyncInterval(bool force);

	static CommandInfo cmdInfo_[256];

	FramebufferManager framebufferManager_;
	TextureCache textureCache_;
	DepalShaderCache depalShaderCache_;
	TransformDrawEngine transformDraw_;
	ShaderManager *shaderManager_;

	bool resized_;
	int lastVsync_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;
};
