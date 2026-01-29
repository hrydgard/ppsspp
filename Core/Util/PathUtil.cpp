#include <string_view>

#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/Log.h"
#include "Core/Util/PathUtil.h"
#include "Core/Config.h"
#include "Common/VR/PPSSPPVR.h"

Path FindConfigFile(const Path &searchPath, std::string_view baseFilename, bool *exists) {
	// Don't search for an absolute path.
	if (baseFilename.size() > 1 && baseFilename[0] == '/') {
		Path path(baseFilename);
		*exists = File::Exists(path);
		return path;
	}
#ifdef _WIN32
	// Handle paths starting with a drive letter.
	if (baseFilename.size() > 3 && baseFilename[1] == ':' && (baseFilename[2] == '/' || baseFilename[2] == '\\')) {
		Path path(baseFilename);
		*exists = File::Exists(path);
		return path;
	}
#endif

	Path filename = searchPath / baseFilename;
	if (File::Exists(filename)) {
		*exists = true;
		return filename;
	}
	*exists = false;
	// Make sure at least the directory it's supposed to be in exists.
	Path parent = filename.NavigateUp();

	// We try to create the path and ignore if it fails (already exists).
	if (parent != GetSysDirectory(DIRECTORY_SYSTEM)) {
		File::CreateFullPath(parent);
	}
	return filename;
}

Path GetSysDirectory(PSPDirectories directoryType) {
	const Path &memStickDirectory = g_Config.memStickDirectory;
	Path pspDirectory;
	if (!strcasecmp(memStickDirectory.GetFilename().c_str(), "PSP")) {
		// Let's strip this off, to easily allow choosing a root directory named "PSP" on Android.
		pspDirectory = memStickDirectory;
	} else {
		pspDirectory = memStickDirectory / "PSP";
	}

	switch (directoryType) {
	case DIRECTORY_PSP:
		return pspDirectory;
	case DIRECTORY_CHEATS:
		return pspDirectory / "Cheats";
	case DIRECTORY_GAME:
		return pspDirectory / "GAME";
	case DIRECTORY_SAVEDATA:
		return pspDirectory / "SAVEDATA";
	case DIRECTORY_SCREENSHOT:
		return pspDirectory / "SCREENSHOT";
	case DIRECTORY_SYSTEM:
		return pspDirectory / "SYSTEM";
	case DIRECTORY_PAUTH:
		return memStickDirectory / "PAUTH";  // This one's at the root...
	case DIRECTORY_EXDATA:
		return memStickDirectory / "EXDATA";  // This one's traditionally at the root...
	case DIRECTORY_DUMP:
		return pspDirectory / "SYSTEM/DUMP";
	case DIRECTORY_SAVESTATE:
		return pspDirectory / "PPSSPP_STATE";
	case DIRECTORY_CACHE:
		return pspDirectory / "SYSTEM/CACHE";
	case DIRECTORY_TEXTURES:
		return pspDirectory / "TEXTURES";
	case DIRECTORY_PLUGINS:
		return pspDirectory / "PLUGINS";
	case DIRECTORY_APP_CACHE:
		if (!g_Config.appCacheDirectory.empty()) {
			return g_Config.appCacheDirectory;
		}
		return pspDirectory / "SYSTEM/CACHE";
	case DIRECTORY_VIDEO:
		return pspDirectory / "VIDEO";
	case DIRECTORY_AUDIO:
		return pspDirectory / "AUDIO";
	case DIRECTORY_CUSTOM_SHADERS:
		return pspDirectory / "shaders";
	case DIRECTORY_CUSTOM_THEMES:
		return pspDirectory / "themes";

	case DIRECTORY_MEMSTICK_ROOT:
		return g_Config.memStickDirectory;
		// Just return the memory stick root if we run into some sort of problem.
	default:
		ERROR_LOG(Log::FileSystem, "Unknown directory type.");
		return g_Config.memStickDirectory;
	}
}

bool CreateSysDirectories() {
#if PPSSPP_PLATFORM(ANDROID)
	const bool createNoMedia = true;
#else
	const bool createNoMedia = false;
#endif

	Path pspDir = GetSysDirectory(DIRECTORY_PSP);
	INFO_LOG(Log::IO, "Creating '%s' and subdirs:", pspDir.c_str());
	File::CreateFullPath(pspDir);
	if (!File::Exists(pspDir)) {
		INFO_LOG(Log::IO, "Not a workable memstick directory. Giving up");
		return false;
	}

	// Create the default directories that a real PSP creates. Good for homebrew so they can
	// expect a standard environment. Skipping THEME though, that's pointless.
	static const PSPDirectories sysDirs[] = {
		DIRECTORY_CHEATS,
		DIRECTORY_SAVEDATA,
		DIRECTORY_SAVESTATE,
		DIRECTORY_GAME,
		DIRECTORY_SYSTEM,
		DIRECTORY_TEXTURES,
		DIRECTORY_PLUGINS,
		DIRECTORY_CACHE,
	};

	for (auto dir : sysDirs) {
		Path path = GetSysDirectory(dir);
		File::CreateFullPath(path);
		if (createNoMedia) {
			// Create a nomedia file in each specified subdirectory.
			File::CreateEmptyFile(path / ".nomedia");
		}
	}
	return true;
}

Path GetGameConfigFilePath(const Path &searchPath, std::string_view gameId, bool *exists) {
	std::string_view ppssppIniFilename = IsVREnabled() ? "_ppssppvr.ini" : "_ppsspp.ini";
	std::string iniFileName = join(gameId, ppssppIniFilename);
	Path iniFileNameFull = FindConfigFile(searchPath, iniFileName, exists);
	return iniFileNameFull;
}

// On iOS, the path to the app documents directory changes on each launch.
// Example path:
// /var/mobile/Containers/Data/Application/0E0E89DE-8D8E-485A-860C-700D8BC87B86/Documents/PSP/GAME/SuicideBarbie
// The GUID part changes on each launch.
bool TryUpdateSavedPath(Path *path) {
#if PPSSPP_PLATFORM(IOS)
	// DEBUG_LOG(Log::Loader, "Original path: %s", path->c_str());
	std::string pathStr = path->ToString();

	const std::string_view applicationRoot = "/var/mobile/Containers/Data/Application/";
	if (startsWith(pathStr, applicationRoot)) {
		size_t documentsPos = pathStr.find("/Documents/");
		if (documentsPos == std::string::npos) {
			return false;
		}
		std::string memstick = g_Config.memStickDirectory.ToString();
		size_t memstickDocumentsPos = memstick.find("/Documents");  // Note: No trailing slash, or we won't find it.
		*path = Path(memstick.substr(0, memstickDocumentsPos) + pathStr.substr(documentsPos));
		return true;
	} else {
		// Path can't be auto-updated.
		return false;
	}
#else
	return false;
#endif
}

Path GetFailedBackendsDir() {
	Path failedBackendsDir;
	if (System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS)) {
		failedBackendsDir = GetSysDirectory(DIRECTORY_APP_CACHE);
	} else {
		failedBackendsDir = GetSysDirectory(DIRECTORY_SYSTEM);
	}
	return failedBackendsDir;
}

std::string GetFriendlyPath(Path path, const Path &rootMatch, std::string_view rootDisplay) {
	const Path &root = rootMatch.empty() ? g_Config.memStickDirectory : rootMatch;

	// Show relative to memstick root if there.
	if (path.StartsWith(root)) {
		std::string p;
		if (root.ComputePathTo(path, p)) {
			return join(rootDisplay, p);
		}
		std::string str = path.ToString();
		if (root.size() < str.length()) {
			return join(rootDisplay, str.substr(root.size()));
		} else {
			return std::string(rootDisplay);
		}
	}

#if !PPSSPP_PLATFORM(ANDROID) && (PPSSPP_PLATFORM(LINUX) || PPSSPP_PLATFORM(MAC))
	std::string str = path.ToString();
	char *home = getenv("HOME");
	if (home != nullptr && !strncmp(str.c_str(), home, strlen(home))) {
		return "~" + str.substr(strlen(home));
	}
#endif
	return path.ToVisualString();
}
