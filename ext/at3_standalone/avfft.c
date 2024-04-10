/*
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

#include "attributes.h"
#include "avfft.h"
#include "fft.h"
#include "mem.h"

#if CONFIG_MDCT

FFTContext *av_mdct_init(int nbits, int inverse, double scale)
{
    FFTContext *s = av_malloc(sizeof(*s));

    if (s && ff_mdct_init(s, nbits, inverse, scale))
        av_freep(&s);

    return s;
}

void av_imdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->imdct_calc(s, output, input);
}

void av_imdct_half(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->imdct_half(s, output, input);
}

void av_mdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->mdct_calc(s, output, input);
}

av_cold void av_mdct_end(FFTContext *s)
{
    if (s) {
        ff_mdct_end(s);
        av_free(s);
    }
}

#endif /* CONFIG_MDCT */
