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

// This is a slight modification of PPSSPP's Spline36 shader into an opencl program.
// The only big addition to the code is sub-pixel shifting.
// This operates on all the channels of an image.

float Spline36_0_1(float x) {
	return ((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0;
}

float Spline36_1_2(float x) {
	return ((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0;
}

float Spline36_2_3(float x) {
	return ((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0;
}

constant sampler_t srcSampler = CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;
constant float2 HALF_PIXEL = (float2)(0.5, 0.5);

float4 GetPixel(__read_only image2d_t srcImg, int const inputX, int const inputY, float2 const inputDelta) {
	float4 pix = read_imagef(srcImg, srcSampler, ((float2)(inputX, inputY) + HALF_PIXEL) * inputDelta);
	return pix;
}

float4 InterpolateHorizontally(__read_only image2d_t srcImg, float2 const inputPos, float2 const inputDelta, int2 const inputPosFloor, int const dy) {
	float sumOfWeights = 0.0;
	float4 sumOfWeightedPixel = 0.0;

	float x;
	float weight;

	x = inputPos.x - (float)(inputPosFloor.x - 2);
	weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * GetPixel(srcImg, inputPosFloor.x - 2, inputPosFloor.y + dy, inputDelta);

	x = x - 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * GetPixel(srcImg, inputPosFloor.x - 1, inputPosFloor.y + dy, inputDelta);

	x = x - 1.0;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * GetPixel(srcImg, inputPosFloor.x + 0, inputPosFloor.y + dy, inputDelta);

	x = 1.0 - x;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * GetPixel(srcImg, inputPosFloor.x + 1, inputPosFloor.y + dy, inputDelta);

	x = x + 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * GetPixel(srcImg, inputPosFloor.x + 2, inputPosFloor.y + dy, inputDelta);

	x = x + 1.0;
	weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * GetPixel(srcImg, inputPosFloor.x + 3, inputPosFloor.y + dy, inputDelta);

	return (sumOfWeightedPixel / sumOfWeights);
}

float4 Process(__read_only image2d_t srcImg, float2 const outputPos, float const srcWidth, float const srcHeight, float const shiftLeft, float const shiftTop) {
	float2 outputPosFlt = (float2)(outputPos.x / get_global_size(0), outputPos.y / get_global_size(1));
	
	float2 inputDelta = (float2)(1.0 / srcWidth, 1.0 / srcHeight);
	float2 inputPos = (float2)(((outputPosFlt.x / inputDelta.x) + shiftLeft), ((outputPosFlt.y / inputDelta.y) + shiftTop));
	int2 inputPosFloor = convert_int2(inputPos);

	//Vertical interpolation
	float sumOfWeights = 0.0;
	float4 sumOfWeightedPixel = 0.0;

	float weight;
	float y;

	y = inputPos.y - (float)(inputPosFloor.y - 2);
	weight = Spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(srcImg, inputPos, inputDelta, inputPosFloor, -2);

	y = y - 1.0;
	weight = Spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(srcImg, inputPos, inputDelta, inputPosFloor, -1);

	y = y - 1.0;
	weight = Spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(srcImg, inputPos, inputDelta, inputPosFloor, +0);

	y = 1.0 - y;
	weight = Spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(srcImg, inputPos, inputDelta, inputPosFloor, +1);

	y = y + 1.0;
	weight = Spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(srcImg, inputPos, inputDelta, inputPosFloor, +2);

	y = y + 1.0;
	weight = Spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(srcImg, inputPos, inputDelta, inputPosFloor, +3);

	return (sumOfWeightedPixel / sumOfWeights);
}

__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void Spline36(__read_only image2d_t srcImg, __write_only image2d_t dstImg, float srcWidth, float srcHeight, uint resizedWidth, uint resizedHeight, float shiftLeft, float shiftTop) {
	int2 outputPos = (int2)(get_global_id(0), get_global_id(1));

	float4 output = Process(srcImg, convert_float2(outputPos), srcWidth, srcHeight, shiftLeft, shiftTop);

	//If we don't have these if statements certain drivers (Nvidia) will crash and lockup on the rare texture that isn't divisible by 8
	uint y = get_group_id(1) * 8 + get_local_id(1);
	if (y < resizedHeight) {
		uint x = get_group_id(0) * 8 + get_local_id(0);
		if (x < resizedWidth) {
			write_imagef(dstImg, outputPos, output);
		}
	}
}
