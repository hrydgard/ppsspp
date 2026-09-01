#ifndef NAETT_INTERNAL_H
#define NAETT_INTERNAL_H

// PPSSPP: The upstream amalgamated build (naett.c) included naett.h before this header.
// We build the sources directly, so include it here instead.
#include "../naett.h"

#ifdef _MSC_VER
    #define strcasecmp _stricmp
    #undef strdup
    #define strdup _strdup
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#ifndef min
    #define min(x,y) ((x) < (y) ? (x) : (y))
#endif
#define __WINDOWS__ 1
#endif

#if __linux__ && !__ANDROID__
#define __LINUX__ 1
#include <curl/curl.h>
#endif

#if __ANDROID__
#include <jni.h>
#include <pthread.h>
#endif

#ifdef __APPLE__
#include "TargetConditionals.h"
#include <objc/objc.h>
#if TARGET_OS_IPHONE
#define __IOS__ 1
#else
#define __MACOS__ 1
#endif
#endif

#define naettAlloc(TYPE, VAR) TYPE* VAR = (TYPE*)calloc(1, sizeof(TYPE))

typedef struct KVLink {
    const char* key;
    const char* value;
    struct KVLink* next;
} KVLink;

typedef struct Buffer {
    void* data;
    int size;
    int capacity;
    int position;
} Buffer;

typedef struct {
    const char* method;
    const char* userAgent;
    int timeoutMS;
    naettReadFunc bodyReader;
    void* bodyReaderData;
    naettWriteFunc bodyWriter;
    void* bodyWriterData;
    KVLink* headers;
    Buffer body;
} RequestOptions;

typedef struct {
    RequestOptions options;
    const char* url;
#if __APPLE__
    id urlRequest;
#endif
#if __ANDROID__
    jobject urlObject;
#endif
#if __WINDOWS__
    HINTERNET session;
    HINTERNET connection;
    HINTERNET request;
    LPWSTR host;
    LPWSTR resource;
#endif
} InternalRequest;

typedef struct {
    InternalRequest* request;
    int code;
    int complete;
    KVLink* headers;
    Buffer body;
    int contentLength;  // 0 if headers not read, -1 if Content-Length missing.
    int totalBytesRead;
#if __APPLE__
    id session;
#endif
#if __ANDROID__
    pthread_t workerThread;
    int closeRequested;
#endif
#if __LINUX__
    struct curl_slist* headerList;
#endif
#if __WINDOWS__
    char buffer[10240];
    size_t bytesLeft;
#endif
} InternalResponse;

void naettPlatformInit(naettInitData initData);
int naettPlatformInitRequest(InternalRequest* req);
void naettPlatformMakeRequest(InternalResponse* res);
void naettPlatformFreeRequest(InternalRequest* req);
void naettPlatformCloseResponse(InternalResponse* res);

#endif  // NAETT_INTERNAL_H
