// PPSSPP addition - not part of upstream naett.
//
// Loads libcurl at runtime instead of linking against it, so that libcurl stays a soft
// dependency: a build made on a machine with the curl headers still starts on a machine
// without libcurl installed, just with HTTPS reported as unavailable. Same idea as
// Common/GPU/Vulkan/VulkanLoader.cpp.
//
// The curl headers are still needed at build time, for the types and the option enums.
// Only naett_linux.c and naett_curl.c want the redirect macros, so those are behind
// NAETT_CURL_INTERNAL; everyone else gets just the loader entry point and doesn't pull
// <curl/curl.h> into their translation unit.

#ifndef NAETT_CURL_H
#define NAETT_CURL_H

#ifdef __cplusplus
extern "C" {
#endif

// Loads libcurl if it isn't loaded yet, and returns 1 if it's available.
// The result is cached, so calling this repeatedly is cheap. Not thread safe - call it
// once during startup before any other thread can reach it (PPSSPP does so in net::Init).
int naettCurlLoad(void);

#ifdef __cplusplus
}
#endif

#ifdef NAETT_CURL_INTERNAL

#include <curl/curl.h>

// curl.h defines these as typechecking macros when built with GCC/Clang. We need the
// plain names to redirect, and we lose the typechecking, which is fine - the calls below
// are checked at build time in exactly the same way for anyone building against a normal
// libcurl.
#undef curl_easy_setopt
#undef curl_easy_getinfo

typedef struct {
	CURLcode (*global_init)(long flags);

	CURL *(*easy_init)(void);
	CURLcode (*easy_setopt)(CURL *handle, CURLoption option, ...);
	CURLcode (*easy_getinfo)(CURL *handle, CURLINFO info, ...);
	void (*easy_cleanup)(CURL *handle);

	CURLM *(*multi_init)(void);
	CURLMcode (*multi_add_handle)(CURLM *multi, CURL *easy);
	CURLMcode (*multi_perform)(CURLM *multi, int *runningHandles);
	CURLMsg *(*multi_info_read)(CURLM *multi, int *msgsInQueue);
	CURLMcode (*multi_wait)(CURLM *multi, struct curl_waitfd extraFDs[], unsigned int extraNFDs, int timeoutMS,
			int *numFDs);

	struct curl_slist *(*slist_append)(struct curl_slist *list, const char *data);
	void (*slist_free_all)(struct curl_slist *list);
} NaettCurl;

extern NaettCurl g_curl;

#define curl_global_init g_curl.global_init
#define curl_easy_init g_curl.easy_init
#define curl_easy_setopt g_curl.easy_setopt
#define curl_easy_getinfo g_curl.easy_getinfo
#define curl_easy_cleanup g_curl.easy_cleanup
#define curl_multi_init g_curl.multi_init
#define curl_multi_add_handle g_curl.multi_add_handle
#define curl_multi_perform g_curl.multi_perform
#define curl_multi_info_read g_curl.multi_info_read
#define curl_multi_wait g_curl.multi_wait
#define curl_slist_append g_curl.slist_append
#define curl_slist_free_all g_curl.slist_free_all

#endif  // NAETT_CURL_INTERNAL

#endif  // NAETT_CURL_H
