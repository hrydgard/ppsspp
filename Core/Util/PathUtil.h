#pragma once

#include <string>
#include <string_view>

#include "Common/File/Path.h"

// Use these in conjunction with GetSysDirectory.
enum PSPDirectories {
	DIRECTORY_PSP,
	DIRECTORY_CHEATS,
	DIRECTORY_SCREENSHOT,
	DIRECTORY_SYSTEM,
	DIRECTORY_NAND,
	DIRECTORY_GAME,
	DIRECTORY_SAVEDATA,
	DIRECTORY_PAUTH,
	DIRECTORY_DUMP,
	DIRECTORY_SAVESTATE,
	DIRECTORY_CACHE,
	DIRECTORY_TEXTURES,
	DIRECTORY_PLUGINS,
	DIRECTORY_APP_CACHE,  // Use the OS app cache if available
	DIRECTORY_VIDEO,
	DIRECTORY_AUDIO,
	DIRECTORY_MEMSTICK_ROOT,
	DIRECTORY_EXDATA,
	DIRECTORY_CUSTOM_SHADERS,
	DIRECTORY_CUSTOM_THEMES,
	COUNT,
};

// Returns true if the given path (e.g. a zip entry name) contains a parent
// directory ("..") component. Used to guard against path traversal when
// extracting or writing files to disk.
bool HasParentDirComponent(std::string_view path);

// Returns true if the given string contains a path separator ('/' or '\\')
// or is a bare dot component ("." or ".."). Used to reject guest-controlled
// strings that would otherwise become host filesystem path components.
bool HasPathTraversal(std::string_view path);

Path FindConfigFile(const Path &searchPath, std::string_view baseFilename, bool *exists);
Path GetSysDirectory(PSPDirectories directoryType);
bool CreateSysDirectories();
Path GetGameConfigFilePath(const Path &searchPath, std::string_view gameId, bool *exists);
bool TryUpdateSavedPath(Path *path);
Path GetFailedBackendsDir();
std::string GetFriendlyPath(Path path, const Path &rootMatch = Path(), std::string_view rootDisplay = "ms:/");
