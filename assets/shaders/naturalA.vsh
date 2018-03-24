/*
   Copyright (C) 2005 guest(r) - guest.r@gmail.com

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

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

float scaleoffset = 0.6;

void main()

{
  float x = u_texelDelta.x*scaleoffset;
  float y = u_texelDelta.y*scaleoffset;
  vec2 dg1 = vec2( x,y);
  vec2 dg2 = vec2(-x,y);
  vec2 sd1 = dg1*0.5;
  vec2 sd2 = dg2*0.5;
  gl_Position = a_position;
  v_texcoord0=a_texcoord0.xyxy;
  v_texcoord1.xy = v_texcoord0.xy - sd1;
  v_texcoord2.xy = v_texcoord0.xy - sd2;
  v_texcoord3.xy = v_texcoord0.xy + sd1;
  v_texcoord4.xy = v_texcoord0.xy + sd2;
  v_texcoord5.xy = v_texcoord0.xy - dg1;
  v_texcoord6.xy = v_texcoord0.xy + dg1;
  v_texcoord1.zw = v_texcoord0.xy - dg2;
  v_texcoord2.zw = v_texcoord0.xy + dg2;

}
