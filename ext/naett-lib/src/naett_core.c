#include "naett_internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

typedef struct InternalParam* InternalParamPtr;
typedef void (*ParamSetter)(InternalParamPtr param, InternalRequest* req);

typedef struct InternalParam {
        ParamSetter setter;
        int offset;
        union {
            int integer;
            const char* string;
            struct {
                const char* key;
                const char* value;
            } kv;
            void* ptr;
            void (*func)(void);
        };
} InternalParam;

typedef struct InternalOption {
    #define maxParams 2
    int numParams;
    InternalParam params[maxParams];
} InternalOption;

static void stringSetter(InternalParamPtr param, InternalRequest* req) {
    char* stringCopy = strdup(param->string);
    char* opaque = (char*)&req->options;
    char** stringField = (char**)(opaque + param->offset);
    if (*stringField) {
        free(*stringField);
    }
    *stringField = stringCopy;
}

static void intSetter(InternalParamPtr param, InternalRequest* req) {
    char* opaque = (char*)&req->options;
    int* intField = (int*)(opaque + param->offset);
    *intField = param->integer;
}

static void ptrSetter(InternalParamPtr param, InternalRequest* req) {
    char* opaque = (char*)&req->options;
    void** ptrField = (void**)(opaque + param->offset);
    *ptrField = param->ptr;
}

static void kvSetter(InternalParamPtr param, InternalRequest* req) {
    char* opaque = (char*)&req->options;
    KVLink** kvField = (KVLink**)(opaque + param->offset);

    naettAlloc(KVLink, newNode);
    newNode->key = strdup(param->kv.key);
    newNode->value = strdup(param->kv.value);
    newNode->next = *kvField;

    *kvField = newNode;
}

static int defaultBodyReader(void* dest, int bufferSize, void* userData) {
    Buffer* buffer = (Buffer*) userData;

    if (dest == NULL) {
        return buffer->size;
    }

    int bytesToRead = buffer->size - buffer->position;
    if (bytesToRead > bufferSize) {
        bytesToRead = bufferSize;
    }

    const char* source = ((const char*)buffer->data) + buffer->position;
    memcpy(dest, source, bytesToRead);
    buffer->position += bytesToRead;
    return bytesToRead;
}

static int defaultBodyWriter(const void* source, int bytes, void* userData) {
    Buffer* buffer = (Buffer*) userData;
    int newCapacity = buffer->capacity;
    if (newCapacity == 0) {
        newCapacity = bytes;
    }
    while (newCapacity - buffer->size < bytes) {
        newCapacity *= 2;
    }
    if (newCapacity != buffer->capacity) {
        buffer->data = realloc(buffer->data, newCapacity);
        buffer->capacity = newCapacity;
    }
    char* dest = ((char*)buffer->data) + buffer->size;
    memcpy(dest, source, bytes);
    buffer->size += bytes;
    return bytes;
}

static int initialized = 0;

static void initRequest(InternalRequest* req, const char* url) {
    assert(initialized);
    req->options.method = strdup("GET");
    req->options.timeoutMS = 5000;
    req->url = strdup(url);
}

static void applyOptionParams(InternalRequest* req, InternalOption* option) {
    for (int j = 0; j < option->numParams; j++) {
        InternalParam* param = option->params + j;
        param->setter(param, req);
    }
}

// Public API

void naettInit(naettInitData initData) {
    assert(!initialized);
    naettPlatformInit(initData);
    initialized = 1;
}

naettOption* naettMethod(const char* method) {
    naettAlloc(InternalOption, option);
    option->numParams = 1;
    InternalParam* param = &option->params[0];

    param->string = method;
    param->offset = offsetof(RequestOptions, method);
    param->setter = stringSetter;

    return (naettOption*)option;
}

naettOption* naettHeader(const char* name, const char* value) {
    naettAlloc(InternalOption, option);
    option->numParams = 1;
    InternalParam* param = &option->params[0];

    param->kv.key = name;
    param->kv.value = value;
    param->offset = offsetof(RequestOptions, headers);
    param->setter = kvSetter;

    return (naettOption*)option;
}

naettOption* naettUserAgent(const char* userAgent) {
    naettAlloc(InternalOption, option);
    option->numParams = 1;
    InternalParam* param = &option->params[0];

    param->string = userAgent;
    param->offset = offsetof(RequestOptions, userAgent);
    param->setter = stringSetter;

    return (naettOption*)option;
}

naettOption* naettTimeout(int timeoutMS) {
    naettAlloc(InternalOption, option);
    option->numParams = 1;
    InternalParam* param = &option->params[0];

    param->integer = timeoutMS;
    param->offset = offsetof(RequestOptions, timeoutMS);
    param->setter = intSetter;

    return (naettOption*)option;
}

naettOption* naettBody(const char* body, int size) {
    naettAlloc(InternalOption, option);
    option->numParams = 2;

    InternalParam* bodyParam = &option->params[0];
    InternalParam* sizeParam = &option->params[1];

    bodyParam->ptr = (void*)body;
    bodyParam->offset = offsetof(RequestOptions, body) + offsetof(Buffer, data);
    bodyParam->setter = ptrSetter;

    sizeParam->integer = size;
    sizeParam->offset = offsetof(RequestOptions, body) + offsetof(Buffer, size);
    sizeParam->setter = intSetter;

    return (naettOption*)option;
}

naettOption* naettBodyReader(naettReadFunc reader, void* userData) {
    naettAlloc(InternalOption, option);
    option->numParams = 2;

    InternalParam* readerParam = &option->params[0];
    InternalParam* dataParam = &option->params[1];

    readerParam->func = (void (*)(void)) reader;
    readerParam->offset = offsetof(RequestOptions, bodyReader);
    readerParam->setter = ptrSetter;

    dataParam->ptr = userData;
    dataParam->offset = offsetof(RequestOptions, bodyReaderData);
    dataParam->setter = ptrSetter;

    return (naettOption*)option;
}

naettOption* naettBodyWriter(naettWriteFunc writer, void* userData) {
    naettAlloc(InternalOption, option);
    option->numParams = 2;

    InternalParam* writerParam = &option->params[0];
    InternalParam* dataParam = &option->params[1];

    writerParam->func = (void(*)(void)) writer;
    writerParam->offset = offsetof(RequestOptions, bodyWriter);
    writerParam->setter = ptrSetter;

    dataParam->ptr = userData;
    dataParam->offset = offsetof(RequestOptions, bodyWriterData);
    dataParam->setter = ptrSetter;

    return (naettOption*)option;
}

void setupDefaultRW(InternalRequest* req) {
    if (req->options.bodyReader == NULL) {
        req->options.bodyReader = defaultBodyReader;
        req->options.bodyReaderData = (void*) &req->options.body;
    }
    if (req->options.bodyReader == defaultBodyReader) {
        req->options.body.position = 0;
    }
    if (req->options.bodyWriter == NULL) {
        req->options.bodyWriter = defaultBodyWriter;
    }
}

naettReq* naettRequest_va(const char* url, int numArgs, ...) {
    assert(url != NULL);

    va_list args;
    InternalOption* option;
    naettAlloc(InternalRequest, req);
    initRequest(req, url);

    va_start(args, numArgs);
    for (int i = 0; i < numArgs; i++) {
        option = va_arg(args, InternalOption*);
        applyOptionParams(req, option);
        free(option);
    }
    va_end(args);

    setupDefaultRW(req);

    if (naettPlatformInitRequest(req)) {
        return (naettReq*)req;
    }

    naettFree((naettReq*) req);
    return NULL;
}

naettReq* naettRequestWithOptions(const char* url, int numOptions, const naettOption** options) {
    assert(url != NULL);
    assert(numOptions == 0 || options != NULL);

    naettAlloc(InternalRequest, req);
    initRequest(req, url);

    for (int i = 0; i < numOptions; i++) {
        InternalOption* option = (InternalOption*)options[i];
        applyOptionParams(req, option);
        free(option);
    }

    setupDefaultRW(req);

    if (naettPlatformInitRequest(req)) {
        return (naettReq*)req;
    }

    naettFree((naettReq*) req);
    return NULL;
}

naettRes* naettMake(naettReq* request) {
    assert(initialized);
    assert(request != NULL);

    InternalRequest* req = (InternalRequest*)request;
    naettAlloc(InternalResponse, res);
    res->request = req;

    if (req->options.bodyWriter == defaultBodyWriter) {
        req->options.bodyWriterData = (void*) &res->body;
    }

    naettPlatformMakeRequest(res);
    return (naettRes*) res;
}

const void* naettGetBody(naettRes* response, int* size) {
    assert(response != NULL);
    assert(size != NULL);

    InternalResponse* res = (InternalResponse*)response;
    *size = res->body.size;
    return res->body.data;
}

int naettGetTotalBytesRead(naettRes* response, int* totalSize) {
    assert(response != NULL);
    assert(totalSize != NULL);

    InternalResponse* res = (InternalResponse*)response;
    *totalSize = res->contentLength;
    return res->totalBytesRead;
}

const char* naettGetHeader(naettRes* response, const char* name) {
    assert(response != NULL);
    assert(name != NULL);

    InternalResponse* res = (InternalResponse*)response;
    KVLink* node = res->headers;
    while (node) {
        if (strcasecmp(name, node->key) == 0) {
            return node->value;
        }
        node = node->next;
    }
    return NULL;
}

void naettListHeaders(naettRes* response, naettHeaderLister lister, void* userData) {
    assert(response != NULL);
    assert(lister != NULL);

    InternalResponse* res = (InternalResponse*)response;
    KVLink* node = res->headers;
    while (node) {
        if (!lister(node->key, node->value, userData)) {
            return;
        }
        node = node->next;
    }
}

naettReq* naettGetRequest(naettRes* response) {
    assert(response != NULL);
    InternalResponse* res = (InternalResponse*)response;
    return (naettReq*) res->request;
}

int naettComplete(const naettRes* response) {
    assert(response != NULL);
    InternalResponse* res = (InternalResponse*)response;
    return res->complete;
}

int naettGetStatus(const naettRes* response) {
    assert(response != NULL);
    InternalResponse* res = (InternalResponse*)response;
    return res->code;
}

static void freeKVList(KVLink* node) {
    while (node != NULL) {
        free((void*) node->key);
        free((void*) node->value);
        KVLink* next = node->next;
        free(node);
        node = next;
    }
}

void naettFree(naettReq* request) {
    assert(request != NULL);

    InternalRequest* req = (InternalRequest*)request;
    naettPlatformFreeRequest(req);
    KVLink* node = req->options.headers;
    freeKVList(node);
    free((void*)req->options.method);
    free((void*)req->url);
    free(request);
}

void naettClose(naettRes* response) {
    assert(response != NULL);

    InternalResponse* res = (InternalResponse*)response;
    res->request = NULL;
    naettPlatformCloseResponse(res);
    KVLink* node = res->headers;
    freeKVList(node);
    free(res->body.data);
    free(response);
}
