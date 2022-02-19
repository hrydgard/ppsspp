
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

#include "Common/Profiler/Profiler.h"

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/GraphicsContext.h"
#include "Common/Serialize/Serializer.h"
#include "Common/TimeUtil.h"

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GPU/Vulkan/VulkanQueueRunner.h"

#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

GPU_Vulkan::GPU_Vulkan(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw), drawEngine_(draw) {
	CheckGPUFeatures();

	VulkanContext *vulkan = (VulkanContext *)gfxCtx->GetAPIContext();

	vulkan->SetProfilerEnabledPtr(&g_Config.bGpuLogProfiler);

	shaderManagerVulkan_ = new ShaderManagerVulkan(draw);
	pipelineManager_ = new PipelineManagerVulkan(vulkan);
	framebufferManagerVulkan_ = new FramebufferManagerVulkan(draw);
	framebufferManager_ = framebufferManagerVulkan_;
	textureCacheVulkan_ = new TextureCacheVulkan(draw, framebufferManager_->GetDraw2D(), vulkan);
	textureCache_ = textureCacheVulkan_;
	drawEngineCommon_ = &drawEngine_;
	shaderManager_ = shaderManagerVulkan_;

	drawEngine_.SetTextureCache(textureCacheVulkan_);
	drawEngine_.SetFramebufferManager(framebufferManagerVulkan_);
	drawEngine_.SetShaderManager(shaderManagerVulkan_);
	drawEngine_.SetPipelineManager(pipelineManager_);
	drawEngine_.Init();
	framebufferManagerVulkan_->SetTextureCache(textureCacheVulkan_);
	framebufferManagerVulkan_->SetDrawEngine(&drawEngine_);
	framebufferManagerVulkan_->SetShaderManager(shaderManagerVulkan_);
	framebufferManagerVulkan_->Init();
	textureCacheVulkan_->SetFramebufferManager(framebufferManagerVulkan_);
	textureCacheVulkan_->SetShaderManager(shaderManagerVulkan_);
	textureCacheVulkan_->SetDrawEngine(&drawEngine_);

	InitDeviceObjects();

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	BuildReportingInfo();
	// Update again after init to be sure of any silly driver problems.
	UpdateVsyncInterval(true);

	textureCache_->NotifyConfigChanged();

	// Load shader cache.
	std::string discID = g_paramSFO.GetDiscID();
	if (discID.size()) {
		File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
		shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) / (discID + ".vkshadercache");
		shaderCacheLoaded_ = false;

		std::thread th([&] {
			LoadCache(shaderCachePath_);
			shaderCacheLoaded_ = true;
		});
		th.detach();
	} else {
		shaderCacheLoaded_ = true;
	}
}

bool GPU_Vulkan::IsReady() {
	return shaderCacheLoaded_;
}

void GPU_Vulkan::CancelReady() {
	pipelineManager_->CancelCache();
}

void GPU_Vulkan::LoadCache(const Path &filename) {
	if (!g_Config.bShaderCache) {
		INFO_LOG(G3D, "Shader cache disabled. Not loading.");
		return;
	}

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
		// WARNING: See comment in LoadCache if you are tempted to flip the second parameter to true.
		result = pipelineManager_->LoadCache(f, false, shaderManagerVulkan_, draw_, drawEngine_.GetPipelineLayout());
	}
	fclose(f);
	if (!result) {
		WARN_LOG(G3D, "Incompatible Vulkan pipeline cache - rebuilding.");
		// Bad cache file for this GPU/Driver/etc. Delete it.
		File::Delete(filename);
	} else {
		INFO_LOG(G3D, "Loaded Vulkan pipeline cache.");
	}
}

void GPU_Vulkan::SaveCache(const Path &filename) {
	if (!g_Config.bShaderCache) {
		INFO_LOG(G3D, "Shader cache disabled. Not saving.");
		return;
	}

	if (!draw_) {
		// Already got the lost message, we're in shutdown.
		WARN_LOG(G3D, "Not saving shaders - shutting down from in-game.");
		return;
	}

	FILE *f = File::OpenCFile(filename, "wb");
	if (!f)
		return;
	shaderManagerVulkan_->SaveCache(f);
	// WARNING: See comment in LoadCache if you are tempted to flip the second parameter to true.
	pipelineManager_->SaveCache(f, false, shaderManagerVulkan_, draw_);
	INFO_LOG(G3D, "Saved Vulkan pipeline cache");
	fclose(f);
}

GPU_Vulkan::~GPU_Vulkan() {
	SaveCache(shaderCachePath_);
	// Note: We save the cache in DeviceLost
	DestroyDeviceObjects();
	framebufferManagerVulkan_->DestroyAllFBOs();
	drawEngine_.DeviceLost();
	delete textureCacheVulkan_;
	delete pipelineManager_;
	delete shaderManagerVulkan_;
	delete framebufferManagerVulkan_;
}

void GPU_Vulkan::CheckGPUFeatures() {
	uint32_t features = 0;

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	switch (vulkan->GetPhysicalDeviceProperties().properties.vendorID) {
	case VULKAN_VENDOR_AMD:
		// Accurate depth is required on AMD (due to reverse-Z driver bug) so we ignore the compat flag to disable it on those. See #9545
		features |= GPU_SUPPORTS_ACCURATE_DEPTH;
		break;
	case VULKAN_VENDOR_QUALCOMM:
		// Accurate depth is required on Adreno too (seems to also have a reverse-Z driver bug).
		features |= GPU_SUPPORTS_ACCURATE_DEPTH;
		break;
	case VULKAN_VENDOR_ARM:
	{
		// This check is probably not exactly accurate. But old drivers had problems with reverse-Z, just like AMD and Qualcomm.

		// NOTE: Galaxy S8 has version 16 but still seems to have some problems with accurate depth.

		bool driverTooOld = IsHashMaliDriverVersion(vulkan->GetPhysicalDeviceProperties().properties)
			|| VK_VERSION_MAJOR(vulkan->GetPhysicalDeviceProperties().properties.driverVersion) < 14;

		if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth || driverTooOld) {
			features |= GPU_SUPPORTS_ACCURATE_DEPTH;
		}
		break;
	}
	default:
		if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth) {
			features |= GPU_SUPPORTS_ACCURATE_DEPTH;
		}
		break;
	}

	// Might enable this later - in the first round we are mostly looking at depth/stencil/discard.
	// if (!g_Config.bEnableVendorBugChecks)
	// 	features |= GPU_SUPPORTS_ACCURATE_DEPTH;

	// Mandatory features on Vulkan, which may be checked in "centralized" code
	features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;
	features |= GPU_SUPPORTS_BLEND_MINMAX;
	features |= GPU_SUPPORTS_TEXTURE_NPOT;
	features |= GPU_SUPPORTS_INSTANCE_RENDERING;
	features |= GPU_SUPPORTS_VERTEX_TEXTURE_FETCH;
	features |= GPU_SUPPORTS_TEXTURE_FLOAT;
	features |= GPU_SUPPORTS_DEPTH_TEXTURE;

	// input attachments
	features |= GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH;

	auto &enabledFeatures = vulkan->GetDeviceFeatures().enabled;
	if (enabledFeatures.depthClamp) {
		features |= GPU_SUPPORTS_DEPTH_CLAMP;
	}
	if (enabledFeatures.shaderClipDistance) {
		features |= GPU_SUPPORTS_CLIP_DISTANCE;
	}
	if (enabledFeatures.shaderCullDistance) {
		// Must support at least 8 if feature supported, so we're fine.
		features |= GPU_SUPPORTS_CULL_DISTANCE;
	}
	if (!draw_->GetBugs().Has(Draw::Bugs::BROKEN_NAN_IN_CONDITIONAL)) {
		// Ignore the compat setting if clip and cull are both enabled.
		// When supported, we can do the depth side of range culling more correctly.
		const bool supported = draw_->GetDeviceCaps().clipDistanceSupported && draw_->GetDeviceCaps().cullDistanceSupported;
		const bool disabled = PSP_CoreParameter().compat.flags().DisableRangeCulling;
		if (supported || !disabled) {
			features |= GPU_SUPPORTS_VS_RANGE_CULLING;
		}
	}
	if (enabledFeatures.dualSrcBlend) {
		if (!g_Config.bVendorBugChecksEnabled || !draw_->GetBugs().Has(Draw::Bugs::DUAL_SOURCE_BLENDING_BROKEN)) {
			features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
		}
	}
	if (draw_->GetDeviceCaps().logicOpSupported) {
		features |= GPU_SUPPORTS_LOGIC_OP;
	}
	if (draw_->GetDeviceCaps().anisoSupported) {
		features |= GPU_SUPPORTS_ANISOTROPY;
	}

	// These are VULKAN_4444_FORMAT and friends.
	uint32_t fmt4444 = draw_->GetDataFormatSupport(Draw::DataFormat::B4G4R4A4_UNORM_PACK16);
	uint32_t fmt1555 = draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16);

	// Note that we are (accidentally) using B5G6R5 instead of the mandatory R5G6B5.
	// Support is almost as widespread, but not quite. So let's just not use any 16-bit formats
	// if it's not available, for simplicity.
	uint32_t fmt565 = draw_->GetDataFormatSupport(Draw::DataFormat::B5G6R5_UNORM_PACK16);
	if ((fmt4444 & Draw::FMT_TEXTURE) && (fmt565 & Draw::FMT_TEXTURE) && (fmt1555 & Draw::FMT_TEXTURE)) {
		features |= GPU_SUPPORTS_16BIT_FORMATS;
	} else {
		INFO_LOG(G3D, "Deficient texture format support: 4444: %d  1555: %d  565: %d", fmt4444, fmt1555, fmt565);
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
		textureCache_->NotifyConfigChanged();
		resized_ = false;
	}

	textureCacheVulkan_->StartFrame();

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	int curFrame = vulkan->GetCurFrame();
	FrameData &frame = frameData_[curFrame];

	frame.push_->Reset();
	frame.push_->Begin(vulkan);

	framebufferManagerVulkan_->BeginFrame();
	textureCacheVulkan_->SetPushBuffer(frameData_[curFrame].push_);

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
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	int curFrame = vulkan->GetCurFrame();
	FrameData &frame = frameData_[curFrame];
	frame.push_->End();

	drawEngine_.EndFrame();
	textureCacheVulkan_->EndFrame();

	draw_->InvalidateCachedState();
}

// Needs to be called on GPU thread, not reporting thread.
void GPU_Vulkan::BuildReportingInfo() {
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	const auto &props = vulkan->GetPhysicalDeviceProperties().properties;
	const auto &features = vulkan->GetDeviceFeatures().available;

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
}

void GPU_Vulkan::InitClear() {
	if (!framebufferManager_->UseBufferedRendering()) {
		// TODO?
	}
}

void GPU_Vulkan::CopyDisplayToOutput(bool reallyDirty) {
	// Flush anything left over.
	drawEngine_.Flush();

	shaderManagerVulkan_->DirtyLastShader();

	framebufferManagerVulkan_->CopyDisplayToOutput(reallyDirty);

	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
}

void GPU_Vulkan::FinishDeferred() {
	drawEngine_.FinishDeferred();
}

inline void GPU_Vulkan::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE)) {
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
	INFO_LOG(G3D, "GPU_Vulkan::InitDeviceObjects");
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	// Initialize framedata
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		_assert_(!frameData_[i].push_);
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		frameData_[i].push_ = new VulkanPushBuffer(vulkan, "gpuPush", 256 * 1024, usage, PushBufferType::CPU_TO_GPU);
	}

	VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	uint32_t hacks = 0;
	if (PSP_CoreParameter().compat.flags().MGS2AcidHack)
		hacks |= QUEUE_HACK_MGS2_ACID;
	if (PSP_CoreParameter().compat.flags().SonicRivalsHack)
		hacks |= QUEUE_HACK_SONIC;

	// Always on.
	hacks |= QUEUE_HACK_RENDERPASS_MERGE;

	if (hacks) {
		rm->GetQueueRunner()->EnableHacks(hacks);
	}
}

void GPU_Vulkan::DestroyDeviceObjects() {
	INFO_LOG(G3D, "GPU_Vulkan::DestroyDeviceObjects");
	for (int i = 0; i < VulkanContext::MAX_INFLIGHT_FRAMES; i++) {
		if (frameData_[i].push_) {
			VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
			frameData_[i].push_->Destroy(vulkan);
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
	CancelReady();
	while (!IsReady()) {
		sleep_ms(10);
	}
	if (shaderCachePath_.Valid()) {
		SaveCache(shaderCachePath_);
	}
	DestroyDeviceObjects();
	drawEngine_.DeviceLost();
	pipelineManager_->DeviceLost();
	textureCacheVulkan_->DeviceLost();
	shaderManagerVulkan_->DeviceLost();

	GPUCommon::DeviceLost();
}

void GPU_Vulkan::DeviceRestore() {
	GPUCommon::DeviceRestore();
	InitDeviceObjects();

	CheckGPUFeatures();
	BuildReportingInfo();
	UpdateCmdInfo();

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	drawEngine_.DeviceRestore(draw_);
	pipelineManager_->DeviceRestore(vulkan);
	textureCacheVulkan_->DeviceRestore(draw_);
	shaderManagerVulkan_->DeviceRestore(draw_);
}

void GPU_Vulkan::GetStats(char *buffer, size_t bufsize) {
	size_t offset = FormatGPUStatsCommon(buffer, bufsize);
	buffer += offset;
	bufsize -= offset;
	if ((int)bufsize < 0)
		return;
	const DrawEngineVulkanStats &drawStats = drawEngine_.GetStats();
	char texStats[256];
	textureCacheVulkan_->GetStats(texStats, sizeof(texStats));
	snprintf(buffer, bufsize,
		"Vertex, Fragment, Pipelines loaded: %i, %i, %i\n"
		"Pushbuffer space used: UBO %d, Vtx %d, Idx %d\n"
		"%s\n",
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
		textureCache_->Clear(true);

		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		framebufferManager_->DestroyAllFBOs();
	}
}

std::vector<std::string> GPU_Vulkan::DebugGetShaderIDs(DebugShaderType type) {
	if (type == SHADER_TYPE_VERTEXLOADER) {
		return drawEngine_.DebugGetVertexLoaderIDs();
	} else if (type == SHADER_TYPE_PIPELINE) {
		return pipelineManager_->DebugGetObjectIDs(type);
	} else if (type == SHADER_TYPE_TEXTURE) {
		return textureCache_->GetTextureShaderCache()->DebugGetShaderIDs(type);
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
	} else if (type == SHADER_TYPE_TEXTURE) {
		return textureCache_->GetTextureShaderCache()->DebugGetShaderString(id, type, stringType);
	} else if (type == SHADER_TYPE_SAMPLER) {
		return textureCacheVulkan_->DebugGetSamplerString(id, stringType);
	} else if (type == SHADER_TYPE_VERTEX || type == SHADER_TYPE_FRAGMENT) {
		return shaderManagerVulkan_->DebugGetShaderString(id, type, stringType);
	} else {
		return std::string();
	}
}

std::string GPU_Vulkan::GetGpuProfileString() {
	VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	return rm->GetGpuProfileString();
}
