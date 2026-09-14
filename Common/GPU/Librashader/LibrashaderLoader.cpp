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

#include "Common/GPU/Librashader/LibrashaderLoader.h"
#if USE_LIBRASHADER
#include <mutex>
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#else
#include <dlfcn.h>
#endif

namespace Librashader {

static std::mutex g_mutex;
static bool g_attempted = false;
static libra_instance_t g_instance{};
static std::string g_error;
#if PPSSPP_PLATFORM(WINDOWS)
static HMODULE g_preloaded = nullptr;
#else
static void *g_preloaded = nullptr;
#endif

static const char *PlatformLibraryName() {
#if PPSSPP_PLATFORM(WINDOWS)
	return "librashader.dll";
#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	return "librashader.dylib";
#else
	return "librashader.so";
#endif
}

// Preloads from an explicit path so that librashader_load_instance()'s bare-name
// dlopen/LoadLibrary resolves to the already-loaded module.
static bool Preload(const Path &path) {
	if (!File::Exists(path))
		return false;
#if PPSSPP_PLATFORM(WINDOWS)
	g_preloaded = LoadLibraryW(path.ToWString().c_str());
#else
	g_preloaded = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
	return g_preloaded != nullptr;
}

bool Load(std::string *error) {
	std::lock_guard<std::mutex> guard(g_mutex);
	if (!g_attempted) {
		g_attempted = true;
		const char *env = getenv("LIBRASHADER_PATH");
		bool preloaded = false;
		if (env && env[0]) {
			preloaded = Preload(Path(env));
			if (!preloaded)
				g_error = std::string("LIBRASHADER_PATH set but not loadable: ") + env;
		}
		if (!preloaded && !(env && env[0])) {
			preloaded = Preload(File::GetExeDirectory() / PlatformLibraryName());
		}
		if (!(env && env[0]) || preloaded) {
			g_instance = librashader_load_instance();
			if (!g_instance.instance_loaded) {
				g_error = std::string("librashader not found or ABI mismatch (want ABI ") +
				          std::to_string(LIBRASHADER_CURRENT_ABI) + ")";
			} else {
				INFO_LOG(Log::G3D, "librashader loaded (ABI %d, API %d)", (int)g_instance.instance_abi_version(),
				         (int)g_instance.instance_api_version());
			}
		}
		if (!g_instance.instance_loaded)
			INFO_LOG(Log::G3D, "librashader unavailable: %s", g_error.c_str());
	}
	if (error)
		*error = g_instance.instance_loaded ? "" : g_error;
	return g_instance.instance_loaded;
}

bool IsLoaded() {
	std::lock_guard<std::mutex> guard(g_mutex);
	return g_attempted && g_instance.instance_loaded;
}

const libra_instance_t &Instance() {
	return g_instance;
}

std::string ErrorToString(libra_error_t err) {
	if (!err)
		return "";
	std::string result = "librashader error";
	if (g_instance.instance_loaded && g_instance.error_write && g_instance.error_free_string) {
		char *msg = nullptr;
		if (g_instance.error_write(err, &msg) == 0 && msg) {
			result = msg;
			g_instance.error_free_string(&msg);
		}
	}
	if (g_instance.instance_loaded && g_instance.error_free)
		g_instance.error_free(&err);
	return result;
}

void Unload() {
	std::lock_guard<std::mutex> guard(g_mutex);
	g_attempted = false;
	g_instance = libra_instance_t{};
	g_error.clear();
	if (g_preloaded) {
#if PPSSPP_PLATFORM(WINDOWS)
		FreeLibrary(g_preloaded);
#else
		dlclose(g_preloaded);
#endif
		g_preloaded = nullptr;
	}
}

}  // namespace Librashader
#endif  // USE_LIBRASHADER
