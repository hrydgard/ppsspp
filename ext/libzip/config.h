#ifndef HAD_CONFIG_H
#define HAD_CONFIG_H
#ifndef _HAD_ZIPCONF_H
#include "zipconf.h"
#endif

#if defined(WINAPI_FAMILY) && defined(WINAPI_FAMILY_PARTITION)
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP
#define MS_UWP
#define _WIN32_WINNT 0x0602
#endif
#endif

/* BEGIN DEFINES */
#ifdef _WIN32
#define HAVE__CLOSE
#define HAVE__DUP
#define HAVE__FDOPEN
#define HAVE__FILENO
#define HAVE__SETMODE
#define HAVE__STRDUP
#define HAVE__STRICMP
#define HAVE__STRTOI64
#define HAVE__STRTOUI64
#define HAVE__UMASK
#define HAVE__UNLINK
#endif
#ifndef MS_UWP
#define HAVE_FILENO
#define HAVE_GETPROGNAME
#endif
#ifndef _WIN32
#define HAVE_FSEEKO
#define HAVE_FTELLO
#define HAVE_LOCALTIME_R
#define HAVE_MKSTEMP 1
#endif
#define HAVE_SNPRINTF
#define HAVE_STRDUP
#if !defined(__MINGW32__) && defined(_WIN32)
#define HAVE_STRICMP
#else
#define HAVE_STRCASECMP
#endif
#ifndef _WIN32
#define HAVE_STRINGS_H 1
#define HAVE_UNISTD_H
#endif
#if defined(_M_X64) || defined(__amd64__) || defined(__x86_64__) || defined(__aarch64__) || defined(_M_ARM64) || defined(__mips64__)
#define SIZEOF_OFF_T 8
#define SIZEOF_SIZE_T 8
#else
#define SIZEOF_OFF_T 4
#define SIZEOF_SIZE_T 4
#endif
/* END DEFINES */
#define PACKAGE "libzip"
#define VERSION "1.7.3"

#endif /* HAD_CONFIG_H */
