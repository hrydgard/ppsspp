uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
attribute vec4 a_position;
attribute vec2 a_texcoord0;
varying vec2 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;

void main() {
	v_texcoord0 = a_texcoord0 + 0.000001; //HLSL precision workaround
	gl_Position = a_position;
	v_texcoord1=a_texcoord0.xyxy+vec4(-0.5,-0.5,-1.5,-1.5)*u_texelDelta.xyxy;
	v_texcoord2=a_texcoord0.xyxy+vec4( 0.5,-0.5, 1.5,-1.5)*u_texelDelta.xyxy;
	v_texcoord3=a_texcoord0.xyxy+vec4(-0.5, 0.5,-1.5, 1.5)*u_texelDelta.xyxy;
	v_texcoord4=a_texcoord0.xyxy+vec4( 0.5, 0.5, 1.5, 1.5)*u_texelDelta.xyxy;
}
