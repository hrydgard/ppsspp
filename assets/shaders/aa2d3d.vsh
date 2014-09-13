/*
   Copyright (C) 2005 guest(r) - guest.r@gmail.com
*/

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
    vec2 dg1 = vec2( x,y);  vec2 dg2 = vec2(-x,y);
    vec2 sd1 = dg1*0.5/0.75;     vec2 sd2 = dg2*0.5/0.75;
    vec2 ddx = vec2(x,0.0); vec2 ddy = vec2(0.0,y);

	gl_Position = a_position;
	v_texcoord0=a_texcoord0.xyxy;
    v_texcoord1.xy = v_texcoord0.xy - sd1;
    v_texcoord2.xy = v_texcoord0.xy - sd2;
    v_texcoord3.xy = v_texcoord0.xy + sd1;
    v_texcoord4.xy = v_texcoord0.xy + sd2;
    v_texcoord5.xy = v_texcoord0.xy - dg1;
    v_texcoord6.xy = v_texcoord0.xy + dg1;
    v_texcoord5.zw = v_texcoord0.xy - dg2;
    v_texcoord6.zw = v_texcoord0.xy + dg2;
    v_texcoord1.zw = v_texcoord0.xy - ddy;
    v_texcoord2.zw = v_texcoord0.xy + ddx;
    v_texcoord3.zw = v_texcoord0.xy + ddy;
    v_texcoord4.zw = v_texcoord0.xy - ddx;
}