//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define Guest_AA4o   0     	//ON:1/OFF:0 /alternative to FXAA /taken from http://forums.ppsspp.org/showthread.php?tid=6594&pid=124441#pid124441
//#define scale        7.0   	//Default: 7.0 / 4.0 -> smooth, 8.0 -> sharp (4.0 + filtro works well also as upscaling filter for 2D games)
//#define filtro       0     	//ON:1/OFF:0 /less blurry with smoother settings, but also like 3 times heavier
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define Guest_AA4ov  1     	//ON:1/OFF:0 /alternative to FXAA /taken from http://forums.ppsspp.org/showthread.php?tid=6594&pid=124441#pid124441
//#define scalev       3.0   	//Default: 7.0 / 4.0 -> smooth, 8.0 -> sharp (4.0 + filtro works well also as upscaling filter for 2D games)
//#define filtrov      1     	//ON:1/OFF:0 /less blurry with smoother settings, but also like 3 times heavier

//================
//Approximate performance hit
//Free: 
//Light: 
//Medium: FXAA, GAUSS_SQ, GAUSS_S
//Heavy: Guest_AA4o
//Extremely Heavy: Guest_AA4o(filtro)
//======================================================================================================================================================================
//~packed together, corrected to fit requirements of popular with PPSSPP AMD legacy drivers v11.11(if it works on that, it will on almost anything lol),
// currently meet requirements of GLSL version 100 meaning it will probably run on anything unless the driver is horribly broken
// partially written, ported to glsl where required and tested by LunaMoo (Moon Cow?;p),
// other credits mentioned earlier, any more info / required licenses can be found below.



/*
   AA shader 4.o / AA shader 4.o - filtro
   
   Copyright (C) 2014 guest(r) - guest.r@gmail.com

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


const vec3 dt = vec3(1.0,1.0,1.0);
vec3 texture2d (vec2 texcoord) {
	float scaleGuest_AA4o = u_setting.y;

	vec4 yx = vec4(1.0/480.0,1.0/272.0,-1.0/480.0,-1.0/272.0)/scaleGuest_AA4o;
	vec4 xy = vec4(2.0/480.0,2.0/272.0,-2.0/480.0,-2.0/272.0)/scaleGuest_AA4o;
	vec3 s00 = texture2D(sampler0, texcoord + yx.zw).xyz; 
	vec3 s20 = texture2D(sampler0, texcoord + yx.xw).xyz; 
	vec3 s22 = texture2D(sampler0, texcoord + yx.xy).xyz; 
	vec3 s02 = texture2D(sampler0, texcoord + yx.zy).xyz; 

	float m1=dot(abs(s00-s22),dt)+0.001;
	float m2=dot(abs(s02-s20),dt)+0.001;

	return 0.5*(m2*(s00+s22)+m1*(s02+s20))/(m1+m2);
}
vec3 texture2dd (vec2 texcoord) {
	float scaleGuest_AA4o = u_setting.y;

	vec4 yx = vec4(1.0/480.0,1.0/272.0,-1.0/480.0,-1.0/272.0)/scaleGuest_AA4o;
	vec4 xy = vec4(2.0/480.0,2.0/272.0,-2.0/480.0,-2.0/272.0)/scaleGuest_AA4o;
	vec3 c11 = texture2D(sampler0, texcoord        ).xyz; 
	vec3 c00 = texture2D(sampler0, texcoord + xy.zw).xyz; 
	vec3 c20 = texture2D(sampler0, texcoord + xy.xw).xyz; 
	vec3 c22 = texture2D(sampler0, texcoord + xy.xy).xyz; 
	vec3 c02 = texture2D(sampler0, texcoord + xy.zy).xyz; 
	vec3 s00 = texture2D(sampler0, texcoord + yx.zw).xyz; 
	vec3 s20 = texture2D(sampler0, texcoord + yx.xw).xyz; 
	vec3 s22 = texture2D(sampler0, texcoord + yx.xy).xyz; 
	vec3 s02 = texture2D(sampler0, texcoord + yx.zy).xyz; 

	float d1=dot(abs(c00-c22),dt)+0.001;
	float d2=dot(abs(c20-c02),dt)+0.001;
	float m1=dot(abs(s00-s22),dt)+0.001;
	float m2=dot(abs(s02-s20),dt)+0.001;

	vec3 t2=(d1*(c20+c02)+d2*(c00+c22))/(2.0*(d1+d2));
	
	return 0.25*(c11+t2+(m2*(s00+s22)+m1*(s02+s20))/(m1+m2));
}
vec3 processGuestAA4o() {
	float scaleGuest_AA4o = u_setting.y;

	// Calculating texel coordinates

	vec2 size     = vec2(480.0,272.0)*scaleGuest_AA4o;
	vec2 inv_size = vec2(1.0/480.0,1.0/272.0)/scaleGuest_AA4o;
	
	vec2 OGL2Pos = v_texcoord0 * size;
	vec2 fp = fract(OGL2Pos);
	vec2 dx = vec2(inv_size.x,0.0);
	vec2 dy = vec2(0.0, inv_size.y);
	vec2 g1 = vec2(inv_size.x,inv_size.y);
	vec2 g2 = vec2(-inv_size.x,inv_size.y);
	
	vec2 pC4 = floor(OGL2Pos) * inv_size + 0.5*inv_size;	
	
	// Reading the texels
	vec3 C0 = texture2d(pC4 - g1); 
	vec3 C1 = texture2d(pC4 - dy);
	vec3 C2 = texture2d(pC4 - g2);
	vec3 C3 = texture2d(pC4 - dx);
	vec3 C4 = texture2d(pC4     );
	vec3 C5 = texture2d(pC4 + dx);
	vec3 C6 = texture2d(pC4 + g2);
	vec3 C7 = texture2d(pC4 + dy);
	vec3 C8 = texture2d(pC4 + g1);
	
	vec3 ul, ur, dl, dr;
	float m1, m2;
	
	m1 = dot(abs(C0-C4),dt)+0.001;
	m2 = dot(abs(C1-C3),dt)+0.001;
	ul = (m2*(C0+C4)+m1*(C1+C3))/(m1+m2);  
	
	m1 = dot(abs(C1-C5),dt)+0.001;
	m2 = dot(abs(C2-C4),dt)+0.001;
	ur = (m2*(C1+C5)+m1*(C2+C4))/(m1+m2);
	
	m1 = dot(abs(C3-C7),dt)+0.001;
	m2 = dot(abs(C6-C4),dt)+0.001;
	dl = (m2*(C3+C7)+m1*(C6+C4))/(m1+m2);
	
	m1 = dot(abs(C4-C8),dt)+0.001;
	m2 = dot(abs(C5-C7),dt)+0.001;
	dr = (m2*(C4+C8)+m1*(C5+C7))/(m1+m2);
	
	vec3 result = vec3(0.5*((dr*fp.x+dl*(1.0-fp.x))*fp.y+(ur*fp.x+ul*(1.0-fp.x))*(1.0-fp.y)));

	if(u_setting.z==1.0){
		vec3 c11 = 0.5*((dr*fp.x+dl*(1.0-fp.x))*fp.y+(ur*fp.x+ul*(1.0-fp.x))*(1.0-fp.y));

		inv_size = vec2(2.0/480.0,2.0/272.0)/scaleGuest_AA4o;;

		dx  = vec2(inv_size.x,0.0);
		dy  = vec2(0.0,inv_size.y);
		g1  = vec2( inv_size.x,inv_size.y);
		g2  = vec2(-inv_size.x,inv_size.y);
		pC4 = v_texcoord0;
  
		C0 = texture2dd(pC4-g1); 
		C1 = texture2dd(pC4-dy);
		C2 = texture2dd(pC4-g2);
		C3 = texture2dd(pC4-dx);
		C4 = texture2dd(pC4   );
		C5 = texture2dd(pC4+dx);
		C6 = texture2dd(pC4+g2);
		C7 = texture2dd(pC4+dy);
		C8 = texture2dd(pC4+g1);
 
		vec3 mn1 = min(min(C0,C1),C2);
		vec3 mn2 = min(min(C3,C4),C5);
		vec3 mn3 = min(min(C6,C7),C8);
		vec3 mx1 = max(max(C0,C1),C2);
		vec3 mx2 = max(max(C3,C4),C5);
		vec3 mx3 = max(max(C6,C7),C8);
 
		mn1 = min(min(mn1,mn2),mn3);
		mx1 = max(max(mx1,mx2),mx3);
 
		vec3 dif1 = abs(c11-mn1) + 0.0001*dt;
		vec3 dif2 = abs(c11-mx1) + 0.0001*dt;
 
		float filterparam = 2.0; 
 
		dif1=vec3(pow(dif1.x,filterparam),pow(dif1.y,filterparam),pow(dif1.z,filterparam));
		dif2=vec3(pow(dif2.x,filterparam),pow(dif2.y,filterparam),pow(dif2.z,filterparam));
 
		c11.r = (dif1.x*mx1.x + dif2.x*mn1.x)/(dif1.x + dif2.x);
		c11.g = (dif1.y*mx1.y + dif2.y*mn1.y)/(dif1.y + dif2.y);
		c11.b = (dif1.z*mx1.z + dif2.z*mn1.z)/(dif1.z + dif2.z);

		result = c11;
	}
	
	return result;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processGuestAA4o();
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
