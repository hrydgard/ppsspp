/*
   AA shader v2.o - Daisy
   
   Copyright (C) 2006 guest(r) - guest.r@gmail.com

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


#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;

varying vec4 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;
varying vec4 v_texcoord5;
varying vec4 v_texcoord6;


float hd(float x)
{ 
  float y = x*1.1547-1.0;
  y = (sin(y*1.5707)+1.1)*0.83;
  return (x+y+y)/3.0;
}

void main()
{
	vec3 c00 = texture2D(sampler0, v_texcoord5.xy).xyz; 
	vec3 c10 = texture2D(sampler0, v_texcoord1.xy).xyz; 
	vec3 c20 = texture2D(sampler0, v_texcoord2.zw).xyz; 
	vec3 c01 = texture2D(sampler0, v_texcoord3.xy).xyz; 
	vec3 c11 = texture2D(sampler0, v_texcoord0.xy).xyz; 
	vec3 c21 = texture2D(sampler0, v_texcoord4.xy).xyz; 
	vec3 c02 = texture2D(sampler0, v_texcoord1.zw).xyz; 
	vec3 c12 = texture2D(sampler0, v_texcoord2.xy).xyz; 
	vec3 c22 = texture2D(sampler0, v_texcoord6.xy).xyz; 
	vec3 dt = vec3(1.0,1.0,1.0); 

	float d1=dot(abs(c00-c22),dt)+0.0001;
	float d2=dot(abs(c20-c02),dt)+0.0001;
	float hl=dot(abs(c01-c21),dt)+0.0001;
	float vl=dot(abs(c10-c12),dt)+0.0001;

	float md = d1+d2;   float mc = hl+vl;
	hl*=  md;vl*= md;   d1*=  mc;d2*= mc;
	
        float ww = d1+d2+hl+vl;
        
	c11 = (hl*(c10+c12)+vl*(c01+c21)+d1*(c20+c02)+d2*(c00+c22)+ww*c11)/(3.0*ww);

	gl_FragColor.xyz=hd(length(c11))*normalize(c11);
}