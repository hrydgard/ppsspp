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
	while (true) {
		if (curBlock_ < (int)blocks_.size()) {
			Block &block = blocks_[curBlock_];
			size_t offset = (offset_ + align - 1) & ~(align - 1);
			if (offset + size <= block.size) {
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
		block.size = std::max(blockSize_, size);
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
		// macOS 11 and iOS 15 are the minimum, and both have MSL 2.3.
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

MetalRenderManager::MetalRenderManager(id<MTLDevice> device, CAMetalLayer *layer, int offscreenWidth, int offscreenHeight) : device_(device), layer_(layer) {
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

	if (layer_) {
		layer_.device = device_;
		layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
		// So we can read back screenshots, and copy from it.
		layer_.framebufferOnly = NO;
		layer_.maximumDrawableCount = 3;
		CGSize size = layer_.drawableSize;
		bbWidth_ = (int)size.width;
		bbHeight_ = (int)size.height;
	} else {
		CreateOffscreenBackbuffer(offscreenWidth, offscreenHeight);
	}
	CreateBackbufferDepth();
}

MetalRenderManager::~MetalRenderManager() {
	WaitIdle();
	for (int i = 0; i < METAL_MAX_INFLIGHT_FRAMES; i++) {
		frames_[i].push.Destroy();
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

void MetalRenderManager::CreateOffscreenBackbuffer(int width, int height) {
	MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
	desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	desc.storageMode = MTLStorageModePrivate;
	offscreenBackbuffer_ = [device_ newTextureWithDescriptor:desc];
	offscreenBackbuffer_.label = @"offscreen_backbuffer";
	bbWidth_ = width;
	bbHeight_ = height;
}

void MetalRenderManager::CreateBackbufferDepth() {
	if (bbWidth_ <= 0 || bbHeight_ <= 0) {
		bbDepth_ = nil;
		return;
	}
	MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:bbWidth_ height:bbHeight_ mipmapped:NO];
	desc.usage = MTLTextureUsageRenderTarget;
	if (appleGPU_) {
		// The backbuffer's depth never needs to survive a render pass.
		desc.storageMode = MTLStorageModeMemoryless;
	} else {
		desc.storageMode = MTLStorageModePrivate;
	}
	bbDepth_ = [device_ newTextureWithDescriptor:desc];
	bbDepth_.label = @"backbuffer_depth";
}

void MetalRenderManager::Resize(int width, int height) {
	if (!layer_) {
		return;
	}
	if (pass_.color != nil && passIsBackbuffer_) {
		UnbindRenderPass();
	}
	if (width > 0 && height > 0) {
		layer_.drawableSize = CGSizeMake(width, height);
	}
	CGSize size = layer_.drawableSize;
	bbWidth_ = (int)size.width;
	bbHeight_ = (int)size.height;
	CreateBackbufferDepth();
}

void MetalRenderManager::AcquireDrawable() {
	if (drawable_ || !layer_) {
		return;
	}
	@autoreleasepool {
		drawable_ = [layer_ nextDrawable];
	}
	if (drawable_) {
		id<MTLTexture> tex = drawable_.texture;
		if ((int)tex.width != bbWidth_ || (int)tex.height != bbHeight_) {
			WARN_LOG(Log::G3D, "Drawable size %dx%d doesn't match backbuffer size %dx%d", (int)tex.width, (int)tex.height, bbWidth_, bbHeight_);
		}
	}
}

id<MTLTexture> MetalRenderManager::BackbufferTexture() {
	if (!layer_) {
		return offscreenBackbuffer_;
	}
	AcquireDrawable();
	return drawable_ ? drawable_.texture : nil;
}

id<MTLTexture> MetalRenderManager::BackbufferDepth() {
	return bbDepth_;
}

void MetalRenderManager::NewCommandBuffers() {
	if (cmd_) {
		return;
	}
	// The upload buffer is enqueued first, so it runs first even though it's committed at the same time.
	uploadCmd_ = [queue_ commandBuffer];
	uploadCmd_.label = @"upload";
	[uploadCmd_ enqueue];
	cmd_ = [queue_ commandBuffer];
	cmd_.label = @"main";
	[cmd_ enqueue];
}

void MetalRenderManager::BeginFrame() {
	_dbg_assert_(!inFrame_);
	inFrame_ = true;
	NewCommandBuffers();
}

void MetalRenderManager::EndFrame() {
	_dbg_assert_(inFrame_);
	inFrame_ = false;
	EndEncoders();
}

void MetalRenderManager::Present(bool vsync) {
	EndEncoders();
	NewCommandBuffers();

#if PPSSPP_PLATFORM(MAC)
	if (layer_ && layer_.displaySyncEnabled != vsync) {
		layer_.displaySyncEnabled = vsync;
	}
#endif

	if (drawable_) {
		[cmd_ presentDrawable:drawable_];
		drawable_ = nil;
	}

	if (uploadEncoder_) {
		[uploadEncoder_ endEncoding];
		uploadEncoder_ = nil;
	}
	[uploadCmd_ commit];
	uploadCmd_ = nil;

	dispatch_semaphore_t semaphore = frameSemaphore_;
	[cmd_ addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
		dispatch_semaphore_signal(semaphore);
	}];
	[cmd_ commit];
	lastCommitted_ = cmd_;
	id<MTLCommandBuffer> committed = cmd_;
	cmd_ = nil;

	if (inflightFrames_ == 1) {
		[committed waitUntilCompleted];
	}

	lastStats_ = stats_;
	stats_ = {};

	// Advance to the next frame, and wait for the GPU to be done with its previous use.
	frameNumber_++;
	curFrame_ = (int)(frameNumber_ % METAL_MAX_INFLIGHT_FRAMES);
	dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
	frames_[curFrame_].push.Reset();
}

void MetalRenderManager::Flush(bool wait) {
	EndEncoders();
	if (!cmd_) {
		if (wait && lastCommitted_) {
			[lastCommitted_ waitUntilCompleted];
		}
		return;
	}
	if (uploadEncoder_) {
		[uploadEncoder_ endEncoding];
		uploadEncoder_ = nil;
	}
	[uploadCmd_ commit];
	uploadCmd_ = nil;
	[cmd_ commit];
	if (wait) {
		[cmd_ waitUntilCompleted];
	}
	lastCommitted_ = cmd_;
	cmd_ = nil;
	stats_.flushes++;
	NewCommandBuffers();
}

void MetalRenderManager::WaitIdle() {
	Flush(true);
}

void MetalRenderManager::BindBackbufferPass(MTLLoadAction colorLoad, MTLClearColor clearColor, const char *tag) {
	MetalRenderPassDesc desc{};
	desc.colorLoad = colorLoad;
	desc.clearColor = clearColor;
	desc.depthLoad = MTLLoadActionClear;
	desc.stencilLoad = MTLLoadActionClear;
	desc.depthMemoryless = appleGPU_;
	desc.tag = tag;
	if (passIsBackbuffer_ && pass_.color != nil && renderEncoder_ && colorLoad == MTLLoadActionLoad) {
		// Already rendering to it.
		return;
	}
	EndEncoders();
	pass_ = desc;
	passIsBackbuffer_ = true;
	// Placeholder until the pass starts, so HasRenderPass() is true. Acquiring the drawable is delayed as long as possible.
	pass_.color = layer_ ? nil : offscreenBackbuffer_;
	pass_.depth = bbDepth_;
	curFormat_.color = MTLPixelFormatBGRA8Unorm;
	curFormat_.depth = bbDepth_ ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid;
	curFormat_.stencil = curFormat_.depth;
	curFormat_.sampleCount = 1;
	passWidth_ = bbWidth_;
	passHeight_ = bbHeight_;
}

void MetalRenderManager::BindRenderPass(const MetalRenderPassDesc &desc) {
	if (!passIsBackbuffer_ && HasRenderPass() && desc.color == pass_.color && desc.depth == pass_.depth) {
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
	EndEncoders();
	pass_ = desc;
	passIsBackbuffer_ = false;
	id<MTLTexture> sizeTex = desc.color ? desc.color : desc.depth;
	passWidth_ = sizeTex ? (int)sizeTex.width : 0;
	passHeight_ = sizeTex ? (int)sizeTex.height : 0;
	curFormat_.color = desc.color ? desc.color.pixelFormat : MTLPixelFormatInvalid;
	curFormat_.depth = desc.depth ? desc.depth.pixelFormat : MTLPixelFormatInvalid;
	curFormat_.stencil = (curFormat_.depth == MTLPixelFormatDepth32Float_Stencil8) ? curFormat_.depth : MTLPixelFormatInvalid;
	curFormat_.sampleCount = sizeTex ? (int)sizeTex.sampleCount : 1;
}

void MetalRenderManager::UnbindRenderPass() {
	EndEncoders();
	pass_ = MetalRenderPassDesc{};
	passIsBackbuffer_ = false;
	curFormat_ = MetalRenderTargetFormat{};
}

void MetalRenderManager::Clear(bool color, bool depth, bool stencil, MTLClearColor clearColor, double clearDepth, uint32_t clearStencil) {
	if (!HasRenderPass() && !passIsBackbuffer_) {
		return;
	}
	if (renderEncoder_) {
		// Metal has no clear command within a pass, so restart it with clear load actions.
		[renderEncoder_ endEncoding];
		renderEncoder_ = nil;
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

void MetalRenderManager::StartRenderEncoder() {
	EndBlitEncoder();
	NewCommandBuffers();

	id<MTLTexture> color = pass_.color;
	if (passIsBackbuffer_) {
		color = BackbufferTexture();
		if (!color) {
			return;
		}
	}

	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	if (color) {
		rp.colorAttachments[0].texture = color;
		rp.colorAttachments[0].loadAction = pass_.colorLoad;
		rp.colorAttachments[0].clearColor = pass_.clearColor;
		rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	}
	if (pass_.depth) {
		MTLLoadAction depthLoad = pass_.depthLoad;
		MTLLoadAction stencilLoad = pass_.stencilLoad;
		MTLStoreAction store = MTLStoreActionStore;
		if (pass_.depthMemoryless) {
			// Can't load or store memoryless attachments.
			if (depthLoad == MTLLoadActionLoad)
				depthLoad = MTLLoadActionClear;
			if (stencilLoad == MTLLoadActionLoad)
				stencilLoad = MTLLoadActionClear;
			store = MTLStoreActionDontCare;
		}
		rp.depthAttachment.texture = pass_.depth;
		rp.depthAttachment.loadAction = depthLoad;
		rp.depthAttachment.clearDepth = pass_.clearDepth;
		rp.depthAttachment.storeAction = store;
		if (pass_.depth.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
			rp.stencilAttachment.texture = pass_.depth;
			rp.stencilAttachment.loadAction = stencilLoad;
			rp.stencilAttachment.clearStencil = pass_.clearStencil;
			rp.stencilAttachment.storeAction = store;
		}
	}

	renderEncoder_ = [cmd_ renderCommandEncoderWithDescriptor:rp];
	if (pass_.tag) {
		renderEncoder_.label = [NSString stringWithUTF8String:pass_.tag];
	}
	stats_.renderPasses++;
	encoderGeneration_++;

	// If the pass is interrupted and resumed, keep the contents.
	pass_.colorLoad = MTLLoadActionLoad;
	pass_.depthLoad = MTLLoadActionLoad;
	pass_.stencilLoad = MTLLoadActionLoad;

	ResetEncoderState();
	if (encoderResetCallback_) {
		encoderResetCallback_();
	}
}

void MetalRenderManager::ResetEncoderState() {
	state_ = EncoderState{};
}

id<MTLRenderCommandEncoder> MetalRenderManager::RenderEncoder() {
	if (!renderEncoder_) {
		if (!HasRenderPass() && !passIsBackbuffer_) {
			return nil;
		}
		StartRenderEncoder();
	}
	return renderEncoder_;
}

void MetalRenderManager::EndRenderEncoder() {
	if (!renderEncoder_ && (HasRenderPass() || passIsBackbuffer_)) {
		// A pass that was set up to clear, but never drawn to, still has to clear.
		if (pass_.colorLoad == MTLLoadActionClear || pass_.depthLoad == MTLLoadActionClear || pass_.stencilLoad == MTLLoadActionClear) {
			if (!(pass_.depthMemoryless && pass_.colorLoad != MTLLoadActionClear)) {
				StartRenderEncoder();
			} else {
				// Only the memoryless depth would be cleared, which nobody can see.
				pass_.depthLoad = MTLLoadActionLoad;
				pass_.stencilLoad = MTLLoadActionLoad;
			}
		}
	}
	if (renderEncoder_) {
		[renderEncoder_ endEncoding];
		renderEncoder_ = nil;
	}
}

void MetalRenderManager::EndBlitEncoder() {
	if (blitEncoder_) {
		[blitEncoder_ endEncoding];
		blitEncoder_ = nil;
	}
}

void MetalRenderManager::EndEncoders() {
	EndRenderEncoder();
	EndBlitEncoder();
}

id<MTLBlitCommandEncoder> MetalRenderManager::BlitEncoder() {
	EndRenderEncoder();
	if (!blitEncoder_) {
		NewCommandBuffers();
		blitEncoder_ = [cmd_ blitCommandEncoder];
		stats_.blitPasses++;
	}
	return blitEncoder_;
}

id<MTLBlitCommandEncoder> MetalRenderManager::UploadEncoder() {
	NewCommandBuffers();
	if (!uploadEncoder_) {
		uploadEncoder_ = [uploadCmd_ blitCommandEncoder];
		uploadEncoder_.label = @"upload";
	}
	return uploadEncoder_;
}

void MetalRenderManager::SetPipeline(id<MTLRenderPipelineState> pipeline) {
	if (state_.pipeline != pipeline) {
		[renderEncoder_ setRenderPipelineState:pipeline];
		state_.pipeline = pipeline;
	}
}

void MetalRenderManager::SetDepthStencilState(id<MTLDepthStencilState> dss) {
	if (state_.depthStencil != dss) {
		[renderEncoder_ setDepthStencilState:dss];
		state_.depthStencil = dss;
	}
}

void MetalRenderManager::SetStencilRef(uint32_t ref) {
	if (!state_.stencilRefValid || state_.stencilRef != ref) {
		[renderEncoder_ setStencilReferenceValue:ref];
		state_.stencilRef = ref;
		state_.stencilRefValid = true;
	}
}

void MetalRenderManager::SetBlendColor(float r, float g, float b, float a) {
	if (!state_.blendColorValid || state_.blendColor[0] != r || state_.blendColor[1] != g || state_.blendColor[2] != b || state_.blendColor[3] != a) {
		[renderEncoder_ setBlendColorRed:r green:g blue:b alpha:a];
		state_.blendColor[0] = r;
		state_.blendColor[1] = g;
		state_.blendColor[2] = b;
		state_.blendColor[3] = a;
		state_.blendColorValid = true;
	}
}

void MetalRenderManager::SetCullMode(MTLCullMode mode) {
	if (!state_.cullValid || state_.cull != mode) {
		[renderEncoder_ setCullMode:mode];
		state_.cull = mode;
		state_.cullValid = true;
	}
}

void MetalRenderManager::SetFrontFacing(MTLWinding winding) {
	if (!state_.windingValid || state_.winding != winding) {
		[renderEncoder_ setFrontFacingWinding:winding];
		state_.winding = winding;
		state_.windingValid = true;
	}
}

void MetalRenderManager::SetDepthClip(MTLDepthClipMode mode) {
	if (!state_.depthClipValid || state_.depthClip != mode) {
		[renderEncoder_ setDepthClipMode:mode];
		state_.depthClip = mode;
		state_.depthClipValid = true;
	}
}

void MetalRenderManager::SetViewport(const MTLViewport &vp) {
	if (!state_.viewportValid || memcmp(&vp, &state_.viewport, sizeof(vp)) != 0) {
		[renderEncoder_ setViewport:vp];
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
		[renderEncoder_ setScissorRect:rc];
		state_.scissor = rc;
		state_.scissorValid = true;
	}
	return true;
}

void MetalRenderManager::SetVertexBuffer(id<MTLBuffer> buffer, uint32_t offset, int index) {
	if (state_.vertexBuffers[index] != buffer) {
		[renderEncoder_ setVertexBuffer:buffer offset:offset atIndex:index];
		state_.vertexBuffers[index] = buffer;
		state_.vertexBufferOffsets[index] = offset;
	} else if (state_.vertexBufferOffsets[index] != offset) {
		[renderEncoder_ setVertexBufferOffset:offset atIndex:index];
		state_.vertexBufferOffsets[index] = offset;
	}
}

void MetalRenderManager::SetVertexBytes(const void *data, size_t size, int index) {
	[renderEncoder_ setVertexBytes:data length:size atIndex:index];
	state_.vertexBuffers[index] = nil;
}

void MetalRenderManager::SetFragmentBuffer(id<MTLBuffer> buffer, uint32_t offset, int index) {
	if (state_.fragmentBuffers[index] != buffer) {
		[renderEncoder_ setFragmentBuffer:buffer offset:offset atIndex:index];
		state_.fragmentBuffers[index] = buffer;
		state_.fragmentBufferOffsets[index] = offset;
	} else if (state_.fragmentBufferOffsets[index] != offset) {
		[renderEncoder_ setFragmentBufferOffset:offset atIndex:index];
		state_.fragmentBufferOffsets[index] = offset;
	}
}

void MetalRenderManager::SetFragmentBytes(const void *data, size_t size, int index) {
	[renderEncoder_ setFragmentBytes:data length:size atIndex:index];
	state_.fragmentBuffers[index] = nil;
}

void MetalRenderManager::SetFragmentTexture(id<MTLTexture> texture, int index) {
	if (state_.fragmentTextures[index] != texture) {
		[renderEncoder_ setFragmentTexture:texture atIndex:index];
		state_.fragmentTextures[index] = texture;
	}
}

void MetalRenderManager::SetFragmentSampler(id<MTLSamplerState> sampler, int index) {
	if (state_.fragmentSamplers[index] != sampler) {
		[renderEncoder_ setFragmentSamplerState:sampler atIndex:index];
		state_.fragmentSamplers[index] = sampler;
	}
}

void MetalRenderManager::SetVertexTexture(id<MTLTexture> texture, int index) {
	if (state_.vertexTextures[index] != texture) {
		[renderEncoder_ setVertexTexture:texture atIndex:index];
		state_.vertexTextures[index] = texture;
	}
}

void MetalRenderManager::SetVertexSampler(id<MTLSamplerState> sampler, int index) {
	if (state_.vertexSamplers[index] != sampler) {
		[renderEncoder_ setVertexSamplerState:sampler atIndex:index];
		state_.vertexSamplers[index] = sampler;
	}
}

static char g_arrayViewKey;

id<MTLTexture> MetalRenderManager::ArrayView(id<MTLTexture> texture) {
	if (!texture || texture.textureType == MTLTextureType2DArray) {
		return texture;
	}
	id<MTLTexture> view = objc_getAssociatedObject(texture, &g_arrayViewKey);
	if (!view) {
		view = [texture newTextureViewWithPixelFormat:texture.pixelFormat textureType:MTLTextureType2DArray levels:NSMakeRange(0, texture.mipmapLevelCount) slices:NSMakeRange(0, 1)];
		objc_setAssociatedObject(texture, &g_arrayViewKey, view, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
	}
	return view;
}
