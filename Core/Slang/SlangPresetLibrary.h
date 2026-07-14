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

struct SlangParamDesc;

struct SlangPresetEntry {
	std::string category;     // first path segment under the slang root; "" -> "misc"
	std::string displayName;  // .slangp filename without extension
	Path path;                // absolute path to the .slangp
};

class SlangPresetLibrary {
public:
	// Rescan GetSlangShaderDir() (recursively) for .slangp files. Cheap; call after import
	// or on screen open. Device-free (no shader compilation).
	void Rescan();
	// Test-only overload: scan a specific root directory
	void Rescan(const Path &root);
	const std::vector<std::string> &GetCategories() const { return categories_; }
	// Presets in a category, in stable (sorted) order.
	std::vector<SlangPresetEntry> GetPresets(const std::string &category) const;
	const std::vector<SlangPresetEntry> &All() const { return entries_; }
	bool Empty() const { return entries_.empty(); }
	// Category of the indexed preset whose path matches 'presetPath', or "" if not found.
	std::string CategoryOf(const Path &presetPath) const;
private:
	std::vector<SlangPresetEntry> entries_;
	std::vector<std::string> categories_;  // unique, sorted
};

// Enumerate a preset's #pragma parameters by text-parsing its .slangp + each .slang
// (with includes resolved). No GPU/compile. Returns the merged list (.slangp-level
// overrides win over .slang defaults, first-seen wins across passes), or false + *error.
// 'error' must be non-null (the underlying slang parsers dereference it unconditionally).
bool GetPresetParameters(const Path &presetPath, std::vector<SlangParamDesc> *out, std::string *error);
