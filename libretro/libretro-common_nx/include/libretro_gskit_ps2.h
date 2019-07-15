/* Copyright (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------------
 * The following license statement only applies to this libretro API header (libretro_d3d.h)
 * ---------------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the
 * "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBRETRO_GSKIT_PS2_H_
#define LIBRETRO_GSKIT_PS2_H_

#include "libretro.h"

#if defined(PS2)

#include <gsKit.h>

#define RETRO_HW_RENDER_INTERFACE_GSKIT_PS2_VERSION 1

struct retro_hw_ps2_insets
{
  float top;
  float left;
  float bottom;
  float right;
};

#define empty_ps2_insets (struct retro_hw_ps2_insets){0.f, 0.f, 0.f, 0.f}

struct retro_hw_render_interface_gskit_ps2
{
  /* Must be set to RETRO_HW_RENDER_INTERFACE_GSKIT_PS2. */
  enum retro_hw_render_interface_type interface_type;
  /* Must be set to RETRO_HW_RENDER_INTERFACE_GSKIT_PS2_VERSION. */
  unsigned interface_version;

  /* Opaque handle to the GSKit_PS2 backend in the frontend
   * which must be passed along to all function pointers
   * in this interface.
   */
   GSTEXTURE *coreTexture;
   bool clearTexture;
   bool updatedPalette;
   struct retro_hw_ps2_insets padding;
};
typedef struct retro_hw_render_interface_gskit_ps2 RETRO_HW_RENDER_INTEFACE_GSKIT_PS2;

#endif

#endif /* LIBRETRO_GSKIT_PS2_H_ */
