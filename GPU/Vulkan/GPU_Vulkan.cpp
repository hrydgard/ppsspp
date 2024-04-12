
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
#include "Common/Thread/ThreadUtil.h"

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

GPU_Vulkan::GPU_Vulkan(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommonHW(gfxCtx, draw), drawEngine_(draw) {
	gstate_c.SetUseFlags(CheckGPUFeatures());
	drawEngine_.InitDeviceObjects();

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
	framebufferManagerVulkan_->Init(msaaLevel_);
	textureCacheVulkan_->SetFramebufferManager(framebufferManagerVulkan_);
	textureCacheVulkan_->SetShaderManager(shaderManagerVulkan_);
	textureCacheVulkan_->SetDrawEngine(&drawEngine_);

	InitDeviceObjects();

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	BuildReportingInfo();

	textureCache_->NotifyConfigChanged();

	// Load shader cache.
	std::string discID = g_paramSFO.GetDiscID();
	if (discID.size()) {
		File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
		shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) / (discID + ".vkshadercache");
		LoadCache(shaderCachePath_);
	}
}

void GPU_Vulkan::LoadCache(const Path &filename) {
	if (!g_Config.bShaderCache) {
		WARN_LOG(G3D, "Shader cache disabled. Not loading.");
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
	bool result = shaderManagerVulkan_->LoadCacheFlags(f, &drawEngine_);
	if (!result) {
		WARN_LOG(G3D, "ShaderManagerVulkan failed to load cache header.");
	}
	if (result) {
		// Reload use flags in case LoadCacheFlags() changed them.
		if (drawEngineCommon_->EverUsedExactEqualDepth()) {
			sawExactEqualDepth_ = true;
		}
		gstate_c.SetUseFlags(CheckGPUFeatures());
		result = shaderManagerVulkan_->LoadCache(f);
		if (!result) {
			WARN_LOG(G3D, "ShaderManagerVulkan failed to load cache.");
		}
	}
	if (result) {
		// WARNING: See comment in LoadPipelineCache if you are tempted to flip the second parameter to true.
		result = pipelineManager_->LoadPipelineCache(f, false, shaderManagerVulkan_, draw_, drawEngine_.GetPipelineLayout(), msaaLevel_);
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
	shaderManagerVulkan_->SaveCache(f, &drawEngine_);
	// WARNING: See comment in LoadCache if you are tempted to flip the second parameter to true.
	pipelineManager_->SavePipelineCache(f, false, shaderManagerVulkan_, draw_);
	INFO_LOG(G3D, "Saved Vulkan pipeline cache");
	fclose(f);
}

GPU_Vulkan::~GPU_Vulkan() {
	if (draw_) {
		VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		// This now also does a hard sync with the render thread, so that we can safely delete our pipeline layout below.
		rm->StopThreads();
		rm->CheckNothingPending();
	}

	SaveCache(shaderCachePath_);

	// StopThreads should have ensured that no pipelines are queued to compile at this point. So we can tear it down.
	delete pipelineManager_;
	pipelineManager_ = nullptr;

	// Note: We save the cache in DeviceLost
	DestroyDeviceObjects();
	drawEngine_.DeviceLost();
	shaderManager_->ClearShaders();

	// other managers are deleted in ~GPUCommonHW.
	if (draw_) {
		VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->StartThreads();
	}
}

u32 GPU_Vulkan::CheckGPUFeatures() const {
	uint32_t features = GPUCommonHW::CheckGPUFeatures();

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);

	// Could simplify this, but it's good as documentation.
	switch (vulkan->GetPhysicalDeviceProperties().properties.vendorID) {
	case VULKAN_VENDOR_AMD:
		// Accurate depth is required on AMD (due to reverse-Z driver bug) so we ignore the compat flag to disable it on those. See #9545
		features |= GPU_USE_ACCURATE_DEPTH;
		break;
	case VULKAN_VENDOR_QUALCOMM:
		// Accurate depth is required on Adreno too (seems to also have a reverse-Z driver bug).
		features |= GPU_USE_ACCURATE_DEPTH;
		break;
	case VULKAN_VENDOR_ARM:
	{
		// This check is probably not exactly accurate. But old drivers had problems with reverse-Z, just like AMD and Qualcomm.

		// NOTE: Galaxy S8 has version 16 but still seems to have some problems with accurate depth.

		// TODO: Move this check to thin3d_vulkan.

		bool driverTooOld = IsHashMaliDriverVersion(vulkan->GetPhysicalDeviceProperties().properties)
			|| VK_VERSION_MAJOR(vulkan->GetPhysicalDeviceProperties().properties.driverVersion) < 14;

		if (!PSP_CoreParameter().compat.flags().DisableAccurateDepth || driverTooOld) {
			features |= GPU_USE_ACCURATE_DEPTH;
		} else {
			features &= ~GPU_USE_ACCURATE_DEPTH;
		}
		break;
	}
	case VULKAN_VENDOR_IMGTEC:
		// We ignore the disable flag on IMGTec. Another reverse-Z bug (plus, not really any reason to bother). See #17044
		features |= GPU_USE_ACCURATE_DEPTH;
		break;
	default:
		// On other GPUs we'll just assume we don't need inaccurate depth, leaving ARM Mali as the odd one out.
		features |= GPU_USE_ACCURATE_DEPTH;
		break;
	}

	// Might enable this later - in the first round we are mostly looking at depth/stencil/discard.
	// if (!g_Config.bEnableVendorBugChecks)
	// 	features |= GPU_USE_ACCURATE_DEPTH;

	// Mandatory features on Vulkan, which may be checked in "centralized" code
	features |= GPU_USE_TEXTURE_LOD_CONTROL;
	features |= GPU_USE_INSTANCE_RENDERING;
	features |= GPU_USE_VERTEX_TEXTURE_FETCH;
	features |= GPU_USE_TEXTURE_FLOAT;

	// Fall back to geometry shader culling if we can't do vertex range culling.
	// Checking accurate depth here because the old depth path is uncommon and not well tested for this.
	if (draw_->GetDeviceCaps().geometryShaderSupported && (features & GPU_USE_ACCURATE_DEPTH) != 0) {
		const bool useGeometry = g_Config.bUseGeometryShader && !draw_->GetBugs().Has(Draw::Bugs::GEOMETRY_SHADERS_SLOW_OR_BROKEN);
		const bool vertexSupported = draw_->GetDeviceCaps().clipDistanceSupported && draw_->GetDeviceCaps().cullDistanceSupported;
		if (useGeometry && (!vertexSupported || (features & GPU_USE_VS_RANGE_CULLING) == 0)) {
			// Switch to culling via the geometry shader if not fully supported in vertex.
			features |= GPU_USE_GS_CULLING;
			features &= ~GPU_USE_VS_RANGE_CULLING;
		}
	}

	if (!draw_->GetBugs().Has(Draw::Bugs::PVR_BAD_16BIT_TEXFORMATS)) {
		// These are VULKAN_4444_FORMAT and friends.
		// Note that we are now using the correct set of formats - the only cases where some may be missing
		// are non-conformant implementations like MoltenVK.
		uint32_t fmt4444 = draw_->GetDataFormatSupport(Draw::DataFormat::B4G4R4A4_UNORM_PACK16);
		uint32_t fmt1555 = draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16);
		uint32_t fmt565 = draw_->GetDataFormatSupport(Draw::DataFormat::R5G6B5_UNORM_PACK16);
		if ((fmt4444 & Draw::FMT_TEXTURE) && (fmt565 & Draw::FMT_TEXTURE) && (fmt1555 & Draw::FMT_TEXTURE)) {
			features |= GPU_USE_16BIT_FORMATS;
		} else {
			INFO_LOG(G3D, "Deficient texture format support: 4444: %d  1555: %d  565: %d", fmt4444, fmt1555, fmt565);
		}
	}

	if (g_Config.bStereoRendering && draw_->GetDeviceCaps().multiViewSupported) {
		features |= GPU_USE_SINGLE_PASS_STEREO;
		features |= GPU_USE_SIMPLE_STEREO_PERSPECTIVE;

		if (features & GPU_USE_GS_CULLING) {
			// Many devices that support stereo and GS don't support GS during stereo.
			features &= ~GPU_USE_GS_CULLING;
			features |= GPU_USE_VS_RANGE_CULLING;
		}
	}

	// Attempt to workaround #17386
	if (draw_->GetBugs().Has(Draw::Bugs::UNIFORM_INDEXING_BROKEN)) {
		features &= ~GPU_USE_LIGHT_UBERSHADER;
	}

	features |= GPU_USE_FRAMEBUFFER_ARRAYS;
	return CheckGPUFeaturesLate(features);
}

void GPU_Vulkan::BeginHostFrame() {
	GPUCommonHW::BeginHostFrame();

	drawEngine_.BeginFrame();
	textureCache_->StartFrame();

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	int curFrame = vulkan->GetCurFrame();

	framebufferManager_->BeginFrame();

	shaderManagerVulkan_->DirtyLastShader();
	gstate_c.Dirty(DIRTY_ALL);

	if (gstate_c.useFlagsChanged) {
		// TODO: It'd be better to recompile them in the background, probably?
		// This most likely means that saw equal depth changed.
		WARN_LOG(G3D, "Shader use flags changed, clearing all shaders and depth buffers");
		// TODO: Not all shaders need to be recompiled. In fact, quite few? Of course, depends on
		// the use flag change.. This is a major frame rate hitch in the start of a race in Outrun.
		shaderManager_->ClearShaders();
		pipelineManager_->Clear();
		framebufferManager_->ClearAllDepthBuffers();
		gstate_c.useFlagsChanged = false;
	}

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

	drawEngine_.EndFrame();

	GPUCommonHW::EndHostFrame();
}

// Needs to be called on GPU thread, not reporting thread.
void GPU_Vulkan::BuildReportingInfo() {
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	const auto &props = vulkan->GetPhysicalDeviceProperties().properties;
	const auto &available = vulkan->GetDeviceFeatures().available;

#define CHECK_BOOL_FEATURE(n) do { if (available.standard.n) { featureNames += ", " #n; } } while (false)
#define CHECK_BOOL_FEATURE_MULTIVIEW(n) do { if (available.multiview.n) { featureNames += ", " #n; } } while (false)

	std::string featureNames = "";
	CHECK_BOOL_FEATURE(fullDrawIndexUint32);
	CHECK_BOOL_FEATURE(geometryShader);
	CHECK_BOOL_FEATURE(sampleRateShading);
	CHECK_BOOL_FEATURE(dualSrcBlend);
	CHECK_BOOL_FEATURE(logicOp);
	CHECK_BOOL_FEATURE(multiDrawIndirect);
	CHECK_BOOL_FEATURE(drawIndirectFirstInstance);
	CHECK_BOOL_FEATURE(depthClamp);
	CHECK_BOOL_FEATURE(depthBiasClamp);
	CHECK_BOOL_FEATURE(depthBounds);
	CHECK_BOOL_FEATURE(samplerAnisotropy);
	CHECK_BOOL_FEATURE(textureCompressionETC2);
	CHECK_BOOL_FEATURE(textureCompressionASTC_LDR);
	CHECK_BOOL_FEATURE(textureCompressionBC);
	CHECK_BOOL_FEATURE(occlusionQueryPrecise);
	CHECK_BOOL_FEATURE(pipelineStatisticsQuery);
	CHECK_BOOL_FEATURE(fragmentStoresAndAtomics);
	CHECK_BOOL_FEATURE(shaderTessellationAndGeometryPointSize);
	CHECK_BOOL_FEATURE(shaderStorageImageMultisample);
	CHECK_BOOL_FEATURE(shaderSampledImageArrayDynamicIndexing);
	CHECK_BOOL_FEATURE(shaderClipDistance);
	CHECK_BOOL_FEATURE(shaderCullDistance);
	CHECK_BOOL_FEATURE(shaderInt64);
	CHECK_BOOL_FEATURE(shaderInt16);
	CHECK_BOOL_FEATURE_MULTIVIEW(multiview);
	CHECK_BOOL_FEATURE_MULTIVIEW(multiviewGeometryShader);

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

void GPU_Vulkan::FinishDeferred() {
	drawEngine_.FinishDeferred();
}

void GPU_Vulkan::InitDeviceObjects() {
	INFO_LOG(G3D, "GPU_Vulkan::InitDeviceObjects");

	uint32_t hacks = 0;
	if (PSP_CoreParameter().compat.flags().MGS2AcidHack)
		hacks |= QUEUE_HACK_MGS2_ACID;
	if (PSP_CoreParameter().compat.flags().SonicRivalsHack)
		hacks |= QUEUE_HACK_SONIC;

	// Always on.
	hacks |= QUEUE_HACK_RENDERPASS_MERGE;

	if (hacks) {
		VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->GetQueueRunner()->EnableHacks(hacks);
	}
}

void GPU_Vulkan::DestroyDeviceObjects() {
	INFO_LOG(G3D, "GPU_Vulkan::DestroyDeviceObjects");
	// Need to turn off hacks when shutting down the GPU. Don't want them running in the menu.
	if (draw_) {
		VulkanRenderManager *rm = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		if (rm)
			rm->GetQueueRunner()->EnableHacks(0);
	}
}

void GPU_Vulkan::CheckRenderResized() {
	if (renderResized_) {
		GPUCommonHW::CheckRenderResized();
		pipelineManager_->InvalidateMSAAPipelines();
		framebufferManager_->ReleasePipelines();
	}
}

void GPU_Vulkan::DeviceLost() {
	// draw_ is normally actually still valid here in Vulkan. But we null it out in GPUCommonHW::DeviceLost so we don't try to use it again.
	// So, we have to save it here to be able to call ReleaseCompileQueue().
	Draw::DrawContext *draw = draw_;
	if (draw) {
		VulkanRenderManager *rm = (VulkanRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->StopThreads();
	}

	if (shaderCachePath_.Valid()) {
		SaveCache(shaderCachePath_);
	}
	DestroyDeviceObjects();
	pipelineManager_->DeviceLost();

	GPUCommonHW::DeviceLost();

	if (draw) {
		VulkanRenderManager *rm = (VulkanRenderManager *)draw->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		rm->StartThreads();
	}
}

void GPU_Vulkan::DeviceRestore(Draw::DrawContext *draw) {
	GPUCommonHW::DeviceRestore(draw);

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	pipelineManager_->DeviceRestore(vulkan);

	InitDeviceObjects();
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
		"Pushbuffer space used: Vtx %d, Idx %d\n"
		"%s\n",
		shaderManagerVulkan_->GetNumVertexShaders(),
		shaderManagerVulkan_->GetNumFragmentShaders(),
		pipelineManager_->GetNumPipelines(),
		drawStats.pushVertexSpaceUsed,
		drawStats.pushIndexSpaceUsed,
		texStats
	);
}

std::vector<std::string> GPU_Vulkan::DebugGetShaderIDs(DebugShaderType type) {
	switch (type) {
	case SHADER_TYPE_PIPELINE:
		return pipelineManager_->DebugGetObjectIDs(type);
	case SHADER_TYPE_SAMPLER:
		return textureCacheVulkan_->DebugGetSamplerIDs();
	default:
		return GPUCommonHW::DebugGetShaderIDs(type);
	}
}

std::string GPU_Vulkan::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	switch (type) {
	case SHADER_TYPE_PIPELINE:
		return pipelineManager_->DebugGetObjectString(id, type, stringType, shaderManagerVulkan_);
	case SHADER_TYPE_SAMPLER:
		return textureCacheVulkan_->DebugGetSamplerString(id, stringType);
	default:
		return GPUCommonHW::DebugGetShaderString(id, type, stringType);
	}
}
