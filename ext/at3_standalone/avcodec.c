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
#include "common.h"
#include "util_internal.h"
#include "mem.h"
#include <string.h>

static const AVClass av_codec_context_class = {
    .class_name              = "AVCodecContext",
    .category                = AV_CLASS_CATEGORY_DECODER,
};

AVCodecContext *avcodec_alloc_context3(const AVCodec *codec)
{
	AVCodecContext *avctx = av_mallocz(sizeof(AVCodecContext));
	if (!avctx)
		return NULL;
	int flags = 0;
	memset(avctx, 0, sizeof(AVCodecContext));

	avctx->av_class = &av_codec_context_class;

	if (codec) {
		avctx->codec = codec;
		avctx->codec_id = codec->id;
	}

	if (codec && codec->priv_data_size) {
		if (!avctx->priv_data) {
			avctx->priv_data = av_mallocz(codec->priv_data_size);
			if (!avctx->priv_data) {
				return NULL;
			}
		}
		if (codec->priv_class) {
			*(const AVClass**)avctx->priv_data = codec->priv_class;
		}
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
