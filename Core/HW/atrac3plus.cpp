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

extern std::string externalDirectory;

namespace Atrac3plus_Decoder {

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

	int initdecoder() {

#ifdef _WIN32 

#ifdef _M_X64
		hlib = LoadLibraryA("at3plusdecoder64.dll");
#else
		hlib = LoadLibraryA("at3plusdecoder.dll");
#endif
		if (hlib && g_Config.bEnableAtrac3plus) {
			frame_decoder = (ATRAC3PLUS_DECODEFRAME)GetProcAddress(hlib, "Atrac3plusDecoder_decodeFrame");
			open_context = (ATRAC3PLUS_OPENCONTEXT)GetProcAddress(hlib, "Atrac3plusDecoder_openContext");
			close_context = (ATRAC3PLUS_CLOSECONTEXT)GetProcAddress(hlib, "Atrac3plusDecoder_closeContext");
		} else {
			return -1;
		}
#else

#if defined(ANDROID) && defined(ARM)
		std::string sdFilename;
		std::string internalFilename = g_Config.internalDataDirectory + "libat3plusdecoder.so";
#if ARMEABI_V7A
		sdFilename = g_Config.memCardDirectory + "PSP/libs/armeabi-v7a/libat3plusdecoder.so";
#else
		sdFilename = g_Config.memCardDirectory + "PSP/libs/armeabi/libat3plusdecoder.so";
#endif

		// SD cards are often mounted no-exec.
		if (!File::Exists(internalFilename)) {
			if (!File::Copy(sdFilename, internalFilename)) {
				ELOG("Failed to copy %s to %s", sdFilename.c_str(), internalFilename.c_str());
				return -1;
			}
			if (chmod(internalFilename.c_str(), 0777) < 0) {
				ELOG("Failed to chmod %s, continuing anyway", internalFilename.c_str());
			}
		}
		std::string filename = internalFilename;
#else
		std::string filename = "libat3plusdecoder.so";
#endif

		ILOG("Attempting to load atrac3plus decoder from %s", filename.c_str());
		// TODO: from which directory on Android?
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

	int shutdowndecoder() {

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

		return 0;
	}

	void* openContext() {
		if (!open_context)
			return 0;
		return open_context();
	}

	int closeContext(void** context) {
		if (!close_context || !context)
			return 0;
		close_context(*context);
		*context = 0;
		return 0;
	}

	bool atrac3plus_decode(void* context, void* inbuf, int inbytes, int *outbytes, void* outbuf) {
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
