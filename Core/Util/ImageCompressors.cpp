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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>
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
#if PPSSPP_PLATFORM(MAC)
#include <mach/mach.h>
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
// Optional, newer builds: the image in memory.
struct MaxcsoReader {
	void *user;
	uint64_t size;
	int64_t (LIB_CALL *read_at)(void *user, uint64_t offset, void *buffer, size_t length);
};
struct MaxcsoWriter {
	void *user;
	int (LIB_CALL *begin)(void *user, uint64_t size);
	int64_t (LIB_CALL *write_at)(void *user, uint64_t offset, const void *buffer, size_t length);
};
typedef int (LIB_CALL *MaxcsoProcessReaderFn)(const MaxcsoReader *input, const char *output, const MaxcsoOptions *options,
	MaxcsoProgressFn progress, void *userdata, char *error, size_t errorSize);
typedef int (LIB_CALL *MaxcsoDecompressWriterFn)(const char *input, const MaxcsoWriter *output,
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
// 0.4.0: the image in memory.
struct ChdmIo {
	void *user;
	uint64_t (*size)(void *user);
	size_t (*read_at)(void *user, uint64_t offset, void *buffer, size_t length);
	void (*close)(void *user);
};
struct ChdmWriter {
	void *user;
	int (*begin)(void *user, uint64_t total);
	size_t (*write_at)(void *user, uint64_t offset, const void *buffer, size_t length);
};
typedef int (*ChdmCreateDvdIoFn)(const ChdmIo *input, const char *output, uint32_t hunkBytes,
	const uint32_t compression[4], ChdmProgressFn progress, void *user);
typedef int (*ChdmExtractRawIoFn)(const char *input, const ChdmWriter *output, ChdmProgressFn progress, void *user);

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
// 1.2.0: the image in memory.
struct IpbpReader {
	void *user;
	uint64_t size;
	int (*read_at)(void *user, uint64_t offset, void *buf, size_t len);
};
struct IpbpWriter {
	void *user;
	int (*begin)(void *user, uint64_t size);
	int (*write_at)(void *user, uint64_t offset, const void *buf, size_t len);
	int (*finish)(void *user, uint64_t finalSize);
};
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
	// Optional.
	int (*iso_to_pbp_reader)(ipbp_job *job, const IpbpReader *iso, const char *pbp, const IpbpIso2PbpOptions *opt);
	int (*pbp_to_iso_writer)(ipbp_job *job, const char *pbp, const IpbpWriter *iso, const IpbpPbp2IsoOptions *opt);
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
MaxcsoProcessReaderFn g_maxcsoProcessReader = nullptr;
MaxcsoDecompressWriterFn g_maxcsoDecompressWriter = nullptr;
ChdmLastErrorFn g_chdmLastError = nullptr;
ChdmCreateDvdFn g_chdmCreateDvd = nullptr;
ChdmExtractRawFn g_chdmExtractRaw = nullptr;
ChdmCreateDvdIoFn g_chdmCreateDvdIo = nullptr;
ChdmExtractRawIoFn g_chdmExtractRawIo = nullptr;

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
	// And the image in memory, which needs the options too.
	g_maxcsoProcessReader = (MaxcsoProcessReaderFn)GetSymbol(handle, "maxcso_process_reader");
	g_maxcsoDecompressWriter = (MaxcsoDecompressWriterFn)GetSymbol(handle, "maxcso_decompress_writer");
	if (!g_maxcsoOptionsInit || !g_maxcsoProcessReader || !g_maxcsoDecompressWriter) {
		g_maxcsoProcessReader = nullptr;
		g_maxcsoDecompressWriter = nullptr;
	}
	INFO_LOG(Log::Loader, "Loaded maxcso %s from %s%s%s", g_maxcso.version.c_str(), g_maxcso.path.c_str(),
		g_maxcsoProcessEx ? "" : " (no maxcso_process_ex: Max runs without its pre-check)",
		g_maxcsoProcessReader ? "" : " (no maxcso_process_reader: repacks go through a temporary file)");
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
	g_chdmCreateDvdIo = (ChdmCreateDvdIoFn)GetSymbol(handle, "chdm_create_dvd_io");
	g_chdmExtractRawIo = (ChdmExtractRawIoFn)GetSymbol(handle, "chdm_extract_raw_io");
	if (!g_chdmCreateDvdIo || !g_chdmExtractRawIo) {
		g_chdmCreateDvdIo = nullptr;
		g_chdmExtractRawIo = nullptr;
	}
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
	// 1.2.0.
	get(api.iso_to_pbp_reader, "ipbp_iso_to_pbp_reader");
	get(api.pbp_to_iso_writer, "ipbp_pbp_to_iso_writer");
	if (!api.iso_to_pbp_reader || !api.pbp_to_iso_writer) {
		api.iso_to_pbp_reader = nullptr;
		api.pbp_to_iso_writer = nullptr;
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

// Physical memory not in use, or 0 if we can't tell.
uint64_t AvailableMemory() {
#if defined(_WIN32)
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status)) {
		return std::min<uint64_t>(status.ullAvailPhys, status.ullAvailVirtual);
	}
	return 0;
#elif PPSSPP_PLATFORM(MAC)
	vm_statistics64_data_t stats{};
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	vm_size_t pageSize = 0;
	if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS ||
		host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&stats, &count) != KERN_SUCCESS) {
		return 0;
	}
	// Free plus what can be dropped without writing anything out.
	return (uint64_t)(stats.free_count + stats.inactive_count + stats.purgeable_count) * pageSize;
#else
	FILE *f = fopen("/proc/meminfo", "r");
	if (!f) {
		return 0;
	}
	char line[256];
	uint64_t kb = 0;
	while (fgets(line, sizeof(line), f)) {
		unsigned long long value;
		if (sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
			kb = value;
			break;
		}
	}
	fclose(f);
	return kb * 1024;
#endif
}

// Room left for everything else while the image is held.
const uint64_t MEMORY_HEADROOM = 512ULL * 1024 * 1024;

// The ISO being extracted into memory. The libraries write it from their own threads, maxcso
// several blocks at once, but always to separate ranges of a buffer sized up front.
struct MemoryWriteContext {
	MemoryImage *image;
	bool refused = false;

	bool Begin(uint64_t size) {
		const uint64_t available = AvailableMemory();
		if (size > (uint64_t)SIZE_MAX || (available != 0 && size + MEMORY_HEADROOM > available)) {
			INFO_LOG(Log::Loader, "Not extracting %llu bytes into memory, %llu available", (unsigned long long)size, (unsigned long long)available);
			refused = true;
			return false;
		}
		try {
			image->clear();
			image->resize((size_t)size);
		} catch (const std::bad_alloc &) {
			MemoryImage().swap(*image);
			refused = true;
			return false;
		}
		return true;
	}
	bool Write(uint64_t offset, const void *buffer, size_t length) {
		if (offset > image->size() || length > image->size() - offset) {
			return false;
		}
		memcpy(image->data() + offset, buffer, length);
		return true;
	}
};

size_t ReadMemory(const MemoryImage &image, uint64_t offset, void *buffer, size_t length) {
	if (offset >= image.size()) {
		return 0;
	}
	const size_t n = (size_t)std::min<uint64_t>(length, image.size() - offset);
	memcpy(buffer, image.data() + offset, n);
	return n;
}

// For a failed extraction: what to tell, and nothing left behind.
void FailMemory(MemoryImage *output, const MemoryWriteContext &ctx, std::string *error) {
	MemoryImage().swap(*output);
	if (ctx.refused) {
		*error = "Not enough memory for the image";
	}
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

bool SupportsMemory(ImageFormat format) {
	switch (format) {
	case ImageFormat::CHD: return g_chdmCreateDvdIo != nullptr;
	case ImageFormat::PBP: return g_ipbpLoaded && g_ipbpApi.iso_to_pbp_reader != nullptr;
	default: return g_maxcsoProcessReader != nullptr;
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

struct PresetSettings {
	uint32_t flags;
	uint32_t blockSize;
	double precheck;
};

PresetSettings SettingsFor(Preset preset) {
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
	return { flags, blockSize, precheck };
}

int64_t LIB_CALL MaxcsoReadThunk(void *user, uint64_t offset, void *buffer, size_t length) {
	return (int64_t)ReadMemory(*(const MemoryImage *)user, offset, buffer, length);
}

int LIB_CALL MaxcsoBeginThunk(void *user, uint64_t size) {
	return ((MemoryWriteContext *)user)->Begin(size) ? 0 : 1;
}

int64_t LIB_CALL MaxcsoWriteThunk(void *user, uint64_t offset, const void *buffer, size_t length) {
	return ((MemoryWriteContext *)user)->Write(offset, buffer, length) ? (int64_t)length : -1;
}

}  // namespace

bool Compress(const Path &input, const Path &output, Preset preset, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	const PresetSettings settings = SettingsFor(preset);
	const std::string in = input.ToCString(), out = output.ToCString();
	return RunMaxcso(output, error, cancelled, [&](char *errorBuf, size_t errorSize) {
		if (settings.precheck > 0.0 && g_maxcsoProcessEx) {
			MaxcsoOptions options;
			g_maxcsoOptionsInit(&options, MAXCSO_MODE_STANDARD);
			options.flags = settings.flags;
			options.block_size = settings.blockSize;
			options.precheck_percent = settings.precheck;
			return g_maxcsoProcessEx(in.c_str(), out.c_str(), &options, &MaxcsoProgressThunk, (void *)&progress, errorBuf, errorSize);
		}
		// Without maxcso_process_ex the result is the same size, it just takes longer.
		return g_maxcsoProcessFlags(in.c_str(), out.c_str(), settings.flags, settings.blockSize, &MaxcsoProgressThunk, (void *)&progress, errorBuf, errorSize);
	});
}

bool CompressFromMemory(const MemoryImage &input, const Path &output, Preset preset, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	if (!g_maxcsoProcessReader) {
		*cancelled = false;
		*error = "This maxcso can't compress from memory";
		return false;
	}
	const PresetSettings settings = SettingsFor(preset);
	const std::string out = output.ToCString();
	return RunMaxcso(output, error, cancelled, [&](char *errorBuf, size_t errorSize) {
		MaxcsoOptions options;
		g_maxcsoOptionsInit(&options, MAXCSO_MODE_STANDARD);
		options.flags = settings.flags;
		options.block_size = settings.blockSize;
		options.precheck_percent = settings.precheck;
		MaxcsoReader reader{ (void *)&input, (uint64_t)input.size(), &MaxcsoReadThunk };
		return g_maxcsoProcessReader(&reader, out.c_str(), &options, &MaxcsoProgressThunk, (void *)&progress, errorBuf, errorSize);
	});
}

bool DecompressToMemory(const Path &input, MemoryImage *output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (!g_maxcsoDecompressWriter) {
		*error = "This maxcso can't decompress into memory";
		return false;
	}
	MemoryWriteContext ctx{ output };
	MaxcsoWriter writer{ &ctx, &MaxcsoBeginThunk, &MaxcsoWriteThunk };
	char errorBuf[512]{};
	const int result = g_maxcsoDecompressWriter(input.ToCString().c_str(), &writer, &MaxcsoProgressThunk, (void *)&progress, errorBuf, sizeof(errorBuf));
	if (result == 0) {
		return true;
	}
	*cancelled = result == MAXCSO_RESULT_CANCELLED;
	*error = errorBuf[0] ? errorBuf : "Unknown error";
	FailMemory(output, ctx, error);
	return false;
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

bool CreateDVDFromMemory(const MemoryImage &input, const Path &output, uint32_t hunkBytes, Codecs codecs, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (!g_chdmCreateDvdIo) {
		*error = "This CHDmodern can't compress from memory";
		return false;
	}
	if (!CheckOutput(output, error)) {
		return false;
	}
	ChdmIo io{};
	io.user = (void *)&input;
	io.size = [](void *user) -> uint64_t {
		return ((const MemoryImage *)user)->size();
	};
	io.read_at = [](void *user, uint64_t offset, void *buffer, size_t length) -> size_t {
		return ReadMemory(*(const MemoryImage *)user, offset, buffer, length);
	};
	static const uint32_t zstd[4] = { 0x7a737464, 0, 0, 0 };  // 'zstd'
	int result = g_chdmCreateDvdIo(&io, output.ToCString().c_str(), hunkBytes,
		codecs == Codecs::Zstd ? zstd : nullptr, &ChdmProgressThunk, (void *)&progress);
	return FinishChd(result, output, error, cancelled);
}

bool ExtractToMemory(const Path &input, MemoryImage *output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (!g_chdmExtractRawIo) {
		*error = "This CHDmodern can't extract into memory";
		return false;
	}
	MemoryWriteContext ctx{ output };
	ChdmWriter writer{};
	writer.user = &ctx;
	writer.begin = [](void *user, uint64_t total) -> int {
		return ((MemoryWriteContext *)user)->Begin(total) ? 0 : 1;
	};
	writer.write_at = [](void *user, uint64_t offset, const void *buffer, size_t length) -> size_t {
		return ((MemoryWriteContext *)user)->Write(offset, buffer, length) ? length : (size_t)-1;
	};
	int result = g_chdmExtractRawIo(input.ToCString().c_str(), &writer, &ChdmProgressThunk, (void *)&progress);
	if (result == 0) {
		return true;
	}
	*cancelled = result == CHDM_ERR_CANCELLED;
	const char *message = g_chdmLastError();
	*error = message && *message ? message : "Unknown error";
	FailMemory(output, ctx, error);
	return false;
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

bool IsoToPbpFromMemory(const MemoryImage &input, const Path &output, Compression compression, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	if (!g_ipbpApi.iso_to_pbp_reader || (compression == Compression::Max && !SupportsMaxCompression())) {
		*cancelled = false;
		*error = "This iPBP can't convert from memory";
		return false;
	}
	return RunIpbp(output, progress, error, cancelled, [&](ipbp_job *job) {
		IpbpIso2PbpOptions options;
		g_ipbpApi.iso2pbp_options_init(&options);
		options.compress = (int)compression;
		IpbpReader reader{};
		reader.user = (void *)&input;
		reader.size = input.size();
		reader.read_at = [](void *user, uint64_t offset, void *buf, size_t len) -> int {
			return ReadMemory(*(const MemoryImage *)user, offset, buf, len) == len ? 0 : 1;
		};
		return g_ipbpApi.iso_to_pbp_reader(job, &reader, output.ToCString().c_str(), &options);
	});
}

bool PbpToIsoMemory(const Path &input, MemoryImage *output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	if (!g_ipbpApi.pbp_to_iso_writer) {
		*error = "This iPBP can't convert into memory";
		return false;
	}
	ipbp_job *job = g_ipbpApi.job_create();
	if (!job) {
		*error = "Out of memory";
		return false;
	}
	IpbpProgressContext progressCtx{ &progress, job };
	g_ipbpApi.job_set_progress(job, &IpbpProgressThunk, &progressCtx);

	MemoryWriteContext ctx{ output };
	IpbpWriter writer{};
	writer.user = &ctx;
	writer.begin = [](void *user, uint64_t size) -> int {
		return ((MemoryWriteContext *)user)->Begin(size) ? 0 : 1;
	};
	writer.write_at = [](void *user, uint64_t offset, const void *buf, size_t len) -> int {
		return ((MemoryWriteContext *)user)->Write(offset, buf, len) ? 0 : 1;
	};
	writer.finish = [](void *user, uint64_t finalSize) -> int {
		// At most what begin got, so this never reallocates.
		MemoryImage *image = ((MemoryWriteContext *)user)->image;
		if (finalSize > image->size()) {
			return 1;
		}
		image->resize((size_t)finalSize);
		return 0;
	};
	IpbpPbp2IsoOptions options;
	g_ipbpApi.pbp2iso_options_init(&options);
	const int result = g_ipbpApi.pbp_to_iso_writer(job, input.ToCString().c_str(), &writer, &options);
	if (result != IPBP_OK) {
		*cancelled = result == IPBP_CANCELLED;
		const char *message = g_ipbpApi.job_error(job);
		*error = message && *message ? message : "Unknown error";
		FailMemory(output, ctx, error);
	}
	g_ipbpApi.job_destroy(job);
	return result == IPBP_OK;
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
bool SupportsMemory(ImageFormat format) { return false; }
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

bool CompressFromMemory(const MemoryImage &input, const Path &output, Preset preset, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

bool DecompressToMemory(const Path &input, MemoryImage *output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
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

bool CreateDVDFromMemory(const MemoryImage &input, const Path &output, uint32_t hunkBytes, Codecs codecs, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

bool ExtractToMemory(const Path &input, MemoryImage *output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
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

bool IsoToPbpFromMemory(const MemoryImage &input, const Path &output, Compression compression, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	*cancelled = false;
	*error = "Not supported on this platform";
	return false;
}

bool PbpToIsoMemory(const Path &input, MemoryImage *output, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
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
