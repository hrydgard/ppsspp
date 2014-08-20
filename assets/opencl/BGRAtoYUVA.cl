// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/. 

// This performs a BGRA to YUVA conversion on an image.

constant sampler_t srcSampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void BGRAtoYUVA(__read_only image2d_t srcImg, uint width, uint height, __write_only image2d_t dstImg) {
	int2 outputPos = (int2)(get_global_id(0), get_global_id(1));

	float4 pix = read_imagef(srcImg, srcSampler, outputPos);

	float4 output = 0.0;

	output.s0 = pix.s0 * 0.299 + pix.s1 * 0.587 + pix.s2 * 0.114;
	output.s1 = pix.s0 * -0.169 + pix.s1 * -0.331 + pix.s2 * 0.5 + 0.5;
	output.s2 = pix.s0 * 0.5 + pix.s1 * -0.419 + pix.s2 * -0.081 + 0.5;
	output.s3 = pix.s3;

	uint y = get_group_id(1) * 8 + get_local_id(1);
	if (y < height) {
		uint x = get_group_id(0) * 8 + get_local_id(0);
		if (x < width) {
			write_imagef(dstImg, outputPos, output);
		}
	}
}
