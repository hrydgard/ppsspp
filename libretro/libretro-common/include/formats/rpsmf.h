/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (rpsmf.h).
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

#ifndef __LIBRETRO_SDK_FORMAT_RPSMF_H
#define __LIBRETRO_SDK_FORMAT_RPSMF_H

/* PSP movie (PSMF, .pmf) demultiplexer.
 *
 * A PSMF file is a 2048-byte header and an MPEG-2 program stream: H.264
 * video in stream E0h, ATRAC3plus audio in private stream 1, substream
 * by substream. The stream goes through rmpeg1_ps; this layer reads the
 * header, passes video on as it comes, and cuts the audio into whole
 * ATRAC3plus frames.
 *
 * An ATRAC3plus frame in a PSMF is an 8-byte header - 0Fh D0h, two bytes
 * of parameters, four of zero - and (((p0 & 3) << 8) | p1) * 8 + 8 bytes
 * of frame. Each private stream 1 packet starts with the substream byte
 * and a few header bytes of its own before the frame data, which runs on
 * across packets; the number of those bytes is learnt from the first
 * packet of a substream, where a frame starts right after them, and
 * checked against every frame header after. A frame that does not chain
 * resynchronises on the next frame header.
 *
 * Usage is push/pull, as rmpeg1_ps's:
 *
 *    rpsmf_info_t info;
 *    rpsmf_parse_header(file, file_len, &info);
 *    rpsmf_t *d = rpsmf_init();
 *    rpsmf_write(d, file + info.data_offset, n);
 *    while (rpsmf_next(d, &pkt))
 *       ...
 *
 * Packet data points into the demuxer, or into borrowed input, and stays
 * valid until the next rpsmf_next(). C89. */

#include <stddef.h>
#include <stdint.h>

#include <retro_common_api.h>
#include <boolean.h>

RETRO_BEGIN_DECLS

#define RPSMF_HEADER_SIZE 0x64
#define RPSMF_NO_PTS ((uint64_t)UINT64_C(0xFFFFFFFFFFFFFFFF))

typedef struct
{
   uint32_t version;     /* "0012" .. "0015" as 12 .. 15 */
   uint32_t data_offset; /* where the program stream starts, 2048 */
   uint32_t data_size;   /* its length from there */
   uint32_t tick_freq;   /* ticks a second of @duration */
   uint32_t duration;
   uint32_t mux_rate;    /* 50 bytes/s, as the pack headers say */
} rpsmf_info_t;

enum rpsmf_kind
{
   RPSMF_NONE = 0,
   RPSMF_VIDEO,      /* H.264 byte stream, as the PES carried it */
   RPSMF_ATRAC3P,    /* one whole ATRAC3plus frame */
   RPSMF_PRIVATE     /* another private stream 1 substream, raw */
};

typedef struct
{
   const uint8_t *data;
   size_t         size;
   uint64_t       pts;    /* 90 kHz, or RPSMF_NO_PTS */
   uint64_t       dts;    /* video only; 90 kHz, or RPSMF_NO_PTS */
   uint8_t        kind;   /* enum rpsmf_kind */
   uint8_t        stream; /* E0h's low nibble, or the substream */
   /* An ATRAC3plus frame's two parameter bytes, as its header has them:
    * the frame size, and the channel layout and rate of the stream */
   uint8_t        params[2];
} rpsmf_packet_t;

/* The header's fields from the first RPSMF_HEADER_SIZE bytes of a file.
 * False when it is not a PSMF or @len is short. */
bool rpsmf_parse_header(const uint8_t *buf, size_t len, rpsmf_info_t *out);

/* Bytes of an ATRAC3plus frame after its header, from the two parameter
 * bytes */
#define RPSMF_ATRAC3P_FRAME_BYTES(p0, p1) \
   ((((size_t)((p0) & 3) << 8) | (size_t)(p1)) * 8 + 8)

typedef struct rpsmf rpsmf_t;

rpsmf_t *rpsmf_init(void);
void rpsmf_free(rpsmf_t *d);
/* After a seek: everything buffered and every partial frame goes */
void rpsmf_reset(rpsmf_t *d);
/* Program stream bytes, from the header's data_offset on, copied in.
 * Returns what was taken: less than @len only when full, or nothing while
 * the last packet is not yet drained by rpsmf_next(). */
size_t rpsmf_write(rpsmf_t *d, const uint8_t *data, size_t len);
/* The same parsed where it lies (rmpeg1_ps_borrow): video packets and
 * the audio frames whole within one packet point into @data, which stays
 * valid and unchanged until rpsmf_next() returns 0. No copy but the
 * frames that run across packets, and what @data ends part way into. */
size_t rpsmf_borrow(rpsmf_t *d, const uint8_t *data, size_t len);
/* One packet: 1 and *out filled, 0 when more input is needed */
int rpsmf_next(rpsmf_t *d, rpsmf_packet_t *out);
/* Frames dropped because they did not chain */
uint32_t rpsmf_resyncs(const rpsmf_t *d);

RETRO_END_DECLS

#endif
