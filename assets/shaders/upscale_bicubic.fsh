// Bicubic upscaling shader.
// Implements Mitchell-Netravali class filters (aka BC-splines), see:
// https://en.wikipedia.org/wiki/Mitchell%E2%80%93Netravali_filters .

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_position;

uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;

uniform vec4 u_setting;
// Shader parameters (somewhat poorly named):
//   u_setting.x - Sharpness
//   u_setting.y - Anisotropy
// from which filter coefficients are computed as
//   B = 1 - Sharpness + 0.5*Anisotropy
//   C = 0.5 * Sharpness + Anisotropy

// Note: some popular filters are
//   B-spline                 - B=1, C=0   <=> Sharpness=0,   Anisotropy=0
//   'The' Mitchell-Netravali - B=C=1/3    <=> Sharpness=2/3, Anisotropy=0
//   Catmull-Rom              - B=0, C=1/2 <=> Sharpness=1,   Anisotropy=0

const vec2 HALF_PIXEL = vec2(0.5, 0.5);

vec3 rgb(int inputX, int inputY) {
	return texture2D(sampler0, (vec2(inputX, inputY) + HALF_PIXEL) * u_texelDelta).xyz;
}

vec4 getWeights(mat4 W, float t) {
	return W * vec4(1.0, t, t*t, t*t*t);
}

vec3 filterX(ivec2 inputPosFloor, int dy, vec4 w) {
	return
		w.x * rgb(inputPosFloor.x - 1, inputPosFloor.y + dy) +
		w.y * rgb(inputPosFloor.x    , inputPosFloor.y + dy) +
		w.z * rgb(inputPosFloor.x + 1, inputPosFloor.y + dy) +
		w.w * rgb(inputPosFloor.x + 2, inputPosFloor.y + dy);
}

vec4 filterBC(vec2 outputPos, float B, float C) {
	vec2 inputPos = outputPos / u_texelDelta - HALF_PIXEL;
	ivec2 inputPosFloor = ivec2(inputPos);
	float x = inputPos.x - float(inputPosFloor.x);
	float y = inputPos.y - float(inputPosFloor.y);
	const float r6 = 0.166666666, r3 = 0.333333333; // Precomputed 1/6 and 1/3.
	// Matrix for computing weights.
	// NOTE: column-major.
	mat4 W = mat4(
		B*r6       , 1.0-B*r3    , B*r6            , 0.0   ,
		-C-0.5*B   , 0.0         , C+0.5*B         , 0.0   ,
		2.0*C+0.5*B, C+2.0*B-3.0 , -2.0*C-2.5*B+3.0, -C    ,
		-C-B*r6    , -C-1.5*B+2.0, C+1.5*B-2.0     , C+B*r6);
	vec4 Wx = getWeights(W, x);
	vec4 Wy = getWeights(W, y);

	vec3 ret =
		Wy.x * filterX(inputPosFloor, -1, Wx) +
		Wy.y * filterX(inputPosFloor,  0, Wx) +
		Wy.z * filterX(inputPosFloor, +1, Wx) +
		Wy.w * filterX(inputPosFloor, +2, Wx);
	return vec4(ret, 1.0);
}

void main() {
	gl_FragColor.rgba = filterBC(
		v_position,
		dot(u_setting.xy, vec2(-1.0, 0.5)) + 1.0,
		dot(u_setting.xy, vec2(0.5, +1.0)));
}
