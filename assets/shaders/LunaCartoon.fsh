//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define ACARTOON     0     	//ON:1/OFF:0 Advanced Cartoon from Guest shader pack
//#define th           0.10  	//Default: 0.10 /outlines sensitivity, recommended from 0.00...0.50
//#define bb           0.45  	//Default: 0.45 /outlines strength,    recommended from 0.10...2.00
//#define pp           1.50  	//Default: 1.50 /outlines blackening,  recommended from 0.25...2.00
//#define acpower      1.0   	//Default:1.0 / reduce for more subtle effect
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define ACARTOONv    0     	//ON:1/OFF:0 Advanced Cartoon from Guest shader pack
//#define thv          0.10  	//Default: 0.10 /outlines sensitivity, recommended from 0.00...0.50
//#define bbv          0.45  	//Default: 0.45 /outlines strength,    recommended from 0.10...2.00
//#define ppv          1.50  	//Default: 1.50 /outlines blackening,  recommended from 0.25...2.00
//#define acpowerv     1.0   	//Default: 1.0 / reduce for more subtle effect
//================
//Approximate performance hit
//Free: 
//Light: VIGNETTE, BLOOM(<= 2 samples)
//Medium: ACARTOON, BLOOM(== 3 samples)
//Heavy: BLOOM(>= 4 samples)
//Extremely Heavy: BLOOM(>= 8 samples)
//======================================================================================================================================================================
//~packed together, corrected to fit requirements of popular with PPSSPP AMD legacy drivers v11.11(if it works on that, it will on almost anything lol),
// currently meet requirements of GLSL version 100 meaning it will probably run on anything unless the driver is horribly broken
// partially written, ported to glsl where required and tested by LunaMoo (Moon Cow?;p),
// other credits mentioned earlier, any more info / required licenses can be found below.



/*
   Advanced Cartoon shader
   
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
uniform vec4 u_time;
uniform float u_video;

uniform vec4 u_setting;

//===========
// The inverse of the texture dimensions along X and Y
uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
varying vec2 v_texcoord0;



//===========
//===========

vec3 processACARTOON(vec3 color){
	float x = u_pixelDelta.x;
	float y = u_pixelDelta.y;
	vec2 dg1 = vec2( x,y);
	vec2 dg2 = vec2(-x,y);
	vec2 acdx  = vec2(x,0.0);
	vec2 acdy  = vec2(0.0,y);

	float thACARTOON = u_setting.y;
	float bbACARTOON = u_setting.z;
	float ppACARTOON = u_setting.w;
	float acpowerACARTOON = 1.0;

	vec3 c00 = texture2D(sampler0, v_texcoord0.xy - dg1).xyz; 
	vec3 c10 = texture2D(sampler0, v_texcoord0.xy - acdy).xyz; 
	vec3 c20 = texture2D(sampler0, v_texcoord0.xy + dg2).xyz; 
	vec3 c01 = texture2D(sampler0, v_texcoord0.xy - acdx).xyz; 
	vec3 c11 = texture2D(sampler0, v_texcoord0.xy).xyz; 
	vec3 c21 = texture2D(sampler0, v_texcoord0.xy + acdx).xyz; 
	vec3 c02 = texture2D(sampler0, v_texcoord0.xy - dg2).xyz; 
	vec3 c12 = texture2D(sampler0, v_texcoord0.xy + acdy).xyz; 
	vec3 c22 = texture2D(sampler0, v_texcoord0.xy + dg1).xyz; 
	vec3 dt = vec3(1.0,1.0,1.0); 

	float d1=dot(abs(c00-c22),dt);
	float d2=dot(abs(c20-c02),dt);
	float hl=dot(abs(c01-c21),dt);
	float vl=dot(abs(c10-c12),dt);

	c11 = (c11+ 0.5*(c01+c10+c21+c12)+ 0.25*(c00+c22+c20+c02))/4.0;
	float d =bbACARTOON*pow(max(d1+d2+hl+vl-thACARTOON,0.0),ppACARTOON)/(dot(c11,dt)+0.50);
	
	float lc = 5.0*length(c11); 
	lc = 0.2*(floor(lc) + pow(fract(lc),4.0));
	c11 = 4.0*normalize(c11); 
	vec3 frct = fract(c11); frct*=frct;
	c11 = floor(c11) + frct*frct;
	c11 = 0.25*(c11)*lc; lc*=0.577;
	c11 = mix(c11,lc*dt,lc);
	color.xyz = mix(color,(1.15-d)*c11,acpowerACARTOON);
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processACARTOON(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
