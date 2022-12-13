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

#ifdef _WIN32
//#define SHADERLOG
#endif

#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/TimeUtil.h"

#include "Common/StringUtils.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/Log.h"
#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Common/GeometryShaderGenerator.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/FramebufferManagerVulkan.h"

// Most drivers treat vkCreateShaderModule as pretty much a memcpy. What actually
// takes time here, and makes this worthy of parallelization, is GLSLtoSPV.
// Takes ownership over tag.
static Promise<VkShaderModule> *CompileShaderModuleAsync(VulkanContext *vulkan, VkShaderStageFlagBits stage, const char *code, std::string *tag) {
	auto compile = [=] {
		PROFILE_THIS_SCOPE("shadercomp");

		std::string errorMessage;
		std::vector<uint32_t> spirv;

		bool success = GLSLtoSPV(stage, code, GLSLVariant::VULKAN, spirv, &errorMessage);

		if (!errorMessage.empty()) {
			if (success) {
				ERROR_LOG(G3D, "Warnings in shader compilation!");
			} else {
				ERROR_LOG(G3D, "Error in shader compilation!");
			}
			std::string numberedSource = LineNumberString(code);
			ERROR_LOG(G3D, "Messages: %s", errorMessage.c_str());
			ERROR_LOG(G3D, "Shader source:\n%s", numberedSource.c_str());
#if PPSSPP_PLATFORM(WINDOWS)
			OutputDebugStringA("Error messages:\n");
			OutputDebugStringA(errorMessage.c_str());
			OutputDebugStringA(numberedSource.c_str());
#endif
			Reporting::ReportMessage("Vulkan error in shader compilation: info: %s / code: %s", errorMessage.c_str(), code);
		}

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		if (success) {
			const char *createTag = tag ? tag->c_str() : nullptr;
			if (!createTag) {
				switch (stage) {
				case VK_SHADER_STAGE_VERTEX_BIT: createTag = "game_vertex"; break;
				case VK_SHADER_STAGE_FRAGMENT_BIT: createTag = "game_fragment"; break;
				case VK_SHADER_STAGE_GEOMETRY_BIT: createTag = "game_geometry"; break;
				case VK_SHADER_STAGE_COMPUTE_BIT: createTag = "game_compute"; break;
				default: break;
				}
			}

			success = vulkan->CreateShaderModule(spirv, &shaderModule, createTag);
#ifdef SHADERLOG
			OutputDebugStringA("OK");
#endif
			if (tag)
				delete tag;
		}
		return shaderModule;
	};

#if defined(_DEBUG)
	// Don't parallelize in debug mode, pathological behavior due to mutex locks in allocator which is HEAVILY used by glslang.
	return Promise<VkShaderModule>::AlreadyDone(compile());
#else
	return Promise<VkShaderModule>::Spawn(&g_threadManager, compile, TaskType::CPU_COMPUTE);
#endif
}


VulkanFragmentShader::VulkanFragmentShader(VulkanContext *vulkan, FShaderID id, FragmentShaderFlags flags, const char *code)
	: vulkan_(vulkan), id_(id), flags_(flags) {
	source_ = code;
	module_ = CompileShaderModuleAsync(vulkan, VK_SHADER_STAGE_FRAGMENT_BIT, source_.c_str(), new std::string(FragmentShaderDesc(id)));
	if (!module_) {
		failed_ = true;
	} else {
		VERBOSE_LOG(G3D, "Compiled fragment shader:\n%s\n", (const char *)code);
	}
}

VulkanFragmentShader::~VulkanFragmentShader() {
	if (module_) {
		VkShaderModule shaderModule = module_->BlockUntilReady();
		vulkan_->Delete().QueueDeleteShaderModule(shaderModule);
		vulkan_->Delete().QueueCallback([](void *m) {
			auto module = (Promise<VkShaderModule> *)m;
			delete module;
		}, module_);
	}
}

std::string VulkanFragmentShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return FragmentShaderDesc(id_);
	default:
		return "N/A";
	}
}

VulkanVertexShader::VulkanVertexShader(VulkanContext *vulkan, VShaderID id, VertexShaderFlags flags, const char *code, bool useHWTransform)
	: vulkan_(vulkan), useHWTransform_(useHWTransform), flags_(flags), id_(id) {
	source_ = code;
	module_ = CompileShaderModuleAsync(vulkan, VK_SHADER_STAGE_VERTEX_BIT, source_.c_str(), new std::string(VertexShaderDesc(id)));
	if (!module_) {
		failed_ = true;
	} else {
		VERBOSE_LOG(G3D, "Compiled vertex shader:\n%s\n", (const char *)code);
	}
}

VulkanVertexShader::~VulkanVertexShader() {
	if (module_) {
		VkShaderModule shaderModule = module_->BlockUntilReady();
		vulkan_->Delete().QueueDeleteShaderModule(shaderModule);
		vulkan_->Delete().QueueCallback([](void *m) {
			auto module = (Promise<VkShaderModule> *)m;
			delete module;
		}, module_);
	}
}

std::string VulkanVertexShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return VertexShaderDesc(id_);
	default:
		return "N/A";
	}
}

VulkanGeometryShader::VulkanGeometryShader(VulkanContext *vulkan, GShaderID id, const char *code)
	: vulkan_(vulkan), id_(id) {
	source_ = code;
	module_ = CompileShaderModuleAsync(vulkan, VK_SHADER_STAGE_GEOMETRY_BIT, source_.c_str(), new std::string(GeometryShaderDesc(id).c_str()));
	if (!module_) {
		failed_ = true;
	} else {
		VERBOSE_LOG(G3D, "Compiled geometry shader:\n%s\n", (const char *)code);
	}
}

VulkanGeometryShader::~VulkanGeometryShader() {
	if (module_) {
		VkShaderModule shaderModule = module_->BlockUntilReady();
		vulkan_->Delete().QueueDeleteShaderModule(shaderModule);
		vulkan_->Delete().QueueCallback([](void *m) {
			auto module = (Promise<VkShaderModule> *)m;
			delete module;
		}, module_);
	}
}

std::string VulkanGeometryShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return GeometryShaderDesc(id_);
	default:
		return "N/A";
	}
}

static constexpr size_t CODE_BUFFER_SIZE = 32768;

ShaderManagerVulkan::ShaderManagerVulkan(Draw::DrawContext *draw)
	: ShaderManagerCommon(draw), compat_(GLSL_VULKAN), fsCache_(16), vsCache_(16), gsCache_(16) {
	codeBuffer_ = new char[CODE_BUFFER_SIZE];
	VulkanContext *vulkan = (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	uboAlignment_ = vulkan->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment;
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));
	memset(&ub_bones, 0, sizeof(ub_bones));

	static_assert(sizeof(ub_base) <= 512, "ub_base grew too big");
	static_assert(sizeof(ub_lights) <= 512, "ub_lights grew too big");
	static_assert(sizeof(ub_bones) <= 384, "ub_bones grew too big");
}

ShaderManagerVulkan::~ShaderManagerVulkan() {
	ClearShaders();
	delete[] codeBuffer_;
}

void ShaderManagerVulkan::DeviceLost() {
	draw_ = nullptr;
}

void ShaderManagerVulkan::DeviceRestore(Draw::DrawContext *draw) {
	VulkanContext *vulkan = (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	draw_ = draw;
	uboAlignment_ = vulkan->GetPhysicalDeviceProperties().properties.limits.minUniformBufferOffsetAlignment;
}

void ShaderManagerVulkan::Clear() {
	fsCache_.Iterate([&](const FShaderID &key, VulkanFragmentShader *shader) {
		delete shader;
	});
	vsCache_.Iterate([&](const VShaderID &key, VulkanVertexShader *shader) {
		delete shader;
	});
	gsCache_.Iterate([&](const GShaderID &key, VulkanGeometryShader *shader) {
		delete shader;
	});
	fsCache_.Clear();
	vsCache_.Clear();
	gsCache_.Clear();
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	lastGSID_.set_invalid();
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
}

void ShaderManagerVulkan::ClearShaders() {
	Clear();
	DirtyShader();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
}

void ShaderManagerVulkan::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	lastGSID_.set_invalid();
	DirtyLastShader();
}

void ShaderManagerVulkan::DirtyLastShader() {
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
	lastGShader_ = nullptr;
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE);
}

uint64_t ShaderManagerVulkan::UpdateUniforms(bool useBufferedRendering) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		if (dirty & DIRTY_BASE_UNIFORMS)
			BaseUpdateUniforms(&ub_base, dirty, false, useBufferedRendering);
		if (dirty & DIRTY_LIGHT_UNIFORMS)
			LightUpdateUniforms(&ub_lights, dirty);
		if (dirty & DIRTY_BONE_UNIFORMS)
			BoneUpdateUniforms(&ub_bones, dirty);
	}
	gstate_c.CleanUniforms();
	return dirty;
}

void ShaderManagerVulkan::GetShaders(int prim, u32 vertType, VulkanVertexShader **vshader, VulkanFragmentShader **fshader, VulkanGeometryShader **gshader, const ComputedPipelineState &pipelineState, bool useHWTransform, bool useHWTessellation, bool weightsAsFloat, bool useSkinInDecode) {
	VShaderID VSID;
	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, vertType, useHWTransform, useHWTessellation, weightsAsFloat, useSkinInDecode);
	} else {
		VSID = lastVSID_;
	}

	FShaderID FSID;
	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID, pipelineState, draw_->GetBugs());
	} else {
		FSID = lastFSID_;
	}

	GShaderID GSID;
	if (gstate_c.IsDirty(DIRTY_GEOMETRYSHADER_STATE)) {
		gstate_c.Clean(DIRTY_GEOMETRYSHADER_STATE);
		ComputeGeometryShaderID(&GSID, draw_->GetBugs(), prim);
	} else {
		GSID = lastGSID_;
	}

	_dbg_assert_(FSID.Bit(FS_BIT_LMODE) == VSID.Bit(VS_BIT_LMODE));
	_dbg_assert_(FSID.Bit(FS_BIT_DO_TEXTURE) == VSID.Bit(VS_BIT_DO_TEXTURE));
	_dbg_assert_(FSID.Bit(FS_BIT_FLATSHADE) == VSID.Bit(VS_BIT_FLATSHADE));

	if (GSID.Bit(GS_BIT_ENABLED)) {
		_dbg_assert_(GSID.Bit(GS_BIT_LMODE) == VSID.Bit(VS_BIT_LMODE));
		_dbg_assert_(GSID.Bit(GS_BIT_DO_TEXTURE) == VSID.Bit(VS_BIT_DO_TEXTURE));
	}

	// Just update uniforms if this is the same shader as last time.
	if (lastVShader_ != nullptr && lastFShader_ != nullptr && VSID == lastVSID_ && FSID == lastFSID_ && GSID == lastGSID_) {
		*vshader = lastVShader_;
		*fshader = lastFShader_;
		*gshader = lastGShader_;
		_dbg_assert_msg_((*vshader)->UseHWTransform() == useHWTransform, "Bad vshader was cached");
		// Already all set, no need to look up in shader maps.
		return;
	}

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	VulkanVertexShader *vs = vsCache_.Get(VSID);
	if (!vs)	{
		// Vertex shader not in cache. Let's compile it.
		std::string genErrorString;
		uint64_t uniformMask = 0;  // Not used
		uint32_t attributeMask = 0;  // Not used
		VertexShaderFlags flags{};
		bool success = GenerateVertexShader(VSID, codeBuffer_, compat_, draw_->GetBugs(), &attributeMask, &uniformMask, &flags, &genErrorString);
		_assert_msg_(success, "VS gen error: %s", genErrorString.c_str());
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "VS length error: %d", (int)strlen(codeBuffer_));
		vs = new VulkanVertexShader(vulkan, VSID, flags, codeBuffer_, useHWTransform);
		vsCache_.Insert(VSID, vs);
	}

	VulkanFragmentShader *fs = fsCache_.Get(FSID);
	if (!fs) {
		// Fragment shader not in cache. Let's compile it.
		std::string genErrorString;
		uint64_t uniformMask = 0;  // Not used
		FragmentShaderFlags flags{};
		bool success = GenerateFragmentShader(FSID, codeBuffer_, compat_, draw_->GetBugs(), &uniformMask, &flags, &genErrorString);
		_assert_msg_(success, "FS gen error: %s", genErrorString.c_str());
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "FS length error: %d", (int)strlen(codeBuffer_));
		fs = new VulkanFragmentShader(vulkan, FSID, flags, codeBuffer_);
		fsCache_.Insert(FSID, fs);
	}

	VulkanGeometryShader *gs;
	if (GSID.Bit(GS_BIT_ENABLED)) {
		gs = gsCache_.Get(GSID);
		if (!gs) {
			// Geometry shader not in cache. Let's compile it.
			std::string genErrorString;
			bool success = GenerateGeometryShader(GSID, codeBuffer_, compat_, draw_->GetBugs(), &genErrorString);
			_assert_msg_(success, "GS gen error: %s", genErrorString.c_str());
			_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "GS length error: %d", (int)strlen(codeBuffer_));
			gs = new VulkanGeometryShader(vulkan, GSID, codeBuffer_);
			gsCache_.Insert(GSID, gs);
		}
	} else {
		gs = nullptr;
	}

	lastVSID_ = VSID;
	lastFSID_ = FSID;
	lastGSID_ = GSID;

	lastVShader_ = vs;
	lastFShader_ = fs;
	lastGShader_ = gs;

	*vshader = vs;
	*fshader = fs;
	*gshader = gs;
	_dbg_assert_msg_((*vshader)->UseHWTransform() == useHWTransform, "Bad vshader was computed");
}

std::vector<std::string> ShaderManagerVulkan::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		vsCache_.Iterate([&](const VShaderID &id, VulkanVertexShader *shader) {
			std::string idstr;
			id.ToString(&idstr);
			ids.push_back(idstr);
		});
		break;
	}
	case SHADER_TYPE_FRAGMENT:
	{
		fsCache_.Iterate([&](const FShaderID &id, VulkanFragmentShader *shader) {
			std::string idstr;
			id.ToString(&idstr);
			ids.push_back(idstr);
		});
		break;
	}
	case SHADER_TYPE_GEOMETRY:
	{
		gsCache_.Iterate([&](const GShaderID &id, VulkanGeometryShader *shader) {
			std::string idstr;
			id.ToString(&idstr);
			ids.push_back(idstr);
		});
		break;
	}
	default:
		break;
	}
	return ids;
}

std::string ShaderManagerVulkan::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		VulkanVertexShader *vs = vsCache_.Get(VShaderID(shaderId));
		return vs ? vs->GetShaderString(stringType) : "";
	}
	case SHADER_TYPE_FRAGMENT:
	{
		VulkanFragmentShader *fs = fsCache_.Get(FShaderID(shaderId));
		return fs ? fs->GetShaderString(stringType) : "";
	}
	case SHADER_TYPE_GEOMETRY:
	{
		VulkanGeometryShader *gs = gsCache_.Get(GShaderID(shaderId));
		return gs ? gs->GetShaderString(stringType) : "";
	}
	default:
		return "N/A";
	}
}

VulkanVertexShader *ShaderManagerVulkan::GetVertexShaderFromModule(VkShaderModule module) {
	VulkanVertexShader *vs = nullptr;
	vsCache_.Iterate([&](const VShaderID &id, VulkanVertexShader *shader) {
		Promise<VkShaderModule> *p = shader->GetModule();
		VkShaderModule m = p->BlockUntilReady();
		if (m == module)
			vs = shader;
	});
	return vs;
}

VulkanFragmentShader *ShaderManagerVulkan::GetFragmentShaderFromModule(VkShaderModule module) {
	VulkanFragmentShader *fs = nullptr;
	fsCache_.Iterate([&](const FShaderID &id, VulkanFragmentShader *shader) {
		Promise<VkShaderModule> *p = shader->GetModule();
		VkShaderModule m = p->BlockUntilReady();
		if (m == module)
			fs = shader;
	});
	return fs;
}

VulkanGeometryShader *ShaderManagerVulkan::GetGeometryShaderFromModule(VkShaderModule module) {
	VulkanGeometryShader *gs = nullptr;
	gsCache_.Iterate([&](const GShaderID &id, VulkanGeometryShader *shader) {
		Promise<VkShaderModule> *p = shader->GetModule();
		VkShaderModule m = p->BlockUntilReady();
		if (m == module)
			gs = shader;
	});
	return gs;
}

// Shader cache.
//
// We simply store the IDs of the shaders used during gameplay. On next startup of
// the same game, we simply compile all the shaders from the start, so we don't have to
// compile them on the fly later. We also store the Vulkan pipeline cache, so if it contains
// pipelines compiled from SPIR-V matching these shaders, pipeline creation will be practically
// instantaneous.

#define CACHE_HEADER_MAGIC 0xff51f420 
#define CACHE_VERSION 32
struct VulkanCacheHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t useFlags;
	uint32_t reserved;
	int numVertexShaders;
	int numFragmentShaders;
	int numGeometryShaders;
};

bool ShaderManagerVulkan::LoadCache(FILE *f) {
	VulkanCacheHeader header{};
	bool success = fread(&header, sizeof(header), 1, f) == 1;
	if (!success || header.magic != CACHE_HEADER_MAGIC) {
		WARN_LOG(G3D, "Shader cache magic mismatch");
		return false;
	}
	if (header.version != CACHE_VERSION) {
		WARN_LOG(G3D, "Shader cache version mismatch, %d, expected %d", header.version, CACHE_VERSION);
		return false;
	}

	if (header.useFlags != gstate_c.GetUseFlags()) {
		// This can simply be a result of sawExactEqualDepth_ having been flipped to true in the previous run.
		// Let's just keep going.
		WARN_LOG(G3D, "Shader cache useFlags mismatch, %08x, expected %08x", header.useFlags, gstate_c.GetUseFlags());
	}

	int failCount = 0;

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	for (int i = 0; i < header.numVertexShaders; i++) {
		VShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			ERROR_LOG(G3D, "Vulkan shader cache truncated (in VertexShaders)");
			return false;
		}
		bool useHWTransform = id.Bit(VS_BIT_USE_HW_TRANSFORM);
		std::string genErrorString;
		uint32_t attributeMask = 0;
		uint64_t uniformMask = 0;
		VertexShaderFlags flags;
		if (!GenerateVertexShader(id, codeBuffer_, compat_, draw_->GetBugs(), &attributeMask, &uniformMask, &flags, &genErrorString)) {
			WARN_LOG(G3D, "Failed to generate vertex shader during cache load");
			// We just ignore this one and carry on.
			failCount++;
			continue;
		}
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "VS length error: %d", (int)strlen(codeBuffer_));
		VulkanVertexShader *vs = new VulkanVertexShader(vulkan, id, flags, codeBuffer_, useHWTransform);
		vsCache_.Insert(id, vs);
	}
	uint32_t vendorID = vulkan->GetPhysicalDeviceProperties().properties.vendorID;

	for (int i = 0; i < header.numFragmentShaders; i++) {
		FShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			ERROR_LOG(G3D, "Vulkan shader cache truncated (in FragmentShaders)");
			return false;
		}
		std::string genErrorString;
		uint64_t uniformMask = 0;
		FragmentShaderFlags flags;
		if (!GenerateFragmentShader(id, codeBuffer_, compat_, draw_->GetBugs(), &uniformMask, &flags, &genErrorString)) {
			WARN_LOG(G3D, "Failed to generate fragment shader during cache load");
			// We just ignore this one and carry on.
			failCount++;
			continue;
		}
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "FS length error: %d", (int)strlen(codeBuffer_));
		VulkanFragmentShader *fs = new VulkanFragmentShader(vulkan, id, flags, codeBuffer_);
		fsCache_.Insert(id, fs);
	}

	for (int i = 0; i < header.numGeometryShaders; i++) {
		GShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			ERROR_LOG(G3D, "Vulkan shader cache truncated (in GeometryShaders)");
			return false;
		}
		std::string genErrorString;
		if (!GenerateGeometryShader(id, codeBuffer_, compat_, draw_->GetBugs(), &genErrorString)) {
			WARN_LOG(G3D, "Failed to generate geometry shader during cache load");
			// We just ignore this one and carry on.
			failCount++;
			continue;
		}
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "GS length error: %d", (int)strlen(codeBuffer_));
		VulkanGeometryShader *gs = new VulkanGeometryShader(vulkan, id, codeBuffer_);
		gsCache_.Insert(id, gs);
	}

	NOTICE_LOG(G3D, "ShaderCache: Loaded %d vertex, %d fragment shaders and %d geometry shaders (failed %d)", header.numVertexShaders, header.numFragmentShaders, header.numGeometryShaders, failCount);
	return true;
}

void ShaderManagerVulkan::SaveCache(FILE *f) {
	VulkanCacheHeader header{};
	header.magic = CACHE_HEADER_MAGIC;
	header.version = CACHE_VERSION;
	header.useFlags = gstate_c.GetUseFlags();
	header.reserved = 0;
	header.numVertexShaders = (int)vsCache_.size();
	header.numFragmentShaders = (int)fsCache_.size();
	header.numGeometryShaders = (int)gsCache_.size();
	bool writeFailed = fwrite(&header, sizeof(header), 1, f) != 1;
	vsCache_.Iterate([&](const VShaderID &id, VulkanVertexShader *vs) {
		writeFailed = writeFailed || fwrite(&id, sizeof(id), 1, f) != 1;
	});
	fsCache_.Iterate([&](const FShaderID &id, VulkanFragmentShader *fs) {
		writeFailed = writeFailed || fwrite(&id, sizeof(id), 1, f) != 1;
	});
	gsCache_.Iterate([&](const GShaderID &id, VulkanGeometryShader *gs) {
		writeFailed = writeFailed || fwrite(&id, sizeof(id), 1, f) != 1;
	});
	if (writeFailed) {
		ERROR_LOG(G3D, "Failed to write Vulkan shader cache, disk full?");
	} else {
		NOTICE_LOG(G3D, "Saved %d vertex and %d fragment shaders", header.numVertexShaders, header.numFragmentShaders);
	}
}
