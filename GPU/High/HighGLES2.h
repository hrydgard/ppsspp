#pragma once

#include "GPU/High/HighGpu.h"

// We reuse some subsystems of the regular GL backend.
#include "GPU/GLES/DepalettizeShader.h"
#include "GPU/GLES/FragmentTestCache.h"
#include "GPU/Common/FramebufferCommon.h"

namespace HighGpu {

class ShaderManagerGLES {
public:
	void ClearCache(bool);
	void DirtyShader();
	void DirtyLastShader();
};

class FramebufferManagerGLES : public FramebufferManagerCommon {
public:
	void Init();
	void BeginFrame();
	void EndFrame();
	void DestroyAllFBOs();
	void DeviceLost();
	void Resized();
	void CopyDisplayToOutput();
};

class TextureCacheGLES {
public:
	int NumLoadedTextures();
	bool Clear(bool x);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
};

class HighGPU_GLES : public HighGpuBackend {
public:
	HighGPU_GLES();
	~HighGPU_GLES();

	void Execute(CommandPacket *packet) override;
	void DeviceLost() override;
	bool ProcessEvent(GPUEvent ev) override;
	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override;
	void UpdateStats() override;
	void DoState(PointerWrap &p) override;
	void UpdateVsyncInterval(bool force) override;

private:
	void InitClearInternal();
	void BeginFrameInternal();
	void CopyDisplayToOutputInternal();
	void PerformMemoryCopyInternal(u32 dest, u32 src, int size);
	void PerformMemorySetInternal(u32 dest, u8 v, int size);
	void PerformStencilUploadInternal(u32 dest, int size);
	void InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type);
	void ReinitializeInternal();

	void BuildReportingInfo();

	FramebufferManagerGLES *framebufferManager_;
	FragmentTestCache fragmentTestCache_;
	DepalShaderCache depalShaderCache_;
	ShaderManagerGLES *shaderManager_;
	TextureCacheGLES *textureCache_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;

	bool resized_;
	bool dumpNextFrame_;
	bool dumpThisFrame_;
};

}  // namespace
