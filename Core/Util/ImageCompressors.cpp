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
#include "Core/Util/ImageCompressors.h"

#if (PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)) || PPSSPP_PLATFORM(MAC)
#define IMAGE_COMPRESSORS_SUPPORTED 1
#endif

#ifdef IMAGE_COMPRESSORS_SUPPORTED
#ifdef _WIN32
#include "Common/CommonWindows.h"
#define LIB_CALL __cdecl
#else
#include <dlfcn.h>
#define LIB_CALL
#endif
#endif

namespace ImageCompressors {

const std::vector<std::string> &CompressedExtensions(ImageFormat format) {
	static const std::vector<std::string> chd = { ".chd" };
	static const std::vector<std::string> cso = { ".cso", ".zso", ".dax" };
	return format == ImageFormat::CHD ? chd : cso;
}

const char *OutputExtension(ImageFormat format) {
	return format == ImageFormat::CHD ? ".chd" : ".cso";
}

}  // namespace ImageCompressors

#ifdef IMAGE_COMPRESSORS_SUPPORTED

namespace {

// maxcso_capi.h
const int MAXCSO_CAPI_VERSION = 1;
typedef void (LIB_CALL *MaxcsoProgressFn)(void *userdata, int64_t pos, int64_t total, int64_t written);
typedef int (LIB_CALL *MaxcsoCapiVersionFn)(void);
typedef const char *(LIB_CALL *MaxcsoVersionFn)(void);
typedef int (LIB_CALL *MaxcsoProcessFn)(const char *input, const char *output, int mode,
	MaxcsoProgressFn progress, void *userdata, char *error, size_t errorSize);

// chdmodern.h (plain C, default calling convention)
const int CHDM_ERR_CANCELLED = 106;
typedef int (*ChdmProgressFn)(void *user, double progress);
typedef const char *(*ChdmLastErrorFn)(void);
typedef int (*ChdmCreateDvdFn)(const char *input, const char *output, uint32_t hunkBytes,
	const uint32_t compression[4], ChdmProgressFn progress, void *user);
typedef int (*ChdmExtractRawFn)(const char *input, const char *output, ChdmProgressFn progress, void *user);

struct LoadedLibrary {
	void *handle = nullptr;
	Path path;
	std::string version;
};

std::once_flag g_initOnce;
LoadedLibrary g_maxcso;
LoadedLibrary g_chdmodern;

MaxcsoProcessFn g_maxcsoProcess = nullptr;
ChdmLastErrorFn g_chdmLastError = nullptr;
ChdmCreateDvdFn g_chdmCreateDvd = nullptr;
ChdmExtractRawFn g_chdmExtractRaw = nullptr;

void *OpenLibrary(const Path &path) {
#ifdef _WIN32
	// Altered search path so anything the DLL depends on is looked up next to it.
	return (void *)LoadLibraryExW(path.ToWString().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
	return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void *GetSymbol(void *library, const char *name) {
#ifdef _WIN32
	return (void *)GetProcAddress((HMODULE)library, name);
#else
	return dlsym(library, name);
#endif
}

void CloseLibrary(void *library) {
#ifdef _WIN32
	FreeLibrary((HMODULE)library);
#else
	dlclose(library);
#endif
}

std::string LastLoadError() {
#ifdef _WIN32
	return "error " + std::to_string((unsigned)GetLastError());
#else
	const char *err = dlerror();
	return err ? err : "unknown error";
#endif
}

// Opens memstick/PSP/SYSTEM/<name> if it's there. Never unloaded: a conversion may still be
// running on a worker thread at shutdown.
void *Load(ImageFormat format, LoadedLibrary *lib) {
	Path path = GetSysDirectory(DIRECTORY_SYSTEM) / ImageCompressors::LibraryFileName(format);
	if (!File::Exists(path)) {
		return nullptr;
	}
	void *handle = OpenLibrary(path);
	if (!handle) {
		ERROR_LOG(Log::Loader, "Failed to load %s: %s", path.c_str(), LastLoadError().c_str());
		return nullptr;
	}
	lib->path = path;
	return handle;
}

void LoadMaxcso() {
	void *handle = Load(ImageFormat::CSO, &g_maxcso);
	if (!handle) {
		return;
	}
	auto capiVersion = (MaxcsoCapiVersionFn)GetSymbol(handle, "maxcso_capi_version");
	auto version = (MaxcsoVersionFn)GetSymbol(handle, "maxcso_version");
	auto process = (MaxcsoProcessFn)GetSymbol(handle, "maxcso_process");
	if (!capiVersion || !version || !process) {
		ERROR_LOG(Log::Loader, "%s doesn't export the maxcso C API", g_maxcso.path.c_str());
		CloseLibrary(handle);
		return;
	}
	if (capiVersion() != MAXCSO_CAPI_VERSION) {
		ERROR_LOG(Log::Loader, "%s has maxcso C API version %d, expected %d", g_maxcso.path.c_str(), capiVersion(), MAXCSO_CAPI_VERSION);
		CloseLibrary(handle);
		return;
	}
	const char *v = version();
	g_maxcso.version = v ? v : "";
	g_maxcso.handle = handle;
	g_maxcsoProcess = process;
	INFO_LOG(Log::Loader, "Loaded maxcso %s from %s", g_maxcso.version.c_str(), g_maxcso.path.c_str());
}

void LoadChdmodern() {
	void *handle = Load(ImageFormat::CHD, &g_chdmodern);
	if (!handle) {
		return;
	}
	auto lastError = (ChdmLastErrorFn)GetSymbol(handle, "chdm_last_error_message");
	auto createDvd = (ChdmCreateDvdFn)GetSymbol(handle, "chdm_create_dvd");
	auto extractRaw = (ChdmExtractRawFn)GetSymbol(handle, "chdm_extract_raw");
	if (!lastError || !createDvd || !extractRaw) {
		// chdm_create_dvd and chdm_extract_raw arrived in CHDmodern 0.2.0.
		ERROR_LOG(Log::Loader, "%s doesn't export chdm_create_dvd/chdm_extract_raw, it needs CHDmodern 0.2.0 or newer", g_chdmodern.path.c_str());
		CloseLibrary(handle);
		return;
	}
	g_chdmodern.handle = handle;
	g_chdmLastError = lastError;
	g_chdmCreateDvd = createDvd;
	g_chdmExtractRaw = extractRaw;
	INFO_LOG(Log::Loader, "Loaded CHDmodern from %s", g_chdmodern.path.c_str());
}

const LoadedLibrary &Library(ImageFormat format) {
	return format == ImageFormat::CHD ? g_chdmodern : g_maxcso;
}

void LIB_CALL MaxcsoProgressThunk(void *userdata, int64_t pos, int64_t total, int64_t written) {
	const ImageProgressFunc *progress = (const ImageProgressFunc *)userdata;
	if (progress && *progress && total > 0) {
		// maxcso can't stop mid-file, so a cancel only takes effect between files.
		(*progress)((float)((double)pos / (double)total));
	}
}

int ChdmProgressThunk(void *user, double progress) {
	const ImageProgressFunc *func = (const ImageProgressFunc *)user;
	if (func && *func && !(*func)((float)progress)) {
		return 1;
	}
	return 0;
}

bool CheckOutput(const Path &output, std::string *error) {
	// Never overwrite anything: on failure the output gets deleted, so it must be our own file.
	if (File::Exists(output)) {
		*error = "Output file already exists";
		return false;
	}
	return true;
}

bool FinishChd(int result, const Path &output, std::string *error, bool *cancelled) {
	*cancelled = result == CHDM_ERR_CANCELLED;
	if (result == 0) {
		return true;
	}
	const char *message = g_chdmLastError();
	*error = message && *message ? message : "Unknown error";
	// CHDmodern deletes its partial output itself, but make sure.
	if (File::Exists(output)) {
		File::Delete(output, true);
	}
	return false;
}

}  // namespace

namespace ImageCompressors {

void Init() {
	std::call_once(g_initOnce, [] {
		LoadChdmodern();
		LoadMaxcso();
	});
}

bool IsAvailable(ImageFormat format) {
	return format == ImageFormat::CHD ? g_chdmCreateDvd != nullptr : g_maxcsoProcess != nullptr;
}

const char *LibraryFileName(ImageFormat format) {
#if defined(_WIN32)
	return format == ImageFormat::CHD ? "chdmodern.dll" : "maxcso.dll";
#elif PPSSPP_PLATFORM(MAC)
	return format == ImageFormat::CHD ? "libchdmodern.dylib" : "libmaxcso.dylib";
#else
	return format == ImageFormat::CHD ? "libchdmodern.so" : "libmaxcso.so";
#endif
}

Path LibraryPath(ImageFormat format) {
	return IsAvailable(format) ? Library(format).path : Path();
}

std::string Version(ImageFormat format) {
	return Library(format).version;
}

}  // namespace ImageCompressors

namespace MaxCSO {

bool Process(const Path &input, const Path &output, Mode mode, const ImageProgressFunc &progress, std::string *error) {
	if (!g_maxcsoProcess) {
		*error = "maxcso is not loaded";
		return false;
	}
	if (!CheckOutput(output, error)) {
		return false;
	}

	char errorBuf[512]{};
	int result = g_maxcsoProcess(input.ToCString().c_str(), output.ToCString().c_str(), (int)mode,
		&MaxcsoProgressThunk, (void *)&progress, errorBuf, sizeof(errorBuf));
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

}  // namespace MaxCSO

namespace CHDModern {

bool CreateDVD(const Path &input, const Path &output, uint32_t hunkBytes, Codecs codecs, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (!g_chdmCreateDvd) {
		*error = "CHDmodern is not loaded";
		return false;
	}
	if (!CheckOutput(output, error)) {
		return false;
	}
	static const uint32_t zstd[4] = { 0x7a737464, 0, 0, 0 };  // 'zstd'
	int result = g_chdmCreateDvd(input.ToCString().c_str(), output.ToCString().c_str(), hunkBytes,
		codecs == Codecs::Zstd ? zstd : nullptr, &ChdmProgressThunk, (void *)&progress);
	return FinishChd(result, output, error, cancelled);
}

bool Extract(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (!g_chdmExtractRaw) {
		*error = "CHDmodern is not loaded";
		return false;
	}
	if (!CheckOutput(output, error)) {
		return false;
	}
	int result = g_chdmExtractRaw(input.ToCString().c_str(), output.ToCString().c_str(), &ChdmProgressThunk, (void *)&progress);
	return FinishChd(result, output, error, cancelled);
}

}  // namespace CHDModern

#else  // !IMAGE_COMPRESSORS_SUPPORTED

namespace ImageCompressors {

void Init() {}
bool IsAvailable(ImageFormat format) { return false; }
const char *LibraryFileName(ImageFormat format) { return ""; }
Path LibraryPath(ImageFormat format) { return Path(); }
std::string Version(ImageFormat format) { return ""; }

}  // namespace ImageCompressors

namespace MaxCSO {

bool Process(const Path &input, const Path &output, Mode mode, const ImageProgressFunc &progress, std::string *error) {
	*error = "Not supported on this platform";
	return false;
}

}  // namespace MaxCSO

namespace CHDModern {

bool CreateDVD(const Path &input, const Path &output, uint32_t hunkBytes, Codecs codecs, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

bool Extract(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

}  // namespace CHDModern

#endif
