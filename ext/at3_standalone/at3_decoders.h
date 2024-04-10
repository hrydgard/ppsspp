#pragma once

#include <cstdint>

struct AVCodecContext;
struct AVFrame;

extern "C" {

#include "avcodec.h"

	int atrac3_decode_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame_ptr, const uint8_t *buf, int buf_size);
	int atrac3p_decode_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame_ptr, const uint8_t *buf, int buf_size);
	extern AVCodec ff_atrac3p_decoder;
	extern AVCodec ff_atrac3_decoder;
}
