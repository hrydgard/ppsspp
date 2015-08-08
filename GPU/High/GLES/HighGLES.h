#pragma once

// TODO: Find a more efficient replacement, it's not as slow as map but pretty slow
#include <unordered_map>

#include "GPU/High/HighGpu.h"

// We reuse some subsystems of the regular GL backend.
#include "GPU/GLES/DepalettizeShader.h"
#include "GPU/GLES/FragmentTestCache.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"

class FramebufferManager;
class VertexDecoder;
class VertexDecoderJitCache;

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
	VertexDecoder *GetVertexDecoder(u32 vtype);

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

	// TODO: Eliminate and map GL buffers directly.
	u8 *vertexDecodeBuf_;
	u8 *indexDecodeBuf_;

	std::unordered_map<u32, VertexDecoder *> decoderMap_;
	VertexDecoder *dec_;
	VertexDecoderJitCache *decJitCache_;
	VertexDecoderOptions decOptions_;
};

}  // namespace
