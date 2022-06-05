// Bicubic (Catmull-Rom) upscaling shader.

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

// Catmull-Rom coefficients, multiplied by 2.
float A(float x) {return x*((2.0-x)*x-1.0);}
float B(float x) {return x*x*(3.0*x-5.0)+2.0;}
float C(float x) {return x*((4.0-3.0*x)*x+1.0);}
float D(float x) {return x*x*(x-1.0);}

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
	return vec4(0.25 * ret, 1.0);
}

void main()
{
        gl_FragColor.rgba = process(v_position);
}
