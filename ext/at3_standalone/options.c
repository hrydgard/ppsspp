/*
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
 * Options definition for AVCodecContext.
 */

#include "avcodec.h"
#include "internal.h"
#include "util_internal.h"
#include "mem.h"
#include <float.h>              /* FLT_MIN, FLT_MAX */
#include <string.h>

static const AVClass av_codec_context_class = {
    .class_name              = "AVCodecContext",
    .item_name               = NULL,
    .option                  = NULL,
    .version                 = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset = offsetof(AVCodecContext, log_level_offset),
    .child_next              = NULL,
    .child_class_next        = NULL,
    .category                = AV_CLASS_CATEGORY_DECODER,
};

int avcodec_get_context_defaults3(AVCodecContext *s, const AVCodec *codec)
{
    int flags=0;
    memset(s, 0, sizeof(AVCodecContext));

    s->av_class = &av_codec_context_class;

    if (codec) {
        s->codec = codec;
        s->codec_id = codec->id;
    }

    s->get_buffer2         = avcodec_default_get_buffer2;
    s->sample_fmt          = AV_SAMPLE_FMT_NONE;

    if(codec && codec->priv_data_size){
        if(!s->priv_data){
            s->priv_data= av_mallocz(codec->priv_data_size);
            if (!s->priv_data) {
                return AVERROR(ENOMEM);
            }
        }
        if(codec->priv_class){
            *(const AVClass**)s->priv_data = codec->priv_class;
        }
    }
    return 0;
}

AVCodecContext *avcodec_alloc_context3(const AVCodec *codec)
{
    AVCodecContext *avctx= av_malloc(sizeof(AVCodecContext));

    if (!avctx)
        return NULL;

    if(avcodec_get_context_defaults3(avctx, codec) < 0){
        av_free(avctx);
        return NULL;
    }

    return avctx;
}

void avcodec_free_context(AVCodecContext **pavctx)
{
    AVCodecContext *avctx = *pavctx;

    if (!avctx)
        return;

    avcodec_close(avctx);

    av_freep(&avctx->extradata);

    av_freep(pavctx);
}

#define FOFFSET(x) offsetof(AVFrame,x)

static const AVClass av_frame_class = {
    .class_name              = "AVFrame",
    .item_name               = NULL,
    .option                  = NULL,
    .version                 = LIBAVUTIL_VERSION_INT,
};

#define SROFFSET(x) offsetof(AVSubtitleRect,x)
