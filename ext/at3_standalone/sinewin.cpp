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

#define _USE_MATH_DEFINES
#include <math.h>

#include <assert.h>
#include "aac_defines.h"
#include "compat.h"

#include "mem.h"
#include "aac_defines.h"

#include "sinewin.h"

SINETABLE(32);
SINETABLE(64);
SINETABLE(128);
SINETABLE(256);
SINETABLE(512);
SINETABLE(1024);
SINETABLE(2048);
SINETABLE(4096);
SINETABLE(8192);

// Thie array is only accessed in init. However, not so for the
// sine tables it points to.
static float *av_sine_windows[] = {
	NULL, NULL, NULL, NULL, NULL, // unused
	av_sine_32, av_sine_64, av_sine_128,
	av_sine_256, av_sine_512, av_sine_1024,
	av_sine_2048, av_sine_4096, av_sine_8192
};

// Generate a sine window.
void ff_sine_window_init(float *window, int n) {
	int i;
	for (i = 0; i < n; i++)
		window[i] = sinf((i + 0.5) * (M_PI / (2.0 * n)));
}

void ff_init_ff_sine_windows(int index) {
	assert(index >= 0 && index < FF_ARRAY_ELEMS(av_sine_windows));
	ff_sine_window_init(av_sine_windows[index], 1 << index);
}
