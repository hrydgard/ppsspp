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
	PassOutputSize, PassFeedbackSize, OriginalHistorySize, LutSize,  // Phase 2 size companions
	// texture (sampler2D) semantics:
	TexSource, TexOriginal,
	TexPassOutput, TexPassFeedback, TexOriginalHistory, TexLut,  // Phase 2 multi-input
};

struct SlangUniformMember {
	std::string name;
	SlangSemantic semantic;
	uint32_t offsetBytes;   // offset within the UBO block
	uint32_t sizeBytes;     // member size (4 for float, 16 for vec4, 64 for mat4)
	int index = -1;         // PassOutput3 → 3; alias position; LUT position; -1 if not indexed
};

struct SlangTextureBinding {
	std::string name;
	SlangSemantic semantic;
	int binding;            // sampler binding slot
	int index = -1;         // PassOutput3 → 3; alias position; LUT position; -1 if not indexed
};

struct PassReflection {
	std::vector<SlangUniformMember> uboMembers;
	uint32_t uboSizeBytes = 0;
	int uboBinding = -1;    // -1 if no UBO
	std::vector<SlangTextureBinding> textures;
};

// Classification context for Phase 2 multi-input semantics
struct SlangClassifyContext {
	std::vector<std::string> paramNames;    // #pragma parameter names
	std::vector<std::string> aliasNames;    // pass #pragma name / aliasN, in pass order
	std::vector<std::string> lutNames;      // preset LUT identifiers
};

// Classify a UBO/push member name. Sets *outIndex for indexed semantics (-1 otherwise).
SlangSemantic ClassifyUniform(const std::string &name, const SlangClassifyContext &ctx, int *outIndex);

// Classify a sampler2D name. Sets *outIndex for indexed semantics (-1 otherwise).
SlangSemantic ClassifyTexture(const std::string &name, const SlangClassifyContext &ctx, int *outIndex);
