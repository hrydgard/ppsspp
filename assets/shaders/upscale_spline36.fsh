// Spline36 upscaling shader.
// See issue #3921
// Modified as per #15566

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_position;

uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;

const vec2 HALF_PIXEL = vec2(0.5, 0.5);

float spline36_0_1(float x) {
	return ((13.0 / 11.0 * x - 453.0 / 209.0) * x - 3.0 / 209.0) * x + 1.0;
}

float spline36_1_2(float x) {
	return ((-6.0 / 11.0 * x + 612.0 / 209.0) * x - 1038.0 / 209.0) * x + 540.0 / 209.0;
}

float spline36_2_3(float x) {
	return ((1.0 / 11.0 * x - 159.0 / 209.0) * x + 434.0 / 209.0) * x - 384.0 / 209.0;
}

vec4 rgb(int inputX, int inputY) {
	return texture2D(sampler0, (vec2(inputX, inputY) + HALF_PIXEL) * u_texelDelta);
}

vec4 interpolateHorizontally(vec2 inputPos, ivec2 inputPosFloor, int dy) {
	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float x;
	float weight;

	x = inputPos.x - float(inputPosFloor.x - 2);
	weight = spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * rgb(inputPosFloor.x - 2, inputPosFloor.y + dy);

	--x;
	weight = spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * rgb(inputPosFloor.x - 1, inputPosFloor.y + dy);

	--x;
	weight = spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * rgb(inputPosFloor.x + 0, inputPosFloor.y + dy);

	x = 1.0 - x;
	weight = spline36_0_1(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * rgb(inputPosFloor.x + 1, inputPosFloor.y + dy);

	++x;
	weight = spline36_1_2(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * rgb(inputPosFloor.x + 2, inputPosFloor.y + dy);

	++x;
	weight = spline36_2_3(x);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * rgb(inputPosFloor.x + 3, inputPosFloor.y + dy);

	return sumOfWeightedPixel / sumOfWeights;
}

vec4 process(vec2 outputPos) {
	vec2 inputPos = outputPos / u_texelDelta - HALF_PIXEL;
	ivec2 inputPosFloor = ivec2(inputPos);

	// Vertical interporation
	float sumOfWeights = 0.0;
	vec4 sumOfWeightedPixel = vec4(0.0);

	float weight;
	float y;

	y = inputPos.y - float(inputPosFloor.y - 2);
	weight = spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * interpolateHorizontally(inputPos, inputPosFloor, -2);

	--y;
	weight = spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * interpolateHorizontally(inputPos, inputPosFloor, -1);

	--y;
	weight = spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * interpolateHorizontally(inputPos, inputPosFloor, +0);

	y = 1.0 - y;
	weight = spline36_0_1(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * interpolateHorizontally(inputPos, inputPosFloor, +1);

	++y;
	weight = spline36_1_2(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * interpolateHorizontally(inputPos, inputPosFloor, +2);

	++y;
	weight = spline36_2_3(y);
	sumOfWeights += weight;
	sumOfWeightedPixel += weight * interpolateHorizontally(inputPos, inputPosFloor, +3);

	return vec4((sumOfWeightedPixel / sumOfWeights).xyz, 1.0);
}

void main()
{
  gl_FragColor.rgba = process(v_position);
}
