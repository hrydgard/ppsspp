/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (rpsmf.c).
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

#include <stdlib.h>
#include <string.h>

#include <formats/rpsmf.h>
#include <formats/rmpeg1_ps.h>

#define RPSMF_SUBSTREAMS 16
/* A frame header and the largest frame after it */
#define RPSMF_FRAME_MAX  (8 + RPSMF_ATRAC3P_FRAME_BYTES(3, 255))
/* Where in a substream's first packet the first frame may start */
#define RPSMF_SKIP_MAX   16

typedef struct
{
   uint8_t  frame[RPSMF_FRAME_MAX];
   size_t   have;  /* bytes of the frame held, its header among them */
   size_t   need;  /* the whole frame, once its header is in; else 0 */
   uint64_t pts;
   int      skip;  /* the packets' own header bytes; -1 until learnt */
} rpsmf_sub_t;

struct rpsmf
{
   rmpeg1_ps_t *ps;
   /* The audio packet being cut into frames, where the demuxer has it:
    * nothing is written or borrowed until it is used up */
   const uint8_t *pkt;
   size_t       pkt_len;
   size_t       pkt_pos;
   uint64_t     pkt_pts; /* for the first frame to start in it */
   int          pkt_sub;
   uint32_t     resyncs;
   rpsmf_sub_t  sub[RPSMF_SUBSTREAMS];
};

static uint32_t rpsmf_be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

bool rpsmf_parse_header(const uint8_t *buf, size_t len, rpsmf_info_t *out)
{
   unsigned i;
   if (     !buf || !out || len < RPSMF_HEADER_SIZE
         || memcmp(buf, "PSMF", 4))
      return false;
   out->version = 0;
   for (i = 4; i < 8; i++)
   {
      if (buf[i] < '0' || buf[i] > '9')
         return false;
      out->version = out->version * 10 + (uint32_t)(buf[i] - '0');
   }
   out->data_offset = rpsmf_be32(buf + 0x08);
   out->data_size   = rpsmf_be32(buf + 0x0C);
   out->tick_freq   = rpsmf_be32(buf + 0x56);
   out->duration    = rpsmf_be32(buf + 0x5C);
   out->mux_rate    = rpsmf_be32(buf + 0x60);
   return out->data_offset >= RPSMF_HEADER_SIZE;
}

static bool rpsmf_frame_header(const uint8_t *p)
{
   return p[0] == 0x0F && p[1] == 0xD0
       && !p[4] && !p[5] && !p[6] && !p[7];
}

static void rpsmf_sub_clear(rpsmf_sub_t *s)
{
   s->have = 0;
   s->need = 0;
   s->pts  = RPSMF_NO_PTS;
   s->skip = -1;
}

rpsmf_t *rpsmf_init(void)
{
   rpsmf_t *d = (rpsmf_t*)calloc(1, sizeof(*d));
   if (!d)
      return NULL;
   if (!(d->ps = rmpeg1_ps_init(0)))
   {
      free(d);
      return NULL;
   }
   rpsmf_reset(d);
   return d;
}

void rpsmf_free(rpsmf_t *d)
{
   if (!d)
      return;
   rmpeg1_ps_free(d->ps);
   free(d);
}

void rpsmf_reset(rpsmf_t *d)
{
   unsigned i;
   if (!d)
      return;
   rmpeg1_ps_reset(d->ps);
   d->pkt_len = 0;
   d->pkt_pos = 0;
   for (i = 0; i < RPSMF_SUBSTREAMS; i++)
      rpsmf_sub_clear(&d->sub[i]);
}

size_t rpsmf_write(rpsmf_t *d, const uint8_t *data, size_t len)
{
   return (d && d->pkt_pos >= d->pkt_len)
      ? rmpeg1_ps_write(d->ps, data, len) : 0;
}

size_t rpsmf_borrow(rpsmf_t *d, const uint8_t *data, size_t len)
{
   return (d && d->pkt_pos >= d->pkt_len)
      ? rmpeg1_ps_borrow(d->ps, data, len) : 0;
}

uint32_t rpsmf_resyncs(const rpsmf_t *d)
{
   return d ? d->resyncs : 0;
}

/* The next frame header in the packet from its cursor on: the cursor
 * there and true, or past the end and false */
static bool rpsmf_seek_header(rpsmf_t *d)
{
   while (d->pkt_pos + 8 <= d->pkt_len)
   {
      if (rpsmf_frame_header(d->pkt + d->pkt_pos))
         return true;
      d->pkt_pos++;
   }
   d->pkt_pos = d->pkt_len;
   return false;
}

/* More of the current audio packet into its substream's frame: 1 and
 * *out when a frame is whole, 0 when the packet is used up */
static int rpsmf_cut(rpsmf_t *d, rpsmf_packet_t *out)
{
   rpsmf_sub_t *s = &d->sub[d->pkt_sub];
   while (d->pkt_pos < d->pkt_len)
   {
      size_t take;
      /* A frame whole within the packet goes out from where it lies */
      if (!s->have && d->pkt_len - d->pkt_pos >= 8
            && rpsmf_frame_header(d->pkt + d->pkt_pos))
      {
         const uint8_t *f = d->pkt + d->pkt_pos;
         size_t n         = RPSMF_ATRAC3P_FRAME_BYTES(f[2], f[3]);
         if (8 + n <= d->pkt_len - d->pkt_pos)
         {
            out->data      = f + 8;
            out->size      = n;
            out->pts       = d->pkt_pts;
            out->dts       = RPSMF_NO_PTS;
            out->kind      = RPSMF_ATRAC3P;
            out->stream    = (uint8_t)d->pkt_sub;
            out->params[0] = f[2];
            out->params[1] = f[3];
            d->pkt_pts     = RPSMF_NO_PTS;
            d->pkt_pos    += 8 + n;
            return 1;
         }
      }
      if (!s->need)
      {
         /* A frame starts here: the packet's timestamp is the first
          * such frame's */
         if (!s->have)
         {
            s->pts     = d->pkt_pts;
            d->pkt_pts = RPSMF_NO_PTS;
         }
         take = 8 - s->have;
         if (take > d->pkt_len - d->pkt_pos)
            take = d->pkt_len - d->pkt_pos;
         memcpy(s->frame + s->have, d->pkt + d->pkt_pos, take);
         s->have    += take;
         d->pkt_pos += take;
         if (s->have < 8)
            return 0;
         if (!rpsmf_frame_header(s->frame))
         {
            /* Not where a frame should be: the next header there is */
            d->resyncs++;
            s->have = 0;
            if (!rpsmf_seek_header(d))
               return 0;
            continue;
         }
         s->need = 8 + RPSMF_ATRAC3P_FRAME_BYTES(s->frame[2], s->frame[3]);
      }
      take = s->need - s->have;
      if (take > d->pkt_len - d->pkt_pos)
         take = d->pkt_len - d->pkt_pos;
      memcpy(s->frame + s->have, d->pkt + d->pkt_pos, take);
      s->have    += take;
      d->pkt_pos += take;
      if (s->have == s->need)
      {
         out->data      = s->frame + 8;
         out->size      = s->need - 8;
         out->pts       = s->pts;
         out->dts       = RPSMF_NO_PTS;
         out->kind      = RPSMF_ATRAC3P;
         out->stream    = (uint8_t)d->pkt_sub;
         out->params[0] = s->frame[2];
         out->params[1] = s->frame[3];
         s->have        = 0;
         s->need        = 0;
         return 1;
      }
   }
   return 0;
}

int rpsmf_next(rpsmf_t *d, rpsmf_packet_t *out)
{
   rmpeg1_ps_packet_t pkt;
   if (!d || !out)
      return 0;
   for (;;)
   {
      if (d->pkt_pos < d->pkt_len && rpsmf_cut(d, out))
         return 1;
      if (!rmpeg1_ps_next(d->ps, &pkt))
         return 0;
      if (pkt.type == RMPEG1_PS_VIDEO)
      {
         out->data      = pkt.data;
         out->size      = pkt.size;
         out->pts       = pkt.pts;
         out->dts       = pkt.dts;
         out->kind      = RPSMF_VIDEO;
         out->stream    = pkt.index;
         out->params[0] = out->params[1] = 0;
         return 1;
      }
      if (pkt.type != RMPEG1_PS_PRIVATE_1 || pkt.size < 2)
         continue;
      if (pkt.data[0] >= RPSMF_SUBSTREAMS)
      {
         out->data      = pkt.data + 1;
         out->size      = pkt.size - 1;
         out->pts       = pkt.pts;
         out->dts       = RPSMF_NO_PTS;
         out->kind      = RPSMF_PRIVATE;
         out->stream    = pkt.data[0];
         out->params[0] = out->params[1] = 0;
         return 1;
      }
      {
         rpsmf_sub_t *s = &d->sub[pkt.data[0]];
         d->pkt     = pkt.data + 1;
         d->pkt_len = pkt.size - 1;
         d->pkt_pos = 0;
         d->pkt_pts = pkt.pts;
         d->pkt_sub = pkt.data[0];
         /* The substream's first packet starts a frame right after the
          * packet's own header: how long that is, for every packet */
         if (s->skip < 0)
         {
            int i;
            for (i = 0; i <= RPSMF_SKIP_MAX
                  && (size_t)i + 8 <= d->pkt_len; i++)
               if (rpsmf_frame_header(d->pkt + i))
                  break;
            if (i > RPSMF_SKIP_MAX || (size_t)i + 8 > d->pkt_len)
            {
               d->resyncs++;
               d->pkt_len = 0;
               continue;
            }
            s->skip = i;
         }
         if ((size_t)s->skip >= d->pkt_len)
         {
            d->pkt_len = 0;
            continue;
         }
         d->pkt_pos = (size_t)s->skip;
      }
   }
}
