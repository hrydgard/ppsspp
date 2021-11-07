// Copyright (c) 2013- PPSSPP Project.

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


// Postprocessing shader manager
// For FXAA, "Natural", bloom, B&W, cross processing and whatnot.

#include <string>
#include <vector>

#include "Common/Data/Format/IniFile.h"

struct ShaderInfo {
	Path iniFile;  // which ini file was this definition in? So we can write settings back later
	std::string section;  // ini file section. This is saved.
	std::string name;     // Fancy display name.
	std::string parent;   // Parent shader ini section name.

	Path fragmentShaderFile;
	Path vertexShaderFile;

	// Show this shader in lists (i.e. not just for chaining.)
	bool visible;
	// Run at output instead of input resolution
	bool outputResolution;
	// Use x1 rendering res + nearest screen scaling filter
	bool isUpscalingFilter;
	// Use 2x display resolution for supersampling with blurry shaders.
	int SSAAFilterLevel;
	// Force constant/max refresh for animated filters
	bool requires60fps;
	// Takes previous frame as input (for blending effects.)
	bool usePreviousFrame;

	struct Setting {
		std::string name;
		float value;
		float maxValue;
		float minValue;
		float step;
	};
	Setting settings[4];

	// TODO: Add support for all kinds of fun options like mapping the depth buffer,
	// SRGB texture reads, etc.  prev shader?

	bool operator == (const std::string &other) {
		return name == other;
	}
	bool operator == (const ShaderInfo &other) {
		return name == other.name;
	}
};

struct TextureShaderInfo {
	Path iniFile;
	std::string section;
	std::string name;

	Path computeShaderFile;

	// Upscaling shaders have a fixed scale factor.
	int scaleFactor;

	bool operator == (const std::string &other) {
		return name == other;
	}
	bool operator == (const TextureShaderInfo &other) {
		return name == other.name;
	}
};

void ReloadAllPostShaderInfo(Draw::DrawContext *draw);

const ShaderInfo *GetPostShaderInfo(const std::string &name);
std::vector<const ShaderInfo *> GetPostShaderChain(const std::string &name);
std::vector<const ShaderInfo *> GetFullPostShadersChain(const std::vector<std::string> &names);
bool PostShaderChainRequires60FPS(const std::vector<const ShaderInfo *> &chain);
const std::vector<ShaderInfo> &GetAllPostShaderInfo();

const TextureShaderInfo *GetTextureShaderInfo(const std::string &name);
const std::vector<TextureShaderInfo> &GetAllTextureShaderInfo();
