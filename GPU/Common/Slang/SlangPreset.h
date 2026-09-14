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

#include "Common/File/Path.h"

enum class SlangScaleType {
	Source,    // multiplier of this pass's input size
	Viewport,  // multiplier of the final display viewport
	Absolute,  // fixed pixel count
};

enum class SlangWrapMode {
	ClampToBorder,
	ClampToEdge,
	Repeat,
	MirroredRepeat,
};

enum class SlangFbFormat {
	Default,  // maps to R8G8B8A8_UNORM
	Srgb,     // maps to R8G8B8A8_SRGB
	Float,    // maps to R16G16B16A16_FLOAT
};

struct SlangParamDesc {
	std::string name;         // must match a float UBO/push member
	std::string description;  // human-readable label from #pragma parameter (may be empty)
	float initial = 0.0f;
	float minimum = 0.0f;
	float maximum = 1.0f;
	float step = 0.01f;
};

struct SlangLutDesc {
	std::string name;          // identifier used by shaders (e.g. "SamplerLUT1")
	std::string path;          // resolved absolute path to the PNG
	bool linear = false;       // <name>_linear
	bool mipmap = false;       // <name>_mipmap
	SlangWrapMode wrapMode = SlangWrapMode::ClampToBorder;  // <name>_wrap_mode
};

struct SlangPassDesc {
	std::string shaderPath;   // resolved absolute path to the .slang file
	std::string alias;        // #pragma name / aliasN, "" if none
	bool filterLinear = false;
	SlangScaleType scaleTypeX = SlangScaleType::Source;
	SlangScaleType scaleTypeY = SlangScaleType::Source;
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	bool srgbFramebuffer = false;       // srgb_framebufferN
	bool floatFramebuffer = false;      // float_framebufferN
	bool mipmapInput = false;           // mipmap_inputN
	SlangWrapMode wrapMode = SlangWrapMode::ClampToBorder;  // wrap_modeN (slang default is clamp_to_border)
	int frameCountMod = 0;              // frame_count_modN; 0 = no modulo
	SlangFbFormat formatOverride = SlangFbFormat::Default;  // from #pragma format, filled in Task 4
};

struct SlangPreset {
	Path basePath;   // directory the .slangp lives in
	std::vector<SlangPassDesc> passes;
	std::vector<SlangParamDesc> params;
	std::vector<SlangLutDesc> luts;
	int feedbackPass = -1;   // global feedback_pass; -1 = none
};
