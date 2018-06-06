
// Copyright (c) 2015- PPSSPP Project.

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

#include <thread>
#include "base/logging.h"
#include "profiler/profiler.h"

#include "Common/ChunkFile.h"
#include "Common/GraphicsContext.h"

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/FramebufferCommon.h"

#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "thin3d/VulkanRenderManager.h"
#include "thin3d/VulkanQueueRunner.h"

#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

GPU_Vulkan::GPU_Vulkan(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw),
		vulkan_((VulkanContext *)gfxCtx->GetAPIContext()),
		drawEngine_(vulkan_, draw),
		depalShaderCache_(draw, vulkan_),
		vulkan2D_(vulkan_) {
	UpdateVsyncInterval(true);
	CheckGPUFeatures();

	shaderManagerVulkan_ = new ShaderManagerVulkan(vulkan_);
	pipelineManager_ = new PipelineManagerVulkan(vulkan_);
	framebufferManagerVulkan_ = new FramebufferManagerVulkan(draw, vulkan_);
	framebufferManager_ = framebufferManagerVulkan_;
	textureCacheVulkan_ = new TextureCacheVulkan(draw, vulkan_);
	textureCache_ = textureCacheVulkan_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerVulkan_;

	drawEngine_.SetTextureCache(textureCacheVulkan_);
	drawEngine_.SetFramebufferManager(framebufferManagerVulkan_);
	drawEngine_.SetShaderManager(shaderManagerVulkan_);
	drawEngine_.SetPipelineManager(pipelineManager_);
	framebufferManagerVulkan_->SetVulkan2D(&vulkan2D_);
	framebufferManagerVulkan_->Init();
	framebufferManagerVulkan_->SetTextureCache(textureCacheVulkan_);
	framebufferManagerVulkan_->SetDrawEngine(&drawEngine_);
	framebufferManagerVulkan_->SetShaderManager(shaderManagerVulkan_);
	textureCacheVulkan_->SetDepalShaderCache(&depalShaderCache_);
	textureCacheVulkan_->SetFramebufferManager(framebufferManagerVulkan_);
	textureCacheVulkan_->SetShaderManager(shaderManagerVulkan_);
	textureCacheVulkan_->SetDrawEngine(&drawEngine_);
	textureCacheVulkan_->SetVulkan2D(&vulkan2D_);

	InitDeviceObjects();

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	BuildReportingInfo();
	// Update again after init to be sure of any silly driver problems.
	UpdateVsyncInterval(true);

	textureCacheVulkan_->NotifyConfigChanged();
	if (vulkan_->GetFeaturesEnabled().wideLines) {
		drawEngine_.SetLineWidth(PSP_CoreParameter().renderWidth / 480.0f);
	}

	// Load shader cache.
	std::string discID = g_paramSFO.GetDiscID();
	if (discID.size()) {
		File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
		shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) + "/" + discID + ".vkshadercache";
		shaderCacheLoaded_ = false;

		std::thread th([&] {
			LoadCache(shaderCachePath_);
			shaderCacheLoaded_ = true;
		});
		th.detach();
	}
}

bool GPU_Vulkan::IsReady() {
	return shaderCacheLoaded_;
}

void GPU_Vulkan::LoadCache(std::string filename) {
	PSP_SetLoading("Loading shader cache...");
	// Actually precompiled by IsReady() since we're single-threaded.
	FILE *f = File::OpenCFile(filename, "rb");
	if (!f)
		return;

	// First compile shaders to SPIR-V, then load the pipeline cache and recreate the pipelines.
	// It's when recreating the pipelines that the pipeline cache is useful - in the ideal case,
	// it can just memcpy the finished shader binaries out of the pipeline cache file.
	bool result = shaderManagerVulkan_->LoadCache(f);
	if (result) {
		result = pipelineManager_->LoadCache(f, false, shaderManagerVulkan_, draw_, drawEngine_.GetPipelineLayout());
	}
	fclose(f);
	if (!result) {
		WARN_LOG(G3D, "Bad Vulkan pipeline cache");
		// Bad cache file for this GPU/Driver/etc. Delete it.
		File::Delete(filename);
	} else {
		INFO_LOG(G3D, "Loaded Vulkan pipeline cache.");
	}
}

void GPU_Vulkan::SaveCache(std::string filename) {
	if (!draw_) {
		// Already got the lost message, we're in shutdown.
		WARN_LOG(G3D, "Not saving shaders - shutting down from in-game.");
		return;
	}

	FILE *f = File::OpenCFile(filename, "wb");
	if (!f)
		return;
	shaderManagerVulkan_->SaveCache(f);
	pipelineManager_->SaveCache(f, false, shaderManagerVulkan_, draw_);
	INFO_LOG(G3D, "Saved Vulkan pipeline cache");
	fclose(f);
}

GPU_Vulkan::~GPU_Vulkan() {
	SaveCache(shaderCachePath_);
	// Note: We save the cache in DeviceLost
	DestroyDeviceObjects();
	framebufferManagerVulkan_->DestroyAllFBOs();
	vulkan2D_.Shutdown();
	depalShaderCache_.Clear();
	drawEngine_.DeviceLost();
	delete textureCacheVulkan_;
	delete pipelineManager_;
	delete shaderManagerVulkan_;
	delete framebufferManagerVulkan_;
}

void GPU_Vulkan::CheckGPUFeatures() {
	uint32_t features = 0;

	switch (vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).vendorID) {
	case VULKAN_VENDOR_AMD:
		// Accurate depth is required on AMD (due to reverse-Z driver bug) so we ignore the compat flag to disable it on those. See #9545
		features |= GPU_SUPPORTS_ACCURATE_DEPTH;
		break;
	case VULKAN_VENDOR_ARM:
		// Also required on older ARM Mali drivers, like the one on many Galaxy S7.
		if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth ||
			  vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).driverVersion <= VK_MAKE_VERSION(428, 811, 2674)) {
			features |= GPU_SUPPORTS_ACCURATE_DEPTH;
		}
		break;
	default:
		if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth)
			features |= GPU_SUPPORTS_ACCURATE_DEPTH;
		break;
	}

	// Mandatory features on Vulkan, which may be checked in "centralized" code
	features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;
	features |= GPU_SUPPORTS_FBO;
	features |= GPU_SUPPORTS_BLEND_MINMAX;
	features |= GPU_SUPPORTS_ANY_COPY_IMAGE;
	features |= GPU_SUPPORTS_OES_TEXTURE_NPOT;
	features |= GPU_SUPPORTS_LARGE_VIEWPORTS;
	features |= GPU_SUPPORTS_16BIT_FORMATS;
	features |= GPU_SUPPORTS_INSTANCE_RENDERING;
	features |= GPU_SUPPORTS_VERTEX_TEXTURE_FETCH;
	features |= GPU_SUPPORTS_TEXTURE_FLOAT;

	if (vulkan_->GetFeaturesEnabled().wideLines) {
		features |= GPU_SUPPORTS_WIDE_LINES;
	}
	if (vulkan_->GetFeaturesEnabled().depthClamp) {
		features |= GPU_SUPPORTS_DEPTH_CLAMP;
	}
	if (vulkan_->GetFeaturesEnabled().dualSrcBlend) {
		switch (vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).vendorID) {
		// We thought we had a bug here on nVidia but turns out we accidentally #ifdef-ed out crucial
		// code on Android.
		case VULKAN_VENDOR_INTEL:
			// Workaround for Intel driver bug.
			break;
		case VULKAN_VENDOR_AMD:
			// See issue #10074, and also #10065 (AMD) and #10109 for the choice of the driver version to check for
			if (vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice()).driverVersion >= 0x00407000)
				features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
			break;
		default:
			features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
			break;
		}
	}
	if (vulkan_->GetFeaturesEnabled().logicOp) {
		features |= GPU_SUPPORTS_LOGIC_OP;
	}
	if (vulkan_->GetFeaturesEnabled().samplerAnisotropy) {
		features |= GPU_SUPPORTS_ANISOTROPY;
	}

	if (PSP_CoreParameter().compat.flags().ClearToRAM) {
		features |= GPU_USE_CLEAR_RAM_HACK;
	}

	if (!g_Config.bHighQualityDepth && (features & GPU_SUPPORTS_ACCURATE_DEPTH) != 0) {
		features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
	}
	else if (PSP_CoreParameter().compat.flags().PixelDepthRounding) {
		// Use fragment rounding on desktop and GLES3, most accurate.
		features |= GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
	}
	else if (PSP_CoreParameter().compat.flags().VertexDepthRounding) {
		features |= GPU_ROUND_DEPTH_TO_16BIT;
	}
	gstate_c.featureFlags = features;
}

void GPU_Vulkan::BeginHostFrame() {
	drawEngine_.BeginFrame();
	UpdateCmdInfo();

	if (resized_) {
		CheckGPUFeatures();
		// In case the GPU changed.
		BuildReportingInfo();
		framebufferManager_->Resized();
		drawEngine_.Resized();
		textureCacheVulkan_->NotifyConfigChanged();
		if (vulkan_->GetFeaturesEnabled	().wideLines) {
			drawEngine_.SetLineWidth(PSP_CoreParameter().renderWidth / 480.0f);
		}
	}
	resized_ = false;

	textureCacheVulkan_->StartFrame();

	int curFrame = vulkan_->GetCurFrame();
	FrameData &frame = frameData_[curFrame];

	frame.push_->Reset();
	frame.push_->Begin(vulkan_);

	framebufferManagerVulkan_->BeginFrameVulkan();
	framebufferManagerVulkan_->SetPushBuffer(frameData_[curFrame].push_);
	depalShaderCache_.SetPushBuffer(frameData_[curFrame].push_);
	textureCacheVulkan_->SetPushBuffer(frameData_[curFrame].push_);

	vulkan2D_.BeginFrame();

	shaderManagerVulkan_->DirtyShader();
	gstate_c.Dirty(DIRTY_ALL);

	if (dumpNextFrame_) {
		NOTICE_LOG(G3D, "DUMPING THIS FRAME");
		dumpThisFrame_ = true;
		dumpNextFrame_ = false;
	} else if (dumpThisFrame_) {
		dumpThisFrame_ = false;
	}
}

void GPU_Vulkan::EndHostFrame() {
	int curFrame = vulkan_->GetCurFrame();
	FrameData &frame = frameData_[curFrame];
	frame.push_->End();

	vulkan2D_.EndFrame();

	drawEngine_.EndFrame();
	framebufferManagerVulkan_->EndFrame();
	textureCacheVulkan_->EndFrame();
}

// Needs to be called on GPU thread, not reporting thread.
void GPU_Vulkan::BuildReportingInfo() {
	const auto &props = vulkan_->GetPhysicalDeviceProperties(vulkan_->GetCurrentPhysicalDevice());
	const auto &features = vulkan_->GetFeaturesAvailable();

#define CHECK_BOOL_FEATURE(n) do { if (features.n) { featureNames += ", " #n; } } while (false)

	std::string featureNames = "";
	CHECK_BOOL_FEATURE(robustBufferAccess);
	CHECK_BOOL_FEATURE(fullDrawIndexUint32);
	CHECK_BOOL_FEATURE(imageCubeArray);
	CHECK_BOOL_FEATURE(independentBlend);
	CHECK_BOOL_FEATURE(geometryShader);
	CHECK_BOOL_FEATURE(tessellationShader);
	CHECK_BOOL_FEATURE(sampleRateShading);
	CHECK_BOOL_FEATURE(dualSrcBlend);
	CHECK_BOOL_FEATURE(logicOp);
	CHECK_BOOL_FEATURE(multiDrawIndirect);
	CHECK_BOOL_FEATURE(drawIndirectFirstInstance);
	CHECK_BOOL_FEATURE(depthClamp);
	CHECK_BOOL_FEATURE(depthBiasClamp);
	CHECK_BOOL_FEATURE(fillModeNonSolid);
	CHECK_BOOL_FEATURE(depthBounds);
	CHECK_BOOL_FEATURE(wideLines);
	CHECK_BOOL_FEATURE(largePoints);
	CHECK_BOOL_FEATURE(alphaToOne);
	CHECK_BOOL_FEATURE(multiViewport);
	CHECK_BOOL_FEATURE(samplerAnisotropy);
	CHECK_BOOL_FEATURE(textureCompressionETC2);
	CHECK_BOOL_FEATURE(textureCompressionASTC_LDR);
	CHECK_BOOL_FEATURE(textureCompressionBC);
	CHECK_BOOL_FEATURE(occlusionQueryPrecise);
	CHECK_BOOL_FEATURE(pipelineStatisticsQuery);
	CHECK_BOOL_FEATURE(vertexPipelineStoresAndAtomics);
	CHECK_BOOL_FEATURE(fragmentStoresAndAtomics);
	CHECK_BOOL_FEATURE(shaderTessellationAndGeometryPointSize);
	CHECK_BOOL_FEATURE(shaderImageGatherExtended);
	CHECK_BOOL_FEATURE(shaderStorageImageExtendedFormats);
	CHECK_BOOL_FEATURE(shaderStorageImageMultisample);
	CHECK_BOOL_FEATURE(shaderStorageImageReadWithoutFormat);
	CHECK_BOOL_FEATURE(shaderStorageImageWriteWithoutFormat);
	CHECK_BOOL_FEATURE(shaderUniformBufferArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderSampledImageArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderStorageBufferArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderStorageImageArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderClipDistance);
	CHECK_BOOL_FEATURE(shaderCullDistance);
	CHECK_BOOL_FEATURE(shaderFloat64);
	CHECK_BOOL_FEATURE(shaderInt64);
	CHECK_BOOL_FEATURE(shaderInt16);
	CHECK_BOOL_FEATURE(shaderResourceResidency);
	CHECK_BOOL_FEATURE(shaderResourceMinLod);
	CHECK_BOOL_FEATURE(sparseBinding);
	CHECK_BOOL_FEATURE(sparseResidencyBuffer);
	CHECK_BOOL_FEATURE(sparseResidencyImage2D);
	CHECK_BOOL_FEATURE(sparseResidencyImage3D);
	CHECK_BOOL_FEATURE(sparseResidency2Samples);
	CHECK_BOOL_FEATURE(sparseResidency4Samples);
	CHECK_BOOL_FEATURE(sparseResidency8Samples);
	CHECK_BOOL_FEATURE(sparseResidency16Samples);
	CHECK_BOOL_FEATURE(sparseResidencyAliased);
	CHECK_BOOL_FEATURE(variableMultisampleRate);
	CHECK_BOOL_FEATURE(inheritedQueries);

#undef CHECK_BOOL_FEATURE

	if (!featureNames.empty()) {
		featureNames = featureNames.substr(2);
	}

	char temp[16384];
	snprintf(temp, sizeof(temp), "v%08x driver v%08x (%s), vendorID=%d, deviceID=%d (features: %s)", props.apiVersion, props.driverVersion, props.deviceName, props.vendorID, props.deviceID, featureNames.c_str());
	reportingPrimaryInfo_ = props.deviceName;
	reportingFullInfo_ = temp;

	Reporting::UpdateConfig();
}

void GPU_Vulkan::Reinitialize() {
	GPUCommon::Reinitialize();
	textureCacheVulkan_->Clear(true);
	depalShaderCache_.Clear();
	framebufferManagerVulkan_->DestroyAllFBOs();
}

void GPU_Vulkan::InitClear() {
	bool useNonBufferedRendering = g_Config.iRenderingMode == FB_NON_BUFFERED_MODE;
	if (useNonBufferedRendering) {
		// TODO?
	}
}

void GPU_Vulkan::UpdateVsyncInterval(bool force) {
	// TODO
}

void GPU_Vulkan::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	host->GPUNotifyDisplay(framebuf, stride, format);
	framebufferManager_->SetDisplayFramebuffer(framebuf, stride, format);
}

void GPU_Vulkan::CopyDisplayToOutput() {
	// Flush anything left over.
	drawEngine_.Flush();

	shaderManagerVulkan_->DirtyLastShader();

	framebufferManagerVulkan_->CopyDisplayToOutput();

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

void GPU_Vulkan::FinishDeferred() {
	drawEngine_.FinishDeferred();
}

inline void GPU_Vulkan::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if ((cmdFlags & FLAG_FLUSHBEFORE) || (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE))) {
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		drawEngine_.Flush();
	}
}

void GPU_Vulkan::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPU_Vulkan::ExecuteOp(u32 op, u32 diff) {
	const u8 cmd = op >> 24;
	const CommandInfo info = cmdInfo_[cmd];
	const u8 cmdFlags = info.flags;
	if ((cmdFlags & FLAG_EXECUTE) || (diff && (cmdFlags & FLAG_EXECUTEONCHANGE))) {
		(this->*info.func)(op, diff);
	} else if (diff) {
		uint64_t dirty = info.flags >> 8;
		if (dirty)
			gstate_c.Dirty(dirty);
	}
}

void GPU_Vulkan::InitDeviceObjects() {
	ILOG("GPU_Vulkan::InitDeviceObjects");
	// Initialize framedata
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		assert(!frameData_[i].push_);
		frameData_[i].push_ = new VulkanPushBuffer(vulkan_, 64 * 1024);
	}

	VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	uint32_t hacks = 0;
	if (PSP_CoreParameter().compat.flags().MGS2AcidHack)
		hacks |= QUEUE_HACK_MGS2_ACID;
	if (PSP_CoreParameter().compat.flags().SonicRivalsHack)
		hacks |= QUEUE_HACK_SONIC;
	if (hacks) {
		rm->GetQueueRunner()->EnableHacks(hacks);
	}
}

void GPU_Vulkan::DestroyDeviceObjects() {
	ILOG("GPU_Vulkan::DestroyDeviceObjects");
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		if (frameData_[i].push_) {
			frameData_[i].push_->Destroy(vulkan_);
			delete frameData_[i].push_;
			frameData_[i].push_ = nullptr;
		}
	}

	// Need to turn off hacks when shutting down the GPU. Don't want them running in the menu.
	if (draw_) {
		VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		if (rm)
			rm->GetQueueRunner()->EnableHacks(0);
	}
}

void GPU_Vulkan::DeviceLost() {
	if (!shaderCachePath_.empty()) {
		SaveCache(shaderCachePath_);
	}
	DestroyDeviceObjects();
	framebufferManagerVulkan_->DeviceLost();
	vulkan2D_.DeviceLost();
	drawEngine_.DeviceLost();
	pipelineManager_->DeviceLost();
	textureCacheVulkan_->DeviceLost();
	depalShaderCache_.DeviceLost();
	shaderManagerVulkan_->ClearShaders();
	draw_ = nullptr;
}

void GPU_Vulkan::DeviceRestore() {
	vulkan_ = (VulkanContext *)PSP_CoreParameter().graphicsContext->GetAPIContext();
	draw_ = (Draw::DrawContext *)PSP_CoreParameter().graphicsContext->GetDrawContext();
	InitDeviceObjects();

	CheckGPUFeatures();
	BuildReportingInfo();
	UpdateCmdInfo();

	framebufferManagerVulkan_->DeviceRestore(vulkan_, draw_);
	vulkan2D_.DeviceRestore(vulkan_);
	drawEngine_.DeviceRestore(vulkan_, draw_);
	pipelineManager_->DeviceRestore(vulkan_);
	textureCacheVulkan_->DeviceRestore(vulkan_, draw_);
	shaderManagerVulkan_->DeviceRestore(vulkan_);
	depalShaderCache_.DeviceRestore(draw_, vulkan_);
}

void GPU_Vulkan::GetStats(char *buffer, size_t bufsize) {
	const DrawEngineVulkanStats &drawStats = drawEngine_.GetStats();
	char texStats[256];
	textureCacheVulkan_->GetStats(texStats, sizeof(texStats));
	float vertexAverageCycles = gpuStats.numVertsSubmitted > 0 ? (float)gpuStats.vertexGPUCycles / (float)gpuStats.numVertsSubmitted : 0.0f;
	snprintf(buffer, bufsize - 1,
		"DL processing time: %0.2f ms\n"
		"Draw calls: %i, flushes %i, clears %i\n"
		"Cached Draw calls: %i\n"
		"Num Tracked Vertex Arrays: %i\n"
		"GPU cycles executed: %d (%f per vertex)\n"
		"Commands per call level: %i %i %i %i\n"
		"Vertices submitted: %i\n"
		"Cached, Uncached Vertices Drawn: %i, %i\n"
		"FBOs active: %i\n"
		"Textures active: %i, decoded: %i  invalidated: %i\n"
		"Readbacks: %d, uploads: %d\n"
		"Vertex, Fragment, Pipelines loaded: %i, %i, %i\n"
		"Pushbuffer space used: UBO %d, Vtx %d, Idx %d\n"
		"%s\n",
		gpuStats.msProcessingDisplayLists * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numClears,
		gpuStats.numCachedDrawCalls,
		gpuStats.numTrackedVertexArrays,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles,
		gpuStats.gpuCommandsAtCallLevel[0], gpuStats.gpuCommandsAtCallLevel[1], gpuStats.gpuCommandsAtCallLevel[2], gpuStats.gpuCommandsAtCallLevel[3],
		gpuStats.numVertsSubmitted,
		gpuStats.numCachedVertsDrawn,
		gpuStats.numUncachedVertsDrawn,
		(int)framebufferManager_->NumVFBs(),
		(int)textureCacheVulkan_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		gpuStats.numReadbacks,
		gpuStats.numUploads,
		shaderManagerVulkan_->GetNumVertexShaders(),
		shaderManagerVulkan_->GetNumFragmentShaders(),
		pipelineManager_->GetNumPipelines(),
		drawStats.pushUBOSpaceUsed,
		drawStats.pushVertexSpaceUsed,
		drawStats.pushIndexSpaceUsed,
		texStats
	);
}

void GPU_Vulkan::ClearCacheNextFrame() {
	textureCacheVulkan_->ClearNextFrame();
}

void GPU_Vulkan::ClearShaderCache() {
	// TODO
}

void GPU_Vulkan::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	// In Freeze-Frame mode, we don't want to do any of this.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCacheVulkan_->Clear(true);
		depalShaderCache_.Clear();

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManagerVulkan_->DestroyAllFBOs();
	}
}

std::vector<std::string> GPU_Vulkan::DebugGetShaderIDs(DebugShaderType type) {
	if (type == SHADER_TYPE_VERTEXLOADER) {
		return drawEngine_.DebugGetVertexLoaderIDs();
	} else if (type == SHADER_TYPE_PIPELINE) {
		return pipelineManager_->DebugGetObjectIDs(type);
	} else if (type == SHADER_TYPE_DEPAL) {
		///...
		return std::vector<std::string>();
	} else if (type == SHADER_TYPE_VERTEX || type == SHADER_TYPE_FRAGMENT) {
		return shaderManagerVulkan_->DebugGetShaderIDs(type);
	} else if (type == SHADER_TYPE_SAMPLER) {
		return textureCacheVulkan_->DebugGetSamplerIDs();
	} else {
		return std::vector<std::string>();
	}
}

std::string GPU_Vulkan::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	if (type == SHADER_TYPE_VERTEXLOADER) {
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	} else if (type == SHADER_TYPE_PIPELINE) {
		return pipelineManager_->DebugGetObjectString(id, type, stringType);
	} else if (type == SHADER_TYPE_DEPAL) {
		return "";
	} else if (type == SHADER_TYPE_SAMPLER) {
		return textureCacheVulkan_->DebugGetSamplerString(id, stringType);
	} else if (type == SHADER_TYPE_VERTEX || type == SHADER_TYPE_FRAGMENT) {
		return shaderManagerVulkan_->DebugGetShaderString(id, type, stringType);
	} else {
		return std::string();
	}
}
