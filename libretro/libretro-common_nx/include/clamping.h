/* Copyright  (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (clamping.h).
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

#ifndef _LIBRETRO_SDK_CLAMPING_H
#define _LIBRETRO_SDK_CLAMPING_H

#include <stdint.h>
#include <retro_inline.h>

/**
 * clamp_float:
 * @val           : initial value
 * @lower         : lower limit that value should be clamped against
 * @upper         : upper limit that value should be clamped against
 *
 * Clamps a floating point value.
 *
 * Returns: a clamped value of initial float value @val.
 */
static INLINE float clamp_float(float val, float lower, float upper)
{
   if (val < lower)
      return lower;
   if (val > upper)
      return upper;
   return val;
}

/**
 * clamp_8bit:
 * @val           : initial value
 *
 * Clamps an unsigned 8-bit value.
 *
 * Returns: a clamped value of initial unsigned 8-bit value @val.
 */
static INLINE uint8_t clamp_8bit(int val)
{
   if (val > 255)
      return 255;
   if (val < 0)
      return 0;
   return (uint8_t)val;
}

#endif
