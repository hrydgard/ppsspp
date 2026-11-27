// Copyright (c) 2017- PPSSPP Project.

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

#include <condition_variable>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Common/CommonWindows.h"
#include <d3d11.h>
#include <wrl/client.h>

#include "Common/CommonTypes.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/Common/FragmentShaderGenerator.h"

class D3D11Context;
class D3D11PushBuffer;

class D3D11FragmentShader {
public:
	D3D11FragmentShader(ID3D11Device *device, FShaderID id, const char *code, const std::vector<uint8_t> &bytecode, bool useHWTransform);
	~D3D11FragmentShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	ID3D11PixelShader *GetShader() const { return module_.Get(); }

protected:
	Microsoft::WRL::ComPtr<ID3D11PixelShader> module_;

	ID3D11Device *device_;
	std::string source_;
	bool failed_ = false;
	bool useHWTransform_;
	FShaderID id_;
};

class D3D11VertexShader {
public:
	D3D11VertexShader(ID3D11Device *device, VShaderID id, const char *code, const std::vector<uint8_t> &bytecode, bool useHWTransform);
	~D3D11VertexShader();

	const std::string &source() const { return source_; }
	const std::vector<uint8_t> &bytecode() const { return bytecode_; }
	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	ID3D11VertexShader *GetShader() const { return module_.Get(); }

protected:
	Microsoft::WRL::ComPtr<ID3D11VertexShader> module_;

	ID3D11Device *device_;
	std::string source_;
	std::vector<uint8_t> bytecode_;

	bool failed_ = false;
	bool useHWTransform_;
	VShaderID id_;
};

class D3D11PushBuffer;

enum class ClipInfoFlags;

class ShaderManagerD3D11 : public ShaderManagerCommon {
public:
	ShaderManagerD3D11(Draw::DrawContext *draw, ID3D11Device *device, ID3D11DeviceContext *context, D3D_FEATURE_LEVEL featureLevel);
	~ShaderManagerD3D11();

	void GetShaders(int prim, u32 vertexType, D3D11VertexShader **vshader, D3D11FragmentShader **fshader, const ComputedPipelineState &pipelineState, bool useHWTransform, ClipInfoFlags clipInfoFlags);
	void ClearShaders() override;

	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;
	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) override;

	uint64_t UpdateUniforms(bool useBufferedRendering, bool pixelMapped);
	void BindUniforms();

	// Compiles the shaders in use as they'll be with these use flags and sampling shader, on worker threads, into
	// the bytecode cache. Called when settings change, so the switch (on the next frame of the game) doesn't stall.
	void PrecompileForFlags(u32 useFlags, std::string_view samplingSource);

	// TODO: Avoid copying these buffers if same as last draw, can still point to it assuming we're still in the same pushbuffer.
	// Applies dirty changes and copies the buffer.
	bool IsBaseDirty() { return true; }
	bool IsLightDirty() { return true; }
	bool IsBoneDirty() { return true; }

private:
	void Clear();

	// Compiled bytecode by source, kept across ClearShaders and on disk. Thread-safe; waits for a worker that's
	// compiling the same code.
	std::vector<uint8_t> GetBytecode(const char *code, size_t size, bool vertex);
	void LoadBytecodeCache();
	void SaveBytecodeCache();
	void WaitForPrecompile();

	std::mutex bytecodeMutex_;
	std::condition_variable bytecodeCond_;
	std::unordered_map<uint64_t, std::vector<uint8_t>> bytecodeCache_;
	std::unordered_set<uint64_t> bytecodeUsed_;  // This session, the ones kept when the file gets too big.
	std::unordered_set<uint64_t> bytecodeInFlight_;
	int precompileTasks_ = 0;
	bool bytecodeCacheDirty_ = false;
	// The use flags the shaders in the caches were generated with.
	u32 shaderUseFlags_ = 0;

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;
	D3D_FEATURE_LEVEL featureLevel_;

	typedef std::map<FShaderID, D3D11FragmentShader *> FSCache;
	FSCache fsCache_;

	typedef std::map<VShaderID, D3D11VertexShader *> VSCache;
	VSCache vsCache_;

	char *codeBuffer_;

	// Uniform block scratchpad. These (the relevant ones) are copied to the current pushbuffer at draw time.
	UB_VS_FS_Base ub_base;
	UB_VS_Lights ub_lights;

	// Not actual pushbuffers, requires D3D11.1, let's try to live without that first.
	Microsoft::WRL::ComPtr<ID3D11Buffer> push_base;
	Microsoft::WRL::ComPtr<ID3D11Buffer> push_lights;

	D3D11FragmentShader *lastFShader_ = nullptr;
	D3D11VertexShader *lastVShader_ = nullptr;

	FShaderID lastFSID_;
	VShaderID lastVSID_;
};
