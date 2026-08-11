#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba8) uniform writeonly image2D dstImg;
layout(set = 0, binding = 3, rgba16f) uniform readonly image2D srcImg;

layout(push_constant) uniform Params {
	int srcWidth;
	int srcHeight;
	int dstWidth;
	int dstHeight;
} params;

float Spline36_0_1(float x) {
	return ((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0;
}

float Spline36_1_2(float x) {
	return ((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0;
}

float Spline36_2_3(float x) {
	return ((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0;
}

vec4 readSrc(ivec2 pos) {
	pos = clamp(pos, ivec2(0, 0), ivec2(params.srcWidth - 1, params.srcHeight - 1));
	return imageLoad(srcImg, pos);
}

vec4 InterpolateHorizontally(vec2 inputPos, ivec2 inputPosFloor, int dy) {
	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float x = inputPos.x - float(inputPosFloor.x - 2);
	float weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * readSrc(ivec2(inputPosFloor.x - 2, inputPosFloor.y + dy));

	x -= 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * readSrc(ivec2(inputPosFloor.x - 1, inputPosFloor.y + dy));

	x -= 1.0;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * readSrc(ivec2(inputPosFloor.x + 0, inputPosFloor.y + dy));

	x = 1.0 - x;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * readSrc(ivec2(inputPosFloor.x + 1, inputPosFloor.y + dy));

	x += 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * readSrc(ivec2(inputPosFloor.x + 2, inputPosFloor.y + dy));

	x += 1.0;
	weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * readSrc(ivec2(inputPosFloor.x + 3, inputPosFloor.y + dy));

	return sumOfWeightedPixel / sumOfWeights;
}

vec4 Process(ivec2 outputPos) {
	const vec2 shift = vec2(-1.5, -1.5);
	vec2 inputPos = vec2(outputPos) + shift;
	ivec2 inputPosFloor = ivec2(floor(inputPos));

	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float y = inputPos.y - float(inputPosFloor.y - 2);
	float weight = Spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(inputPos, inputPosFloor, -2);

	y -= 1.0;
	weight = Spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(inputPos, inputPosFloor, -1);

	y -= 1.0;
	weight = Spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(inputPos, inputPosFloor, 0);

	y = 1.0 - y;
	weight = Spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(inputPos, inputPosFloor, 1);

	y += 1.0;
	weight = Spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(inputPos, inputPosFloor, 2);

	y += 1.0;
	weight = Spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateHorizontally(inputPos, inputPosFloor, 3);

	return clamp(sumOfWeightedPixel / sumOfWeights, 0.0, 1.0);
}

void main() {
	ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
	if (dst.x >= params.dstWidth || dst.y >= params.dstHeight)
		return;

	imageStore(dstImg, dst, Process(dst));
}

