#pragma once

#include "GPU/High/HighGpu.h"
#include "GPU/GLES/TextureCache.h"

namespace HighGpu {

class HighGPU_GLES : public HighGpuBackend {
public:
	HighGPU_GLES();
	~HighGPU_GLES();

	void Execute(CommandPacket *packet, int start, int end) override;
	void DeviceLost() override;
	void ProcessEvent(GPUEvent ev) override;
	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override;

private:
	void InitClearInternal();
	void BeginFrameInternal();
	void CopyDisplayToOutputInternal();
	void PerformMemoryCopyInternal(u32 dest, u32 src, int size);
	void PerformMemorySetInternal(u32 dest, u8 v, int size);
	void PerformStencilUploadInternal(u32 dest, int size);
	void InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type);
	void ReinitializeInternal();

	FramebufferManager framebufferManager_;
	DepalShaderCache depalShaderCache_;
	FragmentTestCache fragmentTestCache_;
};

}  // namespace
