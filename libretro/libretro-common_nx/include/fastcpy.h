/* Copyright  (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (fastcpy.h).
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

/* in the future ASM and new c++ features can be added to speed up copying */
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>

static INLINE void* memcpy16(void* dst,void* src,size_t size)
{
   return memcpy(dst,src,size * 2);
}

static INLINE void* memcpy32(void* dst,void* src,size_t size)
{
   return memcpy(dst,src,size * 4);
}

static INLINE void* memcpy64(void* dst,void* src,size_t size)
{
   return memcpy(dst,src,size * 8);
}

#ifdef USECPPSTDFILL
#include <algorithm>

static INLINE void* memset16(void* dst,uint16_t val,size_t size)
{
   uint16_t* typedptr = (uint16_t*)dst;
   std::fill(typedptr, typedptr + size, val);
   return dst;
}

static INLINE void* memset32(void* dst,uint32_t val,size_t size)
{
   uint32_t* typedptr = (uint32_t*)dst;
   std::fill(typedptr, typedptr + size, val);
   return dst;
}

static INLINE void* memset64(void* dst,uint64_t val,size_t size)
{
   uint64_t* typedptr = (uint64_t*)dst;
   std::fill(typedptr, typedptr + size, val);
   return dst;
}
#else

static INLINE void* memset16(void* dst,uint16_t val,size_t size)
{
   size_t i;
   uint16_t* typedptr = (uint16_t*)dst;
   for(i = 0;i < size;i++)
      typedptr[i] = val;
   return dst;
}

static INLINE void* memset32(void* dst,uint32_t val,size_t size)
{
   size_t i;
   uint32_t* typedptr = (uint32_t*)dst;
   for(i = 0;i < size;i++)
      typedptr[i] = val;
   return dst;
}

static INLINE void* memset64(void* dst,uint64_t val,size_t size)
{
   size_t i;
   uint64_t* typedptr = (uint64_t*)dst;
   for(i = 0;i < size;i++)
      typedptr[i] = val;
   return dst;
}
#endif
