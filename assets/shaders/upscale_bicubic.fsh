// Bicubic upscaling shader.

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_position;

uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;

uniform vec4 u_setting;
// u_setting.x - B
// u_setting.y - C

const vec2 HALF_PIXEL = vec2(0.5, 0.5);

vec3 rgb(int inputX, int inputY) {
	return texture2D(sampler0, (vec2(inputX, inputY) + HALF_PIXEL) * u_texelDelta).xyz;
}

vec4 getWeights(mat4 W, float t) {
	return W * vec4(1.0, t, t*t, t*t*t);
}

vec3 interpolateHorizontally(ivec2 inputPosFloor, int dy, vec4 w) {
	return
		w.x * rgb(inputPosFloor.x - 1, inputPosFloor.y + dy) +
		w.y * rgb(inputPosFloor.x    , inputPosFloor.y + dy) +
		w.z * rgb(inputPosFloor.x + 1, inputPosFloor.y + dy) +
		w.w * rgb(inputPosFloor.x + 2, inputPosFloor.y + dy);
}

vec4 process(vec2 outputPos, float B, float C) {
	vec2 inputPos = outputPos / u_texelDelta - HALF_PIXEL;
	ivec2 inputPosFloor = ivec2(inputPos);

	float x = inputPos.x - float(inputPosFloor.x);
	float y = inputPos.y - float(inputPosFloor.y);
	mat4 W = mat4(
		B/6.0      , 1.0-B/3.0   , B/6.0           , 0.0    ,
		-C-0.5*B   , 0.0         , C+0.5*B         , 0.0    ,
		2.0*C+0.5*B, C+2.0*B-3.0 , -2.0*C-2.5*B+3.0, -C     ,
		-C-B/6.0   , -C-1.5*B+2.0, C+1.5*B-2.0     , C+B/6.0);
	vec4 Wx = getWeights(W, x);
	vec4 Wy = getWeights(W, y);

	vec3 ret =
		Wy.x * interpolateHorizontally(inputPosFloor, -1, Wx) +
		Wy.y * interpolateHorizontally(inputPosFloor,  0, Wx) +
		Wy.z * interpolateHorizontally(inputPosFloor, +1, Wx) +
		Wy.w * interpolateHorizontally(inputPosFloor, +2, Wx);
	return vec4(ret, 1.0);
}

void main() {
	gl_FragColor.rgba = process(v_position, u_setting.x, u_setting.y);
}
