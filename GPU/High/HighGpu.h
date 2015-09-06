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

// High Level GPU
// Takes an ugly Ge command stream and produces a nice list of draw calls
// that can then be translated and optimized into any desired API easily.

#pragma once

#include <list>
#include <deque>

#include "gfx_es2/fbo.h"

#include "Common/CommonTypes.h"
#include "GPU/GPUCommon.h"
#include "GPU/High/Command.h"

namespace HighGpu {

// This is the consumer of the high level data. This converts the high level drawing commands
// back to OpenGL, DirectX, Vulkan or Metal calls, and makes those calls.
// Later, HighGpuFrontend may become the one and only "Backend" with the current concept,
// and SoftGpu and so on will inherit from this. Although SoftGPU not going through this mechanism
// also makes some sense...
//
// A HighGpu backend has no knowledge of GPUState or gstate_c at all. Its only inputs are CommandPacket
// and the RAM/VRAM of the PSP, and it outputs are graphics API calls and writes to RAM/VRAM of the PSP in
// cases like copying back to memory.
//
// There is no separate DrawEngine, it's integrated into the HighGpuBackend. Or, well, there can be, but
// not mandated.
class HighGpuBackend {
public:
	virtual ~HighGpuBackend() {}
	virtual void Execute(CommandPacket *packet) = 0;
	virtual void DeviceLost() = 0;
	virtual bool ProcessEvent(GPUEvent ev) = 0;
	virtual void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) = 0;

	// TODO: Try to get rid of these
	virtual void UpdateStats() = 0;
	virtual void DoState(PointerWrap &p) = 0;
	virtual void UpdateVsyncInterval(bool force) = 0;
	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) = 0;
};

// A HighGpu frontend does not touch the 3D API at all, it simply interprets display lists and bundles
// the state up into convenient command packets, to be consumed by a HighGpuBackend. The frontend is
// not 3D API-specific.
class HighGpuFrontend : public GPUCommon {
public:
	explicit HighGpuFrontend(HighGpuBackend *backend);
	~HighGpuFrontend();

	void InitClear() override;
	void Reinitialize() override;
	void PreExecuteOp(u32 op, u32 diff) override;
	void ExecuteOp(u32 op, u32 diff) override;

	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;
	void CopyDisplayToOutput() override;

	void BeginFrame() override;
	void UpdateStats() override;
	void InvalidateCache(u32 addr, int size, GPUInvalidationType type) override;
	bool PerformMemoryCopy(u32 dest, u32 src, int size) override;
	bool PerformMemorySet(u32 dest, u8 v, int size) override;
	bool PerformMemoryDownload(u32 dest, int size) override;
	bool PerformMemoryUpload(u32 dest, int size) override;
	bool PerformStencilUpload(u32 dest, int size) override;
	void ClearCacheNextFrame() override;
	void DeviceLost() override;  // Only happens on Android. Drop all textures and shaders.

	void DumpNextFrame() override;
	void DoState(PointerWrap &p) override;

	// Called by the window system if the window size changed. This will be reflected in PSPCoreParam.pixel*.
	void Resized() override;
	void ClearShaderCache() override;
	void CleanupBeforeUI() override;
	bool DecodeTexture(u8 *dest, const GPUgstate &state) override {
		return false;
	}
	bool FramebufferDirty() override;
	bool FramebufferReallyDirty() override;

	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override;
	std::vector<FramebufferInfo> GetFramebufferList();

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer);
	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level);
	static bool GetDisplayFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices);

	bool DescribeCodePtr(const u8 *ptr, std::string &name) override;

	typedef void (HighGpuFrontend::*CmdFunc)(u32 op, u32 diff);
	struct CommandInfo {
		HighGpuFrontend::CmdFunc func;
		u32 dirtyState;
	};

	void Execute_Vaddr(u32 op, u32 diff);
	void Execute_Iaddr(u32 op, u32 diff);
	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_BoundingBox(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);
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

	void SyncEnd(GPUSyncType waitType, int listid, bool wokeThreads) override;

protected:
	void FastRunLoop(DisplayList &list) override;
	void ProcessEvent(GPUEvent ev) override;
	void FastLoadBoneMatrix(u32 target) override;
	void LoadClut();

private:
	// This happens a lot more seldomly than the old flush in the other backends.
	// Only on a drawsync, end of major displaylist (though not currently) or on buffer full.
	void FlushCommandPacket();

	void DoBlockTransfer();
	void ApplyDrawState(int prim);

	static CommandInfo cmdInfo_[256];

	bool resized_;
	int lastVsync_;

	u32 dirty_;

	HighGpuBackend *backend_;
	CommandPacket *cmdPacket_;
	MemoryArena arena_;

	// This is used to diff the first draw in a packet against.
	// TODO: Is is better to eliminate it through adding extra logic?
	Command dummyDraw_;

	// The CLUT no longer lives in the texture cache. It doesn't belong there, more like in the gstate together
	// with the matrices, as it's a similar kind of state. But it'll have to stay here until we remove all old
	// style backends, then we can move it to gstate.
	u8 *clutData_;
	u32 clutTotalBytes_;
	u32 clutMaxBytes_;
};

}  // namespace
