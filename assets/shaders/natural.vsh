#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform vec2 u_texelDelta;

attribute vec4 a_position;
attribute vec2 a_texcoord0;

varying vec4 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;

void main()
{
  gl_Position=a_position;
      
  v_texcoord0=a_texcoord0.xyxy+vec4(-0.5,-0.5,-1.5,-1.5)*u_texelDelta.xyxy;
  v_texcoord1=a_texcoord0.xyxy+vec4( 0.5,-0.5, 1.5,-1.5)*u_texelDelta.xyxy;
  v_texcoord2=a_texcoord0.xyxy+vec4(-0.5, 0.5,-1.5, 1.5)*u_texelDelta.xyxy;
  v_texcoord3=a_texcoord0.xyxy+vec4( 0.5, 0.5, 1.5, 1.5)*u_texelDelta.xyxy;
}
