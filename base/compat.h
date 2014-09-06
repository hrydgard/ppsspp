#pragma once

// Implement C99 functions and similar that are missing in MSVC.

#ifdef _MSC_VER

int c99_snprintf(char* str, size_t size, const char* format, ...);
#define snprintf c99_snprintf
#define vscprintf _vscprintf

#endif