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
#include "Common/File/Path.h"
#include "GPU/Common/Slang/SlangPreset.h"

// Parse a .slangp preset. Relative shaderN paths resolve against basePath.
// Unknown keys are ignored (forward-compat). Returns false + *error on fatal errors.
bool ParseSlangPreset(const std::string &text, const Path &basePath, SlangPreset *out, std::string *error);

// Split a .slang source into vertex and fragment stages, extracting #pragma metadata.
struct SlangSource {
	std::string vertex;    // full GLSL for the vertex stage (shared prologue + vertex body)
	std::string fragment;  // full GLSL for the fragment stage (shared prologue + fragment body)
	std::string name;      // #pragma name value, "" if none
	std::vector<SlangParamDesc> params;  // one per #pragma parameter
};
bool SplitSlangSource(const std::string &src, SlangSource *out, std::string *error);
