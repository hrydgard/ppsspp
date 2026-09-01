// PPSSPP addition - not part of upstream naett. See naett_curl.h.

#if __linux__ && !__ANDROID__

#define NAETT_CURL_INTERNAL
#include "naett_curl.h"

#include <dlfcn.h>
#include <stdio.h>

NaettCurl g_curl;

// libcurl's SONAME has been libcurl.so.4 since 2007. The gnutls and nss flavors are
// separate sonames on Debian-likes and old Fedora respectively, and are ABI compatible.
// libcurl.so last, since that one only exists if the -dev package is installed.
static const char *g_curlLibNames[] = {
	"libcurl.so.4",
	"libcurl-gnutls.so.4",
	"libcurl-nss.so.4",
	"libcurl.so",
};

static int LoadSymbols(void *lib) {
	// Every one of these has existed since libcurl 7.28 (2012), so a partial load means
	// something is badly wrong rather than "the host libcurl is a bit old" - bail out
	// entirely rather than crash later on a null pointer.
#define LOAD(field, name)                                            \
	g_curl.field = (__typeof__(g_curl.field))dlsym(lib, name);       \
	if (!g_curl.field) {                                             \
		fprintf(stderr, "naett: libcurl is missing %s\n", name);     \
		return 0;                                                    \
	}

	LOAD(global_init, "curl_global_init")
	LOAD(easy_init, "curl_easy_init")
	LOAD(easy_setopt, "curl_easy_setopt")
	LOAD(easy_getinfo, "curl_easy_getinfo")
	LOAD(easy_cleanup, "curl_easy_cleanup")
	LOAD(multi_init, "curl_multi_init")
	LOAD(multi_add_handle, "curl_multi_add_handle")
	LOAD(multi_perform, "curl_multi_perform")
	LOAD(multi_info_read, "curl_multi_info_read")
	LOAD(multi_wait, "curl_multi_wait")
	LOAD(slist_append, "curl_slist_append")
	LOAD(slist_free_all, "curl_slist_free_all")

#undef LOAD
	return 1;
}

int naettCurlLoad(void) {
	static int loaded = -1;
	if (loaded >= 0) {
		return loaded;
	}

	loaded = 0;
	for (size_t i = 0; i < sizeof(g_curlLibNames) / sizeof(g_curlLibNames[0]); i++) {
		// RTLD_GLOBAL so that libcurl's own dependencies resolve normally.
		void *lib = dlopen(g_curlLibNames[i], RTLD_NOW | RTLD_GLOBAL);
		if (!lib) {
			continue;
		}
		if (LoadSymbols(lib)) {
			loaded = 1;
		} else {
			dlclose(lib);
		}
		break;
	}

	if (!loaded) {
		fprintf(stderr, "naett: libcurl not found, HTTPS will be unavailable\n");
	}
	return loaded;
}

#endif
