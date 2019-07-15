/* Copyright  (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (filters.h).
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

#ifndef _LIBRETRO_SDK_FILTERS_H
#define _LIBRETRO_SDK_FILTERS_H

/* for MSVC; should be benign under any circumstances */
#define _USE_MATH_DEFINES

#include <stdlib.h>
#include <math.h>
#include <retro_inline.h>
#include <retro_math.h>

static INLINE double sinc(double val)
{
   if (fabs(val) < 0.00001)
      return 1.0;
   return sin(val) / val;
}

/* Paeth prediction filter. */
static INLINE int paeth(int a, int b, int c)
{
   int p  = a + b - c;
   int pa = abs(p - a);
   int pb = abs(p - b);
   int pc = abs(p - c);

   if (pa <= pb && pa <= pc)
      return a;
   else if (pb <= pc)
      return b;
   return c;
}

/* Modified Bessel function of first order.
 * Check Wiki for mathematical definition ... */
static INLINE double besseli0(double x)
{
   unsigned i;
   double sum            = 0.0;
   double factorial      = 1.0;
   double factorial_mult = 0.0;
   double x_pow          = 1.0;
   double two_div_pow    = 1.0;
   double x_sqr          = x * x;

   /* Approximate. This is an infinite sum.
    * Luckily, it converges rather fast. */
   for (i = 0; i < 18; i++)
   {
      sum += x_pow * two_div_pow / (factorial * factorial);

      factorial_mult += 1.0;
      x_pow *= x_sqr;
      two_div_pow *= 0.25;
      factorial *= factorial_mult;
   }

   return sum;
}

static INLINE double kaiser_window_function(double index, double beta)
{
   return besseli0(beta * sqrtf(1 - index * index));
}

static INLINE double lanzcos_window_function(double index)
{
   return sinc(M_PI * index);
}

#endif
