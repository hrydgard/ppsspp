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

#include "Core/Slang/SlangPresetLibrary.h"
#include "Core/Slang/SlangPaths.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "GPU/Common/Slang/SlangpParser.h"
#include <algorithm>
#include <functional>
#include <set>

void SlangPresetLibrary::Rescan() {
	Rescan(GetSlangShaderDir());
}

static void RecursiveScanForPresets(const Path &currentDir, const Path &rootDir,
                                    std::vector<SlangPresetEntry> *entries) {
	std::vector<File::FileInfo> fileInfos;
	if (!File::GetFilesInDir(currentDir, &fileInfos, nullptr)) {
		return;
	}

	for (const auto &fileInfo : fileInfos) {
		if (fileInfo.isDirectory) {
			// Recurse into subdirectory
			RecursiveScanForPresets(fileInfo.fullName, rootDir, entries);
		} else {
			// Check if it's a .slangp file (case-insensitive)
			std::string ext = fileInfo.fullName.GetFileExtension();
			if (strcasecmp(ext.c_str(), ".slangp") == 0) {
				SlangPresetEntry entry;
				entry.path = fileInfo.fullName;

				// Extract display name (filename without the .slangp extension). ext was already
				// validated case-insensitively above, so strip exactly its length to handle any
				// case (.slangp/.SLANGP/.Slangp) uniformly.
				std::string filename = fileInfo.name;
				if (filename.size() >= ext.size()) {
					entry.displayName = filename.substr(0, filename.size() - ext.size());
				} else {
					entry.displayName = filename;
				}

				// Compute category: first path segment relative to root
				std::string relativePath;
				if (rootDir.ComputePathTo(fileInfo.fullName, relativePath)) {
					size_t firstSlash = relativePath.find('/');
					if (firstSlash != std::string::npos) {
						entry.category = relativePath.substr(0, firstSlash);
					} else {
						// File directly in root -> "misc"
						entry.category = "misc";
					}
				} else {
					// Fallback if path computation fails
					entry.category = "misc";
				}

				entries->push_back(entry);
			}
		}
	}
}

void SlangPresetLibrary::Rescan(const Path &root) {
	entries_.clear();
	categories_.clear();

	// Recursively scan for .slangp files
	RecursiveScanForPresets(root, root, &entries_);

	// Build sorted unique category list
	std::set<std::string> categorySet;
	for (const auto &entry : entries_) {
		categorySet.insert(entry.category);
	}
	categories_.assign(categorySet.begin(), categorySet.end());

	// Sort entries by category, then displayName
	std::sort(entries_.begin(), entries_.end(),
		[](const SlangPresetEntry &a, const SlangPresetEntry &b) {
			if (a.category != b.category) {
				return a.category < b.category;
			}
			return a.displayName < b.displayName;
		});
}

std::vector<SlangPresetEntry> SlangPresetLibrary::GetPresets(const std::string &category) const {
	std::vector<SlangPresetEntry> result;
	for (const auto &entry : entries_) {
		if (entry.category == category) {
			result.push_back(entry);
		}
	}
	return result;
}

std::string SlangPresetLibrary::CategoryOf(const Path &presetPath) const {
	const std::string target = presetPath.ToString();
	if (target.empty()) {
		return "";
	}
	for (const auto &entry : entries_) {
		if (entry.path.ToString() == target) {
			return entry.category;
		}
	}
	return "";
}

bool GetPresetParameters(const Path &presetPath, std::vector<SlangParamDesc> *out, std::string *error) {
	// 'error' must be non-null: the slang parsers (ParseSlangPreset/ResolveSlangIncludes/
	// SplitSlangSource) dereference it unconditionally, so we require it too rather than
	// pretend to be null-safe on only some paths.
	out->clear();

	// Read the .slangp file
	std::string presetText;
	if (!File::ReadBinaryFileToString(presetPath, &presetText)) {
		*error = "Failed to read preset file";
		return false;
	}

	// Parse the preset
	SlangPreset preset;
	Path presetDir = Path(presetPath.GetDirectory());
	if (!ParseSlangPreset(presetText, presetDir, &preset, error)) {
		return false;
	}

	// Seed output with preset-level parameters (usually empty)
	*out = preset.params;

	// File reader for includes
	SlangFileReader reader = [](const Path &p, std::string *o) {
		return File::ReadBinaryFileToString(p, o);
	};

	// Process each pass
	for (const auto &pass : preset.passes) {
		// Read shader source
		std::string shaderSrc;
		Path shaderPath = Path(pass.shaderPath);
		if (!File::ReadBinaryFileToString(shaderPath, &shaderSrc)) {
			*error = "Failed to read shader: " + pass.shaderPath;
			return false;
		}

		// Resolve includes
		std::string resolved;
		Path shaderDir = Path(shaderPath.GetDirectory());
		if (!ResolveSlangIncludes(shaderSrc, shaderDir, reader, &resolved, error)) {
			return false;
		}

		// Split source to extract parameters
		SlangSource slangSrc;
		if (!SplitSlangSource(resolved, &slangSrc, error)) {
			return false;
		}

		// Merge parameters (first-seen wins)
		for (const auto &param : slangSrc.params) {
			bool alreadyExists = false;
			for (const auto &existing : *out) {
				if (existing.name == param.name) {
					alreadyExists = true;
					break;
				}
			}
			if (!alreadyExists) {
				out->push_back(param);
			}
		}
	}

	return true;
}
