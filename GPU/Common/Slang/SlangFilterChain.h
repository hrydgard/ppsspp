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
#include <map>
#include "Common/File/Path.h"
#include "Common/GPU/thin3d.h"
#include "GPU/Common/Slang/SlangPreset.h"
#include "GPU/Common/Slang/SlangPassCompiler.h"

// SlangFilterChain loads and executes a multi-pass slang shader preset.
// It manages GPU resources (pipelines, framebuffers, samplers), parses the preset,
// compiles each pass, and runs the full chain frame-by-frame.
class SlangFilterChain {
public:
	explicit SlangFilterChain(Draw::DrawContext *draw);
	~SlangFilterChain();

	// Parse and compile a .slangp preset. Reads the preset file and all .slang shader files,
	// compiles each pass, and creates GPU resources. Returns false + *error on failure.
	bool Load(const Path &presetPath, std::string *error);

	bool IsValid() const { return valid_; }

	// Execute the filter chain for one frame. `source` is the input framebuffer (game output),
	// sourceW/H are its dimensions, viewportW/H are the final display dimensions, and frameCount
	// is the current frame number (for animations). Returns the final pass output framebuffer,
	// or nullptr on failure.
	Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
	                       int viewportW, int viewportH, int frameCount);

	// Device loss/restore handling for graphics API resets.
	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	// Runtime overrides for #pragma parameter values: name -> value. Applied in Run()'s
	// UserParameter binding; a name absent from the map falls back to the parameter's default.
	void SetParamOverrides(const std::map<std::string, float> &overrides);

	// Resolve a parameter value: if present in overrides return it, else return the param's initial, else 0.0f.
	static float ResolveParamValue(const std::string &name, const std::vector<SlangParamDesc> &params,
	                                const std::map<std::string, float> &overrides);

private:
	// Release all GPU resources (pipelines, framebuffers, samplers, quad) WITHOUT
	// dropping the device pointer. Safe to call from Load() to clear a previous chain.
	void ReleaseResources();

	Draw::DrawContext *draw_ = nullptr;
	SlangPreset preset_;
	std::vector<SlangCompiledPass> passes_;
	std::vector<Draw::Framebuffer *> passFramebuffers_;
	std::vector<Draw::Framebuffer *> historyRing_;  // OriginalHistory1..N ring (newest-first)
	int historyDepth_ = 0;  // max OriginalHistory index referenced; 0 if none
	std::vector<bool> passHasFeedback_;  // index-aligned: true if pass i needs a feedback buffer
	std::vector<Draw::Framebuffer *> feedbackBuffers_;  // previous-frame pass output for feedback
	std::vector<Draw::Texture *> lutTextures_;
	std::vector<Draw::SamplerState *> lutSamplers_;
	std::vector<std::pair<int, int>> lutSizes_;  // optional, for Task 6
	Draw::Buffer *quad_ = nullptr;
	Draw::SamplerState *samplerLinear_ = nullptr;
	Draw::SamplerState *samplerNearest_ = nullptr;
	Path presetPath_;  // for DeviceRestore
	bool valid_ = false;
	std::map<std::string, float> paramOverrides_;
};
