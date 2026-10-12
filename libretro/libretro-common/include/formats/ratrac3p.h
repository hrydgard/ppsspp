/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (ratrac3p.h).
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

#ifndef __LIBRETRO_SDK_FORMAT_RATRAC3P_H
#define __LIBRETRO_SDK_FORMAT_RATRAC3P_H

/* ATRAC3plus audio decoder (Sony's codec in PSP .at3 files and
 * OpenMG, WAVE_FORMAT_EXTENSIBLE with the ATRAC3plus sub-format GUID).
 *
 * Written clean-room: the bit-stream syntax from public format notes
 * and from crafted streams fed to an existing decoder, the signal path
 * measured against that decoder's output, never its source. Every
 * frame decodes to 2048 samples a channel.
 *
 *    ratrac3p_info_t info;
 *    ratrac3p_parse_wave_fmt(fmt_chunk, fmt_len, &info);
 *    ratrac3p_t *d = ratrac3p_new(info.channels, info.block_align);
 *    while (have a frame)
 *       ratrac3p_decode_s16(d, frame, info.block_align, pcm);
 *
 * C89. A decoder holds no pointer into the input between calls. */

#include <stddef.h>
#include <stdint.h>

#include <retro_common_api.h>
#include <boolean.h>

RETRO_BEGIN_DECLS

#define RATRAC3P_SAMPLES      2048 /* a channel, every frame */
#define RATRAC3P_MAX_CHANNELS 8

typedef struct ratrac3p ratrac3p_t;

typedef struct
{
   unsigned channels;      /* 1 to 8 */
   unsigned sample_rate;   /* 44100 as a rule */
   unsigned block_align;   /* bytes a frame */
   unsigned bitrate;       /* bits a second */
} ratrac3p_info_t;

/* Reads a WAVE 'fmt ' chunk's body (format tag FFFEh with the
 * ATRAC3plus sub-format). Returns false when it is not ATRAC3plus or
 * not a layout this decoder takes. */
bool ratrac3p_parse_wave_fmt(const uint8_t *fmt, size_t len,
      ratrac3p_info_t *info);

/* channels 1 to 8; block_align as the container gives it. NULL on a
 * layout the decoder does not take. */
ratrac3p_t *ratrac3p_new(unsigned channels, unsigned block_align);
void ratrac3p_free(ratrac3p_t *d);

/* Forgets the signal history, as at a seek. */
void ratrac3p_reset(ratrac3p_t *d);

/* Decodes one frame of len (at least block_align) bytes into
 * RATRAC3P_SAMPLES samples a channel, interleaved. Returns
 * RATRAC3P_SAMPLES, or -1 on a frame that does not parse: the output
 * is then silence and the history is kept as it was. */
int ratrac3p_decode_s16(ratrac3p_t *d, const uint8_t *frame, size_t len,
      int16_t *out);
int ratrac3p_decode_float(ratrac3p_t *d, const uint8_t *frame, size_t len,
      float *out);

RETRO_END_DECLS

#endif
