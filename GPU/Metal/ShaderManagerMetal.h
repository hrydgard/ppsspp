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

// Objective-C++ only.

#include <cstdio>
#include <string>

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/Metal/MetalRenderManager.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Common/FragmentShaderGenerator.h"

enum class ClipInfoFlags;
struct ComputedPipelineState;

// The game shaders are the Vulkan ones, translated to MSL. So these follow DRAW_BINDING_* in DrawEngineVulkan.h.
enum {
	METAL_DRAW_BINDING_TEXTURE = 0,
	METAL_DRAW_BINDING_2ND_TEXTURE = 1,
	METAL_DRAW_BINDING_DEPAL_TEXTURE = 2,
	METAL_DRAW_BINDING_DYNUBO_BASE = 3,
	METAL_DRAW_BINDING_DYNUBO_LIGHT = 4,
};

class MetalShader {
public:
	MetalShader(id<MTLDevice> device, ShaderStage stage, const std::string &source, const std::string &desc);

	// Only translation failures. Compile failures show up when the pipeline is created.
	bool Failed() const { return function == nil; }
	std::string GetShaderString(DebugShaderStringType type) const;

	// Compiled when a pipeline first needs it, on a compile thread.
	MetalShaderFunction *function;
	MetalTranslatedShader info;
	std::string source;
	std::string desc;
};

class MetalVertexShader : public MetalShader {
public:
	MetalVertexShader(id<MTLDevice> device, VShaderID vsid, VertexShaderFlags flags, const std::string &source)
		: MetalShader(device, ShaderStage::Vertex, source, vsid.Description()), id_(vsid), flags_(flags) {}
	VShaderID id_;
	VertexShaderFlags flags_;
};

class MetalFragmentShader : public MetalShader {
public:
	MetalFragmentShader(id<MTLDevice> device, FShaderID fsid, FragmentShaderFlags flags, const std::string &source)
		: MetalShader(device, ShaderStage::Fragment, source, fsid.Description()), id_(fsid), flags_(flags) {}
	FShaderID id_;
	FragmentShaderFlags flags_;
};

class ShaderManagerMetal : public ShaderManagerCommon {
public:
	ShaderManagerMetal(Draw::DrawContext *draw);
	~ShaderManagerMetal();

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;
	void ClearShaders() override;

	void GetShaderIDs(int prim, u32 vertexType, VShaderID *vshader, FShaderID *fshader, const ComputedPipelineState &pipelineState, bool useHWTransform, ClipInfoFlags clipInfoFlags);

	MetalVertexShader *GetVertexShaderFromID(VShaderID vsid);
	MetalFragmentShader *GetFragmentShaderFromID(FShaderID fsid);

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) override;

	// The shader cache: the IDs of this game's shaders.
	bool SaveCache(FILE *f);
	bool LoadCache(FILE *f, int numVertexShaders, int numFragmentShaders);

	// Returns the dirty uniform flags.
	uint64_t UpdateUniforms(bool useBufferedRendering, bool pixelMapped);
	const UB_VS_FS_Base &BaseUniforms() const { return ub_base_; }
	const UB_VS_Lights &LightUniforms() const { return ub_lights_; }

private:
	void Clear();

	ShaderLanguageDesc compat_;
	DenseHashMap<VShaderID, MetalVertexShader *> vsCache_;
	DenseHashMap<FShaderID, MetalFragmentShader *> fsCache_;
	char *codeBuffer_;

	UB_VS_FS_Base ub_base_{};
	UB_VS_Lights ub_lights_{};

	VShaderID lastVSID_;
	FShaderID lastFSID_;
};
