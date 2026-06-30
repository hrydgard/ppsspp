// Derived from NNEDI3 prescaler shaders in bjin/mpv-prescalers.
// Upstream repository is distributed under the LGPL-3.0 license.
// Original NNEDI3 algorithm by tritical.
// Adapted for PPSSPP to have single-pass texture scaling with shared memory.
// Copies cbuffer weights into shared memory once per workgroup.
// Uses flat shared memory with aliasing and half-float packing (uvec2) for ~23.3KB peak.

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
const int SCALE = 4;
const int WG_W = 8;
const int WG_H = 8;
const int NNEDI3_WEIGHT_COUNT = 272;

// Stage 1 source tile (tightened bounds)
const int S1_SRC_MIN_X = -7;
const int S1_SRC_MAX_X = 16;
const int S1_SRC_MIN_Y = -5;
const int S1_SRC_MAX_Y = 13;
const int S1_SRC_TILE_W = S1_SRC_MAX_X - S1_SRC_MIN_X + 1; // 24
const int S1_SRC_TILE_H = S1_SRC_MAX_Y - S1_SRC_MIN_Y + 1; // 19
const int S1_SRC_SIZE = S1_SRC_TILE_W * S1_SRC_TILE_H;      // 456

// Stage 1 vertical tile
const int S1_V_MIN_X = -4;
const int S1_V_MAX_X = 12;
const int S1_V_MIN_Y = -8;
const int S1_V_MAX_Y = 24;
const int S1_V_TILE_W = S1_V_MAX_X - S1_V_MIN_X + 1; // 17
const int S1_V_TILE_H = S1_V_MAX_Y - S1_V_MIN_Y + 1; // 33
const int S1_V_SIZE = S1_V_TILE_W * S1_V_TILE_H;      // 561

// Stage 1 horizontal tile
const int S1_H_MIN_X = -6;
const int S1_H_MAX_X = 22;
const int S1_H_MIN_Y = -5;
const int S1_H_MAX_Y = 20;
const int S1_H_TILE_W = S1_H_MAX_X - S1_H_MIN_X + 1; // 29
const int S1_H_TILE_H = S1_H_MAX_Y - S1_H_MIN_Y + 1; // 26
const int S1_H_SIZE = S1_H_TILE_W * S1_H_TILE_H;      // 754

// Stage 2 vertical tile
const int S2_V_MIN_X = -3;
const int S2_V_MAX_X = 18;
const int S2_V_MIN_Y = -7;
const int S2_V_MAX_Y = 37;
const int S2_V_TILE_W = S2_V_MAX_X - S2_V_MIN_X + 1; // 22
const int S2_V_TILE_H = S2_V_MAX_Y - S2_V_MIN_Y + 1; // 45
const int S2_V_SIZE = S2_V_TILE_W * S2_V_TILE_H;      // 990

// Stage 2 horizontal tile
const int S2_H_MIN_X = -4;
const int S2_H_MAX_X = 33;
const int S2_H_MIN_Y = -4;
const int S2_H_MAX_Y = 33;
const int S2_H_TILE_W = S2_H_MAX_X - S2_H_MIN_X + 1; // 38
const int S2_H_TILE_H = S2_H_MAX_Y - S2_H_MIN_Y + 1; // 38
const int S2_H_SIZE = S2_H_TILE_W * S2_H_TILE_H;      // 1444

// Flat shared memory with aliasing:
// Phase A: s1Src=[0..455], s1V=[456..1016]
// Phase B: s1V=[456..1016], s1H=[1017..1770]
// Phase C: s1H=[1017..1770], s2V=[0..989]   (reuses s1Src+s1V space)
// Phase D: s2V=[0..989], s2H=[990..2433]
const int OFF_S1_SRC = 0;
const int OFF_S1_V   = S1_SRC_SIZE;                  // 456
const int OFF_S1_H   = S1_SRC_SIZE + S1_V_SIZE;      // 1017
const int OFF_S2_V   = 0;                            // reuses s1Src+s1V
const int OFF_S2_H   = S2_V_SIZE;                    // 990
const int SHARED_SIZE = S2_V_SIZE + S2_H_SIZE;        // 2434

shared uvec2 tile[SHARED_SIZE];
shared vec4 sharedWeights[NNEDI3_WEIGHT_COUNT];

uvec2 packColor(vec4 c) {
	return uvec2(packHalf2x16(c.xy), packHalf2x16(c.zw));
}

vec4 unpackColor(uvec2 p) {
	return vec4(unpackHalf2x16(p.x), unpackHalf2x16(p.y));
}

ivec2 GroupSrcBase() {
	return ivec2(gl_WorkGroupID.xy) * ivec2(WG_W, WG_H);
}

ivec2 GroupStage1RawBase() {
	return GroupSrcBase() * 2;
}

ivec2 GroupFinalBase() {
	return GroupSrcBase() * 4;
}

ivec2 ClampSourceCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width - 1, params.height - 1));
}

ivec2 ClampStage1VerticalCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width - 1, params.height * 2 - 1));
}

ivec2 ClampStage1RawCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width * 2 - 1, params.height * 2 - 1));
}

ivec2 ClampStage2VerticalCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width * 2 - 1, params.height * 4 - 1));
}

ivec2 ClampFinalCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width * 4 - 1, params.height * 4 - 1));
}

vec4 ReadBufferColor(ivec2 p) {
	p = ClampSourceCoord(p);
	return readColorf(uvec2(p));
}

// Tile accessors using flat array + offsets
vec4 ReadS1Src(ivec2 p) {
	ivec2 c = ClampSourceCoord(p);
	ivec2 local = c - GroupSrcBase() - ivec2(S1_SRC_MIN_X, S1_SRC_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(S1_SRC_TILE_W - 1, S1_SRC_TILE_H - 1));
	return unpackColor(tile[OFF_S1_SRC + local.y * S1_SRC_TILE_W + local.x]);
}

vec4 ReadS1V(ivec2 p) {
	ivec2 c = ClampStage1VerticalCoord(p);
	ivec2 local = c - ivec2(GroupSrcBase().x, GroupSrcBase().y * 2) - ivec2(S1_V_MIN_X, S1_V_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(S1_V_TILE_W - 1, S1_V_TILE_H - 1));
	return unpackColor(tile[OFF_S1_V + local.y * S1_V_TILE_W + local.x]);
}

vec4 ReadS1H(ivec2 p) {
	ivec2 c = ClampStage1RawCoord(p);
	ivec2 local = c - GroupStage1RawBase() - ivec2(S1_H_MIN_X, S1_H_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(S1_H_TILE_W - 1, S1_H_TILE_H - 1));
	return unpackColor(tile[OFF_S1_H + local.y * S1_H_TILE_W + local.x]);
}

vec4 ReadS2V(ivec2 p) {
	ivec2 c = ClampStage2VerticalCoord(p);
	ivec2 local = c - ivec2(GroupStage1RawBase().x, GroupStage1RawBase().y * 2) - ivec2(S2_V_MIN_X, S2_V_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(S2_V_TILE_W - 1, S2_V_TILE_H - 1));
	return unpackColor(tile[OFF_S2_V + local.y * S2_V_TILE_W + local.x]);
}

vec4 ReadS2H(ivec2 p) {
	ivec2 c = ClampFinalCoord(p);
	ivec2 local = c - GroupFinalBase() - ivec2(S2_H_MIN_X, S2_H_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(S2_H_TILE_W - 1, S2_H_TILE_H - 1));
	return unpackColor(tile[OFF_S2_H + local.y * S2_H_TILE_W + local.x]);
}

float ReadS1SrcComponent(ivec2 p, int channel) { return ReadS1Src(p)[channel]; }
float ReadS1VComponent(ivec2 p, int channel) { return ReadS1V(p)[channel]; }
float ReadS1HComponent(ivec2 p, int channel) { return ReadS1H(p)[channel]; }
float ReadS2VComponent(ivec2 p, int channel) { return ReadS2V(p)[channel]; }

float nnedi3Kernel(vec4 samples[8]);

float PredictStage1VerticalComponent(ivec2 base, int channel) {
	vec4 samples[8];
#define R(dx, dy) ReadS1SrcComponent(base + ivec2(dx, dy), channel)
	samples[0] = vec4(R(-3, -1), R(-2, -1), R(-1, -1), R(0, -1));
	samples[1] = vec4(R(1, -1), R(2, -1), R(3, -1), R(4, -1));
	samples[2] = vec4(R(-3, 0), R(-2, 0), R(-1, 0), R(0, 0));
	samples[3] = vec4(R(1, 0), R(2, 0), R(3, 0), R(4, 0));
	samples[4] = vec4(R(-3, 1), R(-2, 1), R(-1, 1), R(0, 1));
	samples[5] = vec4(R(1, 1), R(2, 1), R(3, 1), R(4, 1));
	samples[6] = vec4(R(-3, 2), R(-2, 2), R(-1, 2), R(0, 2));
	samples[7] = vec4(R(1, 2), R(2, 2), R(3, 2), R(4, 2));
#undef R
	return nnedi3Kernel(samples);
}

vec4 ComputeStage1VerticalPixel(ivec2 vCoord) {
	ivec2 clamped = ClampStage1VerticalCoord(vCoord);
	if ((clamped.y & 1) == 0) {
		return ReadS1Src(ivec2(clamped.x, clamped.y >> 1));
	}
	ivec2 base = ivec2(clamped.x, clamped.y >> 1);
	vec4 color = vec4(0.0);
	for (int channel = 0; channel < 4; ++channel) {
		color[channel] = PredictStage1VerticalComponent(base, channel);
	}
	return clamp(color, 0.0, 1.0);
}

float nnedi3Kernel(vec4 samples[8]) {
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

float PredictStage1HorizontalComponent(ivec2 base, int channel) {
	vec4 samples[8];
#define R(dx, dy) ReadS1VComponent(base + ivec2(dx, dy), channel)
	samples[0][0] = R(-1, -3);
	samples[0][1] = R(-1, -2);
	samples[0][2] = R(-1, -1);
	samples[0][3] = R(-1, 0);
	samples[1][0] = R(-1, 1);
	samples[1][1] = R(-1, 2);
	samples[1][2] = R(-1, 3);
	samples[1][3] = R(-1, 4);
	samples[2][0] = R(0, -3);
	samples[2][1] = R(0, -2);
	samples[2][2] = R(0, -1);
	samples[2][3] = R(0, 0);
	samples[3][0] = R(0, 1);
	samples[3][1] = R(0, 2);
	samples[3][2] = R(0, 3);
	samples[3][3] = R(0, 4);
	samples[4][0] = R(1, -3);
	samples[4][1] = R(1, -2);
	samples[4][2] = R(1, -1);
	samples[4][3] = R(1, 0);
	samples[5][0] = R(1, 1);
	samples[5][1] = R(1, 2);
	samples[5][2] = R(1, 3);
	samples[5][3] = R(1, 4);
	samples[6][0] = R(2, -3);
	samples[6][1] = R(2, -2);
	samples[6][2] = R(2, -1);
	samples[6][3] = R(2, 0);
	samples[7][0] = R(2, 1);
	samples[7][1] = R(2, 2);
	samples[7][2] = R(2, 3);
	samples[7][3] = R(2, 4);
#undef R
	return nnedi3Kernel(samples);
}

vec4 ComputeStage1HorizontalPixel(ivec2 hCoord) {
	ivec2 clamped = ClampStage1RawCoord(hCoord);
	if ((clamped.x & 1) == 0) {
		return ReadS1V(ivec2(clamped.x >> 1, clamped.y));
	}
	ivec2 base = ivec2(clamped.x >> 1, clamped.y);
	vec4 color = vec4(0.0);
	for (int channel = 0; channel < 4; ++channel) {
		color[channel] = PredictStage1HorizontalComponent(base, channel);
	}
	return clamp(color, 0.0, 1.0);
}

float PredictStage2VerticalComponent(ivec2 base, int channel) {
	vec4 samples[8];
#define R(dx, dy) ReadS1HComponent(base + ivec2(dx, dy), channel)
	samples[0] = vec4(R(-3, -1), R(-2, -1), R(-1, -1), R(0, -1));
	samples[1] = vec4(R(1, -1), R(2, -1), R(3, -1), R(4, -1));
	samples[2] = vec4(R(-3, 0), R(-2, 0), R(-1, 0), R(0, 0));
	samples[3] = vec4(R(1, 0), R(2, 0), R(3, 0), R(4, 0));
	samples[4] = vec4(R(-3, 1), R(-2, 1), R(-1, 1), R(0, 1));
	samples[5] = vec4(R(1, 1), R(2, 1), R(3, 1), R(4, 1));
	samples[6] = vec4(R(-3, 2), R(-2, 2), R(-1, 2), R(0, 2));
	samples[7] = vec4(R(1, 2), R(2, 2), R(3, 2), R(4, 2));
#undef R
	return nnedi3Kernel(samples);
}

vec4 ComputeStage2VerticalPixel(ivec2 vCoord) {
	ivec2 clamped = ClampStage2VerticalCoord(vCoord);
	if ((clamped.y & 1) == 0) {
		return ReadS1H(ivec2(clamped.x, clamped.y >> 1));
	}
	ivec2 base = ivec2(clamped.x, clamped.y >> 1);
	vec4 color = vec4(0.0);
	for (int channel = 0; channel < 4; ++channel) {
		color[channel] = PredictStage2VerticalComponent(base, channel);
	}
	return clamp(color, 0.0, 1.0);
}

float PredictStage2HorizontalComponent(ivec2 base, int channel) {
	vec4 samples[8];
#define R(dx, dy) ReadS2VComponent(base + ivec2(dx, dy), channel)
	samples[0][0] = R(-1, -3);
	samples[0][1] = R(-1, -2);
	samples[0][2] = R(-1, -1);
	samples[0][3] = R(-1, 0);
	samples[1][0] = R(-1, 1);
	samples[1][1] = R(-1, 2);
	samples[1][2] = R(-1, 3);
	samples[1][3] = R(-1, 4);
	samples[2][0] = R(0, -3);
	samples[2][1] = R(0, -2);
	samples[2][2] = R(0, -1);
	samples[2][3] = R(0, 0);
	samples[3][0] = R(0, 1);
	samples[3][1] = R(0, 2);
	samples[3][2] = R(0, 3);
	samples[3][3] = R(0, 4);
	samples[4][0] = R(1, -3);  samples[4][1] = R(1, -2);  samples[4][2] = R(1, -1);  samples[4][3] = R(1, 0);
	samples[5][0] = R(1, 1);   samples[5][1] = R(1, 2);   samples[5][2] = R(1, 3);   samples[5][3] = R(1, 4);
	samples[6][0] = R(2, -3);  samples[6][1] = R(2, -2);  samples[6][2] = R(2, -1);  samples[6][3] = R(2, 0);
	samples[7][0] = R(2, 1);   samples[7][1] = R(2, 2);   samples[7][2] = R(2, 3);   samples[7][3] = R(2, 4);
#undef R
	return nnedi3Kernel(samples);
}

vec4 ComputeStage2HorizontalPixel(ivec2 hCoord) {
	ivec2 clamped = ClampFinalCoord(hCoord);
	if ((clamped.x & 1) == 0) {
		return ReadS2V(ivec2(clamped.x >> 1, clamped.y));
	}
	ivec2 base = ivec2(clamped.x >> 1, clamped.y);
	vec4 color = vec4(0.0);
	for (int channel = 0; channel < 4; ++channel) {
		color[channel] = PredictStage2HorizontalComponent(base, channel);
	}
	return clamp(color, 0.0, 1.0);
}


float Spline36_0_1(float x) {
	return ((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0;
}

float Spline36_1_2(float x) {
	return ((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0;
}

float Spline36_2_3(float x) {
	return ((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0;
}

vec4 InterpolateFinalHorizontally(vec2 inputPos, ivec2 inputPosFloor, int dy) {
	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float x = inputPos.x - float(inputPosFloor.x - 2);
	float weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadS2H(ivec2(inputPosFloor.x - 2, inputPosFloor.y + dy));

	x -= 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadS2H(ivec2(inputPosFloor.x - 1, inputPosFloor.y + dy));

	x -= 1.0;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadS2H(ivec2(inputPosFloor.x + 0, inputPosFloor.y + dy));

	x = 1.0 - x;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadS2H(ivec2(inputPosFloor.x + 1, inputPosFloor.y + dy));

	x += 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadS2H(ivec2(inputPosFloor.x + 2, inputPosFloor.y + dy));

	x += 1.0;
	weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadS2H(ivec2(inputPosFloor.x + 3, inputPosFloor.y + dy));

	return sumOfWeightedPixel / sumOfWeights;
}

vec4 ProcessFinal(ivec2 outputPos) {
	const vec2 shift = vec2(-1.5, -1.5);
	vec2 inputPos = vec2(outputPos) + shift;
	ivec2 inputPosFloor = ivec2(floor(inputPos));

	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float y = inputPos.y - float(inputPosFloor.y - 2);
	float weight = Spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateFinalHorizontally(inputPos, inputPosFloor, -2);

	y -= 1.0;
	weight = Spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateFinalHorizontally(inputPos, inputPosFloor, -1);

	y -= 1.0;
	weight = Spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateFinalHorizontally(inputPos, inputPosFloor, 0);

	y = 1.0 - y;
	weight = Spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateFinalHorizontally(inputPos, inputPosFloor, 1);

	y += 1.0;
	weight = Spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateFinalHorizontally(inputPos, inputPosFloor, 2);

	y += 1.0;
	weight = Spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * InterpolateFinalHorizontally(inputPos, inputPosFloor, 3);

	return clamp(sumOfWeightedPixel / sumOfWeights, 0.0, 1.0);
}

void LoadStage1SourceTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(S1_SRC_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % S1_SRC_TILE_W;
		int y = int(i) / S1_SRC_TILE_W;
		ivec2 globalPos = GroupSrcBase() + ivec2(S1_SRC_MIN_X, S1_SRC_MIN_Y) + ivec2(x, y);
		tile[OFF_S1_SRC + int(i)] = packColor(ReadBufferColor(globalPos));
	}
}

void LoadStage1VerticalTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(S1_V_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % S1_V_TILE_W;
		int y = int(i) / S1_V_TILE_W;
		ivec2 globalPos = ivec2(GroupSrcBase().x, GroupSrcBase().y * 2) + ivec2(S1_V_MIN_X, S1_V_MIN_Y) + ivec2(x, y);
		tile[OFF_S1_V + int(i)] = packColor(ComputeStage1VerticalPixel(globalPos));
	}
}

void LoadStage1HorizontalTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(S1_H_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % S1_H_TILE_W;
		int y = int(i) / S1_H_TILE_W;
		ivec2 globalPos = GroupStage1RawBase() + ivec2(S1_H_MIN_X, S1_H_MIN_Y) + ivec2(x, y);
		tile[OFF_S1_H + int(i)] = packColor(ComputeStage1HorizontalPixel(globalPos));
	}
}

void LoadStage2VerticalTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(S2_V_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % S2_V_TILE_W;
		int y = int(i) / S2_V_TILE_W;
		ivec2 globalPos = ivec2(GroupStage1RawBase().x, GroupStage1RawBase().y * 2) + ivec2(S2_V_MIN_X, S2_V_MIN_Y) + ivec2(x, y);
		tile[OFF_S2_V + int(i)] = packColor(ComputeStage2VerticalPixel(globalPos));
	}
}

void LoadStage2HorizontalTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(S2_H_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % S2_H_TILE_W;
		int y = int(i) / S2_H_TILE_W;
		ivec2 globalPos = GroupFinalBase() + ivec2(S2_H_MIN_X, S2_H_MIN_Y) + ivec2(x, y);
		tile[OFF_S2_H + int(i)] = packColor(ComputeStage2HorizontalPixel(globalPos));
	}
}

void LoadNNEDI3Weights() {
	for (uint i = gl_LocalInvocationIndex; i < uint(NNEDI3_WEIGHT_COUNT); i += uint(WG_W * WG_H)) {
		sharedWeights[int(i)] = nnedi3Weights[int(i)];
	}
}

void WriteFinalPixel(ivec2 dst) {
	if (dst.x >= params.width * SCALE || dst.y >= params.height * SCALE)
		return;
	writeColorf(dst, ProcessFinal(dst));
}

void applyScaling(uvec2 xy) {
	LoadNNEDI3Weights();
	LoadStage1SourceTile();
	barrier();

	LoadStage1VerticalTile();
	barrier();

	LoadStage1HorizontalTile();
	barrier();

	LoadStage2VerticalTile();
	barrier();

	LoadStage2HorizontalTile();
	barrier();

	ivec2 dstBase = GroupFinalBase() + ivec2(gl_LocalInvocationID.xy) * 4;
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			WriteFinalPixel(dstBase + ivec2(x, y));
		}
	}
}
