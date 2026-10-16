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

#include <cstdio>
#include <functional>
#include <mutex>
#include <type_traits>

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
	static const std::vector<std::string> pbp = { ".pbp" };
	switch (format) {
	case ImageFormat::CHD: return chd;
	case ImageFormat::PBP: return pbp;
	default: return cso;
	}
}

const char *OutputExtension(ImageFormat format) {
	switch (format) {
	case ImageFormat::CHD: return ".chd";
	case ImageFormat::PBP: return ".pbp";
	default: return ".cso";
	}
}

}  // namespace ImageCompressors

#ifdef IMAGE_COMPRESSORS_SUPPORTED

namespace {

// maxcso_capi.h
const int MAXCSO_CAPI_VERSION = 2;
const int MAXCSO_RESULT_CANCELLED = 7;
typedef int (LIB_CALL *MaxcsoProgressFn)(void *userdata, int64_t pos, int64_t total, int64_t written);
typedef int (LIB_CALL *MaxcsoCapiVersionFn)(void);
typedef const char *(LIB_CALL *MaxcsoVersionFn)(void);
typedef int (LIB_CALL *MaxcsoProcessFn)(const char *input, const char *output, int mode,
	MaxcsoProgressFn progress, void *userdata, char *error, size_t errorSize);
typedef int (LIB_CALL *MaxcsoProcessFlagsFn)(const char *input, const char *output, uint32_t flags,
	uint32_t blockSize, MaxcsoProgressFn progress, void *userdata, char *error, size_t errorSize);
// Optional, newer builds: settings beyond flags and block size.
struct MaxcsoOptions {
	uint32_t struct_size;
	uint32_t flags;
	uint32_t block_size;
	uint32_t zopfli_split_max;
	double precheck_percent;
};
typedef int (LIB_CALL *MaxcsoOptionsInitFn)(MaxcsoOptions *options, int mode);
typedef int (LIB_CALL *MaxcsoProcessExFn)(const char *input, const char *output, const MaxcsoOptions *options,
	MaxcsoProgressFn progress, void *userdata, char *error, size_t errorSize);
const int MAXCSO_MODE_DECOMPRESS = 0;
const int MAXCSO_MODE_STANDARD = 2;
// maxcso::TaskFlags: methods to leave out.
const uint32_t MAXCSO_NO_ZLIB_BRUTE = 0x02;
const uint32_t MAXCSO_NO_ZLIB = 0x03;
const uint32_t MAXCSO_NO_ZOPFLI = 0x04;
const uint32_t MAXCSO_NO_7ZIP = 0x08;
const uint32_t MAXCSO_NO_LZ4 = 0x380;
const uint32_t MAXCSO_NO_LIBDEFLATE = 0x1000;

// chdmodern.h (plain C, default calling convention)
const int CHDM_ERR_CANCELLED = 106;
typedef int (*ChdmProgressFn)(void *user, double progress);
typedef const char *(*ChdmLastErrorFn)(void);
typedef int (*ChdmCreateDvdFn)(const char *input, const char *output, uint32_t hunkBytes,
	const uint32_t compression[4], ChdmProgressFn progress, void *user);
typedef int (*ChdmExtractRawFn)(const char *input, const char *output, ChdmProgressFn progress, void *user);

// ipbp.h (plain C, default calling convention). The option structs must match it exactly.
const int IPBP_OK = 0;
const int IPBP_CANCELLED = 1;
struct ipbp_job;
struct IpbpIso2PbpOptions {
	int compress;
	const char *content_id;
	const uint8_t *version_key;
	const uint8_t *startdat_png;
	size_t startdat_png_size;
	unsigned threads;
};
struct IpbpPbp2IsoOptions {
	int verify;
	int trim;
	unsigned threads;
};
struct IpbpPbpInfo {
	char content_id[49];
	char disc_id[16];
	char title[129];
	uint32_t np_flags;
	uint32_t block_basis;
	uint32_t blocks;
	uint64_t iso_size;
	uint8_t version_key[16];
	int compressed;
};
typedef void (*IpbpProgressFn)(void *user, uint64_t done, uint64_t total);
struct IpbpApi {
	ipbp_job *(*job_create)(void);
	void (*job_destroy)(ipbp_job *job);
	void (*job_set_progress)(ipbp_job *job, IpbpProgressFn fn, void *user);
	void (*job_cancel)(ipbp_job *job);
	const char *(*job_error)(const ipbp_job *job);
	void (*iso2pbp_options_init)(IpbpIso2PbpOptions *opt);
	void (*pbp2iso_options_init)(IpbpPbp2IsoOptions *opt);
	int (*iso_to_pbp)(ipbp_job *job, const char *iso, const char *pbp, const IpbpIso2PbpOptions *opt);
	int (*pbp_to_iso)(ipbp_job *job, const char *pbp, const char *iso, const IpbpPbp2IsoOptions *opt);
	int (*read_pbp_info)(ipbp_job *job, const char *pbp, IpbpPbpInfo *info);
	const char *(*version)(void);
};

struct LoadedLibrary {
	void *handle = nullptr;
	Path path;
	std::string version;
};

std::once_flag g_initOnce;
LoadedLibrary g_maxcso;
LoadedLibrary g_chdmodern;
LoadedLibrary g_ipbp;
IpbpApi g_ipbpApi{};
bool g_ipbpLoaded = false;

MaxcsoProcessFn g_maxcsoProcess = nullptr;
MaxcsoProcessFlagsFn g_maxcsoProcessFlags = nullptr;
MaxcsoOptionsInitFn g_maxcsoOptionsInit = nullptr;
MaxcsoProcessExFn g_maxcsoProcessEx = nullptr;
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
	auto processFlags = (MaxcsoProcessFlagsFn)GetSymbol(handle, "maxcso_process_flags");
	if (!capiVersion || !version || !process || !processFlags) {
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
	g_maxcsoProcessFlags = processFlags;
	// Newer builds (LunaMoo/maxcso): the Max preset's libdeflate pre-check.
	g_maxcsoOptionsInit = (MaxcsoOptionsInitFn)GetSymbol(handle, "maxcso_options_init");
	g_maxcsoProcessEx = (MaxcsoProcessExFn)GetSymbol(handle, "maxcso_process_ex");
	if (!g_maxcsoOptionsInit || !g_maxcsoProcessEx) {
		g_maxcsoOptionsInit = nullptr;
		g_maxcsoProcessEx = nullptr;
	}
	INFO_LOG(Log::Loader, "Loaded maxcso %s from %s%s", g_maxcso.version.c_str(), g_maxcso.path.c_str(),
		g_maxcsoProcessEx ? "" : " (no maxcso_process_ex: Max runs without its pre-check)");
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
	// chdm_version arrived in 0.3.0.
	auto versionFn = (const char *(*)(void))GetSymbol(handle, "chdm_version");
#ifdef _WIN32
	// 0.2.0 put its 1 MB compressor on the stack, which overflows a Windows thread's stack and
	// crashes the whole process on the first compression.
	if (!versionFn) {
		ERROR_LOG(Log::Loader, "%s is CHDmodern 0.2.0, which crashes when compressing on Windows: it needs 0.3.0 or newer", g_chdmodern.path.c_str());
		CloseLibrary(handle);
		return;
	}
#endif
	const char *chdVersion = versionFn ? versionFn() : nullptr;
	g_chdmodern.version = chdVersion ? chdVersion : "";
	g_chdmodern.handle = handle;
	g_chdmLastError = lastError;
	g_chdmCreateDvd = createDvd;
	g_chdmExtractRaw = extractRaw;
	INFO_LOG(Log::Loader, "Loaded CHDmodern %s from %s", g_chdmodern.version.c_str(), g_chdmodern.path.c_str());
}

void LoadIpbp() {
	void *handle = Load(ImageFormat::PBP, &g_ipbp);
	if (!handle) {
		return;
	}
	IpbpApi api;
	bool ok = true;
	auto get = [&](auto &fn, const char *name) {
		fn = (std::remove_reference_t<decltype(fn)>)GetSymbol(handle, name);
		ok = ok && fn != nullptr;
	};
	get(api.job_create, "ipbp_job_create");
	get(api.job_destroy, "ipbp_job_destroy");
	get(api.job_set_progress, "ipbp_job_set_progress");
	get(api.job_cancel, "ipbp_job_cancel");
	get(api.job_error, "ipbp_job_error");
	get(api.iso2pbp_options_init, "ipbp_iso2pbp_options_init");
	get(api.pbp2iso_options_init, "ipbp_pbp2iso_options_init");
	get(api.iso_to_pbp, "ipbp_iso_to_pbp");
	get(api.pbp_to_iso, "ipbp_pbp_to_iso");
	get(api.read_pbp_info, "ipbp_read_pbp_info");
	get(api.version, "ipbp_version");
	if (!ok) {
		ERROR_LOG(Log::Loader, "%s doesn't export the iPBP C API", g_ipbp.path.c_str());
		CloseLibrary(handle);
		return;
	}
	const char *v = api.version();
	g_ipbp.version = v ? v : "";
	g_ipbp.handle = handle;
	g_ipbpApi = api;
	g_ipbpLoaded = true;
	INFO_LOG(Log::Loader, "Loaded iPBP %s from %s", g_ipbp.version.c_str(), g_ipbp.path.c_str());
}

const LoadedLibrary &Library(ImageFormat format) {
	switch (format) {
	case ImageFormat::CHD: return g_chdmodern;
	case ImageFormat::PBP: return g_ipbp;
	default: return g_maxcso;
	}
}

// Passed to iPBP as the progress callback's user pointer.
struct IpbpProgressContext {
	const ImageProgressFunc *progress;
	ipbp_job *job;
};

void IpbpProgressThunk(void *user, uint64_t done, uint64_t total) {
	const IpbpProgressContext *ctx = (const IpbpProgressContext *)user;
	if (ctx->progress && *ctx->progress && total > 0 && !(*ctx->progress)((float)((double)done / (double)total))) {
		// Thread-safe, and fine from the callback: the conversion stops within one block.
		g_ipbpApi.job_cancel(ctx->job);
	}
}

int LIB_CALL MaxcsoProgressThunk(void *userdata, int64_t pos, int64_t total, int64_t written) {
	const ImageProgressFunc *progress = (const ImageProgressFunc *)userdata;
	if (progress && *progress && total > 0 && !(*progress)((float)((double)pos / (double)total))) {
		return 1;
	}
	return 0;
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
		LoadIpbp();
	});
}

bool IsAvailable(ImageFormat format) {
	switch (format) {
	case ImageFormat::CHD: return g_chdmCreateDvd != nullptr;
	case ImageFormat::PBP: return g_ipbpLoaded;
	default: return g_maxcsoProcess != nullptr;
	}
}

const char *LibraryFileName(ImageFormat format) {
	// Indexed by ImageFormat.
#if defined(_WIN32)
	static const char *const files[] = { "CHDm.dll", "mCSO.dll", "iPBP.dll" };
#elif PPSSPP_PLATFORM(MAC)
	static const char *const files[] = { "CHDm.dylib", "mCSO.dylib", "iPBP.dylib" };
#else
	static const char *const files[] = { "CHDm.so", "mCSO.so", "iPBP.so" };
#endif
	return files[(int)format];
}

Path LibraryPath(ImageFormat format) {
	return IsAvailable(format) ? Library(format).path : Path();
}

std::string Version(ImageFormat format) {
	return Library(format).version;
}

}  // namespace ImageCompressors

namespace MaxCSO {

namespace {

bool RunMaxcso(const Path &output, std::string *error, bool *cancelled, const std::function<int(char *errorBuf, size_t errorSize)> &run) {
	*cancelled = false;
	if (!g_maxcsoProcess) {
		*error = "maxcso is not loaded";
		return false;
	}
	if (!CheckOutput(output, error)) {
		return false;
	}
	char errorBuf[512]{};
	int result = run(errorBuf, sizeof(errorBuf));
	if (result == 0) {
		return true;
	}
	*cancelled = result == MAXCSO_RESULT_CANCELLED;
	*error = errorBuf[0] ? errorBuf : "Unknown error";
	// Don't leave a half-written file behind, it would look like a valid game.
	if (File::Exists(output)) {
		File::Delete(output, true);
	}
	return false;
}

}  // namespace

bool Compress(const Path &input, const Path &output, Preset preset, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	// Every preset writes CSO v1 (deflate), and leaves out lz4 (CSO v2 / ZSO only) and 7-zip, which
	// adds nothing next to Zopfli and is slow.
	uint32_t flags = MAXCSO_NO_LZ4 | MAXCSO_NO_7ZIP;
	uint32_t blockSize = 262144;
	double precheck = 0.0;
	switch (preset) {
	case Preset::Max:
		// Zopfli, with libdeflate both as a trial and as the pre-check that skips Zopfli on blocks
		// it can't shrink below 98% (video, audio): about 1.3x faster for +0.007% in size.
		flags |= MAXCSO_NO_ZLIB;
		precheck = 98.0;
		break;
	case Preset::Great:
		// 64K: only ~0.25% bigger than 256K, and each read of a new block inflates a quarter as much.
		flags |= MAXCSO_NO_ZLIB | MAXCSO_NO_ZOPFLI;
		blockSize = 65536;
		break;
	case Preset::PSPCompatible:
	default:
		// Some custom firmware can't read libdeflate's output or larger blocks.
		flags |= MAXCSO_NO_ZLIB_BRUTE | MAXCSO_NO_ZOPFLI | MAXCSO_NO_LIBDEFLATE;
		blockSize = 2048;
		break;
	}
	const std::string in = input.ToCString(), out = output.ToCString();
	return RunMaxcso(output, error, cancelled, [&](char *errorBuf, size_t errorSize) {
		if (precheck > 0.0 && g_maxcsoProcessEx) {
			MaxcsoOptions options;
			g_maxcsoOptionsInit(&options, MAXCSO_MODE_STANDARD);
			options.flags = flags;
			options.block_size = blockSize;
			options.precheck_percent = precheck;
			return g_maxcsoProcessEx(in.c_str(), out.c_str(), &options, &MaxcsoProgressThunk, (void *)&progress, errorBuf, errorSize);
		}
		// Without maxcso_process_ex the result is the same size, it just takes longer.
		return g_maxcsoProcessFlags(in.c_str(), out.c_str(), flags, blockSize, &MaxcsoProgressThunk, (void *)&progress, errorBuf, errorSize);
	});
}

bool Decompress(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	return RunMaxcso(output, error, cancelled, [&](char *errorBuf, size_t errorSize) {
		return g_maxcsoProcess(input.ToCString().c_str(), output.ToCString().c_str(), MAXCSO_MODE_DECOMPRESS,
			&MaxcsoProgressThunk, (void *)&progress, errorBuf, errorSize);
	});
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

namespace IPBP {

namespace {

// Runs one iPBP conversion on a fresh job, wiring up progress and cancel.
bool RunIpbp(const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled,
	const std::function<int(ipbp_job *)> &convert) {
	*cancelled = false;
	if (!g_ipbpLoaded) {
		*error = "iPBP is not loaded";
		return false;
	}
	if (!CheckOutput(output, error)) {
		return false;
	}
	ipbp_job *job = g_ipbpApi.job_create();
	if (!job) {
		*error = "Out of memory";
		return false;
	}
	IpbpProgressContext ctx{ &progress, job };
	g_ipbpApi.job_set_progress(job, &IpbpProgressThunk, &ctx);
	int result = convert(job);
	if (result != IPBP_OK) {
		*cancelled = result == IPBP_CANCELLED;
		const char *message = g_ipbpApi.job_error(job);
		*error = message && *message ? message : "Unknown error";
	}
	g_ipbpApi.job_destroy(job);
	// iPBP writes to <output>.part and only renames it on success, so there's nothing to clean up.
	return result == IPBP_OK;
}

}  // namespace

bool SupportsMaxCompression() {
	if (!g_ipbpLoaded) {
		return false;
	}
	// "1.1.0" and later. Versions are plain "major.minor.patch".
	int major = 0, minor = 0;
	if (sscanf(g_ipbp.version.c_str(), "%d.%d", &major, &minor) != 2) {
		return false;
	}
	return major > 1 || (major == 1 && minor >= 1);
}

bool IsoToPbp(const Path &input, const Path &output, Compression compression, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	if (compression == Compression::Max && !SupportsMaxCompression()) {
		*cancelled = false;
		*error = "Max compression needs iPBP 1.1.0 or newer";
		return false;
	}
	return RunIpbp(output, progress, error, cancelled, [&](ipbp_job *job) {
		IpbpIso2PbpOptions options;
		g_ipbpApi.iso2pbp_options_init(&options);
		options.compress = (int)compression;
		return g_ipbpApi.iso_to_pbp(job, input.ToCString().c_str(), output.ToCString().c_str(), &options);
	});
}

bool PbpToIso(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	return RunIpbp(output, progress, error, cancelled, [&](ipbp_job *job) {
		IpbpPbp2IsoOptions options;
		g_ipbpApi.pbp2iso_options_init(&options);
		return g_ipbpApi.pbp_to_iso(job, input.ToCString().c_str(), output.ToCString().c_str(), &options);
	});
}

bool ReadDiscId(const Path &pbp, std::string *discId, std::string *error) {
	if (!g_ipbpLoaded) {
		*error = "iPBP is not loaded";
		return false;
	}
	ipbp_job *job = g_ipbpApi.job_create();
	if (!job) {
		*error = "Out of memory";
		return false;
	}
	IpbpPbpInfo info{};
	int result = g_ipbpApi.read_pbp_info(job, pbp.ToCString().c_str(), &info);
	if (result == IPBP_OK) {
		info.disc_id[sizeof(info.disc_id) - 1] = '\0';
		*discId = info.disc_id;
	} else {
		const char *message = g_ipbpApi.job_error(job);
		*error = message && *message ? message : "Unknown error";
	}
	g_ipbpApi.job_destroy(job);
	return result == IPBP_OK;
}

}  // namespace IPBP

#else  // !IMAGE_COMPRESSORS_SUPPORTED

namespace ImageCompressors {

void Init() {}
bool IsAvailable(ImageFormat format) { return false; }
const char *LibraryFileName(ImageFormat format) { return ""; }
Path LibraryPath(ImageFormat format) { return Path(); }
std::string Version(ImageFormat format) { return ""; }

}  // namespace ImageCompressors

namespace MaxCSO {

bool Compress(const Path &input, const Path &output, Preset preset, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

bool Decompress(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
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

namespace IPBP {

bool SupportsMaxCompression() {
	return false;
}

bool IsoToPbp(const Path &input, const Path &output, Compression compression, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

bool PbpToIso(const Path &input, const Path &output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

bool ReadDiscId(const Path &pbp, std::string *discId, std::string *error) {
	*error = "Not supported on this platform";
	return false;
}

}  // namespace IPBP

#endif
