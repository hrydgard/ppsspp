// Spline36 texture upscaler (2x), adapted from the existing post-processing shader.

const vec2 HALF_PIXEL = vec2(0.5, 0.5);
const int SCALE = 2;

float Spline36_0_1(float x) {
	return ((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0;
}

float Spline36_1_2(float x) {
	return ((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0;
}

float Spline36_2_3(float x) {
	return ((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0;
}

vec4 SampleInput(int x, int y) {
	int clampedX = clamp(x, 0, params.width - 1);
	int clampedY = clamp(y, 0, params.height - 1);
	return readColorf(uvec2(clampedX, clampedY));
}

vec4 InterpolateHorizontally(vec2 inputPos, ivec2 inputPosFloor, int dy) {
	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float x = inputPos.x - float(inputPosFloor.x - 2);
	float weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * SampleInput(inputPosFloor.x - 2, inputPosFloor.y + dy);

	x -= 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * SampleInput(inputPosFloor.x - 1, inputPosFloor.y + dy);

	x -= 1.0;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * SampleInput(inputPosFloor.x + 0, inputPosFloor.y + dy);

	x = 1.0 - x;
	weight = Spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * SampleInput(inputPosFloor.x + 1, inputPosFloor.y + dy);

	x += 1.0;
	weight = Spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * SampleInput(inputPosFloor.x + 2, inputPosFloor.y + dy);

	x += 1.0;
	weight = Spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * SampleInput(inputPosFloor.x + 3, inputPosFloor.y + dy);

	return sumOfWeightedPixel / sumOfWeights;
}

vec4 ProcessSpline36(vec2 inputPos) {
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

	return sumOfWeightedPixel / sumOfWeights;
}

void applyScaling(uvec2 xy) {
	ivec2 destXY = ivec2(xy) * SCALE;
	for (int oy = 0; oy < SCALE; ++oy) {
		for (int ox = 0; ox < SCALE; ++ox) {
			vec2 inputPos = vec2(xy) + (vec2(float(ox), float(oy)) + HALF_PIXEL) / float(SCALE) - HALF_PIXEL;
			writeColorf(destXY + ivec2(ox, oy), ProcessSpline36(inputPos));
		}
	}
}
