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

#include <algorithm>
#include "GPU/Common/Slang/SlangReflection.h"

SlangSemantic ClassifyUniform(const std::string &name, const std::vector<std::string> &knownParams) {
	if (name == "MVP") return SlangSemantic::MVP;
	if (name == "OutputSize") return SlangSemantic::OutputSize;
	if (name == "FinalViewportSize") return SlangSemantic::FinalViewportSize;
	if (name == "FrameCount") return SlangSemantic::FrameCount;
	if (name == "FrameDirection") return SlangSemantic::FrameDirection;
	if (name == "Rotation") return SlangSemantic::Rotation;
	if (name == "SourceSize") return SlangSemantic::SourceSize;
	if (name == "OriginalSize") return SlangSemantic::OriginalSize;
	if (std::find(knownParams.begin(), knownParams.end(), name) != knownParams.end())
		return SlangSemantic::UserParameter;
	return SlangSemantic::Unknown;
}

SlangSemantic ClassifyTexture(const std::string &name) {
	if (name == "Source") return SlangSemantic::TexSource;
	if (name == "Original") return SlangSemantic::TexOriginal;
	return SlangSemantic::Unknown;
}
