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

// Objective-C++ only, and requires ARC. Shared by thin3d_metal.mm and the GPU/Metal backend.
//
// Metal resets all encoder state at the start of each render pass, and a pass can only be started
// once we know its attachments and load actions. So render passes are started lazily, on the first
// draw (or when a pass with clears is abandoned), and everything that sets encoder state goes
// through the redundancy-eliminating setters here, so that thin3d and the GPU backend can share an
// encoder without stepping on each other's cached state.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// Foundation's ABS macro breaks ARM64XEmitter::ABS, which gets included along with the vertex decoders.
#ifdef ABS
#undef ABS
#endif

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include "Common/GPU/Shader.h"

// Fixed buffer indices. UBOs (and the push-constant style data) use the Vulkan binding number as the
// Metal buffer index, so they live at the bottom. Vertex data goes above all of them.
enum {
	METAL_VERTEX_BUFFER_INDEX = 16,
	METAL_MAX_TEXTURES = 8,
	METAL_MAX_BUFFERS = 8,
};

constexpr int METAL_MAX_INFLIGHT_FRAMES = 3;

struct MetalPushAlloc {
	__unsafe_unretained id<MTLBuffer> buffer = nil;  // Owned by the push buffer.
	uint32_t offset = 0;
	uint8_t *ptr = nullptr;
};

// Linear allocator for per-frame data (vertices, indices, uniforms, upload staging).
// Grows by adding blocks. Reset once the GPU is done with the frame.
class MetalPushBuffer {
public:
	void Init(id<MTLDevice> device, size_t blockSize, const char *tag);
	void Destroy();
	void Reset();

	MetalPushAlloc Allocate(size_t size, size_t align);
	MetalPushAlloc Push(const void *data, size_t size, size_t align) {
		MetalPushAlloc alloc = Allocate(size, align);
		memcpy(alloc.ptr, data, size);
		return alloc;
	}
	size_t TotalSize() const;

private:
	struct Block {
		id<MTLBuffer> buffer;
		uint8_t *ptr;
		size_t size;
	};
	id<MTLDevice> device_;
	std::vector<Block> blocks_;
	size_t blockSize_ = 0;
	int curBlock_ = 0;
	size_t offset_ = 0;
	std::string tag_;
};

struct MetalRenderPassDesc {
	id<MTLTexture> color;
	id<MTLTexture> depth;  // Has stencil too, when it's a combined format.
	MTLLoadAction colorLoad = MTLLoadActionLoad;
	MTLLoadAction depthLoad = MTLLoadActionLoad;
	MTLLoadAction stencilLoad = MTLLoadActionLoad;
	MTLClearColor clearColor{};
	double clearDepth = 0.0;
	uint32_t clearStencil = 0;
	bool depthMemoryless = false;
	const char *tag = nullptr;
};

// Pipeline-relevant formats of the current render target.
struct MetalRenderTargetFormat {
	MTLPixelFormat color = MTLPixelFormatInvalid;
	MTLPixelFormat depth = MTLPixelFormatInvalid;
	MTLPixelFormat stencil = MTLPixelFormatInvalid;
	int sampleCount = 1;

	uint32_t Key() const {
		return (uint32_t)color | ((uint32_t)depth << 10) | ((uint32_t)stencil << 20) | ((uint32_t)(sampleCount - 1) << 30);
	}
};

struct MetalStats {
	int renderPasses;
	int blitPasses;
	int flushes;
	int draws;
};

// The result of translating a Vulkan GLSL shader to MSL.
struct MetalTranslatedShader {
	std::string msl;
	std::string entryPoint;
	// Bit n is set if texture index n is declared as an array texture (texture2d_array).
	uint32_t arrayTextureMask = 0;
	// Texture indices used at all.
	uint32_t textureMask = 0;
	// Buffer indices used.
	uint32_t bufferMask = 0;
	// Fragment shaders: the shader reads the framebuffer (programmable blending).
	bool readsFramebuffer = false;
};

bool TranslateVulkanGLSLToMSL(ShaderStage stage, const char *glsl, MetalTranslatedShader *out, std::string *errorMessage);
id<MTLFunction> CreateMetalFunction(id<MTLDevice> device, const MetalTranslatedShader &shader, const char *tag, std::string *errorMessage);

const char *MetalPixelFormatToString(MTLPixelFormat fmt);

class MetalRenderManager {
public:
	// layer can be nil, in which case we render to an offscreen backbuffer of the given size.
	MetalRenderManager(id<MTLDevice> device, CAMetalLayer *layer, int offscreenWidth, int offscreenHeight);
	~MetalRenderManager();

	id<MTLDevice> Device() const { return device_; }
	id<MTLCommandQueue> Queue() const { return queue_; }
	CAMetalLayer *Layer() const { return layer_; }
	bool HasUnifiedMemory() const { return unifiedMemory_; }
	bool IsAppleGPU() const { return appleGPU_; }

	void SetInflightFrames(int count);
	int GetInflightFrames() const { return inflightFrames_; }

	// Frame management.
	void BeginFrame();
	void EndFrame();
	void Present(bool vsync);
	int CurFrame() const { return curFrame_; }
	uint64_t FrameNumber() const { return frameNumber_; }

	// Waits for all submitted work to complete.
	void WaitIdle();

	// Command buffers don't retain what they use (that costs a retain and release per binding), so
	// anything that may still be in use by the GPU has to be released through this. Keeps the object
	// alive until the GPU is done with the current frame.
	void DeferRelease(id object) {
		if (object) {
			frames_[curFrame_].deleteList.push_back(object);
		}
	}

	// Backbuffer
	void Resize(int width, int height);
	int BackbufferWidth() const { return bbWidth_; }
	int BackbufferHeight() const { return bbHeight_; }
	MTLPixelFormat BackbufferFormat() const { return MTLPixelFormatBGRA8Unorm; }
	// Acquires a drawable if needed. Can return nil if none is available.
	id<MTLTexture> BackbufferTexture();
	id<MTLTexture> BackbufferDepth();

	// Render passes. The pass is started lazily, see above.
	void BindRenderPass(const MetalRenderPassDesc &desc);
	// Clears parts of the current render target, by restarting the pass with clear load actions if needed.
	void Clear(bool color, bool depth, bool stencil, MTLClearColor clearColor, double clearDepth, uint32_t clearStencil);
	// Forget the current render target, ending its pass.
	void UnbindRenderPass();
	bool HasRenderPass() const { return pass_.color != nil || pass_.depth != nil; }
	const MetalRenderPassDesc &CurPass() const { return pass_; }
	const MetalRenderTargetFormat &CurTargetFormat() const { return curFormat_; }
	int CurPassWidth() const { return passWidth_; }
	int CurPassHeight() const { return passHeight_; }
	bool IsBackbufferPass() const { return passIsBackbuffer_; }
	void BindBackbufferPass(MTLLoadAction colorLoad, MTLClearColor clearColor, const char *tag);

	// Starts the render pass if needed. Returns nil if there's no render target (or no drawable).
	id<MTLRenderCommandEncoder> RenderEncoder();
	// Same, for the per-draw paths: afterwards, encoder is the current render encoder. Avoids ARC traffic.
	bool EnsureRenderEncoder() {
		if (!encoder) {
			RenderEncoder();
		}
		return encoder != nil;
	}
	// The current render encoder, or nil. Owned by the render manager.
	__unsafe_unretained id<MTLRenderCommandEncoder> encoder = nil;
	// Increases every time a new render encoder is started (and all encoder state is reset).
	uint32_t EncoderGeneration() const { return encoderGeneration_; }

	// Ends any render pass. The pass can be resumed (with load actions) by the next draw.
	id<MTLBlitCommandEncoder> BlitEncoder();
	// Blit encoder in the upload command buffer, which runs before everything in the current command buffer.
	// Use only for resources that the current frame hasn't touched yet (like new textures).
	id<MTLBlitCommandEncoder> UploadEncoder();

	void EndEncoders();

	// Submits all work so far. If wait is true, waits for it to complete (for readbacks).
	void Flush(bool wait);

	MetalPushBuffer &Push() { return frames_[curFrame_].push; }

	// Encoder state with redundancy elimination. Only valid while RenderEncoder() is non-nil.
	void SetPipeline(__unsafe_unretained id<MTLRenderPipelineState> pipeline);
	void SetDepthStencilState(__unsafe_unretained id<MTLDepthStencilState> state);
	void SetStencilRef(uint32_t ref);
	void SetBlendColor(float r, float g, float b, float a);
	void SetCullMode(MTLCullMode mode);
	void SetFrontFacing(MTLWinding winding);
	void SetDepthClip(MTLDepthClipMode mode);
	void SetViewport(const MTLViewport &vp);
	// Clamps to the render target. Returns false if the result is empty, in which case nothing should be drawn.
	bool SetScissor(int x, int y, int w, int h);
	void SetVertexBuffer(__unsafe_unretained id<MTLBuffer> buffer, uint32_t offset, int index);
	void SetVertexBytes(const void *data, size_t size, int index);
	void SetFragmentBuffer(__unsafe_unretained id<MTLBuffer> buffer, uint32_t offset, int index);
	void SetFragmentBytes(const void *data, size_t size, int index);
	void SetFragmentTexture(__unsafe_unretained id<MTLTexture> texture, int index);
	void SetFragmentSampler(__unsafe_unretained id<MTLSamplerState> sampler, int index);
	void SetVertexTexture(__unsafe_unretained id<MTLTexture> texture, int index);
	void SetVertexSampler(__unsafe_unretained id<MTLSamplerState> sampler, int index);

	// Called when a new render encoder starts.
	void SetEncoderResetCallback(std::function<void()> callback) { encoderResetCallback_ = callback; }

	const MetalStats &Stats() const { return lastStats_; }

	// A 2D array view of the texture, cached on the texture. For shaders that declare array samplers.
	id<MTLTexture> ArrayView(id<MTLTexture> texture);

private:
	void StartRenderEncoder();
	void EndRenderEncoder();
	void EndBlitEncoder();
	void NewCommandBuffers();
	void ResetEncoderState();
	void AcquireDrawable();
	void CreateOffscreenBackbuffer(int width, int height);
	void CreateBackbufferDepth();

	id<MTLDevice> device_;
	id<MTLCommandQueue> queue_;
	CAMetalLayer *layer_ = nil;
	bool unifiedMemory_ = false;
	bool appleGPU_ = false;

	struct FrameData {
		MetalPushBuffer push;
		std::vector<id> deleteList;
		bool hasPendingWork = false;
	};
	FrameData frames_[METAL_MAX_INFLIGHT_FRAMES];
	dispatch_semaphore_t frameSemaphore_;
	int inflightFrames_ = METAL_MAX_INFLIGHT_FRAMES;
	int curFrame_ = 0;
	uint64_t frameNumber_ = 0;
	bool inFrame_ = false;

	id<MTLCommandBuffer> cmd_;
	id<MTLCommandBuffer> uploadCmd_;
	id<MTLRenderCommandEncoder> renderEncoder_;
	id<MTLBlitCommandEncoder> blitEncoder_;
	id<MTLBlitCommandEncoder> uploadEncoder_;
	id<MTLCommandBuffer> lastCommitted_;

	id<CAMetalDrawable> drawable_;
	id<MTLTexture> offscreenBackbuffer_;
	id<MTLTexture> bbDepth_;
	int bbWidth_ = 0;
	int bbHeight_ = 0;

	MetalRenderPassDesc pass_;
	bool passIsBackbuffer_ = false;
	MetalRenderTargetFormat curFormat_;
	int passWidth_ = 0;
	int passHeight_ = 0;
	uint32_t encoderGeneration_ = 0;

	// Cached encoder state.
	struct EncoderState {
		__unsafe_unretained id<MTLRenderPipelineState> pipeline;
		__unsafe_unretained id<MTLDepthStencilState> depthStencil;
		uint32_t stencilRef;
		float blendColor[4];
		MTLCullMode cull;
		MTLWinding winding;
		MTLDepthClipMode depthClip;
		MTLViewport viewport;
		MTLScissorRect scissor;
		__unsafe_unretained id<MTLBuffer> vertexBuffers[METAL_VERTEX_BUFFER_INDEX + 1];
		uint32_t vertexBufferOffsets[METAL_VERTEX_BUFFER_INDEX + 1];
		__unsafe_unretained id<MTLBuffer> fragmentBuffers[METAL_MAX_BUFFERS];
		uint32_t fragmentBufferOffsets[METAL_MAX_BUFFERS];
		__unsafe_unretained id<MTLTexture> fragmentTextures[METAL_MAX_TEXTURES];
		__unsafe_unretained id<MTLSamplerState> fragmentSamplers[METAL_MAX_TEXTURES];
		__unsafe_unretained id<MTLTexture> vertexTextures[METAL_MAX_TEXTURES];
		__unsafe_unretained id<MTLSamplerState> vertexSamplers[METAL_MAX_TEXTURES];
		bool viewportValid;
		bool scissorValid;
		bool cullValid;
		bool windingValid;
		bool depthClipValid;
		bool stencilRefValid;
		bool blendColorValid;
	};
	EncoderState state_{};

	std::function<void()> encoderResetCallback_;

	MetalStats stats_{};
	MetalStats lastStats_{};
};
