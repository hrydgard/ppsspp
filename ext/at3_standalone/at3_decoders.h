#pragma once

#include <cstdint>

struct AVCodecContext;
struct AVFrame;

#include "avcodec.h"

int atrac3_decode_frame(AVCodecContext *avctx, float *out_data[2], int *nb_samples, int *got_frame_ptr, const uint8_t *buf, int buf_size);
int atrac3p_decode_frame(AVCodecContext *avctx, float *out_data[2], int *nb_samples, int *got_frame_ptr, const uint8_t *buf, int buf_size);
extern AVCodec ff_atrac3p_decoder;
extern AVCodec ff_atrac3_decoder;
