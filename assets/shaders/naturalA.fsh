/*
   Natural Colors
   
   Copyright (C) 2006 guest(r) - guest.r@gmail.com

   part of code by ShadX

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

const mat3 RGBtoYIQ = mat3(0.299, 0.596, 0.212, 
                           0.587,-0.275,-0.523, 
                           0.114,-0.321, 0.311);

const mat3 YIQtoRGB = mat3(1.0, 1.0, 1.0,
                           0.95568806036115671171,-0.27158179694405859326,-1.1081773266826619523,
                           0.61985809445637075388,-0.64687381613840131330, 1.7050645599191817149);

const vec3 val00 = vec3( 1.2, 1.2, 1.2);

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
	 float md = d1+d2;   
	 float mc = hl+vl;
	 float ww = d1+d2+hl+vl;

    c00 = RGBtoYIQ*((hl*(c10+c12)+vl*(c01+c21)+d1*(c20+c02)+d2*(c00+c22)+ww*c11)/(3.0*ww));    
    c00 = vec3(pow(c00.x,val00.x),c00.yz*val00.yz);    

    gl_FragColor.xyz= YIQtoRGB*c00;
	gl_FragColor.w = 1.0;
}
