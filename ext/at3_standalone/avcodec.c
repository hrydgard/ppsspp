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
#include "compat.h"
#include "channel_layout.h"
#include "compat.h"
#include "avcodec.h"
#include "mem.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <float.h>

AVCodecContext *avcodec_alloc_context3(const AVCodec *codec)
{
	AVCodecContext *avctx = av_mallocz(sizeof(AVCodecContext));
	if (!avctx)
		return NULL;
	int flags = 0;
	memset(avctx, 0, sizeof(AVCodecContext));

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


int attribute_align_arg avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, void **options)
{
	int ret = 0;

	if (codec->priv_data_size > 0) {
		if (!avctx->priv_data) {
			avctx->priv_data = av_mallocz(codec->priv_data_size);
			if (!avctx->priv_data) {
				ret = AVERROR(ENOMEM);
				goto end;
			}
		}
	} else {
		avctx->priv_data = NULL;
	}

	avctx->codec = codec;
	avctx->codec_id = codec->id;

	avctx->frame_number = 0;

	if (avctx->codec->init) {
		ret = avctx->codec->init(avctx);
		if (ret < 0) {
			goto free_and_end;
		}
	}

	ret = 0;

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

end:

	return ret;
free_and_end:
	if (avctx->codec)
		avctx->codec->close(avctx);

	av_freep(&avctx->priv_data);
	avctx->codec = NULL;
	goto end;
}

int avcodec_close(AVCodecContext *avctx)
{
	int i;

	if (!avctx)
		return 0;

	if (avctx->codec && avctx->codec->close)
		avctx->codec->close(avctx);
	av_freep(&avctx->priv_data);
	avctx->codec = NULL;
	return 0;
}

void avcodec_flush_buffers(AVCodecContext *avctx)
{
	if (avctx->codec->flush)
		avctx->codec->flush(avctx);
}
