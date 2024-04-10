#pragma once

#include <cstdint>

struct AVCodecContext;
struct AVFrame;

extern "C" {

#include "avcodec.h"

	int atrac3p_decode_init(AVCodecContext *avctx);
	int atrac3p_decode_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame_ptr, const uint8_t *avpkt_data, int avpkt_size);
	int atrac3p_decode_close(AVCodecContext *avctx);
	extern AVCodec ff_atrac3p_decoder;
}
