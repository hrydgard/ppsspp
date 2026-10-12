#ifndef __LIBRETRO_SDK_FORMAT_RMPEG1_PS_H
#define __LIBRETRO_SDK_FORMAT_RMPEG1_PS_H

/* MPEG-1 Systems (Program Stream) demultiplexer.
 *
 * Written from ISO/IEC 11172-1, and takes the MPEG-2 program stream of
 * ISO/IEC 13818-1 as well - its pack and PES headers, packet by packet -
 * which is what a PSP movie (rpsmf.h) and a DVD's VOBs are. Splits an MPEG-1 Program Stream into its
 * constituent elementary streams (MPEG-1 video, MPEG-1 audio layer I/II/III,
 * private and padding streams) and recovers the timestamps attached to them.
 *
 * This is the outermost layer of a Video CD: on a VCD the program stream is
 * the concatenation of the 2324-byte Mode 2 Form 2 payloads of the video
 * tracks. It is equally the outermost layer of an ordinary .mpg file, where
 * the underlying "sector" size is 2048 instead. The demuxer does not care
 * about either: it takes a byte stream.
 *
 * Usage is push/pull. Feed whatever you have with rmpeg1_ps_write(), then
 * drain with rmpeg1_ps_next() until it returns 0, then feed more:
 *
 *    rmpeg1_ps_t *ps = rmpeg1_ps_init(0);
 *    ...
 *    rmpeg1_ps_write(ps, sector_payload, 2324);
 *    while (rmpeg1_ps_next(ps, &pkt))
 *       route(pkt.type, pkt.index, pkt.data, pkt.size, pkt.pts);
 *
 * Packet payloads point into the demuxer's own buffer and stay valid only
 * until the next rmpeg1_ps_write() or rmpeg1_ps_next() call. Copy if you need
 * to keep them.
 *
 * Entering mid-stream is supported: the parser resynchronises on the next
 * start code rather than requiring a pack header first, which is what seeking
 * on a VCD needs.
 */

#include <stddef.h>
#include <stdint.h>

#include <retro_common_api.h>
#include <boolean.h>

RETRO_BEGIN_DECLS

enum rmpeg1_ps_packet_type
{
   RMPEG1_PS_NONE = 0,
   RMPEG1_PS_VIDEO,      /* stream_id E0h..EFh */
   RMPEG1_PS_AUDIO,      /* stream_id C0h..DFh */
   RMPEG1_PS_PRIVATE_1,  /* stream_id BDh      */
   RMPEG1_PS_PRIVATE_2,  /* stream_id BFh      */
   RMPEG1_PS_PADDING     /* stream_id BEh      */
};

/* Timestamps are in the 90 kHz system clock. RMPEG1_PS_NO_PTS marks absent. */
#define RMPEG1_PS_NO_PTS ((uint64_t)UINT64_C(0xFFFFFFFFFFFFFFFF))

typedef struct rmpeg1_ps rmpeg1_ps_t;

typedef struct
{
   const uint8_t *data;       /* into the demuxer's buffer; borrowed        */
   size_t         size;
   uint64_t       pts;        /* 90 kHz, or RMPEG1_PS_NO_PTS                */
   uint64_t       dts;        /* 90 kHz, or RMPEG1_PS_NO_PTS                */
   uint8_t        stream_id;  /* raw stream_id byte                         */
   uint8_t        type;       /* enum rmpeg1_ps_packet_type                 */
   uint8_t        index;      /* substream number within its type           */
} rmpeg1_ps_packet_t;

/* capacity: internal buffer size in bytes. Pass 0 for the default, which is
 * comfortably above the 65541-byte worst-case packet. Must be >= 65541 if
 * given explicitly, or init fails. */
rmpeg1_ps_t *rmpeg1_ps_init(size_t capacity);

void rmpeg1_ps_free(rmpeg1_ps_t *ps);

/* Discard all buffered data and parser state. Use after a seek. */
void rmpeg1_ps_reset(rmpeg1_ps_t *ps);

/* Bytes that can be written without loss. */
size_t rmpeg1_ps_space(const rmpeg1_ps_t *ps);

/* Append input. Returns bytes actually consumed, which is less than len only
 * when the buffer is full; drain with rmpeg1_ps_next() and retry the rest. */
size_t rmpeg1_ps_write(rmpeg1_ps_t *ps, const uint8_t *data, size_t len);

/* Hand over input to be parsed where it lies, rather than copied in: the
 * packets that lie whole within it point into it, and a unit it ends part
 * way into is copied, to be completed by what comes next. @data must stay
 * valid and unchanged until rmpeg1_ps_next() returns 0. Returns @len, or 0
 * while data borrowed before is still being parsed; rmpeg1_ps_write()
 * likewise takes nothing until it has been. */
size_t rmpeg1_ps_borrow(rmpeg1_ps_t *ps, const uint8_t *data, size_t len);

/* Pull one packet. Returns 1 and fills *out on success, 0 when more input is
 * needed. Padding packets are consumed silently and never returned. */
int rmpeg1_ps_next(rmpeg1_ps_t *ps, rmpeg1_ps_packet_t *out);

/* Most recent System Clock Reference from a pack header, 90 kHz, or
 * RMPEG1_PS_NO_PTS if no pack header has been seen yet. */
uint64_t rmpeg1_ps_scr(const rmpeg1_ps_t *ps);

/* Multiplex rate from the most recent pack header, in 50 bytes/s units as
 * stored on disc (multiply by 400 for bits/s). 0 if unknown. */
uint32_t rmpeg1_ps_mux_rate(const rmpeg1_ps_t *ps);

/* True once ISO_11172_end_code (000001B9h) has been parsed. */
bool rmpeg1_ps_ended(const rmpeg1_ps_t *ps);

/* Count of resynchronisation events, i.e. how many times the parser had to
 * hunt for a start code because the stream was damaged or entered mid-way.
 * Useful as a stream-health signal; a clean stream reports 0 or 1. */
uint32_t rmpeg1_ps_resyncs(const rmpeg1_ps_t *ps);

RETRO_END_DECLS

#endif
