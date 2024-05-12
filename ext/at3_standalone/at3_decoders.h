#pragma once

#include <cstdint>

// Notes
//
// Performance-wise, these are OK.
// For Atrac3+, the bottleneck is two functions: decode_qu_spectra and ff_atrac3p_ipqf. At least the latter is quite SIMD-able.

// The full external API for the standalone Atrac3/3+ decoder.

struct ATRAC3Context;
struct ATRAC3PContext;

// If the block_align passed in is 0, tries to audio detect.
// flush_buffers should be called when seeking before the next decode_frame.

ATRAC3Context *atrac3_alloc(int channels, int *block_align, const uint8_t *extra_data, int extra_data_size);
void atrac3_free(ATRAC3Context *ctx);
void atrac3_flush_buffers(ATRAC3Context *ctx);
int atrac3_decode_frame(ATRAC3Context *ctx, float *out_data[2], int *nb_samples, const uint8_t *buf, int buf_size);

ATRAC3PContext *atrac3p_alloc(int channels, int *block_align);
void atrac3p_free(ATRAC3PContext *ctx);
void atrac3p_flush_buffers(ATRAC3PContext *ctx);
int atrac3p_decode_frame(ATRAC3PContext *ctx, float *out_data[2], int *nb_samples, const uint8_t *buf, int buf_size);
