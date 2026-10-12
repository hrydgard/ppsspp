/* Copyright  (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (raac.h).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* raac -- MPEG-4 AAC Low Complexity decoder (ISO/IEC 14496-3).
 *
 * Decodes raw AAC-LC access units as MP4 files carry them ('mp4a'
 * sample entries with object type 0x40, one raw_data_block per
 * sample; no ADTS framing).  The decoder is created from the
 * AudioSpecificConfig found in the esds DecoderSpecificInfo and
 * outputs interleaved signed 16-bit PCM, 1024 samples per channel
 * per access unit.
 *
 * Scope: AAC-LC (audioObjectType 2), the 1024- and 960-sample frame
 * lengths,
 * every standard sampling rate as well as explicitly coded ones
 * (samplingFrequencyIndex 15), channel configurations 1 through 7 and
 * configuration 0 with an embedded PCE, up to eight output channels,
 * M/S and intensity stereo, PNS, pulse data and TNS.  In-stream PCE
 * and DSE elements are parsed and skipped.  Coupling channel
 * elements are decoded and mixed into their targets, dependently or
 * independently switched, with up to four concurrent coupling tags.
 * Channels are emitted in element transmission order, which for the
 * standard configurations is the ISO order (e.g. 5.1: C, L, R, Ls,
 * Rs, LFE); no downmix or reordering is applied.  Not decoded (open
 * or decode fails cleanly): other object types (Main, SSR, LTP,
 * HE-AAC's explicit SBR signalling) and the 960-sample frame length.
 * Implicitly signalled SBR in fill elements is skipped, decoding the
 * LC core band, as plain LC decoders do.
 */
#ifndef __LIBRETRO_SDK_FORMAT_RAAC_H__
#define __LIBRETRO_SDK_FORMAT_RAAC_H__

#include <stdint.h>
#include <stddef.h>

#include <retro_common_api.h>

RETRO_BEGIN_DECLS

typedef struct raac raac_t;

/* Create a decoder from an AudioSpecificConfig (the esds
 * DecoderSpecificInfo payload). Returns NULL when the configuration
 * is malformed or outside the supported scope. */
raac_t *raac_open(const uint8_t *asc, size_t asc_size);

unsigned raac_channels(const raac_t *a);

/* Samples per channel each access unit decodes to, and the return
 * value of the decode calls: 1024, or 960 when the stream signals the
 * short frame length, or 2048 when the doubled output rate is active.
 *
 * The doubled rate turns on at open and never changes mid-stream:
 * either the configuration signals SBR explicitly (hierarchical or
 * syncExtension form) at twice the core rate, or an unsignaled
 * 1024-frame stream has a core rate of 16-24 kHz, the range where
 * HE-AAC streams hide behind plain LC configurations. In that window
 * genuinely plain LC decodes upsampled and reports the doubled rate;
 * raac_sample_rate() always reports the rate the output is actually
 * at. High-frequency reconstruction is not synthesised yet, so
 * SBR-bearing streams currently decode as upsampled core audio.
 * HE-AAC v2 configurations open as v1: the parametric-stereo payload
 * is skipped and the SBR mono signal is decoded. Explicitly signaled
 * downsampled SBR (extension rate equal to the core rate) decodes as
 * plain LC at the core rate. Size output buffers from
 * raac_frame_len() and raac_channels(), never a constant. */
#define RAAC_MAX_FRAME 2048   /* upper bound of raac_frame_len() */
unsigned raac_frame_len(const raac_t *a);
unsigned raac_sample_rate(const raac_t *a);

/* Decode one access unit (one MP4 sample). out must hold at least
 * raac_frame_len(a) * raac_channels(a) samples - the core frame
 * length (1024, or 960) doubles when the SBR path is active, so a
 * fixed buffer must be sized for RAAC_MAX_FRAME per channel.  Returns
 * the number of samples produced per channel (raac_frame_len(a)), or
 * -1 on a bitstream or scope error - the decoder stays usable for
 * following packets.
 *
 * The synthesis pipeline is float throughout; the two entry points
 * differ only in the output conversion.  s16 rounds and saturates
 * once at the edge.  f32 emits the float samples unquantised at unit
 * scale (full scale = +-1.0, ffmpeg/libopus float convention) and is
 * not clamped, so float consumers avoid the 16-bit round trip.  The
 * overlap state is shared, so the entry points may be mixed freely
 * on one instance. */
int raac_decode_s16(raac_t *a, const uint8_t *pkt, size_t size,
      int16_t *out);
int raac_decode_f32(raac_t *a, const uint8_t *pkt, size_t size,
      float *out);

/* Return the decoder to its just-opened state (clears the overlap-add
 * history and reseeds the noise generator) so a stream can be decoded
 * again from its first packet, e.g. for looping. */
void raac_reset(raac_t *a);

/* Bytes the last decoded access unit took, through its END element:
 * less than the size given when more follows it. 0 after a failure. */
size_t raac_last_size(const raac_t *a);

void raac_close(raac_t *a);

RETRO_END_DECLS

#endif
