#pragma once

#include "GPU/High/HighGpu.h"

// We reuse some subsystems of the regular GL backend.
#include "GPU/GLES/DepalettizeShader.h"
#include "GPU/GLES/FragmentTestCache.h"
#include "GPU/Common/FramebufferCommon.h"

class FramebufferManager;

namespace HighGpu {

class ShaderManagerGLES;
class TextureCacheGLES;

class HighGpu_GLES : public HighGpuBackend {
public:
	HighGpu_GLES();
	~HighGpu_GLES();

	void Execute(CommandPacket *packet) override;
	void DeviceLost() override;
	bool ProcessEvent(GPUEvent ev) override;
	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override;
	void UpdateStats() override;
	void DoState(PointerWrap &p) override;
	void UpdateVsyncInterval(bool force) override;
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;

private:
	void BuildReportingInfo();
	void InitClearInternal();
	void BeginFrameInternal();
	void CopyDisplayToOutputInternal();
	void PerformMemoryCopyInternal(u32 dest, u32 src, int size);
	void PerformMemorySetInternal(u32 dest, u8 v, int size);
	void PerformStencilUploadInternal(u32 dest, int size);
	void InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type);
	void ReinitializeInternal();

	void ApplyFramebuffer(const CommandPacket *cmdPacket, const Command *cmd);

	FramebufferManager *framebufferManager_;
	FragmentTestCache fragmentTestCache_;
	DepalShaderCache depalShaderCache_;
	ShaderManagerGLES *shaderManager_;
	TextureCacheGLES *textureCache_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;

	bool resized_;
	// Should this dump decoded draw commands?
	bool dumpNextFrame_;
	bool dumpThisFrame_;
};

}  // namespace
