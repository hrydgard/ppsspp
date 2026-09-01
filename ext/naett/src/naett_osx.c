#include "naett_internal.h"

#ifdef __APPLE__

#include "naett_objc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef DEBUG
static void _showPools(const char* context) {
    fprintf(stderr, "NSAutoreleasePool@%s:\n", context);
    objc_msgSend_void(class("NSAutoreleasePool"), sel("showPools"));
}
#define showPools(x) _showPools((x))
#else
#define showPools(x)
#endif

static id pool(void) {
    return objc_msgSend_id(objc_alloc("NSAutoreleasePool"), sel("init"));
}

static id sessionConfiguration = nil;

void naettPlatformInit(naettInitData initData) {
    id NSThread = class("NSThread");
    SEL isMultiThreaded = sel("isMultiThreaded");

    if (!objc_msgSend_t(bool)(NSThread, isMultiThreaded)) {
        // Make a dummy call from a new thread to kick Cocoa into multi-threaded mode
        objc_msgSend_t(void, SEL, id, id)(
            NSThread, sel("detachNewThreadSelector:toTarget:withObject:"), isMultiThreaded, NSThread, nil);
    }

    sessionConfiguration = objc_msgSend_id(class("NSURLSessionConfiguration"), sel("ephemeralSessionConfiguration"));
    retain(sessionConfiguration);
}

id NSString(const char* string) {
    return objc_msgSend_t(id, const char*)(class("NSString"), sel("stringWithUTF8String:"), string);
}

int naettPlatformInitRequest(InternalRequest* req) {
    id p = pool();

    id urlString = NSString(req->url);
    id url = objc_msgSend_t(id, id)(class("NSURL"), sel("URLWithString:"), urlString);

    id request = objc_msgSend_t(id, id)(class("NSMutableURLRequest"), sel("requestWithURL:"), url);

    objc_msgSend_t(void, double)(request, sel("setTimeoutInterval:"), (double)(req->options.timeoutMS) / 1000.0);
    id methodString = NSString(req->options.method);
    objc_msgSend_t(void, id)(request, sel("setHTTPMethod:"), methodString);

    {
        id name = NSString("User-Agent");
        id value = NSString(req->options.userAgent ? req->options.userAgent : NAETT_UA);
        objc_msgSend_t(void, id, id)(request, sel("setValue:forHTTPHeaderField:"), value, name);
    }

    KVLink* header = req->options.headers;
    while (header != NULL) {
        id name = NSString(header->key);
        id value = NSString(header->value);
        objc_msgSend_t(void, id, id)(request, sel("setValue:forHTTPHeaderField:"), value, name);
        header = header->next;
    }

    char byteBuffer[10240];
    int bytesRead = 0;

    if (req->options.bodyReader != NULL) {
        id bodyData =
            objc_msgSend_t(id, NSUInteger)(class("NSMutableData"), sel("dataWithCapacity:"), sizeof(byteBuffer));

        int totalBytesRead = 0;
        do {
            bytesRead = req->options.bodyReader(byteBuffer, sizeof(byteBuffer), req->options.bodyReaderData);
            totalBytesRead += bytesRead;
            objc_msgSend_t(void, const void*, NSUInteger)(bodyData, sel("appendBytes:length:"), byteBuffer, bytesRead);
        } while (bytesRead > 0);

        if (totalBytesRead > 0) {
            objc_msgSend_t(void, id)(request, sel("setHTTPBody:"), bodyData);
        }
    }

    retain(request);
    req->urlRequest = request;

    release(p);
    return 1;
}

void didReceiveData(id self, SEL _sel, id session, id dataTask, id data) {
    InternalResponse* res = NULL;
    id p = pool();

    object_getInstanceVariable(self, "response", (void**)&res);

    if (res->headers == NULL) {
        id response = objc_msgSend_t(id)(dataTask, sel("response"));
        res->code = objc_msgSend_t(NSInteger)(response, sel("statusCode"));
        id allHeaders = objc_msgSend_t(id)(response, sel("allHeaderFields"));

        NSUInteger headerCount = objc_msgSend_t(NSUInteger)(allHeaders, sel("count"));
        id headerNames[headerCount];
        id headerValues[headerCount];

        objc_msgSend_t(NSInteger, id*, id*, NSUInteger)(
            allHeaders, sel("getObjects:andKeys:count:"), headerValues, headerNames, headerCount);
        KVLink* firstHeader = NULL;
        for (int i = 0; i < headerCount; i++) {
            naettAlloc(KVLink, node);
            node->key = strdup(objc_msgSend_t(const char*)(headerNames[i], sel("UTF8String")));
            node->value = strdup(objc_msgSend_t(const char*)(headerValues[i], sel("UTF8String")));
            node->next = firstHeader;
            firstHeader = node;
        }
        res->headers = firstHeader;

        const char* contentLength = naettGetHeader((naettRes*)res, "Content-Length");
        if (!contentLength || sscanf(contentLength, "%d", &res->contentLength) != 1) {
            res->contentLength = -1;
        }
    }

    const void* bytes = objc_msgSend_t(const void*)(data, sel("bytes"));
    NSUInteger length = objc_msgSend_t(NSUInteger)(data, sel("length"));

    res->request->options.bodyWriter(bytes, length, res->request->options.bodyWriterData);
    res->totalBytesRead += (int)length;

    release(p);
}

static void didComplete(id self, SEL _sel, id session, id dataTask, id error) {
    InternalResponse* res = NULL;
    object_getInstanceVariable(self, "response", (void**)&res);
    if (res != NULL) {
        if (error != nil) {
            res->code = naettConnectionError;
        }
        res->complete = 1;
    }
}

static id createDelegate(void) {
    static Class TaskDelegateClass = nil;

    if (!TaskDelegateClass) {
        TaskDelegateClass = objc_allocateClassPair((Class)objc_getClass("NSObject"), "naettTaskDelegate", 0);
        class_addProtocol(TaskDelegateClass, (Protocol* _Nonnull)objc_getProtocol("NSURLSessionDataDelegate"));

        addMethod(TaskDelegateClass, "URLSession:dataTask:didReceiveData:", didReceiveData, "v@:@@@");
        addMethod(TaskDelegateClass, "URLSession:task:didCompleteWithError:", didComplete, "v@:@@@");
        addIvar(TaskDelegateClass, "response", sizeof(void*), "^v");
    }

    id delegate = objc_msgSend_id((id)TaskDelegateClass, sel("alloc"));
    delegate = objc_msgSend_id(delegate, sel("init"));

    return delegate;
}

void naettPlatformMakeRequest(InternalResponse* res) {
    InternalRequest* req = res->request;
    id p = pool();

    id delegate = createDelegate();
    // delegate will be retained by session below
    autorelease(delegate);
    object_setInstanceVariable(delegate, "response", (void*)res);

    id session = objc_msgSend_t(id, id, id, id)(class("NSURLSession"),
        sel("sessionWithConfiguration:delegate:delegateQueue:"),
        sessionConfiguration,
        delegate,
        nil);

    res->session = session;

    id task = objc_msgSend_t(id, id)(session, sel("dataTaskWithRequest:"), req->urlRequest);
    objc_msgSend_void(task, sel("resume"));

    release(p);
}

void naettPlatformFreeRequest(InternalRequest* req) {
    release(req->urlRequest);
    req->urlRequest = nil;
}

void naettPlatformCloseResponse(InternalResponse* res) {
    objc_msgSend_void(res->session, sel("invalidateAndCancel"));
    res->session = nil;
}

#endif  // __APPLE__
