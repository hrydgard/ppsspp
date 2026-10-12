/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (video_hw_frame.h).
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

#ifndef __LIBRETRO_SDK_FORMAT_VIDEO_HW_FRAME_H__
#define __LIBRETRO_SDK_FORMAT_VIDEO_HW_FRAME_H__

/* A decoded picture that stays on the GPU: what a hardware decoder
 * hands out, and what a video driver that imports such frames reads.
 * Handles are given as their bytes (a Vulkan non-dispatchable handle
 * is 64 bits wide on every target).
 *
 * Vulkan: a VkImage of VK_FORMAT_G8_B8R8_2PLANE_420_UNORM in @layout,
 * and two timeline semaphores. A reader waits for @wait to reach
 * @wait_value before it reads, and signals @done with @done_value once
 * it reads the frame no more: until then the decoder does not write
 * the image again. A frame that will never be read is given back with
 * video_hw_frame_abandon, which signals @done in the reader's place. */

#include <stdint.h>
#include <stddef.h>

#include <retro_common_api.h>
#include <retro_inline.h>

RETRO_BEGIN_DECLS

typedef struct video_hw_frame
{
   uint64_t image;
   uint64_t wait;
   uint64_t wait_value;
   uint64_t done;
   uint64_t done_value;
   /* The decoder's: signals @done as if read. NULL: nothing to do. */
   void (*abandon)(void *owner, uint64_t done, uint64_t done_value);
   void *owner;
   unsigned width, height;
   unsigned layout;
   unsigned yuv;             /* IMAGE_YUV_FLAG_* (image_yuv_blit.h) */
} video_hw_frame_t;

static INLINE void video_hw_frame_abandon(const video_hw_frame_t *f)
{
   if (f && f->abandon)
      f->abandon(f->owner, f->done, f->done_value);
}

RETRO_END_DECLS

#endif
