#include "naett_internal.h"

#ifdef __ANDROID__

#ifdef __cplusplus
#error "Cannot build in C++ mode"
#endif

#include <stdlib.h>
#include <string.h>
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stdarg.h>

#ifndef NDEBUG
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, "naett", __VA_ARGS__))
#else
#define LOGD(...) ((void)0)
#endif
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "naett", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "naett", __VA_ARGS__))

static JavaVM* globalVM = NULL;

static JavaVM* getVM() {
    if (globalVM == NULL) {
        LOGE("Panic: No VM configured, exiting.");
        exit(42);
    }
    return globalVM;
}

// PPSSPP: AttachCurrentThread on an already-attached thread is a no-op that still hands back the
// env, so report whether we're the ones who attached it. A thread that exits while attached is
// fatal on Android ("Native thread exiting without having called DetachCurrentThread"), and
// naettPlatformInitRequest/FreeRequest run on whichever thread the caller uses.
static JNIEnv* getEnvAttached(int* attached) {
    JavaVM* vm = getVM();
    JNIEnv* env = NULL;
    *attached = 0;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    *attached = 1;
    return env;
}

static void detachIfAttached(int attached) {
    if (attached) {
        JavaVM* vm = getVM();
        (*vm)->DetachCurrentThread(vm);
    }
}

static JNIEnv* getEnv() {
    int attached = 0;
    return getEnvAttached(&attached);
}

static int catch (JNIEnv* env) {
    int thrown = (*env)->ExceptionCheck(env);
    if (thrown) {
        (*env)->ExceptionDescribe(env);
    }
    return thrown;
}

static jmethodID getMethod(JNIEnv* env, jobject instance, const char* method, const char* sig) {
    if (instance == NULL) {
        return NULL;
    }
    jclass clazz = (*env)->GetObjectClass(env, instance);
    jmethodID id = (*env)->GetMethodID(env, clazz, method, sig);
    (*env)->DeleteLocalRef(env, clazz);
    return id;
}

// PPSSPP: GetMethodID leaves an exception pending and returns NULL when it can't find the
// method, and calling with a NULL jmethodID aborts the VM. Bail out instead - the caller's
// catch() picks up the pending exception.
static jobject call(JNIEnv* env, jobject instance, const char* method, const char* sig, ...) {
    jmethodID methodID = getMethod(env, instance, method, sig);
    if (methodID == NULL) {
        return NULL;
    }
    va_list args;
    va_start(args, sig);
    jobject result = (*env)->CallObjectMethodV(env, instance, methodID, args);
    va_end(args);
    return result;
}

static void voidCall(JNIEnv* env, jobject instance, const char* method, const char* sig, ...) {
    jmethodID methodID = getMethod(env, instance, method, sig);
    if (methodID == NULL) {
        return;
    }
    va_list args;
    va_start(args, sig);
    (*env)->CallVoidMethodV(env, instance, methodID, args);
    va_end(args);
}

static jint intCall(JNIEnv* env, jobject instance, const char* method, const char* sig, ...) {
    jmethodID methodID = getMethod(env, instance, method, sig);
    if (methodID == NULL) {
        return 0;
    }
    va_list args;
    va_start(args, sig);
    jint result = (*env)->CallIntMethodV(env, instance, methodID, args);
    va_end(args);
    return result;
}

void naettPlatformInit(naettInitData initData) {
    globalVM = initData;
}

int naettPlatformInitRequest(InternalRequest* req) {
    int attached = 0;
    JNIEnv* env = getEnvAttached(&attached);
    if (env == NULL) {
        return 0;
    }
    (*env)->PushLocalFrame(env, 10);
    jclass URL = (*env)->FindClass(env, "java/net/URL");
    jmethodID newURL = (*env)->GetMethodID(env, URL, "<init>", "(Ljava/lang/String;)V");
    jstring urlString = (*env)->NewStringUTF(env, req->url);
    jobject url = (*env)->NewObject(env, URL, newURL, urlString);
    if (catch (env)) {
        (*env)->PopLocalFrame(env, NULL);
        detachIfAttached(attached);
        return 0;
    }
    req->urlObject = (*env)->NewGlobalRef(env, url);
    (*env)->PopLocalFrame(env, NULL);
    detachIfAttached(attached);
    return 1;
}

static void* processRequest(void* data) {
    const int bufSize = 10240;
    char byteBuffer[bufSize];
    InternalResponse* res = (InternalResponse*)data;
    InternalRequest* req = res->request;

    JNIEnv* env = getEnv();
    (*env)->PushLocalFrame(env, 100);

    jobject connection = call(env, req->urlObject, "openConnection", "()Ljava/net/URLConnection;");
    if (catch (env)) {
        res->code = naettConnectionError;
        goto finally;
    }

    {
        jstring name = (*env)->NewStringUTF(env, "User-Agent");
        jstring value = (*env)->NewStringUTF(env, req->options.userAgent ? req->options.userAgent : NAETT_UA);
        voidCall(env, connection, "addRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", name, value);
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, value);
    }

    KVLink* header = req->options.headers;
    while (header != NULL) {
        jstring name = (*env)->NewStringUTF(env, header->key);
        jstring value = (*env)->NewStringUTF(env, header->value);
        voidCall(env, connection, "addRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", name, value);
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, value);
        header = header->next;
    }

    jobject outputStream = NULL;
    if (strcmp(req->options.method, "POST") == 0 || strcmp(req->options.method, "PUT") == 0 ||
        strcmp(req->options.method, "PATCH") == 0 || strcmp(req->options.method, "DELETE") == 0) {
        voidCall(env, connection, "setDoOutput", "(Z)V", 1);
        outputStream = call(env, connection, "getOutputStream", "()Ljava/io/OutputStream;");
        // PPSSPP: most JNI calls are not safe to make with an exception pending, and this one
        // throws for anything from a refused connection to a protocol the server won't take.
        if (catch (env)) {
            res->code = naettConnectionError;
            goto finally;
        }
    }
    jobject methodString = (*env)->NewStringUTF(env, req->options.method);
    voidCall(env, connection, "setRequestMethod", "(Ljava/lang/String;)V", methodString);
    voidCall(env, connection, "setConnectTimeout", "(I)V", req->options.timeoutMS);
    voidCall(env, connection, "setInstanceFollowRedirects", "(Z)V", 1);

    voidCall(env, connection, "connect", "()V");
    if (catch (env)) {
        res->code = naettConnectionError;
        goto finally;
    }

    jbyteArray buffer = (*env)->NewByteArray(env, bufSize);

    if (outputStream != NULL) {
        int bytesRead = 0;
        if (req->options.bodyReader != NULL)
            do {
                bytesRead = req->options.bodyReader(byteBuffer, bufSize, req->options.bodyReaderData);
                if (bytesRead > 0) {
                    (*env)->SetByteArrayRegion(env, buffer, 0, bytesRead, (const jbyte*) byteBuffer);
                    voidCall(env, outputStream, "write", "([BII)V", buffer, 0, bytesRead);
                } else {
                    break;
                }
            } while (!res->closeRequested);
        voidCall(env, outputStream, "close", "()V");
    }

    jobject headerMap = call(env, connection, "getHeaderFields", "()Ljava/util/Map;");
    if (catch (env)) {
        res->code = naettProtocolError;
        goto finally;
    }

    jobject headerSet = call(env, headerMap, "keySet", "()Ljava/util/Set;");
    jarray headers = call(env, headerSet, "toArray", "()[Ljava/lang/Object;");
    jsize headerCount = (*env)->GetArrayLength(env, headers);

    KVLink *firstHeader = NULL;
    for (int i = 0; i < headerCount; i++) {
        jstring name = (*env)->GetObjectArrayElement(env, headers, i);
        if (name == NULL) {
            continue;
        }
        jobject values = call(env, headerMap, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", name);
        jstring value = call(env, values, "get", "(I)Ljava/lang/Object;", 0);
        // PPSSPP: a header with no values gives a null here, and GetStringUTFChars on it aborts.
        if (value == NULL) {
            (*env)->ExceptionClear(env);
            (*env)->DeleteLocalRef(env, name);
            (*env)->DeleteLocalRef(env, values);
            continue;
        }
        const char* nameString = (*env)->GetStringUTFChars(env, name, NULL);
        const char* valueString = (*env)->GetStringUTFChars(env, value, NULL);
        if (nameString == NULL || valueString == NULL) {
            if (nameString) (*env)->ReleaseStringUTFChars(env, name, nameString);
            if (valueString) (*env)->ReleaseStringUTFChars(env, value, valueString);
            (*env)->DeleteLocalRef(env, name);
            (*env)->DeleteLocalRef(env, value);
            (*env)->DeleteLocalRef(env, values);
            continue;
        }

        naettAlloc(KVLink, node);
        node->key = strdup(nameString);
        node->value = strdup(valueString);
        node->next = firstHeader;
        firstHeader = node;

        (*env)->ReleaseStringUTFChars(env, name, nameString);
        (*env)->ReleaseStringUTFChars(env, value, valueString);

        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, value);
        (*env)->DeleteLocalRef(env, values);
    }
    res->headers = firstHeader;

    const char *contentLength = naettGetHeader((naettRes *)res, "Content-Length");
    if (!contentLength || sscanf(contentLength, "%d", &res->contentLength) != 1) {
        res->contentLength = -1;
    }

    int statusCode = intCall(env, connection, "getResponseCode", "()I");

    jobject inputStream = NULL;

    if (statusCode >= 400) {
        inputStream = call(env, connection, "getErrorStream", "()Ljava/io/InputStream;");
    } else {
        inputStream = call(env, connection, "getInputStream", "()Ljava/io/InputStream;");
    }
    if (catch (env)) {
        res->code = naettProtocolError;
        goto finally;
    }

    jint bytesRead = 0;
    do {
        bytesRead = intCall(env, inputStream, "read", "([B)I", buffer);
        if (catch (env)) {
            res->code = naettReadError;
            goto finally;
        }
        if (bytesRead < 0) {
            break;
        } else if (bytesRead > 0) {
            (*env)->GetByteArrayRegion(env, buffer, 0, bytesRead, (jbyte*) byteBuffer);
            req->options.bodyWriter(byteBuffer, bytesRead, req->options.bodyWriterData);
            res->totalBytesRead += bytesRead;
        }
    } while (!res->closeRequested);

    voidCall(env, inputStream, "close", "()V");

    res->code = statusCode;

finally:
    res->complete = 1;
    (*env)->PopLocalFrame(env, NULL);
    JavaVM* vm = getVM();
    (*env)->ExceptionClear(env);
    (*vm)->DetachCurrentThread(vm);
    return NULL;
}

static void startWorkerThread(InternalResponse* res) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    // PPSSPP: upstream ignored this. With no thread, nothing ever sets res->complete and the
    // caller polls naettComplete forever.
    if (pthread_create(&res->workerThread, &attr, processRequest, res) != 0) {
        LOGE("Failed to start the request worker thread");
        res->workerThread = 0;
        res->code = naettGenericError;
        res->complete = 1;
    } else {
        pthread_setname_np(res->workerThread, "naett worker thread");
    }
    pthread_attr_destroy(&attr);
}

void naettPlatformMakeRequest(InternalResponse* res) {
    startWorkerThread(res);
}

void naettPlatformFreeRequest(InternalRequest* req) {
    if (req->urlObject == NULL) {
        // Init failed before it got that far.
        return;
    }
    int attached = 0;
    JNIEnv* env = getEnvAttached(&attached);
    if (env == NULL) {
        return;
    }
    (*env)->DeleteGlobalRef(env, req->urlObject);
    req->urlObject = NULL;
    detachIfAttached(attached);
}

void naettPlatformCloseResponse(InternalResponse* res) {
    res->closeRequested = 1;
    if (res->workerThread != 0) {
        int joinResult = pthread_join(res->workerThread, NULL);
        if (joinResult != 0) {
            LOGE("Failed to join: %s", strerror(joinResult));
        }
    }
}

#endif  // __ANDROID__
