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
#include <cctype>
#include <climits>
#include "GPU/Common/Slang/SlangReflection.h"

// Helper: check if name == prefix + <digits>, return true and set *outIndex if so.
static bool MatchIndexedName(const std::string &name, const std::string &prefix, int *outIndex) {
	if (name.size() <= prefix.size()) return false;
	if (name.substr(0, prefix.size()) != prefix) return false;
	size_t pos = prefix.size();
	if (!std::isdigit((unsigned char)name[pos])) return false;
	int idx = 0;
	for (; pos < name.size(); ++pos) {
		if (!std::isdigit((unsigned char)name[pos])) return false;
		int digit = name[pos] - '0';
		// Reject overflow and absurd indices from untrusted shader files. No real slang
		// shader references >255 passes/history frames; cap well below that ceiling.
		if (idx > (INT_MAX - digit) / 10) return false;
		idx = idx * 10 + digit;
		if (idx > 4096) return false;
	}
	*outIndex = idx;
	return true;
}

SlangSemantic ClassifyUniform(const std::string &name, const SlangClassifyContext &ctx, int *outIndex) {
	*outIndex = -1;

	// Built-in single-instance semantics
	if (name == "MVP") return SlangSemantic::MVP;
	if (name == "OutputSize") return SlangSemantic::OutputSize;
	if (name == "FinalViewportSize") return SlangSemantic::FinalViewportSize;
	if (name == "FrameCount") return SlangSemantic::FrameCount;
	if (name == "FrameDirection") return SlangSemantic::FrameDirection;
	if (name == "Rotation") return SlangSemantic::Rotation;
	if (name == "SourceSize") return SlangSemantic::SourceSize;
	if (name == "OriginalSize") return SlangSemantic::OriginalSize;

	// Phase 2 indexed size semantics (match longest first)
	int idx;
	if (MatchIndexedName(name, "PassOutputSize", &idx)) { *outIndex = idx; return SlangSemantic::PassOutputSize; }
	if (MatchIndexedName(name, "PassFeedbackSize", &idx)) { *outIndex = idx; return SlangSemantic::PassFeedbackSize; }
	if (MatchIndexedName(name, "OriginalHistorySize", &idx)) { *outIndex = idx; return SlangSemantic::OriginalHistorySize; }

	// <alias>Size → PassOutputSize with alias index
	for (size_t i = 0; i < ctx.aliasNames.size(); ++i) {
		if (ctx.aliasNames[i].empty()) continue;
		if (name == ctx.aliasNames[i] + "Size") {
			*outIndex = (int)i;
			return SlangSemantic::PassOutputSize;
		}
	}

	// <lut>Size → LutSize with LUT index
	for (size_t i = 0; i < ctx.lutNames.size(); ++i) {
		if (name == ctx.lutNames[i] + "Size") {
			*outIndex = (int)i;
			return SlangSemantic::LutSize;
		}
	}

	// User parameter
	if (std::find(ctx.paramNames.begin(), ctx.paramNames.end(), name) != ctx.paramNames.end())
		return SlangSemantic::UserParameter;

	return SlangSemantic::Unknown;
}

SlangSemantic ClassifyTexture(const std::string &name, const SlangClassifyContext &ctx, int *outIndex) {
	*outIndex = -1;

	// Single-instance textures
	if (name == "Source") return SlangSemantic::TexSource;
	if (name == "Original") return SlangSemantic::TexOriginal;

	// Phase 2 indexed textures (longest match first)
	int idx;
	if (MatchIndexedName(name, "OriginalHistory", &idx)) { *outIndex = idx; return SlangSemantic::TexOriginalHistory; }
	if (MatchIndexedName(name, "PassOutput", &idx)) { *outIndex = idx; return SlangSemantic::TexPassOutput; }
	if (MatchIndexedName(name, "PassFeedback", &idx)) { *outIndex = idx; return SlangSemantic::TexPassFeedback; }

	// <alias>Feedback → TexPassFeedback with alias index (check before bare alias)
	for (size_t i = 0; i < ctx.aliasNames.size(); ++i) {
		if (ctx.aliasNames[i].empty()) continue;
		if (name == ctx.aliasNames[i] + "Feedback") {
			*outIndex = (int)i;
			return SlangSemantic::TexPassFeedback;
		}
	}

	// <alias> → TexPassOutput with alias index
	for (size_t i = 0; i < ctx.aliasNames.size(); ++i) {
		if (ctx.aliasNames[i].empty()) continue;
		if (name == ctx.aliasNames[i]) {
			*outIndex = (int)i;
			return SlangSemantic::TexPassOutput;
		}
	}

	// LUT name → TexLut with LUT index
	for (size_t i = 0; i < ctx.lutNames.size(); ++i) {
		if (name == ctx.lutNames[i]) {
			*outIndex = (int)i;
			return SlangSemantic::TexLut;
		}
	}

	return SlangSemantic::Unknown;
}
