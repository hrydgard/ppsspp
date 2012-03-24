#pragma once

#ifdef _MSC_VER
#pragma warning (disable:4996)   //strcpy may be dangerous
#endif

// Logging
#ifdef _WIN32

inline void Crash() { __asm { int 3 }; }

#else

#ifdef ANDROID

inline void Crash() {
  char *p = (char *)1337;
  *p = 1;
}
#else

inline void Crash() {
  asm("int $0x3");
}
#endif

#endif

#ifdef ANDROID

#include <android/log.h>

#ifndef APP_NAME
#define APP_NAME "Turboviking"
#endif

#define ILOG(...)    __android_log_print(ANDROID_LOG_INFO, APP_NAME, __VA_ARGS__);
#define WLOG(...)    __android_log_print(ANDROID_LOG_WARN, APP_NAME, __VA_ARGS__);
#define ELOG(...)    __android_log_print(ANDROID_LOG_ERROR, APP_NAME, __VA_ARGS__);
#define FLOG(...)   { __android_log_print(ANDROID_LOG_ERROR, APP_NAME, __VA_ARGS__); Crash(); }

#define MessageBox(a, b, c, d) __android_log_print(ANDROID_LOG_INFO, APP_NAME, "%s %s", (b), (c));

#else

#include <stdio.h>

#define ILOG(...) {printf("I: %s:%i: ", __FILE__, __LINE__); printf("I: " __VA_ARGS__); printf("\n");}
#define WLOG(...) {printf("W: %s:%i: ", __FILE__, __LINE__); printf("W: " __VA_ARGS__); printf("\n");}
#define ELOG(...) {printf("E: %s:%i: ", __FILE__, __LINE__); printf("E: " __VA_ARGS__); printf("\n");}
#define FLOG(...) {printf("F: %s:%i: ", __FILE__, __LINE__); printf("F: " __VA_ARGS__); printf("\n"); Crash();}

#ifndef MessageBox
#define MessageBox(a, b, c, d) printf("MSG: %s %s\n", (b), (c));
#endif

#endif

#define CHECK(a) {if (!(a)) {FLOG("CHECK failed");}}
