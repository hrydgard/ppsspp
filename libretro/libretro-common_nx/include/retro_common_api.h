/* Copyright  (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (retro_common_api.h).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _LIBRETRO_COMMON_RETRO_COMMON_API_H
#define _LIBRETRO_COMMON_RETRO_COMMON_API_H

/*
This file is designed to normalize the libretro-common compiling environment
for public API headers. This should be leaner than a normal compiling environment,
since it gets #included into other project's sources.
*/

/* ------------------------------------ */

/*
Ordinarily we want to put #ifdef __cplusplus extern "C" in C library
headers to enable them to get used by c++ sources.
However, we want to support building this library as C++ as well, so a
special technique is called for.
*/

#define RETRO_BEGIN_DECLS
#define RETRO_END_DECLS

#ifdef __cplusplus

#ifdef CXX_BUILD
/* build wants everything to be built as c++, so no extern "C" */
#else
#undef RETRO_BEGIN_DECLS
#undef RETRO_END_DECLS
#define RETRO_BEGIN_DECLS extern "C" {
#define RETRO_END_DECLS }
#endif

#else

/* header is included by a C source file, so no extern "C" */

#endif

/*
IMO, this non-standard ssize_t should not be used.
However, it's a good example of how to handle something like this.
*/
#ifdef _MSC_VER
#ifndef HAVE_SSIZE_T
#define HAVE_SSIZE_T
#if defined(_WIN64)
typedef __int64 ssize_t;
#elif defined(_WIN32)
typedef int ssize_t;
#endif
#endif
#elif defined(__MACH__)
#include <sys/types.h>
#endif

#ifdef _MSC_VER
#if _MSC_VER >= 1800
#include <inttypes.h>
#else
#ifndef PRId64
#define PRId64 "I64d"
#define PRIu64 "I64u"
#define PRIuPTR "Iu"
#endif
#endif
#else
/* C++11 says this one isn't needed, but apparently (some versions of) mingw require it anyways */
/* https://stackoverflow.com/questions/8132399/how-to-printf-uint64-t-fails-with-spurious-trailing-in-format */
/* https://github.com/libretro/RetroArch/issues/6009 */
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#endif
#ifndef PRId64
#error "inttypes.h is being screwy"
#endif
#define STRING_REP_INT64 "%" PRId64
#define STRING_REP_UINT64 "%" PRIu64
#define STRING_REP_USIZE "%" PRIuPTR

/*
I would like to see retro_inline.h moved in here; possibly boolean too.

rationale: these are used in public APIs, and it is easier to find problems
and write code that works the first time portably when theyre included uniformly
than to do the analysis from scratch each time you think you need it, for each feature.

Moreover it helps force you to make hard decisions: if you EVER bring in boolean.h,
then you should pay the price everywhere, so you can see how much grief it will cause.

Of course, another school of thought is that you should do as little damage as possible
in as few places as possible...
*/

/* _LIBRETRO_COMMON_RETRO_COMMON_API_H */
#endif
