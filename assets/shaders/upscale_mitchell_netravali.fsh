// Bicubic (Mitchell–Netravali, B=1/3, C=1/3) upscaling shader.

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_position;

uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;

const vec2 HALF_PIXEL = vec2(0.5, 0.5);

vec3 rgb(int inputX, int inputY)
{
	return texture2D(sampler0, (vec2(inputX, inputY) + HALF_PIXEL) * u_texelDelta).xyz;
}

// Mitchell–Netravali coefficients, multiplied by 18.
float A(float x) {return ((-7.0*x+15.0)*x-9.0)*x+1.0;}
float B(float x) {return (21.0*x-36.0)*x*x+16.0;}
float C(float x) {return ((-21.0*x+27.0)*x+9.0)*x+1.0;}
float D(float x) {return (7.0*x-6.0)*x*x;}

vec3 interpolateHorizontally(vec2 inputPos, ivec2 inputPosFloor, int dy) {
	vec3 ret = vec3(0.0);
	float x = inputPos.x - float(inputPosFloor.x);

	ret += A(x) * rgb(inputPosFloor.x - 1, inputPosFloor.y + dy);
        ret += B(x) * rgb(inputPosFloor.x    , inputPosFloor.y + dy);
        ret += C(x) * rgb(inputPosFloor.x + 1, inputPosFloor.y + dy);
        ret += D(x) * rgb(inputPosFloor.x + 2, inputPosFloor.y + dy);
	return ret;
}

vec4 process(vec2 outputPos) {
	vec2 inputPos = outputPos / u_texelDelta - HALF_PIXEL;
	ivec2 inputPosFloor = ivec2(inputPos);

        vec3 ret = vec3(0.0);
	float y = inputPos.y - float(inputPosFloor.y);
	ret += A(y) * interpolateHorizontally(inputPos, inputPosFloor, -1);
	ret += B(y) * interpolateHorizontally(inputPos, inputPosFloor,  0);
	ret += C(y) * interpolateHorizontally(inputPos, inputPosFloor, +1);
	ret += D(y) * interpolateHorizontally(inputPos, inputPosFloor, +2);
	return vec4(ret / 324.0, 1.0);
}

void main()
{
        gl_FragColor.rgba = process(v_position);
}
