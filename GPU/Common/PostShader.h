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

#pragma once

#include <string>
#include <vector>

#include "Common/GPU/thin3d.h"

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
	// Is used to post-process stereo-rendering to mono, like red/blue.
	bool isStereo;
	// Use 2x display resolution for supersampling with blurry shaders.
	int SSAAFilterLevel;
	// Force constant/max refresh for animated filters
	bool requires60fps;
	// Takes previous frame(s) as input (for blending effects.)
	bool usePreviousFrame;
	// How many previous frames this shader gets bound (0 if !usePreviousFrame). Already clamped to what
	// the current device supports, see GetPostShaderMaxPreviousFrames(). They're bound as
	// sampler2 (1 frame ago), sampler3 (2 frames ago), ... sampler(1 + previousFrames).
	int previousFrames;
	// What the ini asked for, before clamping. Only for diagnostics.
	int previousFramesRequested;

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

	bool operator == (const std::string &other) const {
		return name == other;
	}
	bool operator == (const ShaderInfo &other) const {
		return name == other.name;
	}

	bool operator < (const ShaderInfo &other) const {
		if (name < other.name) return true;
		if (name > other.name) return false;
		// Tie breaker
		if (iniFile < other.iniFile) return true;
		if (iniFile > other.iniFile) return false;
		return false;
	}
};

struct TextureShaderInfo {
	Path iniFile;
	std::string section;
	std::string name;

	Path computeShaderFile;
	std::vector<Path> computeShaderFiles;

	// Upscaling shaders have a fixed scale factor.
	int scaleFactor;

	// Some shaders need to pass some data to the shader, and this is the buffer for that. It's up to the shader to interpret it.
	Path constantBuffer;
	bool hidden = false;

	bool operator == (const std::string &other) const {
		return name == other;
	}
	bool operator == (const TextureShaderInfo &other) const {
		return name == other.name;
	}

	bool operator < (const TextureShaderInfo &other) const {
		if (name < other.name) return true;
		if (name > other.name) return false;
		// Tie breaker
		if (iniFile < other.iniFile) return true;
		if (iniFile > other.iniFile) return false;
		return false;
	}
};

// Texture slot layout for post-shaders:
//  slot 0 (sampler0): output of the previous pass (or the source image on the first pass)
//  slot 1 (sampler1): the original source image
//  slot 2.. (sampler2..): previous frames, most recent first.
constexpr int POSTSHADER_PREVIOUS_FRAME_FIRST_SLOT = 2;
// Slots we keep free on every device for future post-shader inputs (depth buffer, more multipass
// inputs, etc.), so that adding those later doesn't break shaders that use lots of previous frames.
constexpr int POSTSHADER_RESERVED_TEXTURE_SLOTS = 2;
// Hard cap on previous frames, even on APIs with huge binding limits. Each one is a full-size framebuffer.
constexpr int POSTSHADER_MAX_PREVIOUS_FRAMES = 16;

// How many previous frames a post-shader can get on this device. At least 1 (legacy UsePreviousFrame behavior).
// Examples: GLES 2.0 / D3D11 FL9 (8 slots) -> 4, D3D11 / GLES 3 (16 slots) -> 12, desktop GL / Vulkan (20+) -> 16.
int GetPostShaderMaxPreviousFrames(const Draw::DrawContext *draw);

void ReloadAllPostShaderInfo(Draw::DrawContext *draw);

const ShaderInfo *GetPostShaderInfo(std::string_view name);
std::vector<const ShaderInfo *> GetPostShaderChain(const std::string &name);
std::vector<const ShaderInfo *> GetFullPostShadersChain(const std::vector<std::string> &names);
bool PostShaderChainRequires60FPS(const std::vector<const ShaderInfo *> &chain);
const std::vector<ShaderInfo> &GetAllPostShaderInfo();

const TextureShaderInfo *GetTextureShaderInfo(std::string_view name);
const std::vector<TextureShaderInfo> &GetAllTextureShaderInfo();
void RemoveUnknownPostShaders(std::vector<std::string> *names);

// Call this any time you alter the postshader list. It makes sure
// that "usePrevFrame" shaders are at the end, and that there's only one.
// It'll also enforce any similar future rules.
void FixPostShaderOrder(std::vector<std::string> *names);
