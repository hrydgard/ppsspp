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
#include <algorithm>
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

				// Extract display name (filename without extension)
				std::string filename = fileInfo.name;
				size_t extPos = filename.rfind(".slangp");
				if (extPos == std::string::npos) {
					extPos = filename.rfind(".SLANGP");
				}
				if (extPos != std::string::npos) {
					entry.displayName = filename.substr(0, extPos);
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
