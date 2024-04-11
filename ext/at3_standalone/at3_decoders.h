#pragma once

#include <cstdint>

struct ATRAC3PContext;

struct AVCodecContext;
struct AVFrame;

#include "avcodec.h"

int atrac3_decode_frame(AVCodecContext *avctx, float *out_data[2], int *nb_samples, int *got_frame_ptr, const uint8_t *buf, int buf_size);

ATRAC3PContext *atrac3p_alloc(int block_align, int channels);
void atrac3p_free(ATRAC3PContext *ctx);

int atrac3p_decode_frame(ATRAC3PContext *ctx, float *out_data[2], int *nb_samples, int *got_frame_ptr, const uint8_t *buf, int buf_size);
extern AVCodec ff_atrac3_decoder;
