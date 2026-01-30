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

Path FindConfigFile(const Path &searchPath, std::string_view baseFilename, bool *exists);
Path GetSysDirectory(PSPDirectories directoryType);
bool CreateSysDirectories();
Path GetGameConfigFilePath(const Path &searchPath, std::string_view gameId, bool *exists);
bool TryUpdateSavedPath(Path *path);
Path GetFailedBackendsDir();
std::string GetFriendlyPath(Path path, const Path &rootMatch = Path(), std::string_view rootDisplay = "ms:/");
