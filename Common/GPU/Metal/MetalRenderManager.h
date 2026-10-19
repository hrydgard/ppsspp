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
// The emu thread records commands, and they're encoded into Metal command buffers either on a render thread
// or inline when they're submitted (at present and readbacks). Recording keeps all the decisions: render
// passes are started lazily on the first draw (Metal resets all encoder state with each pass, and the load
// actions have to be known up front), and state setters eliminate redundant calls. The replay merges passes
// to the same framebuffer where nothing in between depends on them, and encodes.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// Foundation's ABS macro breaks ARM64XEmitter::ABS, which gets included along with the vertex decoders.
#ifdef ABS
#undef ABS
#endif

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Common/GPU/Shader.h"

// Fixed buffer indices. UBOs use the Vulkan binding number as the Metal buffer index, so they live at the
// bottom. Vertex data goes above all of them.
enum {
	METAL_VERTEX_BUFFER_INDEX = 16,
	// Push constants (only compute shaders have them) go in a buffer of their own, set with setBytes.
	METAL_PUSH_CONSTANT_INDEX = 15,
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
	// With MSAA, what's drawn to. Each pass resolves them into color and depth (sample 0 for depth and stencil),
	// and keeps them for the next one.
	id<MTLTexture> msaaColor;
	id<MTLTexture> msaaDepth;
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

// Compiling MSL takes long (around 100ms the first time), so it's done off the emu thread, on a thread of its own.
void RunOnCompileThread(std::function<void()> work);

// A shader function that's compiled when first needed, from any thread.
@interface MetalShaderFunction : NSObject
- (instancetype)initWithDevice:(id<MTLDevice>)device shader:(const MetalTranslatedShader &)shader tag:(const std::string &)tag;
// Compiles on the first call, other callers wait for it. nil if it failed.
- (id<MTLFunction>)function;
@end

// A render pipeline state that may still be compiling. Recorded commands refer to these, and the replay waits
// for them, so the emu thread never waits for a compile (the same as with Vulkan).
@interface MetalPipelineState : NSObject
- (void)setPipeline:(id<MTLRenderPipelineState>)pipeline;
// Blocks until it's compiled. nil if it failed.
- (id<MTLRenderPipelineState>)wait;
- (BOOL)isReady;
@end

const char *MetalPixelFormatToString(MTLPixelFormat fmt);

enum class MetalCmd : uint8_t {
	BEGIN_PASS,
	END_PASS,
	PIPELINE,
	DEPTH_STENCIL,
	STENCIL_REF,
	BLEND_COLOR,
	CULL,
	WINDING,
	DEPTH_CLIP,
	VIEWPORT,
	SCISSOR,
	VERTEX_BUFFER,
	VERTEX_BUFFER_OFFSET,
	FRAGMENT_BUFFER,
	FRAGMENT_BUFFER_OFFSET,
	FRAGMENT_TEXTURE,
	FRAGMENT_SAMPLER,
	DRAW,
	DRAW_INDEXED,
	COPY,
	DISPATCH,
};

// Objects are unretained. They're kept alive by their owners or the deferred release list until the GPU is
// done with the frame, which implies that the commands have been replayed.
struct MetalCommand {
	MetalCmd cmd;
	uint8_t index;
	uint8_t prim;
	union {
		struct {
			void *obj;
			uint32_t offset;
		} bind;
		uint32_t value;
		float color[4];
		MTLViewport viewport;
		MTLScissorRect scissor;
		struct {
			uint32_t start;
			uint32_t count;
		} draw;
		struct {
			void *buffer;
			uint32_t offset;
			uint32_t count;
		} drawIndexed;
		uint32_t pass;  // Index into the chunk's passes.
		uint32_t copy;  // Index into the chunk's copies.
		uint32_t dispatch;  // Index into the chunk's dispatches.
	};
};

struct MetalRecordedPass {
	void *color;  // Unused for the backbuffer, it's the drawable's.
	void *depth;
	void *msaaColor;
	void *msaaDepth;
	MTLLoadAction colorLoad;
	MTLLoadAction depthLoad;
	MTLLoadAction stencilLoad;
	MTLClearColor clearColor;
	double clearDepth;
	uint32_t clearStencil;
	MTLStoreAction depthStore;
	bool usesDepth;  // Some draw tested or wrote depth or stencil.
	bool depthMemoryless;
	bool backbuffer;
	std::string tag;
};

enum class MetalCopyType : uint8_t {
	TEXTURE_TO_TEXTURE,
	TEXTURE_TO_BUFFER,
	BUFFER_TO_TEXTURE,
	GENERATE_MIPS,
};

struct MetalRecordedCopy {
	MetalCopyType type;
	bool srcIsBackbuffer;  // The source texture is the current drawable.
	void *src;
	void *dst;
	uint32_t srcOffset;  // For buffers.
	uint32_t dstOffset;
	uint32_t bytesPerRow;
	uint32_t bytesPerImage;
	uint32_t srcLevel;
	uint32_t dstLevel;
	uint32_t dstSlice;
	MTLOrigin srcOrigin;
	MTLOrigin dstOrigin;
	MTLSize size;
	MTLBlitOption options;
};

// A compute dispatch. Only used for uploads, so far (texture scaling). Resources go at their Vulkan binding
// numbers, like for draws.
struct MetalRecordedDispatch {
	void *pipeline;  // id<MTLComputePipelineState>
	void *textures[METAL_MAX_TEXTURES];
	void *buffers[METAL_MAX_BUFFERS];
	uint32_t bufferOffsets[METAL_MAX_BUFFERS];
	uint8_t pushConstants[16];
	uint32_t pushConstantSize;
	MTLSize threadgroups;
	MTLSize threadsPerThreadgroup;
};

enum class MetalChunkEnd {
	FLUSH,
	PRESENT,
};

// A render pass or a copy, when the replay looks for passes to merge.
struct MetalStep {
	bool isPass;
	bool merged;  // Merged into an earlier pass.
	uint32_t index;  // Into the chunk's passes or copies.
	// Command ranges, [first, last), not including the pass begin and end. Merging appends more.
	std::vector<std::pair<uint32_t, uint32_t>> ranges;
	void *target;  // Color (or depth, if there's no color) of a pass. Destination of a copy.
	void *targetDepth;
	void *targetMsaa;  // A pass's multisampled color (or depth), which the shared code can read samples from.
	std::vector<void *> reads;  // Textures sampled by the pass, or the copy's source.
	bool usesDepth;
};

// The commands between two submits.
struct MetalCommandChunk {
	std::vector<MetalCommand> commands;
	std::vector<MetalCommand> uploads;  // Run in a command buffer of their own, before the commands.
	std::vector<MetalRecordedPass> passes;
	std::vector<MetalRecordedCopy> copies;
	std::vector<MetalRecordedDispatch> dispatches;
	MetalChunkEnd end = MetalChunkEnd::FLUSH;
	bool vsync = true;
	int newDrawableWidth = 0;  // Non-zero to resize the layer's drawables first.
	int newDrawableHeight = 0;
	dispatch_semaphore_t completed = nullptr;  // Signalled when the GPU is done with the chunk, if set.
	bool exitThread = false;

	void Clear() {
		commands.clear();
		uploads.clear();
		passes.clear();
		copies.clear();
		dispatches.clear();
		end = MetalChunkEnd::FLUSH;
		vsync = true;
		newDrawableWidth = 0;
		newDrawableHeight = 0;
		completed = nullptr;
		exitThread = false;
	}
};

class MetalRenderManager {
public:
	// layer can be nil, in which case we render to an offscreen backbuffer of the given size.
	MetalRenderManager(id<MTLDevice> device, CAMetalLayer *layer, int offscreenWidth, int offscreenHeight, bool useThread);
	~MetalRenderManager();

	id<MTLDevice> Device() const { return device_; }
	id<MTLCommandQueue> Queue() const { return queue_; }
	bool HasUnifiedMemory() const { return unifiedMemory_; }
	bool IsAppleGPU() const { return appleGPU_; }

	void SetInflightFrames(int count);

	// Frame management.
	void BeginFrame();
	void EndFrame();
	void Present(bool vsync);
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
	// The offscreen backbuffer, or nil when rendering to the layer.
	id<MTLTexture> OffscreenBackbuffer() const { return offscreenBackbuffer_; }
	// False until something renders to the backbuffer (headless never does).
	bool BackbufferDrawn() const { return backbufferDrawn_; }

	// Render passes. The pass is started lazily, see above.
	void BindRenderPass(const MetalRenderPassDesc &desc);
	void BindBackbufferPass(MTLLoadAction colorLoad, MTLClearColor clearColor, const char *tag);
	// Clears parts of the current render target, by restarting the pass with clear load actions if needed.
	void Clear(bool color, bool depth, bool stencil, MTLClearColor clearColor, double clearDepth, uint32_t clearStencil);
	// Forget the current render target, ending its pass.
	void UnbindRenderPass();
	bool HasRenderPass() const { return pass_.color != nil || pass_.depth != nil || passIsBackbuffer_; }
	const MetalRenderPassDesc &CurPass() const { return pass_; }
	const MetalRenderTargetFormat &CurTargetFormat() const { return curFormat_; }
	int CurPassWidth() const { return passWidth_; }
	int CurPassHeight() const { return passHeight_; }
	bool IsBackbufferPass() const { return passIsBackbuffer_; }

	// Starts the render pass if needed. Returns false if there's no render target, in which case nothing
	// should be drawn.
	bool EnsureRenderEncoder() {
		if (!inPass_) {
			StartRenderPass();
		}
		return inPass_;
	}
	// Increases every time a new render pass is started (and all encoder state is reset).
	uint32_t EncoderGeneration() const { return encoderGeneration_; }

	// Submits all work so far. If wait is true, waits for the GPU to complete it (for readbacks).
	void Flush(bool wait);

	MetalPushBuffer &Push() { return frames_[curFrame_].push; }

	// Encoder state with redundancy elimination. Only valid after EnsureRenderEncoder() returned true.
	void SetPipeline(__unsafe_unretained MetalPipelineState *pipeline);
	// usesDepthStencil: the state tests or writes depth or stencil. A pass where no draw does gets its
	// depth/stencil attachment neither loaded nor stored.
	void SetDepthStencilState(__unsafe_unretained id<MTLDepthStencilState> state, bool usesDepthStencil);
	void SetStencilRef(uint32_t ref);
	void SetBlendColor(float r, float g, float b, float a);
	void SetCullMode(MTLCullMode mode);
	void SetFrontFacing(MTLWinding winding);
	void SetDepthClip(MTLDepthClipMode mode);
	void SetViewport(const MTLViewport &vp);
	// Clamps to the render target. Returns false if the result is empty, in which case nothing should be drawn.
	bool SetScissor(int x, int y, int w, int h);
	void SetVertexBuffer(__unsafe_unretained id<MTLBuffer> buffer, uint32_t offset, int index);
	void SetFragmentBuffer(__unsafe_unretained id<MTLBuffer> buffer, uint32_t offset, int index);
	void SetFragmentTexture(__unsafe_unretained id<MTLTexture> texture, int index);
	void SetFragmentSampler(__unsafe_unretained id<MTLSamplerState> sampler, int index);

	void Draw(MTLPrimitiveType prim, uint32_t start, uint32_t count);
	void DrawIndexed(MTLPrimitiveType prim, uint32_t count, __unsafe_unretained id<MTLBuffer> indexBuffer, uint32_t offset);

	// Blits. These end the current render pass (it resumes with the next draw). With upload set, the copy
	// goes in the upload command buffer, which runs before everything else in the current submit. Use that
	// only for resources that nothing earlier in the frame has used (like new textures).
	void CopyTexture(__unsafe_unretained id<MTLTexture> src, int srcLevel, MTLOrigin srcOrigin, MTLSize size, __unsafe_unretained id<MTLTexture> dst, int dstLevel, MTLOrigin dstOrigin);
	// src nil means the backbuffer.
	void CopyTextureToBuffer(__unsafe_unretained id<MTLTexture> src, int level, MTLOrigin origin, MTLSize size, __unsafe_unretained id<MTLBuffer> dst, uint32_t offset, uint32_t bytesPerRow, uint32_t bytesPerImage, MTLBlitOption options);
	void CopyBufferToTexture(__unsafe_unretained id<MTLBuffer> src, uint32_t offset, uint32_t bytesPerRow, uint32_t bytesPerImage, MTLSize size, __unsafe_unretained id<MTLTexture> dst, int slice, int level, MTLOrigin origin, MTLBlitOption options, bool upload);
	void GenerateMips(__unsafe_unretained id<MTLTexture> texture, bool upload);
	// Runs in the upload command buffer, in order with the upload copies. Fill in the returned dispatch.
	MetalRecordedDispatch &RecordUploadDispatch();

	const MetalStats &Stats() const { return lastStats_; }

	// A 2D array view of the texture, cached on the texture. For shaders that declare array samplers.
	id<MTLTexture> ArrayView(id<MTLTexture> texture);

private:
	void StartRenderPass();
	void EndRenderPass();
	void RecordEndPass();
	void ResetEncoderState();
	void CreateOffscreenBackbuffer(int width, int height);
	void CreateBackbufferDepth();
	MetalCommand &Record(MetalCmd cmd) {
		chunk_->commands.emplace_back();
		MetalCommand &c = chunk_->commands.back();
		c.cmd = cmd;
		return c;
	}
	MetalRecordedCopy &RecordCopy(bool upload);
	void Submit(MetalChunkEnd end, bool vsync, dispatch_semaphore_t completed);

	// Replay, on the render thread (or inline).
	void RenderThreadFunc();
	void Replay(MetalCommandChunk *chunk);
	void BuildSteps(const MetalCommandChunk &chunk);
	void MergeSteps(MetalCommandChunk &chunk);
	void ReplayPass(const MetalCommandChunk &chunk, MetalStep &step, id<MTLCommandBuffer> cmd);
	void ReplayCopy(const MetalCommandChunk &chunk, const MetalRecordedCopy &copy, id<MTLCommandBuffer> cmd, bool upload);
	void EndReplayEncoders();
	void ReplayDispatch(const MetalRecordedDispatch &dispatch, id<MTLCommandBuffer> cmd);
	id<MTLTexture> ReplayBackbuffer();

	id<MTLDevice> device_;
	id<MTLCommandQueue> queue_;
	CAMetalLayer *layer_ = nil;
	bool unifiedMemory_ = false;
	bool appleGPU_ = false;

	struct FrameData {
		MetalPushBuffer push;
		std::vector<id> deleteList;
	};
	FrameData frames_[METAL_MAX_INFLIGHT_FRAMES];
	dispatch_semaphore_t frameSemaphore_;
	dispatch_semaphore_t flushSemaphore_;
	int inflightFrames_ = METAL_MAX_INFLIGHT_FRAMES;
	int curFrame_ = 0;
	uint64_t frameNumber_ = 0;
	bool inFrame_ = false;

	// Recording state (emu thread).
	MetalCommandChunk *chunk_ = nullptr;
	id<MTLTexture> offscreenBackbuffer_;
	bool backbufferDrawn_ = false;
	id<MTLTexture> bbDepth_;
	int bbWidth_ = 0;
	int bbHeight_ = 0;
	int pendingDrawableWidth_ = 0;
	int pendingDrawableHeight_ = 0;

	MetalRenderPassDesc pass_;
	bool passIsBackbuffer_ = false;
	bool inPass_ = false;
	bool passUsesDepth_ = false;
	size_t recordedPassIndex_ = 0;
	MetalRenderTargetFormat curFormat_;
	int passWidth_ = 0;
	int passHeight_ = 0;
	uint32_t encoderGeneration_ = 0;

	// Cached encoder state.
	struct EncoderState {
		__unsafe_unretained MetalPipelineState *pipeline;
		__unsafe_unretained id<MTLDepthStencilState> depthStencil;
		bool depthStencilUsed;
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
		bool viewportValid;
		bool scissorValid;
		bool cullValid;
		bool windingValid;
		bool depthClipValid;
		bool stencilRefValid;
		bool blendColorValid;
	};
	EncoderState state_{};

	MetalStats stats_{};
	MetalStats lastStats_{};

	// Hand-off between the threads.
	std::thread renderThread_;
	std::mutex queueMutex_;
	std::condition_variable queueCond_;
	std::deque<MetalCommandChunk *> queue_chunks_;
	std::mutex freeMutex_;
	std::vector<MetalCommandChunk *> freeChunks_;

	// Replay state (render thread, or the emu thread when not threaded).
	id<MTLBlitCommandEncoder> rtBlit_;
	id<MTLBlitCommandEncoder> rtUploadBlit_;
	id<MTLComputeCommandEncoder> rtUploadCompute_;
	id<CAMetalDrawable> rtDrawable_;
	// Replay scratch, reused between chunks.
	std::vector<MetalStep> rtSteps_;
	size_t rtStepCount_ = 0;
};
