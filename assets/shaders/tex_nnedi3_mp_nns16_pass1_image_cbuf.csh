// Derived from NNEDI3 prescaler shaders in bjin/mpv-prescalers.
// Upstream repository is distributed under the LGPL-3.0 license.
// Original NNEDI3 algorithm by tritical.
// Adapted for PPSSPP to load NNS16 weights from a constant buffer via shared memory.

#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform writeonly image2D dstImg;
layout(set = 0, binding = 3, rgba16f) uniform readonly image2D srcImg;

layout(push_constant) uniform Params {
	int srcWidth;
	int srcHeight;
	int dstWidth;
	int dstHeight;
} params;

// Fallbacks for standalone glslangValidator checks
#ifndef CBUFFER_SET
#define CBUFFER_SET 0
#endif
#ifndef CBUFFER_BINDING
#define CBUFFER_BINDING 4
#endif

layout(std140, set = CBUFFER_SET, binding = CBUFFER_BINDING) uniform NNEDI3Weights {
	vec4 nnedi3Weights[272];
};

const int NNEDI3_WEIGHT_COUNT = 272;
const uint WORKGROUP_SIZE = 64u;
shared vec4 sharedWeights[NNEDI3_WEIGHT_COUNT];

void LoadNNEDI3Weights() {
	for (uint i = gl_LocalInvocationIndex; i < uint(NNEDI3_WEIGHT_COUNT); i += WORKGROUP_SIZE) {
		sharedWeights[int(i)] = nnedi3Weights[int(i)];
	}
	barrier();
}

vec4 readColor(ivec2 p) {
	p = clamp(p, ivec2(0, 0), ivec2(params.srcWidth - 1, params.srcHeight - 1));
	return imageLoad(srcImg, p);
}

float readComponent(ivec2 p, int channel) {
	return readColor(p)[channel];
}

float nnedi3(vec4 samples[8]) {
	float sum = 0.0, sumsq = 0.0;
	for (int i = 0; i < 8; i++) {
		sum += dot(samples[i], vec4(1.0));
		sumsq += dot(samples[i], samples[i]);
	}
	float mstd0 = sum / 32.0;
	float mstd1 = sumsq / 32.0 - mstd0 * mstd0;
	float mstd2 = mix(0.0, inversesqrt(mstd1), mstd1 >= 1.192092896e-7);
	mstd1 *= mstd2;
	float vsum = 0.0, wsum = 0.0, sum1, sum2;
#define WT(n, i) sharedWeights[(n) * 17 + (i)]
#define W1(n, i) dot(samples[i], WT(n, ((i) / 2) * 4 + ((i) % 2)))
#define W2(n, i) dot(samples[i], WT(n, ((i) / 2) * 4 + 2 + ((i) % 2)))
#define ACCUM(n) { \
	sum1 = W1(n, 0) + W1(n, 1) + W1(n, 2) + W1(n, 3) + W1(n, 4) + W1(n, 5) + W1(n, 6) + W1(n, 7); \
	sum2 = W2(n, 0) + W2(n, 1) + W2(n, 2) + W2(n, 3) + W2(n, 4) + W2(n, 5) + W2(n, 6) + W2(n, 7); \
	vec2 bias = WT(n, 16).xy; \
	sum1 = exp(sum1 * mstd2 + bias.x); \
	sum2 = sum2 * mstd2 + bias.y; \
	wsum += sum1; \
	vsum += sum1 * (sum2 / (1.0 + abs(sum2))); \
}
	ACCUM(0);  ACCUM(1);  ACCUM(2);  ACCUM(3);
	ACCUM(4);  ACCUM(5);  ACCUM(6);  ACCUM(7);
	ACCUM(8);  ACCUM(9);  ACCUM(10); ACCUM(11);
	ACCUM(12); ACCUM(13); ACCUM(14); ACCUM(15);
#undef ACCUM
#undef W2
#undef W1
#undef WT
	return clamp(mstd0 + 5.0 * vsum / wsum * mstd1, 0.0, 1.0);
}

float predictVertical(ivec2 base, int channel) {
	vec4 samples[8];
#define R(dx, dy) readComponent(base + ivec2(dx, dy), channel)
	samples[0] = vec4(R(-3, -1), R(-2, -1), R(-1, -1), R(0, -1));
	samples[1] = vec4(R(1, -1), R(2, -1), R(3, -1), R(4, -1));
	samples[2] = vec4(R(-3, 0), R(-2, 0), R(-1, 0), R(0, 0));
	samples[3] = vec4(R(1, 0), R(2, 0), R(3, 0), R(4, 0));
	samples[4] = vec4(R(-3, 1), R(-2, 1), R(-1, 1), R(0, 1));
	samples[5] = vec4(R(1, 1), R(2, 1), R(3, 1), R(4, 1));
	samples[6] = vec4(R(-3, 2), R(-2, 2), R(-1, 2), R(0, 2));
	samples[7] = vec4(R(1, 2), R(2, 2), R(3, 2), R(4, 2));
#undef R
	return nnedi3(samples);
}

void main() {
	LoadNNEDI3Weights();

	ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
	if (dst.x >= params.dstWidth || dst.y >= params.dstHeight)
		return;

	ivec2 srcPos = ivec2(dst.x, dst.y >> 1);
	if ((dst.y & 1) == 0) {
		imageStore(dstImg, dst, readColor(srcPos));
		return;
	}

	vec4 color = vec4(0.0);
	for (int channel = 0; channel < 4; ++channel) {
		color[channel] = predictVertical(srcPos, channel);
	}
	imageStore(dstImg, dst, clamp(color, 0.0, 1.0));
}
