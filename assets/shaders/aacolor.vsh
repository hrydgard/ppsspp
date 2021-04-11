// by guest(r) - guest.r@gmail.com
// license: GNU-GPL

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

attribute vec4 a_position;
attribute vec2 a_texcoord0;
uniform vec2 u_texelDelta;

varying vec4 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;
varying vec4 v_texcoord5;
varying vec4 v_texcoord6;

float scaleoffset = 0.8;

void main()

{
  float x = u_texelDelta.x*scaleoffset;
  float y = u_texelDelta.y*scaleoffset;
  gl_Position = a_position;
  v_texcoord0 = a_texcoord0.xyxy;
  v_texcoord1 = v_texcoord0;
  v_texcoord2 = v_texcoord0;
  v_texcoord4 = v_texcoord0;
  v_texcoord5 = v_texcoord0;
  v_texcoord1.y-=y; 
  v_texcoord2.y+=y; 
  v_texcoord4.x-=x; 
  v_texcoord5.x+=x; 
}
