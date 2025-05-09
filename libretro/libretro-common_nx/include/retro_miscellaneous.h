/* Copyright  (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (retro_miscellaneous.h).
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

#ifndef __RARCH_MISCELLANEOUS_H
#define __RARCH_MISCELLANEOUS_H

#define RARCH_MAX_SUBSYSTEMS 10
#define RARCH_MAX_SUBSYSTEM_ROMS 10

#include <stdint.h>
#include <boolean.h>
#include <retro_inline.h>

#if defined(_WIN32) && !defined(_XBOX)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(_WIN32) && defined(_XBOX)
#include <Xtl.h>
#endif

#if defined(__CELLOS_LV2__)
#include <sys/fs_external.h>
#endif

#include <limits.h>

#ifdef _MSC_VER
#include <compat/msvc.h>
#endif

static INLINE void bits_or_bits(uint32_t *a, uint32_t *b, uint32_t count)
{
   uint32_t i;
   for (i = 0; i < count;i++)
      a[i] |= b[i];
}

static INLINE void bits_clear_bits(uint32_t *a, uint32_t *b, uint32_t count)
{
   uint32_t i;
   for (i = 0; i < count;i++)
      a[i] &= ~b[i];
}

static INLINE bool bits_any_set(uint32_t* ptr, uint32_t count)
{
   uint32_t i;
   for (i = 0; i < count; i++)
   {
      if (ptr[i] != 0)
         return true;
   }
   return false;
}

#ifndef PATH_MAX_LENGTH
#if defined(__CELLOS_LV2__)
#define PATH_MAX_LENGTH CELL_FS_MAX_FS_PATH_LENGTH
#elif defined(_XBOX1) || defined(_3DS) || defined(PSP) || defined(PS2) || defined(GEKKO)|| defined(WIIU) || defined(ORBIS)
#define PATH_MAX_LENGTH 512
#else
#define PATH_MAX_LENGTH 4096
#endif
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define ARRAY_SIZE(a)              (sizeof(a) / sizeof((a)[0]))

#define BITS_GET_ELEM(a, i)        ((a).data[i])
#define BITS_GET_ELEM_PTR(a, i)    ((a)->data[i])

#define BIT_SET(a, bit)   ((a)[(bit) >> 3] |=  (1 << ((bit) & 7)))
#define BIT_CLEAR(a, bit) ((a)[(bit) >> 3] &= ~(1 << ((bit) & 7)))
#define BIT_GET(a, bit)   (((a)[(bit) >> 3] >> ((bit) & 7)) & 1)

#define BIT16_SET(a, bit)    ((a) |=  (1 << ((bit) & 15)))
#define BIT16_CLEAR(a, bit)  ((a) &= ~(1 << ((bit) & 15)))
#define BIT16_GET(a, bit)    (((a) >> ((bit) & 15)) & 1)
#define BIT16_CLEAR_ALL(a)   ((a) = 0)

#define BIT32_SET(a, bit)    ((a) |=  (1 << ((bit) & 31)))
#define BIT32_CLEAR(a, bit)  ((a) &= ~(1 << ((bit) & 31)))
#define BIT32_GET(a, bit)    (((a) >> ((bit) & 31)) & 1)
#define BIT32_CLEAR_ALL(a)   ((a) = 0)

#define BIT64_SET(a, bit)    ((a) |=  (UINT64_C(1) << ((bit) & 63)))
#define BIT64_CLEAR(a, bit)  ((a) &= ~(UINT64_C(1) << ((bit) & 63)))
#define BIT64_GET(a, bit)    (((a) >> ((bit) & 63)) & 1)
#define BIT64_CLEAR_ALL(a)   ((a) = 0)

#define BIT128_SET(a, bit)   ((a).data[(bit) >> 5] |=  (1 << ((bit) & 31)))
#define BIT128_CLEAR(a, bit) ((a).data[(bit) >> 5] &= ~(1 << ((bit) & 31)))
#define BIT128_GET(a, bit)   (((a).data[(bit) >> 5] >> ((bit) & 31)) & 1)
#define BIT128_CLEAR_ALL(a)  memset(&(a), 0, sizeof(a))

#define BIT128_SET_PTR(a, bit)   BIT128_SET(*a, bit)
#define BIT128_CLEAR_PTR(a, bit) BIT128_CLEAR(*a, bit)
#define BIT128_GET_PTR(a, bit)   BIT128_GET(*a, bit)
#define BIT128_CLEAR_ALL_PTR(a)  BIT128_CLEAR_ALL(*a)

#define BIT256_SET(a, bit)       BIT128_SET(a, bit)
#define BIT256_CLEAR(a, bit)     BIT128_CLEAR(a, bit)
#define BIT256_GET(a, bit)       BIT128_GET(a, bit)
#define BIT256_CLEAR_ALL(a)      BIT128_CLEAR_ALL(a)

#define BIT256_SET_PTR(a, bit)   BIT256_SET(*a, bit)
#define BIT256_CLEAR_PTR(a, bit) BIT256_CLEAR(*a, bit)
#define BIT256_GET_PTR(a, bit)   BIT256_GET(*a, bit)
#define BIT256_CLEAR_ALL_PTR(a)  BIT256_CLEAR_ALL(*a)

#define BITS_COPY16_PTR(a,bits) \
{ \
   BIT128_CLEAR_ALL_PTR(a); \
   BITS_GET_ELEM_PTR(a, 0) = (bits) & 0xffff; \
}

#define BITS_COPY32_PTR(a,bits) \
{ \
   BIT128_CLEAR_ALL_PTR(a); \
   BITS_GET_ELEM_PTR(a, 0) = (bits); \
}

/* Helper macros and struct to keep track of many booleans. */
/* This struct has 256 bits. */
typedef struct
{
   uint32_t data[8];
} retro_bits_t;

#ifdef _WIN32
#  ifdef _WIN64
#    define PRI_SIZET PRIu64
#  else
#    if _MSC_VER == 1800
#      define PRI_SIZET PRIu32
#    else
#      define PRI_SIZET "u"
#    endif
#  endif
#elif PS2
#  define PRI_SIZET "u"
#else
#  if (SIZE_MAX == 0xFFFF)
#    define PRI_SIZET "hu"
#  elif (SIZE_MAX == 0xFFFFFFFF)
#    define PRI_SIZET "u"
#  elif (SIZE_MAX == 0xFFFFFFFFFFFFFFFF)
#    define PRI_SIZET "lu"
#  else
#    error PRI_SIZET: unknown SIZE_MAX
#  endif
#endif

#endif
