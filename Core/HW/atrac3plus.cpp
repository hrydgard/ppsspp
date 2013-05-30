#ifdef _WIN32 
#include <Windows.h>
#else
#include <dlfcn.h>
#include <errno.h>
#endif // _WIN32

#include <string.h>

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
		hlib = LoadLibraryA("at3plusdecoder.dll");
		if (hlib) {
			frame_decoder = (ATRAC3PLUS_DECODEFRAME)GetProcAddress(hlib, "Atrac3plusDecoder_decodeFrame");
			open_context = (ATRAC3PLUS_OPENCONTEXT)GetProcAddress(hlib, "Atrac3plusDecoder_openContext");
			close_context = (ATRAC3PLUS_CLOSECONTEXT)GetProcAddress(hlib, "Atrac3plusDecoder_closeContext");
		} else {
			return -1;
		}
#else
		// TODO: from which directory on Android?
		so = dlopen("at3plusdecoder.so", RTLD_LAZY);
		if (so) {
			frame_decoder = (ATRAC3PLUS_DECODEFRAME)dlsym(so, "Atrac3plusDecoder_decodeFrame");
			open_context = (ATRAC3PLUS_OPENCONTEXT)dlsym(so, "Atrac3plusDecoder_openContext");
			close_context = (ATRAC3PLUS_CLOSECONTEXT)dlsym(so, "Atrac3plusDecoder_closeContext");
		} else {
			return -1;
		}
#endif // _WIN32

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