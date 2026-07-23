// Derived from NNEDI3 prescaler shaders in bjin/mpv-prescalers.
// Upstream repository is distributed under the LGPL-3.0 license.
// Original NNEDI3 algorithm by tritical.
// Adapted for PPSSPP single-pass texture scaling with shared memory.
// Copies cbuffer weights into shared memory once per workgroup.
// Uses flat shared memory with aliasing and half-float packing (uvec2) for ~10.8KB peak.

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
const int SCALE = 2;
const int WG_W = 8;
const int WG_H = 8;
const int NNEDI3_WEIGHT_COUNT = 272;

// Source tile bounds
const int SRC_MIN_X = -6;
const int SRC_MAX_X = 14;
const int SRC_MIN_Y = -4;
const int SRC_MAX_Y = 12;
const int SRC_TILE_W = SRC_MAX_X - SRC_MIN_X + 1; // 21
const int SRC_TILE_H = SRC_MAX_Y - SRC_MIN_Y + 1; // 17
const int SRC_SIZE = SRC_TILE_W * SRC_TILE_H;      // 357

// Vertical tile bounds
const int V_MIN_X = -3;
const int V_MAX_X = 10;
const int V_MIN_Y = -6;
const int V_MAX_Y = 21;
const int V_TILE_W = V_MAX_X - V_MIN_X + 1; // 14
const int V_TILE_H = V_MAX_Y - V_MIN_Y + 1; // 28
const int V_SIZE = V_TILE_W * V_TILE_H;      // 392

// Horizontal tile bounds
const int H_MIN_X = -3;
const int H_MAX_X = 17;
const int H_MIN_Y = -3;
const int H_MAX_Y = 17;
const int H_TILE_W = H_MAX_X - H_MIN_X + 1; // 21
const int H_TILE_H = H_MAX_Y - H_MIN_Y + 1; // 21
const int H_SIZE = H_TILE_W * H_TILE_H;      // 441

// Flat shared memory with aliasing:
// Phase A: src=[392..748], V unused
// Phase B: src=[392..748], V=[0..391]  (both alive)
// Phase C: V=[0..391], H=[392..832]    (src dead, H aliases src space)
// Phase D: H=[392..832]                (V dead)
const int OFF_V   = 0;
const int OFF_SRC = V_SIZE;              // 392
const int OFF_H   = V_SIZE;             // 392 (aliases src, never concurrent)
const int SHARED_SIZE = V_SIZE + H_SIZE; // 833

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

ivec2 GroupVerticalBase() {
	return ivec2(GroupSrcBase().x, GroupSrcBase().y * 2);
}

ivec2 GroupHorizontalBase() {
	return GroupSrcBase() * 2;
}

ivec2 ClampSourceCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width - 1, params.height - 1));
}

ivec2 ClampVerticalCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width - 1, params.height * 2 - 1));
}

ivec2 ClampHorizontalCoord(ivec2 p) {
	return clamp(p, ivec2(0, 0), ivec2(params.width * 2 - 1, params.height * 2 - 1));
}

vec4 ReadBufferColor(ivec2 p) {
	p = ClampSourceCoord(p);
	return readColorf(uvec2(p));
}

// Tile accessors using flat array + offsets
vec4 ReadSrc(ivec2 p) {
	ivec2 c = ClampSourceCoord(p);
	ivec2 local = c - GroupSrcBase() - ivec2(SRC_MIN_X, SRC_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(SRC_TILE_W - 1, SRC_TILE_H - 1));
	return unpackColor(tile[OFF_SRC + local.y * SRC_TILE_W + local.x]);
}

vec4 ReadV(ivec2 p) {
	ivec2 c = ClampVerticalCoord(p);
	ivec2 local = c - GroupVerticalBase() - ivec2(V_MIN_X, V_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(V_TILE_W - 1, V_TILE_H - 1));
	return unpackColor(tile[OFF_V + local.y * V_TILE_W + local.x]);
}

vec4 ReadH(ivec2 p) {
	ivec2 c = ClampHorizontalCoord(p);
	ivec2 local = c - GroupHorizontalBase() - ivec2(H_MIN_X, H_MIN_Y);
	local = clamp(local, ivec2(0), ivec2(H_TILE_W - 1, H_TILE_H - 1));
	return unpackColor(tile[OFF_H + local.y * H_TILE_W + local.x]);
}

float ReadSrcComponent(ivec2 p, int channel) { return ReadSrc(p)[channel]; }
float ReadVComponent(ivec2 p, int channel) { return ReadV(p)[channel]; }

float nnedi3Kernel(vec4 samples[8]);

float PredictVerticalComponent(ivec2 base, int channel) {
	vec4 samples[8];
#define R(dx, dy) ReadSrcComponent(base + ivec2(dx, dy), channel)
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

vec4 ComputeVerticalPixel(ivec2 vCoord) {
	ivec2 clamped = ClampVerticalCoord(vCoord);
	if ((clamped.y & 1) == 0) {
		return ReadSrc(ivec2(clamped.x, clamped.y >> 1));
	}
	ivec2 base = ivec2(clamped.x, clamped.y >> 1);
	vec4 color = vec4(0.0);
	for (int channel = 0; channel < 4; ++channel) {
		color[channel] = PredictVerticalComponent(base, channel);
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

float PredictHorizontalComponent(ivec2 base, int channel) {
	vec4 samples[8];
#define R(dx, dy) ReadVComponent(base + ivec2(dx, dy), channel)
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

vec4 ComputeHorizontalPixel(ivec2 hCoord) {
	ivec2 clamped = ClampHorizontalCoord(hCoord);
	if ((clamped.x & 1) == 0) {
		return ReadV(ivec2(clamped.x >> 1, clamped.y));
	}
	ivec2 base = ivec2(clamped.x >> 1, clamped.y);
	vec4 color = vec4(0.0);
	for (int channel = 0; channel < 4; ++channel) {
		color[channel] = PredictHorizontalComponent(base, channel);
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

vec4 InterpolateHorizontally(vec2 inputPos, ivec2 inputPosFloor, int dy) {
	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float x = inputPos.x - float(inputPosFloor.x - 2);
	float weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadH(ivec2(inputPosFloor.x - 2, inputPosFloor.y + dy));

	x -= 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadH(ivec2(inputPosFloor.x - 1, inputPosFloor.y + dy));

	x -= 1.0;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadH(ivec2(inputPosFloor.x + 0, inputPosFloor.y + dy));

	x = 1.0 - x;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadH(ivec2(inputPosFloor.x + 1, inputPosFloor.y + dy));

	x += 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadH(ivec2(inputPosFloor.x + 2, inputPosFloor.y + dy));

	x += 1.0;
	weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * ReadH(ivec2(inputPosFloor.x + 3, inputPosFloor.y + dy));

	return sumOfWeightedPixel / sumOfWeights;
}

vec4 ProcessFinal(ivec2 outputPos) {
	const vec2 shift = vec2(-0.5, -0.5);
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

void LoadSourceTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(SRC_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % SRC_TILE_W;
		int y = int(i) / SRC_TILE_W;
		ivec2 globalPos = GroupSrcBase() + ivec2(SRC_MIN_X, SRC_MIN_Y) + ivec2(x, y);
		tile[OFF_SRC + int(i)] = packColor(ReadBufferColor(globalPos));
	}
}

void LoadVerticalTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(V_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % V_TILE_W;
		int y = int(i) / V_TILE_W;
		ivec2 globalPos = GroupVerticalBase() + ivec2(V_MIN_X, V_MIN_Y) + ivec2(x, y);
		tile[OFF_V + int(i)] = packColor(ComputeVerticalPixel(globalPos));
	}
}

void LoadHorizontalTile() {
	for (uint i = gl_LocalInvocationIndex; i < uint(H_SIZE); i += uint(WG_W * WG_H)) {
		int x = int(i) % H_TILE_W;
		int y = int(i) / H_TILE_W;
		ivec2 globalPos = GroupHorizontalBase() + ivec2(H_MIN_X, H_MIN_Y) + ivec2(x, y);
		tile[OFF_H + int(i)] = packColor(ComputeHorizontalPixel(globalPos));
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
	LoadSourceTile();
	barrier();

	LoadVerticalTile();
	barrier();

	LoadHorizontalTile();
	barrier();

	ivec2 dstBase = GroupHorizontalBase() + ivec2(gl_LocalInvocationID.xy) * 2;
	WriteFinalPixel(dstBase + ivec2(0, 0));
	WriteFinalPixel(dstBase + ivec2(1, 0));
	WriteFinalPixel(dstBase + ivec2(0, 1));
	WriteFinalPixel(dstBase + ivec2(1, 1));
}
