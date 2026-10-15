// Copyright (c) 2012- PPSSPP Project.

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

#include <mutex>

#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Core/System.h"
#include "Core/Util/MaxCSO.h"

// Desktop only. Android and iOS won't load native code from user storage, and UWP can only
// load libraries from its own package.
#if (PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)) || PPSSPP_PLATFORM(MAC)
#define MAXCSO_SUPPORTED 1
#endif

#ifdef MAXCSO_SUPPORTED
#ifdef _WIN32
#include "Common/CommonWindows.h"
#define MAXCSO_CALL __cdecl
#else
#include <dlfcn.h>
#define MAXCSO_CALL
#endif
#endif

namespace MaxCSO {

#ifdef MAXCSO_SUPPORTED

// Must match maxcso_capi.h.
static const int EXPECTED_CAPI_VERSION = 1;

typedef void (MAXCSO_CALL *ProgressCallback)(void *userdata, int64_t pos, int64_t total, int64_t written);
typedef int (MAXCSO_CALL *CapiVersionFn)(void);
typedef const char *(MAXCSO_CALL *VersionFn)(void);
typedef int (MAXCSO_CALL *ProcessFn)(const char *input, const char *output, int mode,
	ProgressCallback progress, void *userdata, char *error, size_t error_size);

static std::once_flag g_initOnce;
static void *g_library = nullptr;
static ProcessFn g_process = nullptr;
static std::string g_version;
static Path g_libraryPath;

static void *OpenLibrary(const Path &path) {
#ifdef _WIN32
	// Altered search path so anything the DLL depends on is looked up next to it.
	return (void *)LoadLibraryExW(path.ToWString().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
	return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *GetSymbol(void *library, const char *name) {
#ifdef _WIN32
	return (void *)GetProcAddress((HMODULE)library, name);
#else
	return dlsym(library, name);
#endif
}

static void CloseLibrary(void *library) {
#ifdef _WIN32
	FreeLibrary((HMODULE)library);
#else
	dlclose(library);
#endif
}

static std::string LastLoadError() {
#ifdef _WIN32
	return "error " + std::to_string((unsigned)GetLastError());
#else
	const char *err = dlerror();
	return err ? err : "unknown error";
#endif
}

const char *LibraryFileName() {
#if defined(_WIN32)
	return "maxcso.dll";
#elif PPSSPP_PLATFORM(MAC)
	return "libmaxcso.dylib";
#else
	return "libmaxcso.so";
#endif
}

static void DoInit() {
	Path path = GetSysDirectory(DIRECTORY_SYSTEM) / LibraryFileName();
	if (!File::Exists(path)) {
		return;
	}

	void *library = OpenLibrary(path);
	if (!library) {
		ERROR_LOG(Log::Loader, "MaxCSO: Failed to load %s: %s", path.c_str(), LastLoadError().c_str());
		return;
	}

	auto capiVersion = (CapiVersionFn)GetSymbol(library, "maxcso_capi_version");
	auto version = (VersionFn)GetSymbol(library, "maxcso_version");
	auto process = (ProcessFn)GetSymbol(library, "maxcso_process");
	if (!capiVersion || !version || !process) {
		ERROR_LOG(Log::Loader, "MaxCSO: %s doesn't export the maxcso C API", path.c_str());
		CloseLibrary(library);
		return;
	}
	if (capiVersion() != EXPECTED_CAPI_VERSION) {
		ERROR_LOG(Log::Loader, "MaxCSO: %s has C API version %d, expected %d", path.c_str(), capiVersion(), EXPECTED_CAPI_VERSION);
		CloseLibrary(library);
		return;
	}

	// Never unloaded: a compression job may still be running on a worker thread at shutdown.
	g_library = library;
	g_process = process;
	const char *v = version();
	g_version = v ? v : "";
	g_libraryPath = path;
	INFO_LOG(Log::Loader, "MaxCSO: Loaded maxcso %s from %s", g_version.c_str(), path.c_str());
}

void Init() {
	std::call_once(g_initOnce, DoInit);
}

bool IsAvailable() {
	return g_process != nullptr;
}

std::string Version() {
	return g_version;
}

Path LibraryPath() {
	return g_libraryPath;
}

static void MAXCSO_CALL ProgressThunk(void *userdata, int64_t pos, int64_t total, int64_t written) {
	const ProgressFunc *progress = (const ProgressFunc *)userdata;
	if (progress && *progress && total > 0) {
		(*progress)((float)((double)pos / (double)total));
	}
}

bool Process(const Path &input, const Path &output, Mode mode, const ProgressFunc &progress, std::string *error) {
	if (!g_process) {
		*error = "maxcso is not loaded";
		return false;
	}
	// Never overwrite anything: on failure we delete the output, which must be our own file.
	if (File::Exists(output)) {
		*error = "Output file already exists";
		return false;
	}

	char errorBuf[512]{};
	int result = g_process(input.ToCString().c_str(), output.ToCString().c_str(), (int)mode,
		&ProgressThunk, (void *)&progress, errorBuf, sizeof(errorBuf));
	if (result == 0) {
		return true;
	}

	*error = errorBuf[0] ? errorBuf : "Unknown error";
	// Don't leave a half-written file behind, it would look like a valid game.
	if (File::Exists(output)) {
		File::Delete(output, true);
	}
	return false;
}

#else  // !MAXCSO_SUPPORTED

const char *LibraryFileName() { return ""; }
void Init() {}
bool IsAvailable() { return false; }
std::string Version() { return ""; }
Path LibraryPath() { return Path(); }

bool Process(const Path &input, const Path &output, Mode mode, const ProgressFunc &progress, std::string *error) {
	*error = "maxcso is not supported on this platform";
	return false;
}

#endif

}  // namespace MaxCSO
