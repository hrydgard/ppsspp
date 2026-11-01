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

#include "ppsspp_config.h"

#include <algorithm>
#include <objc/runtime.h>

#include "Common/GPU/Metal/MetalRenderManager.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Thread/ThreadManager.h"

#include "ext/SPIRV-Cross/spirv_msl.hpp"

void MetalPushBuffer::Init(id<MTLDevice> device, size_t blockSize, const char *tag) {
	device_ = device;
	blockSize_ = blockSize;
	tag_ = tag;
	curBlock_ = 0;
	offset_ = 0;
}

void MetalPushBuffer::Destroy() {
	blocks_.clear();
	device_ = nil;
}

void MetalPushBuffer::Reset() {
	// Keep a single block around if we grew, merge the rest away to save memory.
	if (blocks_.size() > 1) {
		size_t total = TotalSize();
		blocks_.clear();
		blockSize_ = std::max(blockSize_, total);
	}
	curBlock_ = 0;
	offset_ = 0;
}

size_t MetalPushBuffer::TotalSize() const {
	size_t total = 0;
	for (const Block &block : blocks_) {
		total += block.size;
	}
	return total;
}

MetalPushAlloc MetalPushBuffer::Allocate(size_t size, size_t align) {
	// The vertex decoder can write a bit past the end of its output (see VertexDecoder::DecodeVerts), so keep
	// some room after every allocation, like the Vulkan backend's push pool does.
	const size_t slack = 256;
	while (true) {
		if (curBlock_ < (int)blocks_.size()) {
			Block &block = blocks_[curBlock_];
			size_t offset = (offset_ + align - 1) & ~(align - 1);
			if (offset + size + slack <= block.size) {
				offset_ = offset + size;
				MetalPushAlloc alloc;
				alloc.buffer = block.buffer;
				alloc.offset = (uint32_t)offset;
				alloc.ptr = block.ptr + offset;
				return alloc;
			}
			curBlock_++;
			offset_ = 0;
			continue;
		}
		Block block;
		block.size = std::max(blockSize_, size + slack);
		block.buffer = [device_ newBufferWithLength:block.size options:MTLResourceStorageModeShared];
		_assert_msg_(block.buffer, "Failed to allocate push buffer block of %d bytes", (int)block.size);
		block.buffer.label = [NSString stringWithUTF8String:tag_.c_str()];
		block.ptr = (uint8_t *)block.buffer.contents;
		blocks_.push_back(block);
		curBlock_ = (int)blocks_.size() - 1;
		offset_ = 0;
	}
}

const char *MetalPixelFormatToString(MTLPixelFormat fmt) {
	switch (fmt) {
	case MTLPixelFormatInvalid: return "Invalid";
	case MTLPixelFormatRGBA8Unorm: return "RGBA8Unorm";
	case MTLPixelFormatBGRA8Unorm: return "BGRA8Unorm";
	case MTLPixelFormatR8Unorm: return "R8Unorm";
	case MTLPixelFormatDepth32Float: return "Depth32Float";
	case MTLPixelFormatDepth32Float_Stencil8: return "Depth32Float_Stencil8";
	case MTLPixelFormatStencil8: return "Stencil8";
	default: return "(other)";
	}
}

bool TranslateVulkanGLSLToMSL(ShaderStage stage, const char *glsl, MetalTranslatedShader *out, std::string *errorMessage) {
	VkShaderStageFlagBits vkStage;
	spv::ExecutionModel model;
	switch (stage) {
	case ShaderStage::Vertex: vkStage = VK_SHADER_STAGE_VERTEX_BIT; model = spv::ExecutionModelVertex; break;
	case ShaderStage::Fragment: vkStage = VK_SHADER_STAGE_FRAGMENT_BIT; model = spv::ExecutionModelFragment; break;
	case ShaderStage::Compute: vkStage = VK_SHADER_STAGE_COMPUTE_BIT; model = spv::ExecutionModelGLCompute; break;
	default: return false;
	}

	std::vector<uint32_t> spirv;
	if (!GLSLtoSPV(vkStage, glsl, GLSLVariant::VULKAN, spirv, errorMessage)) {
		return false;
	}

	try {
		spirv_cross::CompilerMSL msl(std::move(spirv));
		spirv_cross::CompilerMSL::Options options = msl.get_msl_options();
#if PPSSPP_PLATFORM(IOS)
		options.platform = spirv_cross::CompilerMSL::Options::iOS;
#else
		options.platform = spirv_cross::CompilerMSL::Options::macOS;
#endif
		// macOS 12 and iOS 15 are the minimum, and both have MSL 2.3.
		options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 3);
		options.pad_fragment_output_components = true;
		// Point size is only written for point primitives, which we don't draw (they're expanded to quads).
		options.enable_point_size_builtin = false;
		options.use_framebuffer_fetch_subpasses = true;
		msl.set_msl_options(options);

		spirv_cross::CompilerGLSL::Options common = msl.get_common_options();
		// Vulkan's clip space has y pointing down, Metal's has it pointing up. Both have the framebuffer origin
		// at the top left, so flipping in the vertex shader makes everything else match Vulkan exactly.
		common.vertex.flip_vert_y = true;
		msl.set_common_options(common);

		spirv_cross::ShaderResources resources = msl.get_shader_resources();
		out->arrayTextureMask = 0;
		out->textureMask = 0;
		out->bufferMask = 0;

		for (const auto &resource : resources.uniform_buffers) {
			spirv_cross::MSLResourceBinding binding;
			binding.stage = model;
			binding.desc_set = msl.get_decoration(resource.id, spv::DecorationDescriptorSet);
			binding.binding = msl.get_decoration(resource.id, spv::DecorationBinding);
			binding.msl_buffer = binding.binding;
			msl.add_msl_resource_binding(binding);
			out->bufferMask |= 1 << binding.binding;
		}
		for (const auto &resource : resources.storage_buffers) {
			spirv_cross::MSLResourceBinding binding;
			binding.stage = model;
			binding.desc_set = msl.get_decoration(resource.id, spv::DecorationDescriptorSet);
			binding.binding = msl.get_decoration(resource.id, spv::DecorationBinding);
			binding.msl_buffer = binding.binding;
			msl.add_msl_resource_binding(binding);
			out->bufferMask |= 1 << binding.binding;
		}
		auto addImage = [&](const spirv_cross::Resource &resource) {
			spirv_cross::MSLResourceBinding binding;
			binding.stage = model;
			binding.desc_set = msl.get_decoration(resource.id, spv::DecorationDescriptorSet);
			binding.binding = msl.get_decoration(resource.id, spv::DecorationBinding);
			binding.msl_texture = binding.binding;
			binding.msl_sampler = binding.binding;
			msl.add_msl_resource_binding(binding);
			const spirv_cross::SPIRType &type = msl.get_type(resource.type_id);
			if (type.image.arrayed) {
				out->arrayTextureMask |= 1 << binding.binding;
			}
			out->textureMask |= 1 << binding.binding;
		};
		for (const auto &resource : resources.sampled_images) {
			addImage(resource);
		}
		for (const auto &resource : resources.storage_images) {
			addImage(resource);
		}
		if (!resources.push_constant_buffers.empty()) {
			spirv_cross::MSLResourceBinding binding;
			binding.stage = model;
			binding.desc_set = spirv_cross::kPushConstDescSet;
			binding.binding = spirv_cross::kPushConstBinding;
			binding.msl_buffer = METAL_PUSH_CONSTANT_INDEX;
			msl.add_msl_resource_binding(binding);
		}
		out->readsFramebuffer = !resources.subpass_inputs.empty();

		out->msl = msl.compile();
		out->entryPoint = msl.get_cleansed_entry_point_name("main", model);
	} catch (const std::exception &e) {
		*errorMessage = StringFromFormat("SPIRV-Cross MSL error: %s", e.what());
		return false;
	}
	return true;
}

id<MTLFunction> CreateMetalFunction(id<MTLDevice> device, const MetalTranslatedShader &shader, const char *tag, std::string *errorMessage) {
	MTLCompileOptions *options = [MTLCompileOptions new];
	options.languageVersion = MTLLanguageVersion2_3;
	// Multipass effects depend on identical positions from different vertex shaders (equal depth tests).
	options.preserveInvariance = YES;
	NSError *error = nil;
	id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:shader.msl.c_str()] options:options error:&error];
	if (!library) {
		*errorMessage = StringFromFormat("MSL compile failed (%s): %s", tag, error ? [[error localizedDescription] UTF8String] : "?");
		return nil;
	}
	id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:shader.entryPoint.c_str()]];
	if (!function) {
		*errorMessage = StringFromFormat("Entry point %s not found (%s)", shader.entryPoint.c_str(), tag);
		return nil;
	}
	function.label = [NSString stringWithUTF8String:tag];
	return function;
}

class MetalCompileTask : public Task {
public:
	MetalCompileTask(std::function<void()> work) : work_(std::move(work)) {}
	TaskType Type() const override { return TaskType::DEDICATED_THREAD; }
	TaskPriority Priority() const override { return TaskPriority::NORMAL; }
	void Run() override { @autoreleasepool { work_(); } }

private:
	std::function<void()> work_;
};

void RunOnCompileThread(std::function<void()> work) {
	g_threadManager.EnqueueTask(new MetalCompileTask(std::move(work)));
}

@implementation MetalShaderFunction {
	id<MTLDevice> device_;
	MetalTranslatedShader shader_;
	std::string tag_;
	std::once_flag once_;
	id<MTLFunction> function_;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device shader:(const MetalTranslatedShader &)shader tag:(const std::string &)tag {
	if (self = [super init]) {
		device_ = device;
		shader_ = shader;
		tag_ = tag;
	}
	return self;
}

- (id<MTLFunction>)function {
	std::call_once(once_, [self] {
		std::string errorMessage;
		function_ = CreateMetalFunction(device_, shader_, tag_.c_str(), &errorMessage);
		if (!function_) {
			ERROR_LOG(Log::G3D, "%s\n%s", errorMessage.c_str(), LineNumberString(shader_.msl).c_str());
		}
		// Not needed anymore.
		shader_.msl.clear();
	});
	return function_;
}
@end

@implementation MetalPipelineState {
	std::mutex mutex_;
	std::condition_variable cond_;
	std::atomic<bool> ready_;
	id<MTLRenderPipelineState> pipeline_;
}

- (void)setPipeline:(id<MTLRenderPipelineState>)pipeline {
	{
		std::lock_guard<std::mutex> guard(mutex_);
		pipeline_ = pipeline;
		ready_.store(true, std::memory_order_release);
	}
	cond_.notify_all();
}

- (id<MTLRenderPipelineState>)wait {
	if (!ready_.load(std::memory_order_acquire)) {
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [self] { return ready_.load(std::memory_order_acquire); });
	}
	return pipeline_;
}

- (BOOL)isReady {
	return ready_.load(std::memory_order_acquire);
}
@end

MetalRenderManager::MetalRenderManager(id<MTLDevice> device, CAMetalLayer *layer, int offscreenWidth, int offscreenHeight, bool useThread) : device_(device) {
	queue_ = [device_ newCommandQueue];
	queue_.label = @"PPSSPP";
	unifiedMemory_ = device_.hasUnifiedMemory;
	appleGPU_ = [device_ supportsFamily:MTLGPUFamilyApple1];

	for (int i = 0; i < METAL_MAX_INFLIGHT_FRAMES; i++) {
		frames_[i].push.Init(device_, 4 * 1024 * 1024, StringFromFormat("push%d", i).c_str());
	}
	frameSemaphore_ = dispatch_semaphore_create(METAL_MAX_INFLIGHT_FRAMES);
	// Claim the first frame's slot.
	dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
	flushSemaphore_ = dispatch_semaphore_create(0);

	chunk_ = new MetalCommandChunk();
	backbuffer_ = CreateSwapchain(layer, offscreenWidth, offscreenHeight);
	if (useThread) {
		renderThread_ = std::thread([this] { RenderThreadFunc(); });
	}
}

MetalRenderManager::~MetalRenderManager() {
	WaitIdle();
	if (renderThread_.joinable()) {
		MetalCommandChunk *exitChunk = new MetalCommandChunk();
		exitChunk->exitThread = true;
		{
			std::lock_guard<std::mutex> guard(queueMutex_);
			queue_chunks_.push_back(exitChunk);
		}
		queueCond_.notify_one();
		renderThread_.join();
	}
	for (MetalSwapchain *swapchain : swapchains_) {
		delete swapchain;
	}
	swapchains_.clear();
	delete chunk_;
	for (MetalCommandChunk *chunk : freeChunks_) {
		delete chunk;
	}
	for (int i = 0; i < METAL_MAX_INFLIGHT_FRAMES; i++) {
		frames_[i].push.Destroy();
		frames_[i].deleteList.clear();
	}
	// Give back the slot we're holding, and wait for all the others, so the semaphore is
	// back at its initial value. libdispatch crashes when releasing it otherwise.
	dispatch_semaphore_signal(frameSemaphore_);
	for (int i = 0; i < METAL_MAX_INFLIGHT_FRAMES; i++) {
		dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
	}
	for (int i = 0; i < METAL_MAX_INFLIGHT_FRAMES; i++) {
		dispatch_semaphore_signal(frameSemaphore_);
	}
}

void MetalRenderManager::SetInflightFrames(int count) {
	// Changing the number of slots with frames in flight would be messy. We just
	// stop using some of them, by waiting on the frame's completion in Present.
	inflightFrames_ = std::clamp(count, 1, METAL_MAX_INFLIGHT_FRAMES);
}

MetalSwapchain *MetalRenderManager::CreateSwapchain(CAMetalLayer *layer, int width, int height) {
	MetalSwapchain *swapchain = new MetalSwapchain();
	if (layer) {
		swapchain->layer = layer;
		layer.device = device_;
		layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		// So we can read back screenshots, and copy from it.
		layer.framebufferOnly = NO;
		layer.maximumDrawableCount = 3;
		if (width > 0 && height > 0) {
			swapchain->pendingWidth = width;
			swapchain->pendingHeight = height;
		} else {
			CGSize size = layer.drawableSize;
			width = (int)size.width;
			height = (int)size.height;
		}
	} else {
		MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
		desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
		desc.storageMode = MTLStorageModePrivate;
		swapchain->offscreen = [device_ newTextureWithDescriptor:desc];
		swapchain->offscreen.label = @"offscreen_backbuffer";
	}
	swapchain->width = width;
	swapchain->height = height;
	CreateSwapchainDepth(swapchain);
	swapchains_.push_back(swapchain);
	return swapchain;
}

void MetalRenderManager::DestroySwapchain(MetalSwapchain *swapchain) {
	_dbg_assert_(swapchain != backbuffer_);
	if (passSwapchain_ == swapchain) {
		UnbindRenderPass();
	}
	// After this, no chunk that refers to it is left to replay.
	WaitIdle();
	swapchains_.erase(std::remove(swapchains_.begin(), swapchains_.end(), swapchain), swapchains_.end());
	rtAcquired_.erase(std::remove(rtAcquired_.begin(), rtAcquired_.end(), swapchain), rtAcquired_.end());
	delete swapchain;
}

void MetalRenderManager::CreateSwapchainDepth(MetalSwapchain *swapchain) {
	DeferRelease(swapchain->depth);
	if (swapchain->width <= 0 || swapchain->height <= 0) {
		swapchain->depth = nil;
		return;
	}
	MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:swapchain->width height:swapchain->height mipmapped:NO];
	desc.usage = MTLTextureUsageRenderTarget;
	if (appleGPU_) {
		// The backbuffer's depth never needs to survive a render pass.
		desc.storageMode = MTLStorageModeMemoryless;
	} else {
		desc.storageMode = MTLStorageModePrivate;
	}
	swapchain->depth = [device_ newTextureWithDescriptor:desc];
	swapchain->depth.label = @"backbuffer_depth";
}

void MetalRenderManager::Resize(MetalSwapchain *swapchain, int width, int height) {
	if (!swapchain->layer || width <= 0 || height <= 0) {
		return;
	}
	if (passSwapchain_ == swapchain) {
		UnbindRenderPass();
	}
	swapchain->pendingWidth = width;
	swapchain->pendingHeight = height;
	swapchain->width = width;
	swapchain->height = height;
	CreateSwapchainDepth(swapchain);
}

void MetalRenderManager::BeginFrame() {
	_dbg_assert_(!inFrame_);
	inFrame_ = true;
}

void MetalRenderManager::EndFrame() {
	_dbg_assert_(inFrame_);
	inFrame_ = false;
	EndRenderPass();
}

void MetalRenderManager::Submit(MetalChunkEnd end, bool vsync, dispatch_semaphore_t completed) {
	EndRenderPass();
	MetalCommandChunk *chunk = chunk_;
	chunk->end = end;
	chunk->vsync = vsync;
	chunk->completed = completed;
	for (MetalSwapchain *swapchain : swapchains_) {
		if (swapchain->pendingWidth > 0) {
			chunk->drawableResizes.push_back({ swapchain, swapchain->pendingWidth, swapchain->pendingHeight });
			swapchain->pendingWidth = 0;
			swapchain->pendingHeight = 0;
		}
	}

	if (renderThread_.joinable()) {
		{
			std::lock_guard<std::mutex> guard(freeMutex_);
			if (!freeChunks_.empty()) {
				chunk_ = freeChunks_.back();
				freeChunks_.pop_back();
			} else {
				chunk_ = new MetalCommandChunk();
			}
		}
		{
			std::lock_guard<std::mutex> guard(queueMutex_);
			queue_chunks_.push_back(chunk);
		}
		queueCond_.notify_one();
	} else {
		Replay(chunk);
		chunk->Clear();
	}
}

void MetalRenderManager::Present(bool vsync) {
	const bool waitForGPU = inflightFrames_ == 1;
	Submit(MetalChunkEnd::PRESENT, vsync, waitForGPU ? flushSemaphore_ : nullptr);
	if (waitForGPU) {
		dispatch_semaphore_wait(flushSemaphore_, DISPATCH_TIME_FOREVER);
	}

	lastStats_ = stats_;
	stats_ = {};

	// Advance to the next frame, and wait for the GPU to be done with its previous use.
	frameNumber_++;
	curFrame_ = (int)(frameNumber_ % METAL_MAX_INFLIGHT_FRAMES);
	dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
	frames_[curFrame_].push.Reset();
	frames_[curFrame_].deleteList.clear();
}

void MetalRenderManager::Flush(bool wait) {
	Submit(MetalChunkEnd::FLUSH, true, wait ? flushSemaphore_ : nullptr);
	if (wait) {
		dispatch_semaphore_wait(flushSemaphore_, DISPATCH_TIME_FOREVER);
	}
	stats_.flushes++;
}

void MetalRenderManager::WaitIdle() {
	Flush(true);
}

void MetalRenderManager::BindBackbufferPass(MetalSwapchain *swapchain, MTLLoadAction colorLoad, MTLClearColor clearColor, const char *tag) {
	if (passSwapchain_ == swapchain && inPass_ && colorLoad == MTLLoadActionLoad) {
		// Already rendering to it.
		return;
	}
	swapchain->drawn = true;
	EndRenderPass();
	MetalRenderPassDesc desc{};
	desc.colorLoad = colorLoad;
	desc.clearColor = clearColor;
	desc.depthLoad = MTLLoadActionClear;
	desc.stencilLoad = MTLLoadActionClear;
	desc.depthMemoryless = appleGPU_;
	desc.tag = tag;
	desc.depth = swapchain->depth;
	pass_ = desc;
	passSwapchain_ = swapchain;
	curFormat_.color = MTLPixelFormatBGRA8Unorm;
	curFormat_.depth = swapchain->depth ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid;
	curFormat_.stencil = curFormat_.depth;
	curFormat_.sampleCount = 1;
	passWidth_ = swapchain->width;
	passHeight_ = swapchain->height;
}

void MetalRenderManager::BindRenderPass(const MetalRenderPassDesc &desc) {
	if (!passSwapchain_ && HasRenderPass() && desc.color == pass_.color && desc.depth == pass_.depth) {
		// Binding the target we're already on continues the pass, like in the Vulkan backend: clears are applied,
		// and DontCare is ignored since there's content to keep.
		bool clearColor = desc.colorLoad == MTLLoadActionClear;
		bool clearDepth = desc.depthLoad == MTLLoadActionClear;
		bool clearStencil = desc.stencilLoad == MTLLoadActionClear;
		if (clearColor || clearDepth || clearStencil) {
			Clear(clearColor, clearDepth, clearStencil, desc.clearColor, desc.clearDepth, desc.clearStencil);
		}
		return;
	}
	EndRenderPass();
	pass_ = desc;
	passSwapchain_ = nullptr;
	id<MTLTexture> sizeTex = desc.color ? desc.color : desc.depth;
	passWidth_ = sizeTex ? (int)sizeTex.width : 0;
	passHeight_ = sizeTex ? (int)sizeTex.height : 0;
	curFormat_.color = desc.color ? desc.color.pixelFormat : MTLPixelFormatInvalid;
	curFormat_.depth = desc.depth ? desc.depth.pixelFormat : MTLPixelFormatInvalid;
	curFormat_.stencil = (curFormat_.depth == MTLPixelFormatDepth32Float_Stencil8) ? curFormat_.depth : MTLPixelFormatInvalid;
	id<MTLTexture> msaaTex = desc.msaaColor ? desc.msaaColor : desc.msaaDepth;
	curFormat_.sampleCount = msaaTex ? (int)msaaTex.sampleCount : 1;
}

void MetalRenderManager::UnbindRenderPass() {
	EndRenderPass();
	pass_ = MetalRenderPassDesc{};
	passSwapchain_ = nullptr;
	curFormat_ = MetalRenderTargetFormat{};
}

void MetalRenderManager::Clear(bool color, bool depth, bool stencil, MTLClearColor clearColor, double clearDepth, uint32_t clearStencil) {
	if (!HasRenderPass()) {
		return;
	}
	if (inPass_) {
		// Metal has no clear command within a pass, so restart it with clear load actions.
		RecordEndPass();
	}
	if (color) {
		pass_.colorLoad = MTLLoadActionClear;
		pass_.clearColor = clearColor;
	}
	if (depth) {
		pass_.depthLoad = MTLLoadActionClear;
		pass_.clearDepth = clearDepth;
	}
	if (stencil) {
		pass_.stencilLoad = MTLLoadActionClear;
		pass_.clearStencil = clearStencil;
	}
}

void MetalRenderManager::StartRenderPass() {
	if (!HasRenderPass()) {
		return;
	}
	MetalRecordedPass rp;
	rp.swapchain = passSwapchain_;
	rp.color = passSwapchain_ ? nullptr : (__bridge void *)pass_.color;
	rp.depth = (__bridge void *)pass_.depth;
	rp.msaaColor = (__bridge void *)pass_.msaaColor;
	rp.msaaDepth = (__bridge void *)pass_.msaaDepth;
	rp.colorLoad = pass_.colorLoad;
	rp.depthLoad = pass_.depthLoad;
	rp.stencilLoad = pass_.stencilLoad;
	rp.clearColor = pass_.clearColor;
	rp.clearDepth = pass_.clearDepth;
	rp.clearStencil = pass_.clearStencil;
	rp.depthMemoryless = pass_.depthMemoryless;
	rp.depthStore = MTLStoreActionStore;
	rp.usesDepth = false;
	if (pass_.tag) {
		rp.tag = pass_.tag;
	}
	chunk_->passes.push_back(std::move(rp));
	recordedPassIndex_ = chunk_->passes.size() - 1;
	Record(MetalCmd::BEGIN_PASS).pass = (uint32_t)recordedPassIndex_;
	inPass_ = true;
	passUsesDepth_ = false;

	stats_.renderPasses++;
	encoderGeneration_++;

	// If the pass is interrupted and resumed, keep the contents.
	pass_.colorLoad = MTLLoadActionLoad;
	pass_.depthLoad = MTLLoadActionLoad;
	pass_.stencilLoad = MTLLoadActionLoad;

	ResetEncoderState();
}

void MetalRenderManager::EndRenderPass() {
	if (!inPass_ && HasRenderPass()) {
		// A pass that was set up to clear, but never drawn to, still has to clear.
		if (pass_.colorLoad == MTLLoadActionClear || pass_.depthLoad == MTLLoadActionClear || pass_.stencilLoad == MTLLoadActionClear) {
			if (!(pass_.depthMemoryless && pass_.colorLoad != MTLLoadActionClear)) {
				StartRenderPass();
			} else {
				// Only the memoryless depth would be cleared, which nobody can see.
				pass_.depthLoad = MTLLoadActionLoad;
				pass_.stencilLoad = MTLLoadActionLoad;
			}
		}
	}
	if (inPass_) {
		RecordEndPass();
	}
}

void MetalRenderManager::RecordEndPass() {
	chunk_->passes[recordedPassIndex_].usesDepth = passUsesDepth_;
	Record(MetalCmd::END_PASS);
	inPass_ = false;
}

void MetalRenderManager::ResetEncoderState() {
	state_ = EncoderState{};
}

MetalRecordedCopy &MetalRenderManager::RecordCopy(bool upload) {
	if (!upload) {
		EndRenderPass();
		stats_.blitPasses++;
	}
	chunk_->copies.emplace_back();
	MetalCommand c{};
	c.cmd = MetalCmd::COPY;
	c.copy = (uint32_t)chunk_->copies.size() - 1;
	if (upload) {
		chunk_->uploads.push_back(c);
	} else {
		chunk_->commands.push_back(c);
	}
	return chunk_->copies.back();
}

void MetalRenderManager::CopyTexture(__unsafe_unretained id<MTLTexture> src, int srcLevel, MTLOrigin srcOrigin, MTLSize size, __unsafe_unretained id<MTLTexture> dst, int dstLevel, MTLOrigin dstOrigin) {
	MetalRecordedCopy &copy = RecordCopy(false);
	copy.type = MetalCopyType::TEXTURE_TO_TEXTURE;
	copy.src = (__bridge void *)src;
	copy.dst = (__bridge void *)dst;
	copy.srcLevel = srcLevel;
	copy.dstLevel = dstLevel;
	copy.srcOrigin = srcOrigin;
	copy.dstOrigin = dstOrigin;
	copy.size = size;
}

void MetalRenderManager::CopyTextureToBuffer(__unsafe_unretained id<MTLTexture> src, int level, MTLOrigin origin, MTLSize size, __unsafe_unretained id<MTLBuffer> dst, uint32_t offset, uint32_t bytesPerRow, uint32_t bytesPerImage, MTLBlitOption options, MetalSwapchain *srcSwapchain) {
	MetalRecordedCopy &copy = RecordCopy(false);
	copy.type = MetalCopyType::TEXTURE_TO_BUFFER;
	copy.srcSwapchain = srcSwapchain;
	copy.src = (__bridge void *)src;
	copy.dst = (__bridge void *)dst;
	copy.srcLevel = level;
	copy.srcOrigin = origin;
	copy.size = size;
	copy.dstOffset = offset;
	copy.bytesPerRow = bytesPerRow;
	copy.bytesPerImage = bytesPerImage;
	copy.options = options;
}

void MetalRenderManager::CopyBufferToTexture(__unsafe_unretained id<MTLBuffer> src, uint32_t offset, uint32_t bytesPerRow, uint32_t bytesPerImage, MTLSize size, __unsafe_unretained id<MTLTexture> dst, int slice, int level, MTLOrigin origin, MTLBlitOption options, bool upload) {
	MetalRecordedCopy &copy = RecordCopy(upload);
	copy.type = MetalCopyType::BUFFER_TO_TEXTURE;
	copy.src = (__bridge void *)src;
	copy.dst = (__bridge void *)dst;
	copy.srcOffset = offset;
	copy.bytesPerRow = bytesPerRow;
	copy.bytesPerImage = bytesPerImage;
	copy.size = size;
	copy.dstSlice = slice;
	copy.dstLevel = level;
	copy.dstOrigin = origin;
	copy.options = options;
}

void MetalRenderManager::GenerateMips(__unsafe_unretained id<MTLTexture> texture, bool upload) {
	MetalRecordedCopy &copy = RecordCopy(upload);
	copy.type = MetalCopyType::GENERATE_MIPS;
	copy.dst = (__bridge void *)texture;
}

MetalRecordedDispatch &MetalRenderManager::RecordUploadDispatch() {
	chunk_->dispatches.emplace_back();
	MetalCommand c{};
	c.cmd = MetalCmd::DISPATCH;
	c.dispatch = (uint32_t)chunk_->dispatches.size() - 1;
	chunk_->uploads.push_back(c);
	MetalRecordedDispatch &dispatch = chunk_->dispatches.back();
	dispatch = {};
	return dispatch;
}

void MetalRenderManager::SetPipeline(__unsafe_unretained MetalPipelineState *pipeline) {
	if (state_.pipeline != pipeline) {
		Record(MetalCmd::PIPELINE).bind.obj = (__bridge void *)pipeline;
		state_.pipeline = pipeline;
	}
}

void MetalRenderManager::SetDepthStencilState(__unsafe_unretained id<MTLDepthStencilState> dss, bool usesDepthStencil) {
	state_.depthStencilUsed = usesDepthStencil;
	if (state_.depthStencil != dss) {
		Record(MetalCmd::DEPTH_STENCIL).bind.obj = (__bridge void *)dss;
		state_.depthStencil = dss;
	}
}

void MetalRenderManager::SetStencilRef(uint32_t ref) {
	if (!state_.stencilRefValid || state_.stencilRef != ref) {
		Record(MetalCmd::STENCIL_REF).value = ref;
		state_.stencilRef = ref;
		state_.stencilRefValid = true;
	}
}

void MetalRenderManager::SetBlendColor(float r, float g, float b, float a) {
	if (!state_.blendColorValid || state_.blendColor[0] != r || state_.blendColor[1] != g || state_.blendColor[2] != b || state_.blendColor[3] != a) {
		MetalCommand &c = Record(MetalCmd::BLEND_COLOR);
		c.color[0] = r;
		c.color[1] = g;
		c.color[2] = b;
		c.color[3] = a;
		state_.blendColor[0] = r;
		state_.blendColor[1] = g;
		state_.blendColor[2] = b;
		state_.blendColor[3] = a;
		state_.blendColorValid = true;
	}
}

void MetalRenderManager::SetCullMode(MTLCullMode mode) {
	if (!state_.cullValid || state_.cull != mode) {
		Record(MetalCmd::CULL).value = (uint32_t)mode;
		state_.cull = mode;
		state_.cullValid = true;
	}
}

void MetalRenderManager::SetFrontFacing(MTLWinding winding) {
	if (!state_.windingValid || state_.winding != winding) {
		Record(MetalCmd::WINDING).value = (uint32_t)winding;
		state_.winding = winding;
		state_.windingValid = true;
	}
}

void MetalRenderManager::SetDepthClip(MTLDepthClipMode mode) {
	if (!state_.depthClipValid || state_.depthClip != mode) {
		Record(MetalCmd::DEPTH_CLIP).value = (uint32_t)mode;
		state_.depthClip = mode;
		state_.depthClipValid = true;
	}
}

void MetalRenderManager::SetViewport(const MTLViewport &vp) {
	if (!state_.viewportValid || memcmp(&vp, &state_.viewport, sizeof(vp)) != 0) {
		Record(MetalCmd::VIEWPORT).viewport = vp;
		state_.viewport = vp;
		state_.viewportValid = true;
	}
}

bool MetalRenderManager::SetScissor(int x, int y, int w, int h) {
	int x2 = std::min(x + w, passWidth_);
	int y2 = std::min(y + h, passHeight_);
	x = std::max(x, 0);
	y = std::max(y, 0);
	if (x2 <= x || y2 <= y) {
		return false;
	}
	MTLScissorRect rc{ (NSUInteger)x, (NSUInteger)y, (NSUInteger)(x2 - x), (NSUInteger)(y2 - y) };
	if (!state_.scissorValid || memcmp(&rc, &state_.scissor, sizeof(rc)) != 0) {
		Record(MetalCmd::SCISSOR).scissor = rc;
		state_.scissor = rc;
		state_.scissorValid = true;
	}
	return true;
}

void MetalRenderManager::SetVertexBuffer(__unsafe_unretained id<MTLBuffer> buffer, uint32_t offset, int index) {
	if (state_.vertexBuffers[index] != buffer) {
		MetalCommand &c = Record(MetalCmd::VERTEX_BUFFER);
		c.index = index;
		c.bind.obj = (__bridge void *)buffer;
		c.bind.offset = offset;
		state_.vertexBuffers[index] = buffer;
		state_.vertexBufferOffsets[index] = offset;
	} else if (state_.vertexBufferOffsets[index] != offset) {
		MetalCommand &c = Record(MetalCmd::VERTEX_BUFFER_OFFSET);
		c.index = index;
		c.bind.offset = offset;
		state_.vertexBufferOffsets[index] = offset;
	}
}

void MetalRenderManager::SetFragmentBuffer(__unsafe_unretained id<MTLBuffer> buffer, uint32_t offset, int index) {
	if (state_.fragmentBuffers[index] != buffer) {
		MetalCommand &c = Record(MetalCmd::FRAGMENT_BUFFER);
		c.index = index;
		c.bind.obj = (__bridge void *)buffer;
		c.bind.offset = offset;
		state_.fragmentBuffers[index] = buffer;
		state_.fragmentBufferOffsets[index] = offset;
	} else if (state_.fragmentBufferOffsets[index] != offset) {
		MetalCommand &c = Record(MetalCmd::FRAGMENT_BUFFER_OFFSET);
		c.index = index;
		c.bind.offset = offset;
		state_.fragmentBufferOffsets[index] = offset;
	}
}

void MetalRenderManager::SetFragmentTexture(__unsafe_unretained id<MTLTexture> texture, int index) {
	if (state_.fragmentTextures[index] != texture) {
		MetalCommand &c = Record(MetalCmd::FRAGMENT_TEXTURE);
		c.index = index;
		c.bind.obj = (__bridge void *)texture;
		state_.fragmentTextures[index] = texture;
	}
}

void MetalRenderManager::SetFragmentSampler(__unsafe_unretained id<MTLSamplerState> sampler, int index) {
	if (state_.fragmentSamplers[index] != sampler) {
		MetalCommand &c = Record(MetalCmd::FRAGMENT_SAMPLER);
		c.index = index;
		c.bind.obj = (__bridge void *)sampler;
		state_.fragmentSamplers[index] = sampler;
	}
}

void MetalRenderManager::Draw(MTLPrimitiveType prim, uint32_t start, uint32_t count) {
	// Metal doesn't allow empty draws.
	if (count == 0) {
		return;
	}
	passUsesDepth_ |= state_.depthStencilUsed;
	MetalCommand &c = Record(MetalCmd::DRAW);
	c.prim = (uint8_t)prim;
	c.draw.start = start;
	c.draw.count = count;
	stats_.draws++;
}

void MetalRenderManager::DrawIndexed(MTLPrimitiveType prim, uint32_t count, __unsafe_unretained id<MTLBuffer> indexBuffer, uint32_t offset) {
	if (count == 0) {
		return;
	}
	passUsesDepth_ |= state_.depthStencilUsed;
	MetalCommand &c = Record(MetalCmd::DRAW_INDEXED);
	c.prim = (uint8_t)prim;
	c.drawIndexed.buffer = (__bridge void *)indexBuffer;
	c.drawIndexed.offset = offset;
	c.drawIndexed.count = count;
	stats_.draws++;
}

void MetalRenderManager::RenderThreadFunc() {
	SetCurrentThreadName("MetalRender");
	while (true) {
		MetalCommandChunk *chunk;
		{
			std::unique_lock<std::mutex> lock(queueMutex_);
			queueCond_.wait(lock, [this] { return !queue_chunks_.empty(); });
			chunk = queue_chunks_.front();
			queue_chunks_.pop_front();
		}
		if (chunk->exitThread) {
			delete chunk;
			break;
		}
		@autoreleasepool {
			Replay(chunk);
		}
		chunk->Clear();
		std::lock_guard<std::mutex> guard(freeMutex_);
		freeChunks_.push_back(chunk);
	}
}

id<MTLTexture> MetalRenderManager::ReplaySwapchainTexture(MetalSwapchain *swapchain) {
	if (!swapchain->layer) {
		return swapchain->offscreen;
	}
	if (!swapchain->drawable && swapchain->noDrawableFrame != replayFrame_) {
		@autoreleasepool {
			swapchain->drawable = [swapchain->layer nextDrawable];
		}
		if (swapchain->drawable) {
			rtAcquired_.push_back(swapchain);
		} else {
			swapchain->noDrawableFrame = replayFrame_;
		}
	}
	return swapchain->drawable ? swapchain->drawable.texture : nil;
}

void MetalRenderManager::EndReplayEncoders() {
	if (rtBlit_) {
		[rtBlit_ endEncoding];
		rtBlit_ = nil;
	}
	if (rtUploadBlit_) {
		[rtUploadBlit_ endEncoding];
		rtUploadBlit_ = nil;
	}
	if (rtUploadCompute_) {
		[rtUploadCompute_ endEncoding];
		rtUploadCompute_ = nil;
	}
}

// Metal orders the encoders' accesses to the (tracked) textures, so there are no barriers to place.
void MetalRenderManager::ReplayDispatch(const MetalRecordedDispatch &dispatch, id<MTLCommandBuffer> cmd) {
	if (rtUploadBlit_) {
		[rtUploadBlit_ endEncoding];
		rtUploadBlit_ = nil;
	}
	if (!rtUploadCompute_) {
		rtUploadCompute_ = [cmd computeCommandEncoder];
		rtUploadCompute_.label = @"upload compute";
	}
	id<MTLComputeCommandEncoder> enc = rtUploadCompute_;
	[enc setComputePipelineState:(__bridge id<MTLComputePipelineState>)dispatch.pipeline];
	for (int i = 0; i < METAL_MAX_TEXTURES; i++) {
		if (dispatch.textures[i]) {
			[enc setTexture:(__bridge id<MTLTexture>)dispatch.textures[i] atIndex:i];
		}
	}
	for (int i = 0; i < METAL_MAX_BUFFERS; i++) {
		if (dispatch.buffers[i]) {
			[enc setBuffer:(__bridge id<MTLBuffer>)dispatch.buffers[i] offset:dispatch.bufferOffsets[i] atIndex:i];
		}
	}
	if (dispatch.pushConstantSize) {
		[enc setBytes:dispatch.pushConstants length:dispatch.pushConstantSize atIndex:METAL_PUSH_CONSTANT_INDEX];
	}
	[enc dispatchThreadgroups:dispatch.threadgroups threadsPerThreadgroup:dispatch.threadsPerThreadgroup];
}

void MetalRenderManager::Replay(MetalCommandChunk *chunk) {
	for (const auto &resize : chunk->drawableResizes) {
		resize.swapchain->layer.drawableSize = CGSizeMake(resize.width, resize.height);
	}

	// The upload buffer is enqueued first, so it runs first even though they're committed together.
	id<MTLCommandBuffer> uploadCmd;
	if (!chunk->uploads.empty()) {
		uploadCmd = [queue_ commandBufferWithUnretainedReferences];
		uploadCmd.label = @"upload";
		[uploadCmd enqueue];
	}
	id<MTLCommandBuffer> cmd = [queue_ commandBufferWithUnretainedReferences];
	cmd.label = @"main";
	[cmd enqueue];

	for (const MetalCommand &c : chunk->uploads) {
		if (c.cmd == MetalCmd::DISPATCH) {
			ReplayDispatch(chunk->dispatches[c.dispatch], uploadCmd);
		} else {
			if (rtUploadCompute_) {
				[rtUploadCompute_ endEncoding];
				rtUploadCompute_ = nil;
			}
			ReplayCopy(*chunk, chunk->copies[c.copy], uploadCmd, true);
		}
	}
	BuildSteps(*chunk);
	MergeSteps(*chunk);
	for (size_t i = 0; i < rtStepCount_; i++) {
		MetalStep &step = rtSteps_[i];
		if (step.merged) {
			continue;
		}
		if (step.isPass) {
			ReplayPass(*chunk, step, cmd);
		} else {
			ReplayCopy(*chunk, chunk->copies[step.index], cmd, false);
		}
	}
	EndReplayEncoders();

	// The block keeps the drawables alive until the GPU is done with them.
	NSMutableArray *drawables = nil;
	if (chunk->end == MetalChunkEnd::PRESENT) {
		for (MetalSwapchain *swapchain : rtAcquired_) {
#if PPSSPP_PLATFORM(MAC)
			if (swapchain->layer.displaySyncEnabled != chunk->vsync) {
				swapchain->layer.displaySyncEnabled = chunk->vsync;
			}
#endif
			[cmd presentDrawable:swapchain->drawable];
			if (!drawables) {
				drawables = [NSMutableArray arrayWithCapacity:rtAcquired_.size()];
			}
			[drawables addObject:swapchain->drawable];
			swapchain->drawable = nil;
		}
		rtAcquired_.clear();
		replayFrame_++;
	}

	dispatch_semaphore_t frameSemaphore = chunk->end == MetalChunkEnd::PRESENT ? frameSemaphore_ : nullptr;
	dispatch_semaphore_t completed = chunk->completed;
	if (frameSemaphore || completed) {
		[cmd addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
			(void)drawables;
			if (frameSemaphore)
				dispatch_semaphore_signal(frameSemaphore);
			if (completed)
				dispatch_semaphore_signal(completed);
		}];
	}
	[uploadCmd commit];  // A no-op if it's nil.
	[cmd commit];
}

static inline void *BaseTexture(void *obj) {
	id<MTLTexture> tex = (__bridge id<MTLTexture>)obj;
	id<MTLTexture> parent = tex.parentTexture;
	return parent ? (__bridge void *)parent : obj;
}

void MetalRenderManager::BuildSteps(const MetalCommandChunk &chunk) {
	rtStepCount_ = 0;
	auto newStep = [&]() -> MetalStep & {
		if (rtStepCount_ == rtSteps_.size()) {
			rtSteps_.emplace_back();
		}
		MetalStep &step = rtSteps_[rtStepCount_++];
		step.ranges.clear();
		step.reads.clear();
		step.merged = false;
		step.usesDepth = false;
		return step;
	};
	MetalStep *pass = nullptr;
	const std::vector<MetalCommand> &commands = chunk.commands;
	for (uint32_t i = 0; i < (uint32_t)commands.size(); i++) {
		const MetalCommand &c = commands[i];
		switch (c.cmd) {
		case MetalCmd::BEGIN_PASS:
		{
			const MetalRecordedPass &rp = chunk.passes[c.pass];
			pass = &newStep();
			pass->isPass = true;
			pass->index = c.pass;
			pass->ranges.push_back({ i + 1, i + 1 });
			pass->target = rp.swapchain ? (void *)rp.swapchain : (rp.color ? rp.color : rp.depth);
			pass->targetIsSwapchain = rp.swapchain != nullptr;
			pass->targetDepth = rp.depth;
			pass->targetMsaa = rp.msaaColor ? rp.msaaColor : rp.msaaDepth;
			pass->usesDepth = rp.usesDepth;
			break;
		}
		case MetalCmd::END_PASS:
			if (pass) {
				pass->ranges.back().second = i;
				pass = nullptr;
			}
			break;
		case MetalCmd::COPY:
		{
			const MetalRecordedCopy &copy = chunk.copies[c.copy];
			MetalStep &step = newStep();
			step.isPass = false;
			step.index = c.copy;
			// Compared with pass targets, which are never views.
			step.target = copy.type == MetalCopyType::TEXTURE_TO_BUFFER ? copy.dst : BaseTexture(copy.dst);
			step.targetMsaa = nullptr;
			step.targetDepth = nullptr;
			step.targetIsSwapchain = false;
			if (copy.srcSwapchain) {
				step.reads.push_back(copy.srcSwapchain);
			} else if (copy.src && copy.type != MetalCopyType::BUFFER_TO_TEXTURE) {
				step.reads.push_back(BaseTexture(copy.src));
			}
			break;
		}
		case MetalCmd::FRAGMENT_TEXTURE:
			if (pass && c.bind.obj) {
				pass->reads.push_back(BaseTexture(c.bind.obj));
			}
			break;
		default:
			break;
		}
	}
}

static bool Contains(const std::vector<void *> &v, void *p) {
	return p && std::find(v.begin(), v.end(), p) != v.end();
}

// Moves later passes to the same framebuffer into earlier ones, when nothing in between depends on the result
// or writes to it. Same rules as VulkanQueueRunner::ApplyRenderPassMerge. God of War alternates between its
// framebuffer and block transfers to other ones, which otherwise breaks its passes up a dozen times a frame.
void MetalRenderManager::MergeSteps(MetalCommandChunk &chunk) {
	std::vector<void *> touched;
	for (size_t i = 0; i < rtStepCount_; i++) {
		MetalStep &si = rtSteps_[i];
		if (!si.isPass || si.merged || si.targetIsSwapchain) {
			continue;
		}
		touched.clear();
		for (size_t j = i + 1; j < rtStepCount_; j++) {
			MetalStep &sj = rtSteps_[j];
			if (sj.merged) {
				continue;
			}
			const bool sameTarget = sj.isPass && sj.target == si.target && sj.targetDepth == si.targetDepth;
			// Anything else reading this framebuffer needs it as it is at this point.
			if (!sameTarget && (Contains(sj.reads, si.target) || Contains(sj.reads, si.targetDepth) || Contains(sj.reads, si.targetMsaa))) {
				break;
			}
			if (sj.isPass) {
				if (sameTarget) {
					const MetalRecordedPass &rp = chunk.passes[sj.index];
					const bool hasClear = rp.colorLoad == MTLLoadActionClear || rp.depthLoad == MTLLoadActionClear || rp.stencilLoad == MTLLoadActionClear;
					bool readsTouched = false;
					for (void *t : touched) {
						if (Contains(sj.reads, t)) {
							readsTouched = true;
							break;
						}
					}
					if (hasClear || readsTouched) {
						break;
					}
					si.ranges.insert(si.ranges.end(), sj.ranges.begin(), sj.ranges.end());
					si.usesDepth |= sj.usesDepth;
					sj.merged = true;
				} else {
					touched.push_back(sj.target);
					if (sj.targetDepth) {
						touched.push_back(sj.targetDepth);
					}
				}
			} else {
				if (sj.target == si.target || (si.targetDepth && sj.target == si.targetDepth) || (si.targetMsaa && sj.target == si.targetMsaa)) {
					// The copy overwrites the framebuffer.
					break;
				}
				touched.push_back(sj.target);
			}
		}
	}
}

void MetalRenderManager::ReplayPass(const MetalCommandChunk &chunk, MetalStep &step, id<MTLCommandBuffer> cmd) {
	if (rtBlit_) {
		[rtBlit_ endEncoding];
		rtBlit_ = nil;
	}
	const MetalRecordedPass &pass = chunk.passes[step.index];
	id<MTLTexture> color = pass.swapchain ? ReplaySwapchainTexture(pass.swapchain) : (__bridge id<MTLTexture>)pass.color;
	if (pass.swapchain && !color) {
		// No drawable (the window isn't visible?). Drop the pass.
		return;
	}
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	if (color) {
		id<MTLTexture> msaaColor = (__bridge id<MTLTexture>)pass.msaaColor;
		rp.colorAttachments[0].loadAction = pass.colorLoad;
		rp.colorAttachments[0].clearColor = pass.clearColor;
		if (msaaColor) {
			rp.colorAttachments[0].texture = msaaColor;
			rp.colorAttachments[0].resolveTexture = color;
			rp.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
		} else {
			rp.colorAttachments[0].texture = color;
			rp.colorAttachments[0].storeAction = MTLStoreActionStore;
		}
	}
	id<MTLTexture> depth = (__bridge id<MTLTexture>)pass.depth;
	if (depth) {
		MTLLoadAction depthLoad = pass.depthLoad;
		MTLLoadAction stencilLoad = pass.stencilLoad;
		MTLStoreAction store = MTLStoreActionStore;
		if (pass.depthMemoryless) {
			// Can't load or store memoryless attachments.
			if (depthLoad == MTLLoadActionLoad)
				depthLoad = MTLLoadActionClear;
			if (stencilLoad == MTLLoadActionLoad)
				stencilLoad = MTLLoadActionClear;
			store = MTLStoreActionDontCare;
		} else if (!step.usesDepth && depthLoad != MTLLoadActionClear && stencilLoad != MTLLoadActionClear) {
			// Nothing in the pass touches depth or stencil, so leave the memory as it is. The Vulkan backend
			// doesn't even attach depth then. Saves bandwidth, especially on tilers.
			depthLoad = MTLLoadActionDontCare;
			stencilLoad = MTLLoadActionDontCare;
			store = MTLStoreActionDontCare;
		}
		id<MTLTexture> msaaDepth = (__bridge id<MTLTexture>)pass.msaaDepth;
		id<MTLTexture> drawDepth = depth;
		if (msaaDepth) {
			// Resolve to sample 0, like the Vulkan backend.
			drawDepth = msaaDepth;
			if (store == MTLStoreActionStore) {
				store = MTLStoreActionStoreAndMultisampleResolve;
				rp.depthAttachment.resolveTexture = depth;
				rp.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
			}
		}
		rp.depthAttachment.texture = drawDepth;
		rp.depthAttachment.loadAction = depthLoad;
		rp.depthAttachment.clearDepth = pass.clearDepth;
		rp.depthAttachment.storeAction = store;
		if (depth.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
			rp.stencilAttachment.texture = drawDepth;
			rp.stencilAttachment.loadAction = stencilLoad;
			rp.stencilAttachment.clearStencil = pass.clearStencil;
			rp.stencilAttachment.storeAction = store;
			if (store == MTLStoreActionStoreAndMultisampleResolve) {
				rp.stencilAttachment.resolveTexture = depth;
				rp.stencilAttachment.stencilResolveFilter = MTLMultisampleStencilResolveFilterSample0;
			}
		}
	}
	id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
	if (!pass.tag.empty()) {
		enc.label = [NSString stringWithUTF8String:pass.tag.c_str()];
	}

	const std::vector<MetalCommand> &commands = chunk.commands;
	// Draws with a pipeline that failed to compile are dropped.
	bool pipelineOK = false;
	for (const auto &range : step.ranges) {
		for (uint32_t i = range.first; i < range.second; i++) {
			const MetalCommand &c = commands[i];
			switch (c.cmd) {
			case MetalCmd::PIPELINE:
			{
				id<MTLRenderPipelineState> pso = [(__bridge MetalPipelineState *)c.bind.obj wait];
				pipelineOK = pso != nil;
				if (pso) {
					[enc setRenderPipelineState:pso];
				}
				break;
			}
			case MetalCmd::DEPTH_STENCIL: [enc setDepthStencilState:(__bridge id<MTLDepthStencilState>)c.bind.obj]; break;
			case MetalCmd::STENCIL_REF: [enc setStencilReferenceValue:c.value]; break;
			case MetalCmd::BLEND_COLOR: [enc setBlendColorRed:c.color[0] green:c.color[1] blue:c.color[2] alpha:c.color[3]]; break;
			case MetalCmd::CULL: [enc setCullMode:(MTLCullMode)c.value]; break;
			case MetalCmd::WINDING: [enc setFrontFacingWinding:(MTLWinding)c.value]; break;
			case MetalCmd::DEPTH_CLIP: [enc setDepthClipMode:(MTLDepthClipMode)c.value]; break;
			case MetalCmd::VIEWPORT: [enc setViewport:c.viewport]; break;
			case MetalCmd::SCISSOR: [enc setScissorRect:c.scissor]; break;
			case MetalCmd::VERTEX_BUFFER: [enc setVertexBuffer:(__bridge id<MTLBuffer>)c.bind.obj offset:c.bind.offset atIndex:c.index]; break;
			case MetalCmd::VERTEX_BUFFER_OFFSET: [enc setVertexBufferOffset:c.bind.offset atIndex:c.index]; break;
			case MetalCmd::FRAGMENT_BUFFER: [enc setFragmentBuffer:(__bridge id<MTLBuffer>)c.bind.obj offset:c.bind.offset atIndex:c.index]; break;
			case MetalCmd::FRAGMENT_BUFFER_OFFSET: [enc setFragmentBufferOffset:c.bind.offset atIndex:c.index]; break;
			case MetalCmd::FRAGMENT_TEXTURE: [enc setFragmentTexture:(__bridge id<MTLTexture>)c.bind.obj atIndex:c.index]; break;
			case MetalCmd::FRAGMENT_SAMPLER: [enc setFragmentSamplerState:(__bridge id<MTLSamplerState>)c.bind.obj atIndex:c.index]; break;
			case MetalCmd::DRAW: if (pipelineOK) [enc drawPrimitives:(MTLPrimitiveType)c.prim vertexStart:c.draw.start vertexCount:c.draw.count]; break;
			case MetalCmd::DRAW_INDEXED:
				if (pipelineOK) [enc drawIndexedPrimitives:(MTLPrimitiveType)c.prim indexCount:c.drawIndexed.count indexType:MTLIndexTypeUInt16
					indexBuffer:(__bridge id<MTLBuffer>)c.drawIndexed.buffer indexBufferOffset:c.drawIndexed.offset];
				break;
			default:
				break;
			}
		}
	}
	[enc endEncoding];
}

void MetalRenderManager::ReplayCopy(const MetalCommandChunk &chunk, const MetalRecordedCopy &copy, id<MTLCommandBuffer> cmd, bool upload) {
	id<MTLBlitCommandEncoder> blit;
	if (upload) {
		if (!rtUploadBlit_) {
			rtUploadBlit_ = [cmd blitCommandEncoder];
			rtUploadBlit_.label = @"upload";
		}
		blit = rtUploadBlit_;
	} else {
		if (!rtBlit_) {
			rtBlit_ = [cmd blitCommandEncoder];
		}
		blit = rtBlit_;
	}
	switch (copy.type) {
	case MetalCopyType::TEXTURE_TO_TEXTURE:
		[blit copyFromTexture:(__bridge id<MTLTexture>)copy.src sourceSlice:0 sourceLevel:copy.srcLevel sourceOrigin:copy.srcOrigin sourceSize:copy.size
			toTexture:(__bridge id<MTLTexture>)copy.dst destinationSlice:0 destinationLevel:copy.dstLevel destinationOrigin:copy.dstOrigin];
		break;
	case MetalCopyType::TEXTURE_TO_BUFFER:
	{
		id<MTLTexture> src = copy.srcSwapchain ? ReplaySwapchainTexture(copy.srcSwapchain) : (__bridge id<MTLTexture>)copy.src;
		if (!src) {
			break;
		}
		[blit copyFromTexture:src sourceSlice:0 sourceLevel:copy.srcLevel sourceOrigin:copy.srcOrigin sourceSize:copy.size
			toBuffer:(__bridge id<MTLBuffer>)copy.dst destinationOffset:copy.dstOffset destinationBytesPerRow:copy.bytesPerRow destinationBytesPerImage:copy.bytesPerImage options:copy.options];
		break;
	}
	case MetalCopyType::BUFFER_TO_TEXTURE:
		[blit copyFromBuffer:(__bridge id<MTLBuffer>)copy.src sourceOffset:copy.srcOffset sourceBytesPerRow:copy.bytesPerRow sourceBytesPerImage:copy.bytesPerImage sourceSize:copy.size
			toTexture:(__bridge id<MTLTexture>)copy.dst destinationSlice:copy.dstSlice destinationLevel:copy.dstLevel destinationOrigin:copy.dstOrigin options:copy.options];
		break;
	case MetalCopyType::GENERATE_MIPS:
		[blit generateMipmapsForTexture:(__bridge id<MTLTexture>)copy.dst];
		break;
	}
}

static char g_arrayViewKey;

id<MTLTexture> MetalRenderManager::ArrayView(id<MTLTexture> texture) {
	if (!texture || texture.textureType == MTLTextureType2DArray || texture.textureType == MTLTextureType2DMultisampleArray) {
		return texture;
	}
	id<MTLTexture> view = objc_getAssociatedObject(texture, &g_arrayViewKey);
	if (!view) {
		const MTLTextureType type = texture.textureType == MTLTextureType2DMultisample ? MTLTextureType2DMultisampleArray : MTLTextureType2DArray;
		view = [texture newTextureViewWithPixelFormat:texture.pixelFormat textureType:type levels:NSMakeRange(0, texture.mipmapLevelCount) slices:NSMakeRange(0, 1)];
		objc_setAssociatedObject(texture, &g_arrayViewKey, view, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
	}
	return view;
}
