uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
attribute vec4 a_position;
attribute vec2 a_texcoord0;
varying vec2 v_texcoord0;
varying vec4 v_texcoordNC0;
varying vec4 v_texcoordNC1;
varying vec4 v_texcoordNC2;
varying vec4 v_texcoordNC3;
varying vec2 omega;

void main() {
	v_texcoord0 = a_texcoord0 + 0.000001; //HLSL precision workaround
	gl_Position = a_position;
	v_texcoordNC0=a_texcoord0.xyxy+vec4(-0.5,-0.5,-1.5,-1.5)*u_texelDelta.xyxy;
	v_texcoordNC1=a_texcoord0.xyxy+vec4( 0.5,-0.5, 1.5,-1.5)*u_texelDelta.xyxy;
	v_texcoordNC2=a_texcoord0.xyxy+vec4(-0.5, 0.5,-1.5, 1.5)*u_texelDelta.xyxy;
	v_texcoordNC3=a_texcoord0.xyxy+vec4( 0.5, 0.5, 1.5, 1.5)*u_texelDelta.xyxy;
	omega = 3.141592654 * (1.0/u_pixelDelta);
}
