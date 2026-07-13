// Copyright (c) 2026- PPSSPP Project.

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

#include <string>
#include <vector>
#include <cstdint>

enum class SlangSemantic {
	Unknown,
	// uniform (UBO/push member) semantics:
	MVP, OutputSize, FinalViewportSize, FrameCount, FrameDirection, Rotation,
	SourceSize, OriginalSize,          // Phase 1 texture-size companions
	UserParameter,                     // matches a #pragma parameter float
	// texture (sampler2D) semantics:
	TexSource, TexOriginal,
};

struct SlangUniformMember {
	std::string name;
	SlangSemantic semantic;
	uint32_t offsetBytes;   // offset within the UBO block
	uint32_t sizeBytes;     // member size (4 for float, 16 for vec4, 64 for mat4)
};

struct SlangTextureBinding {
	std::string name;
	SlangSemantic semantic;
	int binding;            // sampler binding slot
};

struct PassReflection {
	std::vector<SlangUniformMember> uboMembers;
	uint32_t uboSizeBytes = 0;
	int uboBinding = -1;    // -1 if no UBO
	std::vector<SlangTextureBinding> textures;
};

// Classify a UBO/push member name. knownParams = names from #pragma parameter.
SlangSemantic ClassifyUniform(const std::string &name, const std::vector<std::string> &knownParams);

// Classify a sampler2D name.
SlangSemantic ClassifyTexture(const std::string &name);
