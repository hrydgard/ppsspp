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

#include "ppsspp_config.h"
#include "Core/Slang/SlangPaths.h"
#include "Core/Util/PathUtil.h"
#include <algorithm>
#include <cstring>

#if PPSSPP_PLATFORM(ANDROID)
#include "Common/File/AndroidStorage.h"
#endif

Path GetSlangShaderDir() {
#if PPSSPP_PLATFORM(ANDROID)
	// On Android the memstick may be an SAF (content://) tree, where creating each of the
	// thousands of extracted shader files costs a ContentResolver round-trip (minutes total).
	// The app-private external files dir (g_extFilesDir) is a plain NATIVE filesystem path, so
	// extraction there uses fopen with no per-file JNI (seconds). The slang loader reads the
	// preset by absolute path, so the location is transparent to it. Fall back to the memstick
	// shaders dir only if the app dir is unknown (should not happen on a normal launch).
	if (!g_extFilesDir.empty()) {
		return Path(g_extFilesDir) / "slang";
	}
#endif
	return GetSysDirectory(DIRECTORY_CUSTOM_SHADERS) / "slang";
}

static bool HasAllowedShaderExtension(const std::string &name) {
	// Case-insensitive check against the shader-asset whitelist.
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	static const char *kExts[] = { ".slang", ".slangp", ".inc", ".h", ".png" };
	for (const char *ext : kExts) {
		size_t elen = strlen(ext);
		if (lower.size() >= elen && lower.compare(lower.size() - elen, elen, ext) == 0)
			return true;
	}
	return false;
}

bool ResolveSafeZipEntryPath(const Path &destRoot, const std::string &entryName,
                             bool isDirectory, Path *outPath) {
	if (entryName.empty()) return false;
	// Reject absolute and drive-relative paths.
	if (entryName[0] == '/' || entryName[0] == '\\') return false;
	if (entryName.size() >= 2 && entryName[1] == ':') return false;  // C:\...
	// Normalize separators and split into components; reject any "..".
	std::string norm = entryName;
	std::replace(norm.begin(), norm.end(), '\\', '/');
	size_t start = 0;
	while (start < norm.size()) {
		size_t slash = norm.find('/', start);
		std::string comp = norm.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (comp == "..") return false;
		start = (slash == std::string::npos) ? norm.size() : slash + 1;
	}
	// Files must have an allowed shader-asset extension; directories may not.
	if (!isDirectory && !HasAllowedShaderExtension(norm)) return false;
	Path resolved = destRoot / norm;
	// Defense in depth: the resolved path must still live under destRoot.
	if (!resolved.StartsWith(destRoot)) return false;
	*outPath = resolved;
	return true;
}
