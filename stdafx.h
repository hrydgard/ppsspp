
#pragma once

#define _CRT_SECURE_NO_WARNINGS
#undef __STRICT_ANSI__

#if defined(__clang__)
#if __has_feature(cxx_exceptions)
#define ARMIPS_EXCEPTIONS 1
#else
#define ARMIPS_EXCEPTIONS 0
#endif
#elif defined(_MSC_VER) && defined(_CPPUNWIND)
#define ARMIPS_EXCEPTIONS 1
#elif defined(__EXCEPTIONS) || defined(__cpp_exceptions)
#define ARMIPS_EXCEPTIONS 1
#else
#define ARMIPS_EXCEPTIONS 0
#endif

#include <cstdio>
#include <vector>
#include <cstdlib>
#include <cstdarg>
#include <cctype>
#include <cstring>
#include <cmath>
#include <clocale>

#include <sstream>
#include <iomanip>

#include "ext/tinyformat/tinyformat.h"
#define formatString tfm::format
