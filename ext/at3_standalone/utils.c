/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * utils.
 */

#include "util_internal.h"
#include "config.h"
#include "avcodec.h"
#include "attributes.h"
#include "channel_layout.h"
#include "frame.h"
#include "internal.h"
#include "mathematics.h"
#include "samplefmt.h"
#include "avcodec.h"
#include "internal.h"
#include "bytestream.h"
#include "version.h"
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>

static int (*lockmgr_cb)(void **mutex, enum AVLockOp op) = NULL;


volatile int ff_avcodec_locked;
static int volatile entangled_thread_counter = 0;
static void *codec_mutex;
static void *avformat_mutex;

void av_fast_padded_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    uint8_t **p = ptr;
    if (min_size > SIZE_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        av_freep(p);
        *size = 0;
        return;
    }
    if (!ff_fast_malloc(p, size, min_size + AV_INPUT_BUFFER_PADDING_SIZE, 1))
        memset(*p + min_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
}

static av_cold void avcodec_init(void)
{
    static int initialized = 0;

    if (initialized != 0)
        return;
    initialized = 1;
}

int av_codec_is_decoder(const AVCodec *codec)
{
    return 1;
}

#if FF_API_EMU_EDGE
unsigned avcodec_get_edge_width(void)
{
    return EDGE_WIDTH;
}
#endif

static int update_frame_pool(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int i, ret;

    int ch     = av_frame_get_channels(frame); //av_get_channel_layout_nb_channels(frame->channel_layout);
    int planar = av_sample_fmt_is_planar(frame->format);
    int planes = planar ? ch : 1;

    if (pool->format == frame->format && pool->planes == planes &&
        pool->channels == ch && frame->nb_samples == pool->samples)
        return 0;

    av_buffer_pool_uninit(&pool->pools[0]);
    ret = av_samples_get_buffer_size(&pool->linesize[0], ch,
                                        frame->nb_samples, frame->format, 0);
    if (ret < 0)
        goto fail;

    pool->pools[0] = av_buffer_pool_init(pool->linesize[0], NULL);
    if (!pool->pools[0]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    pool->format     = frame->format;
    pool->planes     = planes;
    pool->channels   = ch;
    pool->samples = frame->nb_samples;
    return 0;
fail:
    for (i = 0; i < 4; i++)
        av_buffer_pool_uninit(&pool->pools[i]);
    pool->format = -1;
    pool->planes = pool->channels = pool->samples = 0;
    return ret;
}

static int audio_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int planes = pool->planes;
    int i;

    frame->linesize[0] = pool->linesize[0];

    if (planes > AV_NUM_DATA_POINTERS) {
        frame->extended_data = av_mallocz_array(planes, sizeof(*frame->extended_data));
        frame->nb_extended_buf = planes - AV_NUM_DATA_POINTERS;
        frame->extended_buf  = av_mallocz_array(frame->nb_extended_buf,
                                          sizeof(*frame->extended_buf));
        if (!frame->extended_data || !frame->extended_buf) {
            av_freep(&frame->extended_data);
            av_freep(&frame->extended_buf);
            return AVERROR(ENOMEM);
        }
    } else {
        frame->extended_data = frame->data;
        av_assert0(frame->nb_extended_buf == 0);
    }

    for (i = 0; i < FFMIN(planes, AV_NUM_DATA_POINTERS); i++) {
        frame->buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->buf[i])
            goto fail;
        frame->extended_data[i] = frame->data[i] = frame->buf[i]->data;
    }
    for (i = 0; i < frame->nb_extended_buf; i++) {
        frame->extended_buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->extended_buf[i])
            goto fail;
        frame->extended_data[i + AV_NUM_DATA_POINTERS] = frame->extended_buf[i]->data;
    }

    return 0;
fail:
    av_frame_unref(frame);
    return AVERROR(ENOMEM);
}

int avcodec_default_get_buffer2(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret;

    if ((ret = update_frame_pool(avctx, frame)) < 0)
        return ret;

    return audio_get_buffer(avctx, frame);
}

int ff_init_buffer_info(AVCodecContext *avctx, AVFrame *frame)
{
    if (!frame->sample_rate)
        frame->sample_rate    = avctx->sample_rate;
    if (frame->format < 0)
        frame->format         = avctx->sample_fmt;
    if (!frame->channel_layout) {
        if (avctx->channel_layout) {
                if (av_get_channel_layout_nb_channels(avctx->channel_layout) !=
                    avctx->channels) {
                    av_log(avctx, AV_LOG_ERROR, "Inconsistent channel "
                        "configuration.\n");
                    return AVERROR(EINVAL);
                }

            frame->channel_layout = avctx->channel_layout;
        } else {
            if (avctx->channels > FF_SANE_NB_CHANNELS) {
                av_log(avctx, AV_LOG_ERROR, "Too many channels: %d.\n",
                        avctx->channels);
                return AVERROR(ENOSYS);
            }
        }
    }
    av_frame_set_channels(frame, avctx->channels);
    return 0;
}

int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
	int override_dimensions = 1;
	int ret;

	ret = ff_init_buffer_info(avctx, frame);
	if (ret < 0)
		return ret;

	ret = avctx->get_buffer2(avctx, frame, flags);
	return ret;
}

int attribute_align_arg ff_codec_open2_recursive(AVCodecContext *avctx, const AVCodec *codec, void **options)
{
    int ret = 0;

    ret = avcodec_open2(avctx, codec, options);

    return ret;
}

int attribute_align_arg avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, void **options)
{
    int ret = 0;

    if (avcodec_is_open(avctx))
        return 0;

    if ((codec && avctx->codec && codec != avctx->codec)) {
        av_log(avctx, AV_LOG_ERROR, "This AVCodecContext was allocated for %s, "
                                    "but %s passed to avcodec_open2()\n", avctx->codec->name, codec->name);
        return AVERROR(EINVAL);
    }
    if (!codec)
        codec = avctx->codec;

    if (avctx->extradata_size < 0 || avctx->extradata_size >= FF_MAX_EXTRADATA_SIZE)
        return AVERROR(EINVAL);

    avctx->internal = av_mallocz(sizeof(AVCodecInternal));
    if (!avctx->internal) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avctx->internal->pool = av_mallocz(sizeof(*avctx->internal->pool));
    if (!avctx->internal->pool) {
        ret = AVERROR(ENOMEM);
        goto free_and_end;
    }

    if (codec->priv_data_size > 0) {
        if (!avctx->priv_data) {
            avctx->priv_data = av_mallocz(codec->priv_data_size);
            if (!avctx->priv_data) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            if (codec->priv_class) {
                *(const AVClass **)avctx->priv_data = codec->priv_class;
            }
        }
    } else {
        avctx->priv_data = NULL;
    }

    if (avctx->channels > FF_SANE_NB_CHANNELS) {
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    avctx->codec = codec;
    avctx->codec_id   = codec->id;
   
    avctx->frame_number = 0;

#if FF_API_VISMV
    if (avctx->debug_mv)
        av_log(avctx, AV_LOG_WARNING, "The 'vismv' option is deprecated, "
               "see the codecview filter instead.\n");
#endif


    if (avctx->codec->init) {
        ret = avctx->codec->init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }

    ret=0;

#if FF_API_AUDIOENC_DELAY
    if (av_codec_is_encoder(avctx->codec))
        avctx->delay = avctx->initial_padding;
#endif

    if (av_codec_is_decoder(avctx->codec)) {
        /* validate channel layout from the decoder */
        if (avctx->channel_layout) {
            int channels = av_get_channel_layout_nb_channels(avctx->channel_layout);
            if (!avctx->channels)
                avctx->channels = channels;
            else if (channels != avctx->channels) {
				char buf[512] = "";
                av_log(avctx, AV_LOG_WARNING,
                       "Channel layout '%s' with %d channels does not match specified number of channels %d: "
                       "ignoring specified channel layout\n",
                       buf, channels, avctx->channels);
                avctx->channel_layout = 0;
            }
        }
        if (avctx->channels && avctx->channels < 0 ||
            avctx->channels > FF_SANE_NB_CHANNELS) {
            ret = AVERROR(EINVAL);
            goto free_and_end;
        }

#if FF_API_AVCTX_TIMEBASE
        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            avctx->time_base = av_inv_q(av_mul_q(avctx->framerate, (AVRational){avctx->ticks_per_frame, 1}));
#endif
    }
    if (codec->priv_data_size > 0 && avctx->priv_data && codec->priv_class) {
        av_assert0(*(const AVClass **)avctx->priv_data == codec->priv_class);
    }

end:

    return ret;
free_and_end:
    if (avctx->codec &&
        (avctx->codec->caps_internal & FF_CODEC_CAP_INIT_CLEANUP))
        avctx->codec->close(avctx);

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    av_frame_free(&avctx->coded_frame);
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    av_freep(&avctx->priv_data);
    if (avctx->internal) {
        av_freep(&avctx->internal->pool);
    }
    av_freep(&avctx->internal);
    avctx->codec = NULL;
    goto end;
}

av_cold int avcodec_close(AVCodecContext *avctx)
{
    int i;

    if (!avctx)
        return 0;

    if (avcodec_is_open(avctx)) {
        FramePool *pool = avctx->internal->pool;
        if (avctx->codec && avctx->codec->close)
            avctx->codec->close(avctx);
        avctx->internal->byte_buffer_size = 0;
        av_freep(&avctx->internal->byte_buffer);
        for (i = 0; i < FF_ARRAY_ELEMS(pool->pools); i++)
            av_buffer_pool_uninit(&pool->pools[i]);
        av_freep(&avctx->internal->pool);
        av_freep(&avctx->internal);
    }
    av_freep(&avctx->priv_data);
    avctx->codec = NULL;
    return 0;
}

void avcodec_flush_buffers(AVCodecContext *avctx)
{
    if (avctx->codec->flush)
        avctx->codec->flush(avctx);
}

int avcodec_is_open(AVCodecContext *s)
{
    return !!s->internal;
}
