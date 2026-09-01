#include "naett_internal.h"

#if __linux__ && !__ANDROID__

// PPSSPP: libcurl is dlopen'd rather than linked, see naett_curl.h. This header stands in
// for <curl/curl.h> and redirects the curl_* calls below through function pointers.
#define NAETT_CURL_INTERNAL
#include "naett_curl.h"

#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_t workerThread;
static int handleReadFD = 0;
static int handleWriteFD = 0;

// PPSSPP: upstream had a panic() here that called exit(1). Taking the whole application
// down because a download failed isn't acceptable for us, so failures now leave the
// backend disabled and requests complete with naettGenericError instead.
static int workerRunning = 0;

static void fail(const char* message) {
    fprintf(stderr, "naett: %s\n", message);
    workerRunning = 0;
}

static void* curlWorker(void* data) {
    CURLM* mc = (CURLM*)data;
    int activeHandles = 0;
    int messagesLeft = 1;

    struct curl_waitfd readFd = { handleReadFD, CURL_WAIT_POLLIN };

    union {
        CURL* handle;
        char buf[sizeof(CURL*)];
    } newHandle;

    int newHandlePos = 0;

    while (workerRunning) {
        int status = curl_multi_perform(mc, &activeHandles);
        if (status != CURLM_OK) {
            fail("curl_multi_perform failed, shutting down the HTTP worker");
            break;
        }

        struct CURLMsg* message = curl_multi_info_read(mc, &messagesLeft);
        if (message && message->msg == CURLMSG_DONE) {
            CURL* handle = message->easy_handle;
            InternalResponse* res = NULL;
            curl_easy_getinfo(handle, CURLINFO_PRIVATE, (char**)&res);
            // PPSSPP: CURLINFO_RESPONSE_CODE writes a long, and res->code is an int -
            // upstream passed &res->code directly, which writes 8 bytes into 4 and only
            // gets away with it because the next field happens to absorb the zeroes.
            long responseCode = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &responseCode);
            res->code = (int)responseCode;
            res->complete = 1;
            curl_easy_cleanup(handle);
        }


        int readyFDs = 0;
        curl_multi_wait(mc, &readFd, 1, 1, &readyFDs);

        if (readyFDs == 0 && activeHandles == 0 && messagesLeft == 0) {
            usleep(100 * 1000);
        }

        int bytesRead = read(handleReadFD, newHandle.buf, sizeof(newHandle.buf) - newHandlePos);
        if (bytesRead > 0) {
            newHandlePos += bytesRead;
        }
        if (newHandlePos == sizeof(newHandle.buf)) {
            curl_multi_add_handle(mc, newHandle.handle);
            newHandlePos = 0;
        }
    }

    return NULL;
}

void naettPlatformInit(naettInitData initData) {
    if (!naettCurlLoad()) {
        return;
    }
    curl_global_init(CURL_GLOBAL_ALL);
    CURLM* mc = curl_multi_init();
    if (!mc) {
        fail("curl_multi_init failed");
        return;
    }
    int fds[2];
    if (pipe(fds) != 0) {
        fail("failed to open pipe");
        return;
    }
    handleReadFD = fds[0];
    handleWriteFD = fds[1];

    int flags = fcntl(handleReadFD, F_GETFL, 0);
    fcntl(handleReadFD, F_SETFL, flags | O_NONBLOCK);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    workerRunning = 1;
    if (pthread_create(&workerThread, &attr, curlWorker, mc) != 0) {
        fail("failed to start the HTTP worker thread");
    }
}

int naettPlatformInitRequest(InternalRequest* req) {
    return 1;
}

static size_t readCallback(char* buffer, size_t size, size_t numItems, void* userData) {
    InternalResponse* res = (InternalResponse*)userData;
    InternalRequest* req = res->request;
    return req->options.bodyReader(buffer, size * numItems, req->options.bodyReaderData);
}

static size_t writeCallback(char* ptr, size_t size, size_t numItems, void* userData) {
    InternalResponse* res = (InternalResponse*)userData;
    InternalRequest* req = res->request;
    size_t bytesWritten = req->options.bodyWriter(ptr, size * numItems, req->options.bodyWriterData);
    res->totalBytesRead += bytesWritten;
    return bytesWritten;
}

#define METHOD(A, B, C) (((A) << 16) | ((B) << 8) | (C))

static void setupMethod(CURL* curl, const char* method) {
    if (strlen(method) < 3) {
        return;
    }

    int methodCode = (method[0] << 16) | (method[1] << 8) | method[2];

    switch (methodCode) {
        case METHOD('G', 'E', 'T'):
        case METHOD('C', 'O', 'N'):
        case METHOD('O', 'P', 'T'):
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            break;
        case METHOD('P', 'O', 'S'):
        case METHOD('P', 'A', 'T'):
        case METHOD('D', 'E', 'L'):
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            break;
        case METHOD('P', 'U', 'T'):
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            break;
        case METHOD('H', 'E', 'A'):
        case METHOD('T', 'R', 'A'):
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            break;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
}

static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userData) {
    InternalResponse* res = (InternalResponse*) userData;
    size_t headerSize = size * nitems;

    char* headerName = strndup(buffer, headerSize);
    char* split = strchr(headerName, ':');
    if (split) {
        *split = 0;
        split++;
        while (*split == ' ') {
            split++;
        }
        char* headerValue = strdup(split);

        char* cr = strchr(headerValue, 13);
        if (cr) {
            *cr = 0;
        }

        char* lf = strchr(headerValue, 10);
        if (lf) {
            *lf = 0;
        }

        naettAlloc(KVLink, node);
        node->next = res->headers;
        node->key = headerName;
        node->value = headerValue;
        res->headers = node;
    }

    return headerSize;
}

void naettPlatformMakeRequest(InternalResponse* res) {
    InternalRequest* req = res->request;

    if (!workerRunning) {
        // No libcurl, or the worker died. Complete the request as failed rather than
        // leaving the caller waiting forever on a request nobody is going to run.
        res->code = naettGenericError;
        res->complete = 1;
        return;
    }

    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, req->url);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)req->options.timeoutMS);

    curl_easy_setopt(c, CURLOPT_READFUNCTION, readCallback);
    curl_easy_setopt(c, CURLOPT_READDATA, res);

    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, res);

    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, res);

    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);

    int bodySize = res->request->options.bodyReader(NULL, 0, res->request->options.bodyReaderData);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)bodySize);

    setupMethod(c, req->options.method);

    struct curl_slist* headerList = NULL;
    char uaBuf[512];
    snprintf(uaBuf, sizeof(uaBuf), "User-Agent: %s", req->options.userAgent ? req->options.userAgent : NAETT_UA);
    headerList = curl_slist_append(headerList, uaBuf);

    KVLink* header = req->options.headers;
    size_t bufferSize = 0;
    char* buffer = NULL;
    while (header) {
        size_t headerLength = strlen(header->key) + strlen(header->value) + 1 + 1;  // colon + null
        if (headerLength > bufferSize) {
            bufferSize = headerLength;
            buffer = (char*)realloc(buffer, bufferSize);
        }
        snprintf(buffer, bufferSize, "%s:%s", header->key, header->value);
        headerList = curl_slist_append(headerList, buffer);
        header = header->next;
    }
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headerList);
    free(buffer);
    res->headerList = headerList;

    curl_easy_setopt(c, CURLOPT_PRIVATE, res);

    write(handleWriteFD, &c, sizeof(c));
}

void naettPlatformFreeRequest(InternalRequest* req) {
}

void naettPlatformCloseResponse(InternalResponse* res) {
    if (!naettCurlLoad()) {
        // Nothing was ever allocated by curl, and the function pointers are all null.
        return;
    }
    curl_slist_free_all(res->headerList);
}

#endif
