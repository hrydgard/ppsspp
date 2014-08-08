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

// This performs an YUVA to BGRA conversion using four images.
// Each image contains one channel of a YUVA image.

constant sampler_t srcSampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void YUVAtoBGRAwithMerge(__read_only image2d_t srcYImg, __read_only image2d_t srcUImg, __read_only image2d_t srcVImg, __read_only image2d_t srcAImg, uint width, uint height, __write_only image2d_t dstImg) {
	int2 outputPos = (int2)(get_global_id(0), get_global_id(1));

	float4 pixY = read_imagef(srcYImg, srcSampler, outputPos);
	float4 pixU = read_imagef(srcUImg, srcSampler, outputPos);
	float4 pixV = read_imagef(srcVImg, srcSampler, outputPos);
	float4 pixA = read_imagef(srcAImg, srcSampler, outputPos);

	float4 yuva = (float4)(pixY.s0, (pixU.s0 - 0.5), (pixV.s0 - 0.5), pixA.s0);

	float4 output = 0.0;

	output.s2 = yuva.s0 * 1.0 + yuva.s1 * 0.0 + yuva.s2 * 1.4;
	output.s1 = yuva.s0 * 1.0 + yuva.s1 * -0.343 + yuva.s2 * -0.711;
	output.s0 = yuva.s0 * 1.0 + yuva.s1 * 1.765 + yuva.s2 * 0.0;
	output.s3 = yuva.s3;

	uint y = get_group_id(1) * 8 + get_local_id(1);
	if (y < height) {
		uint x = get_group_id(0) * 8 + get_local_id(0);
		if (x < width) {
			write_imagef(dstImg, outputPos, output);
		}
	}
}
