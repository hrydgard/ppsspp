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

#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#include <d3d11.h>
#include <D3Dcompiler.h>

#include <map>

#include "Common/GPU/thin3d.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/CommonTypes.h"
#include "Common/Thread/Promise.h"
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/D3D11Util.h"
#include "ext/xxhash.h"

D3D11FragmentShader::D3D11FragmentShader(ID3D11Device *device, FShaderID id, const char *code, const std::vector<uint8_t> &bytecode, bool useHWTransform)
	: device_(device), useHWTransform_(useHWTransform), id_(id) {
	source_ = code;

	if (bytecode.empty() || FAILED(device->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, &module_)))
		failed_ = true;
}

D3D11FragmentShader::~D3D11FragmentShader() {
}

std::string D3D11FragmentShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return id_.Description();
	default:
		return "N/A";
	}
}

D3D11VertexShader::D3D11VertexShader(ID3D11Device *device, VShaderID id, const char *code, const std::vector<uint8_t> &bytecode, bool useHWTransform)
	: device_(device), useHWTransform_(useHWTransform), id_(id), bytecode_(bytecode) {
	source_ = code;

	if (bytecode.empty() || FAILED(device->CreateVertexShader(bytecode.data(), bytecode.size(), nullptr, &module_)))
		failed_ = true;
}

D3D11VertexShader::~D3D11VertexShader() {
}

std::string D3D11VertexShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return id_.Description();
	default:
		return "N/A";
	}
}

static constexpr size_t CODE_BUFFER_SIZE = 32768;

static constexpr uint32_t BYTECODE_CACHE_MAGIC = 0x43533344;  // "D3SC"
static constexpr uint32_t BYTECODE_CACHE_VERSION = 1;
// Past this, only what was used in the last session is written back (old versions' shaders pile up otherwise).
static constexpr size_t BYTECODE_CACHE_MAX_SIZE = 32 * 1024 * 1024;

static Path BytecodeCachePath() {
	return GetSysDirectory(DIRECTORY_APP_CACHE) / "d3d11shaders.bin";
}

ShaderManagerD3D11::ShaderManagerD3D11(Draw::DrawContext *draw, ID3D11Device *device, ID3D11DeviceContext *context, D3D_FEATURE_LEVEL featureLevel)
	: ShaderManagerCommon(draw), device_(device), context_(context), featureLevel_(featureLevel) {
	codeBuffer_ = new char[CODE_BUFFER_SIZE];
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));

	static_assert(sizeof(ub_base) <= 512, "ub_base grew too big");
	static_assert(sizeof(ub_lights) <= 512, "ub_lights grew too big");

	InitDeviceObjects();
	LoadBytecodeCache();
}

ShaderManagerD3D11::~ShaderManagerD3D11() {
	WaitForPrecompile();
	SaveBytecodeCache();
	ClearShaders();
	delete[] codeBuffer_;
	DestroyDeviceObjects();
}

static const char *ShaderProfile(D3D_FEATURE_LEVEL featureLevel, bool vertex) {
	if (featureLevel <= D3D_FEATURE_LEVEL_9_3) {
		return vertex ? "vs_4_0_level_9_1" : "ps_4_0_level_9_1";
	}
	return vertex ? "vs_4_0" : "ps_4_0";
}

std::vector<uint8_t> ShaderManagerD3D11::GetBytecode(const char *code, size_t size, bool vertex) {
	const char *profile = ShaderProfile(featureLevel_, vertex);
	const uint64_t key = XXH3_64bits_withSeed(code, size, XXH3_64bits(profile, strlen(profile)));
	{
		std::unique_lock<std::mutex> lock(bytecodeMutex_);
		bytecodeCond_.wait(lock, [&] { return bytecodeInFlight_.count(key) == 0; });
		auto iter = bytecodeCache_.find(key);
		if (iter != bytecodeCache_.end()) {
			bytecodeUsed_.insert(key);
			return iter->second;
		}
		bytecodeInFlight_.insert(key);
	}
	std::vector<uint8_t> bytecode = CompileShaderToBytecodeD3D11(code, size, profile, 0);
	{
		std::lock_guard<std::mutex> lock(bytecodeMutex_);
		bytecodeInFlight_.erase(key);
		if (!bytecode.empty()) {
			bytecodeCache_[key] = bytecode;
			bytecodeUsed_.insert(key);
			bytecodeCacheDirty_ = true;
		}
	}
	bytecodeCond_.notify_all();
	return bytecode;
}

void ShaderManagerD3D11::PrecompileForFlags(u32 useFlags, std::string_view samplingSource) {
	if (!draw_ || (vsCache_.empty() && fsCache_.empty())) {
		return;
	}
	// Only when just texture sampling changes: it changes the code but not what the IDs mean. With other flags,
	// old IDs can be ones the generators refuse.
	const u32 codeOnlyFlags = GPU_USE_TEX_SAMPLING | GPU_USE_TEX_SAMPLING_BOUNDS;
	if ((useFlags ^ shaderUseFlags_) & ~codeOnlyFlags) {
		return;
	}

	// The generators read the flags and the sampling source from globals, so they're swapped in for this
	// (on this thread, quick), and the compiles go to workers.
	const u32 oldFlags = gstate_c.GetUseFlags();
	const bool oldFlagsChanged = gstate_c.useFlagsChanged;
	const std::string oldSource = GetTextureSamplingShaderSource();
	gstate_c.SetUseFlags(useFlags);
	SetTextureSamplingShaderSource(samplingSource);

	std::vector<std::pair<std::string, bool>> sources;
	for (const auto &[id, vs] : vsCache_) {
		std::string genErrorString;
		uint32_t attrMask;
		uint64_t uniformMask;
		VertexShaderFlags flags;
		if (GenerateVertexShader(id, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &attrMask, &uniformMask, &flags, &genErrorString)) {
			sources.emplace_back(codeBuffer_, true);
		}
	}
	for (const auto &[id, fs] : fsCache_) {
		std::string genErrorString;
		uint64_t uniformMask;
		FragmentShaderFlags flags;
		if (GenerateFragmentShader(id, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &uniformMask, &flags, &genErrorString)) {
			sources.emplace_back(codeBuffer_, false);
		}
	}

	SetTextureSamplingShaderSource(oldSource);
	gstate_c.SetUseFlags(oldFlags);
	gstate_c.useFlagsChanged = oldFlagsChanged;

	{
		std::lock_guard<std::mutex> lock(bytecodeMutex_);
		precompileTasks_ += (int)sources.size();
	}
	for (auto &[source, vertex] : sources) {
		g_threadManager.EnqueueTask(new IndependentTask(TaskType::CPU_COMPUTE, TaskPriority::LOW, [this, source = std::move(source), vertex = vertex]() {
			GetBytecode(source.data(), source.size(), vertex);
			{
				std::lock_guard<std::mutex> lock(bytecodeMutex_);
				precompileTasks_--;
			}
			bytecodeCond_.notify_all();
		}));
	}
	INFO_LOG(Log::G3D, "Precompiling %d shaders for the new settings", (int)sources.size());
}

void ShaderManagerD3D11::WaitForPrecompile() {
	std::unique_lock<std::mutex> lock(bytecodeMutex_);
	bytecodeCond_.wait(lock, [&] { return precompileTasks_ == 0; });
}

void ShaderManagerD3D11::LoadBytecodeCache() {
	const Path path = BytecodeCachePath();
	std::string data;
	if (!VerifyShaderCacheChecksum(path) || !File::ReadBinaryFileToString(path, &data)) {
		return;
	}
	const uint8_t *p = (const uint8_t *)data.data();
	const uint8_t *end = p + data.size() - sizeof(uint64_t);  // Ends with the checksum.
	uint32_t header[3];
	if (end - p < (ptrdiff_t)sizeof(header)) {
		return;
	}
	memcpy(header, p, sizeof(header));
	p += sizeof(header);
	if (header[0] != BYTECODE_CACHE_MAGIC || header[1] != BYTECODE_CACHE_VERSION) {
		return;
	}
	for (uint32_t i = 0; i < header[2]; i++) {
		uint64_t key;
		uint32_t size;
		if (end - p < (ptrdiff_t)(sizeof(key) + sizeof(size))) {
			break;
		}
		memcpy(&key, p, sizeof(key));
		memcpy(&size, p + sizeof(key), sizeof(size));
		p += sizeof(key) + sizeof(size);
		if ((size_t)(end - p) < size) {
			break;
		}
		bytecodeCache_[key] = std::vector<uint8_t>(p, p + size);
		p += size;
	}
	INFO_LOG(Log::G3D, "Loaded %d D3D11 shaders from the cache", (int)bytecodeCache_.size());
}

void ShaderManagerD3D11::SaveBytecodeCache() {
	if (!bytecodeCacheDirty_) {
		return;
	}
	size_t total = 0;
	for (const auto &[key, bytecode] : bytecodeCache_) {
		total += bytecode.size();
	}
	const bool onlyUsed = total > BYTECODE_CACHE_MAX_SIZE;

	std::string data;
	uint32_t header[3] = { BYTECODE_CACHE_MAGIC, BYTECODE_CACHE_VERSION, 0 };
	data.append((const char *)header, sizeof(header));
	for (const auto &[key, bytecode] : bytecodeCache_) {
		if (onlyUsed && !bytecodeUsed_.count(key)) {
			continue;
		}
		const uint32_t size = (uint32_t)bytecode.size();
		data.append((const char *)&key, sizeof(key));
		data.append((const char *)&size, sizeof(size));
		data.append((const char *)bytecode.data(), bytecode.size());
		header[2]++;
	}
	memcpy(&data[0], header, sizeof(header));

	const Path path = BytecodeCachePath();
	File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
	if (!File::WriteDataToFile(false, data.data(), data.size(), path) || !AppendShaderCacheChecksum(path)) {
		WARN_LOG(Log::G3D, "Failed to write the D3D11 shader cache");
		File::Delete(path);
		return;
	}
	bytecodeCacheDirty_ = false;
}

void ShaderManagerD3D11::InitDeviceObjects() {
	D3D11_BUFFER_DESC desc{sizeof(ub_base), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
	ASSERT_SUCCESS(device_->CreateBuffer(&desc, nullptr, &push_base));
	desc.ByteWidth = sizeof(ub_lights);
	ASSERT_SUCCESS(device_->CreateBuffer(&desc, nullptr, &push_lights));
}

void ShaderManagerD3D11::DestroyDeviceObjects() {
	push_base.Reset();
	push_lights.Reset();
	Clear();
}

void ShaderManagerD3D11::DeviceLost() {
	DestroyDeviceObjects();
	draw_ = nullptr;
	device_ = nullptr;
	context_ = nullptr;
}

void ShaderManagerD3D11::DeviceRestore(Draw::DrawContext *draw) {
	// The restored context can be a new device, so don't keep the old pointers.
	draw_ = draw;
	device_ = (ID3D11Device *)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	context_ = (ID3D11DeviceContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	featureLevel_ = (D3D_FEATURE_LEVEL)draw->GetNativeObject(Draw::NativeObject::FEATURE_LEVEL);
	InitDeviceObjects();
}

void ShaderManagerD3D11::Clear() {
	for (const auto &[_, fs] : fsCache_) {
		delete fs;
	}
	for (const auto &[_, vs] : vsCache_) {
		delete vs;
	}
	fsCache_.clear();
	vsCache_.clear();
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

void ShaderManagerD3D11::ClearShaders() {
	Clear();
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);
}

uint64_t ShaderManagerD3D11::UpdateUniforms(bool useBufferedRendering, bool pixelMapped) {
	DirtyDepalForDraw(pixelMapped);
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		D3D11_MAPPED_SUBRESOURCE map;
		if (dirty & DIRTY_BASE_UNIFORMS) {
			BaseUpdateUniforms(&ub_base, dirty, useBufferedRendering, pixelMapped);
			// Map fails after device removal, and map.pData is garbage then.
			if (SUCCEEDED(context_->Map(push_base.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
				memcpy(map.pData, &ub_base, sizeof(ub_base));
				context_->Unmap(push_base.Get(), 0);
			}
		}
		if (dirty & DIRTY_LIGHT_UNIFORMS) {
			LightUpdateUniforms(&ub_lights, dirty);
			if (SUCCEEDED(context_->Map(push_lights.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
				memcpy(map.pData, &ub_lights, sizeof(ub_lights));
				context_->Unmap(push_lights.Get(), 0);
			}
		}
	}
	gstate_c.CleanUniforms();
	return dirty;
}

void ShaderManagerD3D11::BindUniforms() {
	ID3D11Buffer *vs_cbs[2] = { push_base.Get(), push_lights.Get() };
	ID3D11Buffer *ps_cbs[1] = { push_base.Get() };
	context_->VSSetConstantBuffers(0, 2, vs_cbs);
	context_->PSSetConstantBuffers(0, 1, ps_cbs);
}

void ShaderManagerD3D11::GetShaders(int prim, u32 vertexType, D3D11VertexShader **vshader, D3D11FragmentShader **fshader, const ComputedPipelineState &pipelineState, bool useHWTransform, ClipInfoFlags clipInfoFlags) {
	VShaderID VSID;
	FShaderID FSID;

	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, vertexType, useHWTransform, clipInfoFlags);
	} else {
		VSID = lastVSID_;
	}

	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID, pipelineState, draw_->GetBugs(), clipInfoFlags);
	} else {
		FSID = lastFSID_;
	}

	// Just update uniforms if this is the same shader as last time.
	if (lastVShader_ != nullptr && lastFShader_ != nullptr && VSID == lastVSID_ && FSID == lastFSID_) {
		*vshader = lastVShader_;
		*fshader = lastFShader_;
		// Already all set, no need to look up in shader maps.
		return;
	}

	VSCache::iterator vsIter = vsCache_.find(VSID);
	D3D11VertexShader *vs;
	if (vsIter == vsCache_.end()) {
		// Vertex shader not in cache. Let's compile it.
		std::string genErrorString;
		uint32_t attrMask;
		uint64_t uniformMask;
		VertexShaderFlags flags;
		GenerateVertexShader(VSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &attrMask, &uniformMask, &flags, &genErrorString);
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "VS length error: %d", (int)strlen(codeBuffer_));
		// OutputDebugStringA(codeBuffer_);
		vs = new D3D11VertexShader(device_, VSID, codeBuffer_, GetBytecode(codeBuffer_, strlen(codeBuffer_), true), useHWTransform);
		shaderUseFlags_ = gstate_c.GetUseFlags();
		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	lastVSID_ = VSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	D3D11FragmentShader *fs;
	if (fsIter == fsCache_.end()) {
		// Fragment shader not in cache. Let's compile it.
		std::string genErrorString;
		uint64_t uniformMask;
		FragmentShaderFlags flags;
		GenerateFragmentShader(FSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &uniformMask, &flags, &genErrorString);
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "FS length error: %d", (int)strlen(codeBuffer_));
		// OutputDebugStringA(codeBuffer_);
		fs = new D3D11FragmentShader(device_, FSID, codeBuffer_, GetBytecode(codeBuffer_, strlen(codeBuffer_), false), useHWTransform);
		shaderUseFlags_ = gstate_c.GetUseFlags();
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	lastFSID_ = FSID;

	_dbg_assert_(FSID.Bit(FS_BIT_FLATSHADE) == VSID.Bit(VS_BIT_FLATSHADE));
	_dbg_assert_(FSID.Bit(FS_BIT_LMODE) == VSID.Bit(VS_BIT_LMODE));
	_dbg_assert_(FSID.Bit(FS_BIT_MINMAX_DISCARD) == VSID.Bit(VS_BIT_FS_MINMAX_DISCARD));
	_dbg_assert_(FSID.Bit(FS_BIT_DEPTH_CLAMP) == VSID.Bit(VS_BIT_FS_DEPTH_CLAMP));

	lastVShader_ = vs;
	lastFShader_ = fs;

	*vshader = vs;
	*fshader = fs;
}

std::vector<std::string> ShaderManagerD3D11::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<uint64_t> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		for (auto &iter : vsCache_) {
			ids.push_back(iter.first.ToUint64());
		}
		break;
	}
	case SHADER_TYPE_FRAGMENT:
	{
		for (auto &iter : fsCache_) {
			ids.push_back(iter.first.ToUint64());
		}
		break;
	}
	default:
		break;
	}
	return ToSortedDebugShaderIdVec(ids);
}

std::string ShaderManagerD3D11::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		auto iter = vsCache_.find(VShaderID(shaderId));
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}

	case SHADER_TYPE_FRAGMENT:
	{
		auto iter = fsCache_.find(FShaderID(shaderId));
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}
	default:
		return "N/A";
	}
}
