#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <errno.h>
#ifdef ANDROID
#include <sys/stat.h>
#endif
#endif // _WIN32

#include <string.h>
#include <string>

#include "base/logging.h"
#include "Core/Config.h"
#include "Common/FileUtil.h"
#include "Core/HW/atrac3plus.h"

#ifdef __APPLE__
#include "TargetConditionals.h"
#if TARGET_OS_MAC
#define MACOSX
#endif
#endif

extern std::string externalDirectory;

namespace Atrac3plus_Decoder {

	bool IsSupported() {
#if (defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))) || defined(ARMEABI) || defined(ARMEABI_V7A) || defined(MACOSX)
		return true;
#else
		return false;
#endif
	}

#ifdef _WIN32
	HMODULE hlib = 0;
#else
	static void *so;
#endif // _WIN32

	typedef int   (* ATRAC3PLUS_DECODEFRAME)(void* context, void* inbuf, int inbytes, int* channels, void** outbuf);
	typedef void* (* ATRAC3PLUS_OPENCONTEXT)();
	typedef int   (* ATRAC3PLUS_CLOSECONTEXT)(void* context);
	ATRAC3PLUS_DECODEFRAME frame_decoder = 0;
	ATRAC3PLUS_OPENCONTEXT open_context = 0;
	ATRAC3PLUS_CLOSECONTEXT close_context = 0;

	std::string GetInstalledFilename() {
#if defined(ANDROID) && defined(ARM)
		return g_Config.internalDataDirectory + "libat3plusdecoder.so";
#elif defined(_WIN32)
#ifdef _M_X64
		return "at3plusdecoder64.dll";
#else
		return "at3plusdecoder.dll";
#endif
#elif defined(__APPLE__)
		return "libat3plusdecoder.dylib";
#else
		return "libat3plusdecoder.so";
#endif
	}

	std::string GetAutoInstallFilename() {
#if ARMEABI_V7A
		return g_Config.memCardDirectory + "PSP/libs/armeabi-v7a/libat3plusdecoder.so";
#else
		return g_Config.memCardDirectory + "PSP/libs/armeabi/libat3plusdecoder.so";
#endif
	}

	// Android-only: From SD card. .so files must be in internal memory to load on many devices.
	bool CanAutoInstall() {
#if defined(ANDROID) && defined(ARM)
		// Android will auto install from SD card
		if (File::Exists(GetAutoInstallFilename()))
			return true;
#endif

		// Other platforms can't.
		return false;
	}

	bool IsInstalled() {
		return File::Exists(GetInstalledFilename());
	}

	bool DoAutoInstall() {
#if defined(ANDROID) && defined(ARM)
		std::string internalFilename = g_Config.internalDataDirectory + "libat3plusdecoder.so";
#if ARMEABI_V7A
		std::string sdFilename = g_Config.memCardDirectory + "PSP/libs/armeabi-v7a/libat3plusdecoder.so";
#else
		std::string sdFilename = g_Config.memCardDirectory + "PSP/libs/armeabi/libat3plusdecoder.so";
#endif

		// SD cards are often mounted no-exec.
		if (!File::Exists(internalFilename)) {
			if (!File::Copy(sdFilename, internalFilename)) {
				ELOG("Failed to copy %s to %s", sdFilename.c_str(), internalFilename.c_str());
				return false;
			}
			if (chmod(internalFilename.c_str(), 0777) < 0) {
				ELOG("Failed to chmod %s, continuing anyway", internalFilename.c_str());
			}
		}
#else
		ELOG("Autoinstall is for android only");
#endif
		return true;
	}

	int Init() {
		if (!g_Config.bEnableAtrac3plus)
			return -1;

		if (!IsInstalled()) {
			// Okay, we're screwed. Let's bail.
			return -1;
		}

#ifdef _WIN32

#ifdef _M_X64
		hlib = LoadLibraryA(GetInstalledFilename().c_str());
#else
		hlib = LoadLibraryA(GetInstalledFilename().c_str());
#endif
		if (hlib) {
			frame_decoder = (ATRAC3PLUS_DECODEFRAME)GetProcAddress(hlib, "Atrac3plusDecoder_decodeFrame");
			open_context = (ATRAC3PLUS_OPENCONTEXT)GetProcAddress(hlib, "Atrac3plusDecoder_openContext");
			close_context = (ATRAC3PLUS_CLOSECONTEXT)GetProcAddress(hlib, "Atrac3plusDecoder_closeContext");
		} else {
			return -1;
		}
#else
		std::string filename = GetInstalledFilename();

		ILOG("Attempting to load atrac3plus decoder from %s", filename.c_str());
		so = dlopen(filename.c_str(), RTLD_LAZY);
		if (so) {
			frame_decoder = (ATRAC3PLUS_DECODEFRAME)dlsym(so, "Atrac3plusDecoder_decodeFrame");
			open_context = (ATRAC3PLUS_OPENCONTEXT)dlsym(so, "Atrac3plusDecoder_openContext");
			close_context = (ATRAC3PLUS_CLOSECONTEXT)dlsym(so, "Atrac3plusDecoder_closeContext");
			ILOG("Successfully loaded atrac3plus decoder from %s", filename.c_str());
			if (!frame_decoder || !open_context || !close_context) {
				ILOG("Found atrac3plus decoder at %s but failed to load functions", filename.c_str());
				return -1;
			}
		} else {
			if (errno == ENOEXEC) {
				ELOG("Failed to load atrac3plus decoder from %s. errno=%i, dlerror=%s", filename.c_str(), (int)(errno), dlerror());
			} else {
				ELOG("Failed to load atrac3plus decoder from %s. errno=%i", filename.c_str(), (int)(errno));
			}
			return -1;
		}
#endif

		return 0;
	}

	int Shutdown() {
#ifdef _WIN32
		if (hlib) {
			FreeLibrary(hlib);
			hlib = 0;
		}
#else
		if (so) {
			dlclose(so);
			so = 0;
		}
#endif // _WIN32
		frame_decoder = 0;
		open_context = 0;
		close_context = 0;
		return 0;
	}

	void* OpenContext() {
		if (!open_context)
			return 0;
		return open_context();
	}

	int CloseContext(Context *context) {
		if (!close_context || !context)
			return 0;
		close_context(*context);
		*context = 0;
		return 0;
	}

	bool Decode(Context context, void* inbuf, int inbytes, int *outbytes, void* outbuf) {
		if (!frame_decoder) {
			*outbytes = 0;
			return false;
		}
		int channels = 0;
		void* buf;
		int ret = frame_decoder(context, inbuf, inbytes, &channels, &buf);
		if (ret != 0) {
			*outbytes = 0;
			return false;
		}
		*outbytes = channels * 2 * 0x800;
		memcpy(outbuf, buf, *outbytes);
		return true;
	}

} // Atrac3plus_Decoder
