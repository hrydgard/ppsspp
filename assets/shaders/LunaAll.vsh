uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
attribute vec4 a_position;
attribute vec2 a_texcoord0;
varying vec2 v_texcoord0;

void main() {
	v_texcoord0 = a_texcoord0 + 0.000001; //HLSL precision workaround
	gl_Position = a_position;
}
