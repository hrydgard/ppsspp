/*
 * ATRAC3+ compatible decoder
 *
 * Copyright (c) 2010-2013 Maxim Poliakovski
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
 * Sony ATRAC3+ compatible decoder.
 *
 * Container formats used to store its data:
 * RIFF WAV (.at3) and Sony OpenMG (.oma, .aa3).
 *
 * Technical description of this codec can be found here:
 * http://wiki.multimedia.cx/index.php?title=ATRAC3plus
 *
 * Kudos to Benjamin Larsson and Michael Karcher
 * for their precious technical help!
 */

#include <stdint.h>
#include <string.h>

#include "float_dsp.h"
#include "get_bits.h"
#include "compat.h"
#include "atrac.h"
#include "mem.h"
#include "atrac3plus.h"

struct ATRAC3PContext {
    GetBitContext gb;

    DECLARE_ALIGNED(32, float, samples)[2][ATRAC3P_FRAME_SAMPLES];  ///< quantized MDCT spectrum
    DECLARE_ALIGNED(32, float, mdct_buf)[2][ATRAC3P_FRAME_SAMPLES]; ///< output of the IMDCT
    DECLARE_ALIGNED(32, float, time_buf)[2][ATRAC3P_FRAME_SAMPLES]; ///< output of the gain compensation
    DECLARE_ALIGNED(32, float, outp_buf)[2][ATRAC3P_FRAME_SAMPLES];

    AtracGCContext gainc_ctx;   ///< gain compensation context
    FFTContext mdct_ctx;
    FFTContext ipqf_dct_ctx;    ///< IDCT context used by IPQF

    Atrac3pChanUnitCtx *ch_units;   ///< global channel units

    int num_channel_blocks;     ///< number of channel blocks
    uint8_t channel_blocks[5];  ///< channel configuration descriptor

    int block_align;
};

void atrac3p_free(ATRAC3PContext *ctx)
{
    av_freep(&ctx->ch_units);
    ff_mdct_end(&ctx->mdct_ctx);
    ff_mdct_end(&ctx->ipqf_dct_ctx);
    av_freep(&ctx);
}

static int set_channel_params(ATRAC3PContext *ctx, int channels) {
    memset(ctx->channel_blocks, 0, sizeof(ctx->channel_blocks));
    switch (channels) {
    case 1:
        ctx->num_channel_blocks = 1;
        ctx->channel_blocks[0]  = CH_UNIT_MONO;
        break;
    case 2:
        ctx->num_channel_blocks = 1;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        break;
    case 3:
        ctx->num_channel_blocks = 2;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        break;
    case 4:
        ctx->num_channel_blocks = 3;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_MONO;
        break;
    case 6:
        ctx->num_channel_blocks = 4;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_STEREO;
        ctx->channel_blocks[3]  = CH_UNIT_MONO;
        break;
    case 7:
        ctx->num_channel_blocks = 5;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_STEREO;
        ctx->channel_blocks[3]  = CH_UNIT_MONO;
        ctx->channel_blocks[4]  = CH_UNIT_MONO;
        break;
    case 8:
        ctx->num_channel_blocks = 5;
        ctx->channel_blocks[0]  = CH_UNIT_STEREO;
        ctx->channel_blocks[1]  = CH_UNIT_MONO;
        ctx->channel_blocks[2]  = CH_UNIT_STEREO;
        ctx->channel_blocks[3]  = CH_UNIT_STEREO;
        ctx->channel_blocks[4]  = CH_UNIT_MONO;
        break;
    default:
        av_log(AV_LOG_ERROR,
               "Unsupported channel count: %d!\n", channels);
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

ATRAC3PContext *atrac3p_alloc(int channels, int *block_align) {
    int i, ch, ret;

    ATRAC3PContext *ctx = (ATRAC3PContext *)av_mallocz(sizeof(ATRAC3PContext));
	ctx->block_align = *block_align;

	if (!*block_align) {
		// No block align was passed in, using the default.
		*block_align = 0x000002e8;
	}

    ff_atrac3p_init_vlcs();

    /* initialize IPQF */
    ff_mdct_init(&ctx->ipqf_dct_ctx, 5, 1, 32.0 / 32768.0);

    ff_atrac3p_init_imdct(&ctx->mdct_ctx);

    ff_atrac_init_gain_compensation(&ctx->gainc_ctx, 6, 2);

    ff_atrac3p_init_wave_synth();

    if ((ret = set_channel_params(ctx, channels)) < 0) {
        atrac3p_free(ctx);
        return nullptr;
    }

    ctx->ch_units = (Atrac3pChanUnitCtx *)av_mallocz_array(ctx->num_channel_blocks, sizeof(*ctx->ch_units));

    if (!ctx->ch_units) {
        atrac3p_free(ctx);
        return nullptr;
    }

    for (i = 0; i < ctx->num_channel_blocks; i++) {
        for (ch = 0; ch < 2; ch++) {
            ctx->ch_units[i].channels[ch].ch_num          = ch;
            ctx->ch_units[i].channels[ch].wnd_shape       = &ctx->ch_units[i].channels[ch].wnd_shape_hist[0][0];
            ctx->ch_units[i].channels[ch].wnd_shape_prev  = &ctx->ch_units[i].channels[ch].wnd_shape_hist[1][0];
            ctx->ch_units[i].channels[ch].gain_data       = &ctx->ch_units[i].channels[ch].gain_data_hist[0][0];
            ctx->ch_units[i].channels[ch].gain_data_prev  = &ctx->ch_units[i].channels[ch].gain_data_hist[1][0];
            ctx->ch_units[i].channels[ch].tones_info      = &ctx->ch_units[i].channels[ch].tones_info_hist[0][0];
            ctx->ch_units[i].channels[ch].tones_info_prev = &ctx->ch_units[i].channels[ch].tones_info_hist[1][0];
        }

        ctx->ch_units[i].waves_info      = &ctx->ch_units[i].wave_synth_hist[0];
        ctx->ch_units[i].waves_info_prev = &ctx->ch_units[i].wave_synth_hist[1];
    }

    return ctx;
}

static void decode_residual_spectrum(Atrac3pChanUnitCtx *ctx,
                                     float out[2][ATRAC3P_FRAME_SAMPLES],
                                     int num_channels)
{
    int i, sb, ch, qu, nspeclines, RNG_index;
    float *dst, q;
    int16_t *src;
    /* calculate RNG table index for each subband */
    int sb_RNG_index[ATRAC3P_SUBBANDS] = { 0 };

    if (ctx->mute_flag) {
        for (ch = 0; ch < num_channels; ch++)
            memset(out[ch], 0, ATRAC3P_FRAME_SAMPLES * sizeof(*out[ch]));
        return;
    }

    for (qu = 0, RNG_index = 0; qu < ctx->used_quant_units; qu++)
        RNG_index += ctx->channels[0].qu_sf_idx[qu] +
                     ctx->channels[1].qu_sf_idx[qu];

    for (sb = 0; sb < ctx->num_coded_subbands; sb++, RNG_index += 128)
        sb_RNG_index[sb] = RNG_index & 0x3FC;

    /* inverse quant and power compensation */
    for (ch = 0; ch < num_channels; ch++) {
        /* clear channel's residual spectrum */
        memset(out[ch], 0, ATRAC3P_FRAME_SAMPLES * sizeof(*out[ch]));

        for (qu = 0; qu < ctx->used_quant_units; qu++) {
            src        = &ctx->channels[ch].spectrum[av_atrac3p_qu_to_spec_pos[qu]];
            dst        = &out[ch][av_atrac3p_qu_to_spec_pos[qu]];
            nspeclines = av_atrac3p_qu_to_spec_pos[qu + 1] -
                         av_atrac3p_qu_to_spec_pos[qu];

            if (ctx->channels[ch].qu_wordlen[qu] > 0) {
                q = av_atrac3p_sf_tab[ctx->channels[ch].qu_sf_idx[qu]] *
                    av_atrac3p_mant_tab[ctx->channels[ch].qu_wordlen[qu]];
                for (i = 0; i < nspeclines; i++)
                    dst[i] = src[i] * q;
            }
        }

        for (sb = 0; sb < ctx->num_coded_subbands; sb++)
            ff_atrac3p_power_compensation(ctx, ch, &out[ch][0],
                                          sb_RNG_index[sb], sb);
    }

    if (ctx->unit_type == CH_UNIT_STEREO) {
        for (sb = 0; sb < ctx->num_coded_subbands; sb++) {
            if (ctx->swap_channels[sb]) {
                for (i = 0; i < ATRAC3P_SUBBAND_SAMPLES; i++)
                    FFSWAP(float, out[0][sb * ATRAC3P_SUBBAND_SAMPLES + i],
                                  out[1][sb * ATRAC3P_SUBBAND_SAMPLES + i]);
            }

            /* flip coefficients' sign if requested */
            if (ctx->negate_coeffs[sb])
                for (i = 0; i < ATRAC3P_SUBBAND_SAMPLES; i++)
                    out[1][sb * ATRAC3P_SUBBAND_SAMPLES + i] = -(out[1][sb * ATRAC3P_SUBBAND_SAMPLES + i]);
        }
    }
}

static void reconstruct_frame(ATRAC3PContext *ctx, Atrac3pChanUnitCtx *ch_unit,
                              int num_channels)
{
    int ch, sb;

    for (ch = 0; ch < num_channels; ch++) {
        for (sb = 0; sb < ch_unit->num_subbands; sb++) {
            /* inverse transform and windowing */
            ff_atrac3p_imdct(&ctx->mdct_ctx,
                             &ctx->samples[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                             &ctx->mdct_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                             (ch_unit->channels[ch].wnd_shape_prev[sb] << 1) +
                             ch_unit->channels[ch].wnd_shape[sb], sb);

            /* gain compensation and overlapping */
            ff_atrac_gain_compensation(&ctx->gainc_ctx,
                                       &ctx->mdct_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                                       &ch_unit->prev_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES],
                                       &ch_unit->channels[ch].gain_data_prev[sb],
                                       &ch_unit->channels[ch].gain_data[sb],
                                       ATRAC3P_SUBBAND_SAMPLES,
                                       &ctx->time_buf[ch][sb * ATRAC3P_SUBBAND_SAMPLES]);
        }

        /* zero unused subbands in both output and overlapping buffers */
        memset(&ch_unit->prev_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES],
               0,
               (ATRAC3P_SUBBANDS - ch_unit->num_subbands) *
               ATRAC3P_SUBBAND_SAMPLES *
               sizeof(ch_unit->prev_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES]));
        memset(&ctx->time_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES],
               0,
               (ATRAC3P_SUBBANDS - ch_unit->num_subbands) *
               ATRAC3P_SUBBAND_SAMPLES *
               sizeof(ctx->time_buf[ch][ch_unit->num_subbands * ATRAC3P_SUBBAND_SAMPLES]));

        /* resynthesize and add tonal signal */
        if (ch_unit->waves_info->tones_present ||
            ch_unit->waves_info_prev->tones_present) {
            for (sb = 0; sb < ch_unit->num_subbands; sb++)
                if (ch_unit->channels[ch].tones_info[sb].num_wavs ||
                    ch_unit->channels[ch].tones_info_prev[sb].num_wavs) {
                    ff_atrac3p_generate_tones(ch_unit, ch, sb,
                                              &ctx->time_buf[ch][sb * 128]);
                }
        }

        /* subband synthesis and acoustic signal output */
        ff_atrac3p_ipqf(&ctx->ipqf_dct_ctx, &ch_unit->ipqf_ctx[ch],
                        &ctx->time_buf[ch][0], &ctx->outp_buf[ch][0]);
    }

    /* swap window shape and gain control buffers. */
    for (ch = 0; ch < num_channels; ch++) {
        FFSWAP(uint8_t *, ch_unit->channels[ch].wnd_shape,
               ch_unit->channels[ch].wnd_shape_prev);
        FFSWAP(AtracGainInfo *, ch_unit->channels[ch].gain_data,
               ch_unit->channels[ch].gain_data_prev);
        FFSWAP(Atrac3pWavesData *, ch_unit->channels[ch].tones_info,
               ch_unit->channels[ch].tones_info_prev);
    }

    FFSWAP(Atrac3pWaveSynthParams *, ch_unit->waves_info, ch_unit->waves_info_prev);
}

int atrac3p_decode_frame(ATRAC3PContext *ctx, float *out_data[2], int *nb_samples, const uint8_t *indata, int indata_size)
{
    int i, ret, ch_unit_id, ch_block = 0, out_ch_index = 0, channels_to_process;
	float **samples_p = out_data;

    *nb_samples = 0;

    if ((ret = init_get_bits8(&ctx->gb, indata, indata_size)) < 0)
        return ret;

    if (get_bits1(&ctx->gb)) {
        av_log(AV_LOG_ERROR, "Invalid start bit!");
        return AVERROR_INVALIDDATA;
    }

    while (get_bits_left(&ctx->gb) >= 2 &&
           (ch_unit_id = get_bits(&ctx->gb, 2)) != CH_UNIT_TERMINATOR) {
        if (ch_unit_id == CH_UNIT_EXTENSION) {
            avpriv_report_missing_feature("Channel unit extension");
            return AVERROR_PATCHWELCOME;
        }
        if (ch_block >= ctx->num_channel_blocks ||
            ctx->channel_blocks[ch_block] != ch_unit_id) {
            av_log(AV_LOG_ERROR,
                   "Frame data doesn't match channel configuration!");
            return AVERROR_INVALIDDATA;
        }

        ctx->ch_units[ch_block].unit_type = ch_unit_id;
        channels_to_process               = ch_unit_id + 1;

        if ((ret = ff_atrac3p_decode_channel_unit(&ctx->gb,
                                                  &ctx->ch_units[ch_block],
                                                  channels_to_process)) < 0)
            return ret;

        decode_residual_spectrum(&ctx->ch_units[ch_block], ctx->samples,
                                 channels_to_process);
        reconstruct_frame(ctx, &ctx->ch_units[ch_block],
                          channels_to_process);

        for (i = 0; i < channels_to_process; i++)
            memcpy(samples_p[out_ch_index + i], ctx->outp_buf[i],
                   ATRAC3P_FRAME_SAMPLES * sizeof(**samples_p));

        ch_block++;
        out_ch_index += channels_to_process;
    }

    *nb_samples = ATRAC3P_FRAME_SAMPLES;
    return FFMIN(ctx->block_align, indata_size);
}

void atrac3p_flush_buffers(ATRAC3PContext *ctx) {
	// TODO: Not sure what should be zeroed here.
}
