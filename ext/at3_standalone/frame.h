/*
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * @ingroup lavu_frame
 * reference-counted frame API
 */

#ifndef AVUTIL_FRAME_H
#define AVUTIL_FRAME_H

#include <stdint.h>

#include "avutil.h"
#include "buffer.h"
#include "rational.h"
#include "samplefmt.h"
#include "version.h"

/**
 * This structure describes decoded (raw) audio or video data.
 *
 * AVFrame must be allocated using av_frame_alloc(). Note that this only
 * allocates the AVFrame itself, the buffers for the data must be managed
 * through other means (see below).
 * AVFrame must be freed with av_frame_free().
 *
 * AVFrame is typically allocated once and then reused multiple times to hold
 * different data (e.g. a single AVFrame to hold frames received from a
 * decoder). In such a case, av_frame_unref() will free any references held by
 * the frame and reset it to its original clean state before it
 * is reused again.
 *
 * The data described by an AVFrame is usually reference counted through the
 * AVBuffer API. The underlying buffer references are stored in AVFrame.buf /
 * AVFrame.extended_buf. An AVFrame is considered to be reference counted if at
 * least one reference is set, i.e. if AVFrame.buf[0] != NULL. In such a case,
 * every single data plane must be contained in one of the buffers in
 * AVFrame.buf or AVFrame.extended_buf.
 * There may be a single buffer for all the data, or one separate buffer for
 * each plane, or anything in between.
 *
 * sizeof(AVFrame) is not a part of the public ABI, so new fields may be added
 * to the end with a minor bump.
 * Similarly fields that are marked as to be only accessed by
 * av_opt_ptr() can be reordered. This allows 2 forks to add fields
 * without breaking compatibility with each other.
 */
typedef struct AVFrame {
#define AV_NUM_DATA_POINTERS 8
    /**
     * pointer to the picture/channel planes.
     * This might be different from the first allocated byte
     *
     * Some decoders access areas outside 0,0 - width,height, please
     * see avcodec_align_dimensions2(). Some filters and swscale can read
     * up to 16 bytes beyond the planes, if these filters are to be used,
     * then 16 extra bytes must be allocated.
     */
    uint8_t *data[AV_NUM_DATA_POINTERS];

    /**
     * For video, size in bytes of each picture line.
     * For audio, size in bytes of each plane.
     *
     * For audio, only linesize[0] may be set. For planar audio, each channel
     * plane must be the same size.
     *
     * For video the linesizes should be multiples of the CPUs alignment
     * preference, this is 16 or 32 for modern desktop CPUs.
     * Some code requires such alignment other code can be slower without
     * correct alignment, for yet other it makes no difference.
     *
     * @note The linesize may be larger than the size of usable data -- there
     * may be extra padding present for performance reasons.
     */
    int linesize[AV_NUM_DATA_POINTERS];

    /**
     * pointers to the data planes/channels.
     *
     * For video, this should simply point to data[].
     *
     * For planar audio, each channel has a separate data pointer, and
     * linesize[0] contains the size of each channel buffer.
     * For packed audio, there is just one data pointer, and linesize[0]
     * contains the total size of the buffer for all channels.
     *
     * Note: Both data and extended_data should always be set in a valid frame,
     * but for planar audio with more channels that can fit in data,
     * extended_data must be used in order to access all channels.
     */
    uint8_t **extended_data;

    /**
     * number of audio samples (per channel) described by this frame
     */
    int nb_samples;

    /**
     * format of the frame, -1 if unknown or unset
     * Values correspond to enum AVPixelFormat for video frames,
     * enum AVSampleFormat for audio)
     */
    int format;

    /**
     * quality (between 1 (good) and FF_LAMBDA_MAX (bad))
     */
    int quality;

#if FF_API_ERROR_FRAME
    /**
     * @deprecated unused
     */
    attribute_deprecated
    uint64_t error[AV_NUM_DATA_POINTERS];
#endif

    /**
     * Sample rate of the audio data.
     */
    int sample_rate;

    /**
     * Channel layout of the audio data.
     */
    uint64_t channel_layout;

    /**
     * AVBuffer references backing the data for this frame. If all elements of
     * this array are NULL, then this frame is not reference counted. This array
     * must be filled contiguously -- if buf[i] is non-NULL then buf[j] must
     * also be non-NULL for all j < i.
     *
     * There may be at most one AVBuffer per data plane, so for video this array
     * always contains all the references. For planar audio with more than
     * AV_NUM_DATA_POINTERS channels, there may be more buffers than can fit in
     * this array. Then the extra AVBufferRef pointers are stored in the
     * extended_buf array.
     */
    AVBufferRef *buf[AV_NUM_DATA_POINTERS];

    /**
     * For planar audio which requires more than AV_NUM_DATA_POINTERS
     * AVBufferRef pointers, this array will hold all the references which
     * cannot fit into AVFrame.buf.
     *
     * Note that this is different from AVFrame.extended_data, which always
     * contains all the pointers. This array only contains the extra pointers,
     * which cannot fit into AVFrame.buf.
     *
     * This array is always allocated using av_malloc() by whoever constructs
     * the frame. It is freed in av_frame_unref().
     */
    AVBufferRef **extended_buf;
    /**
     * Number of elements in extended_buf.
     */
    int        nb_extended_buf;

/**
 * @defgroup lavu_frame_flags AV_FRAME_FLAGS
 * Flags describing additional frame properties.
 *
 * @{
 */

/**
 * The frame data may be corrupted, e.g. due to decoding errors.
 */
#define AV_FRAME_FLAG_CORRUPT       (1 << 0)
/**
 * @}
 */

    /**
     * Frame flags, a combination of @ref lavu_frame_flags
     */
    int flags;

    /**
     * frame timestamp estimated using various heuristics, in stream time base
     * Code outside libavutil should access this field using:
     * av_frame_get_best_effort_timestamp(frame)
     * - encoding: unused
     * - decoding: set by libavcodec, read by user.
     */
    int64_t best_effort_timestamp;

    /**
     * reordered pos from the last AVPacket that has been input into the decoder
     * Code outside libavutil should access this field using:
     * av_frame_get_pkt_pos(frame)
     * - encoding: unused
     * - decoding: Read by user.
     */
    int64_t pkt_pos;

    /**
     * duration of the corresponding packet, expressed in
     * AVStream->time_base units, 0 if unknown.
     * Code outside libavutil should access this field using:
     * av_frame_get_pkt_duration(frame)
     * - encoding: unused
     * - decoding: Read by user.
     */
    int64_t pkt_duration;

    /**
     * decode error flags of the frame, set to a combination of
     * FF_DECODE_ERROR_xxx flags if the decoder produced a frame, but there
     * were errors during the decoding.
     * Code outside libavutil should access this field using:
     * av_frame_get_decode_error_flags(frame)
     * - encoding: unused
     * - decoding: set by libavcodec, read by user.
     */
    int decode_error_flags;
#define FF_DECODE_ERROR_INVALID_BITSTREAM   1
#define FF_DECODE_ERROR_MISSING_REFERENCE   2

    /**
     * number of audio channels, only used for audio.
     * Code outside libavutil should access this field using:
     * av_frame_get_channels(frame)
     * - encoding: unused
     * - decoding: Read by user.
     */
    int channels;

    /**
     * size of the corresponding packet containing the compressed
     * frame. It must be accessed using av_frame_get_pkt_size() and
     * av_frame_set_pkt_size().
     * It is set to a negative value if unknown.
     * - encoding: unused
     * - decoding: set by libavcodec, read by user.
     */
    int pkt_size;
} AVFrame;

/**
 * Accessors for some AVFrame fields.
 * The position of these field in the structure is not part of the ABI,
 * they should not be accessed directly outside libavutil.
 */
int64_t av_frame_get_best_effort_timestamp(const AVFrame * frame);
void    av_frame_set_best_effort_timestamp(AVFrame * frame, int64_t val);
int64_t av_frame_get_pkt_duration(const AVFrame * frame);
void    av_frame_set_pkt_duration(AVFrame * frame, int64_t val);
int64_t av_frame_get_pkt_pos(const AVFrame * frame);
void    av_frame_set_pkt_pos(AVFrame * frame, int64_t val);
int64_t av_frame_get_channel_layout(const AVFrame * frame);
void    av_frame_set_channel_layout(AVFrame * frame, int64_t val);
int     av_frame_get_channels(const AVFrame * frame);
void    av_frame_set_channels(AVFrame * frame, int     val);
int     av_frame_get_sample_rate(const AVFrame * frame);
void    av_frame_set_sample_rate(AVFrame * frame, int     val);
int     av_frame_get_decode_error_flags(const AVFrame * frame);
void    av_frame_set_decode_error_flags(AVFrame * frame, int     val);
int     av_frame_get_pkt_size(const AVFrame * frame);
void    av_frame_set_pkt_size(AVFrame * frame, int val);

/**
 * Allocate an AVFrame and set its fields to default values.  The resulting
 * struct must be freed using av_frame_free().
 *
 * @return An AVFrame filled with default values or NULL on failure.
 *
 * @note this only allocates the AVFrame itself, not the data buffers. Those
 * must be allocated through other means, e.g. with av_frame_get_buffer() or
 * manually.
 */
AVFrame *av_frame_alloc(void);

/**
 * Free the frame and any dynamically allocated objects in it,
 * e.g. extended_data. If the frame is reference counted, it will be
 * unreferenced first.
 *
 * @param frame frame to be freed. The pointer will be set to NULL.
 */
void av_frame_free(AVFrame **frame);

/**
 * Unreference all the buffers referenced by frame and reset the frame fields.
 */
void av_frame_unref(AVFrame *frame);

/**
 * Allocate new buffer(s) for audio or video data.
 *
 * The following fields must be set on frame before calling this function:
 * - format (pixel format for video, sample format for audio)
 * - width and height for video
 * - nb_samples and channel_layout for audio
 *
 * This function will fill AVFrame.data and AVFrame.buf arrays and, if
 * necessary, allocate and fill AVFrame.extended_data and AVFrame.extended_buf.
 * For planar formats, one buffer will be allocated for each plane.
 *
 * @param frame frame in which to store the new buffers.
 * @param align required buffer size alignment
 *
 * @return 0 on success, a negative AVERROR on error.
 */
int av_frame_get_buffer(AVFrame *frame, int align);

/**
 * @}
 */

#endif /* AVUTIL_FRAME_H */
