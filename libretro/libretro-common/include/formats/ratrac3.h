/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (ratrac3.h).
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

#ifndef __LIBRETRO_SDK_FORMAT_RATRAC3_H
#define __LIBRETRO_SDK_FORMAT_RATRAC3_H

/* ATRAC3 audio decoder (Sony's LP2/LP4 codec, as in PSP .at3 files,
 * WAVE format tag 270h, and OpenMG).
 *
 * Written clean-room: the bit-stream syntax from public format notes,
 * the signal path measured against an existing decoder's output, never
 * its source. Every frame decodes to 1024 samples a channel.
 *
 * A frame is block_align bytes: one sound unit a channel, block_align /
 * channels bytes each, or, in joint stereo (the 66 kb/s LP4 mode), a
 * mid unit read forwards from the start and a side unit read backwards
 * from the end, with the stereo parameters at the very end.
 *
 *    ratrac3_info_t info;
 *    ratrac3_parse_wave_fmt(fmt_chunk, fmt_len, &info);
 *    ratrac3_t *d = ratrac3_new(info.channels, info.block_align,
 *          info.joint_stereo);
 *    while (have a frame)
 *       ratrac3_decode_s16(d, frame, info.block_align, pcm);
 *
 * C89. A decoder holds no pointer into the input between calls. */

#include <stddef.h>
#include <stdint.h>

#include <retro_common_api.h>
#include <boolean.h>

RETRO_BEGIN_DECLS

#define RATRAC3_SAMPLES     1024 /* a channel, every frame */
#define RATRAC3_FORMAT_TAG  0x0270

typedef struct ratrac3 ratrac3_t;

typedef struct
{
   unsigned channels;      /* 1 or 2 */
   unsigned sample_rate;   /* 44100 as a rule */
   unsigned block_align;   /* bytes a frame */
   unsigned bitrate;       /* bits a second */
   bool     joint_stereo;
} ratrac3_info_t;

/* Reads a WAVE 'fmt ' chunk's body (format tag 270h and its 14 bytes
 * of extra data). Returns false when it is not ATRAC3 or not a layout
 * this decoder takes. */
bool ratrac3_parse_wave_fmt(const uint8_t *fmt, size_t len,
      ratrac3_info_t *info);

/* channels 1 or 2; block_align as the container gives it; joint stereo
 * only with two channels. NULL on a layout the decoder does not take. */
ratrac3_t *ratrac3_new(unsigned channels, unsigned block_align,
      bool joint_stereo);
void ratrac3_free(ratrac3_t *d);

/* Forgets the signal history, as at a seek. */
void ratrac3_reset(ratrac3_t *d);

/* Decodes one frame of len (at least block_align) bytes into
 * RATRAC3_SAMPLES samples a channel, interleaved. Returns
 * RATRAC3_SAMPLES, or -1 on a frame that does not parse: the output
 * is then silence and the history is kept as it was. */
int ratrac3_decode_s16(ratrac3_t *d, const uint8_t *frame, size_t len,
      int16_t *out);
int ratrac3_decode_float(ratrac3_t *d, const uint8_t *frame, size_t len,
      float *out);

RETRO_END_DECLS

#endif
