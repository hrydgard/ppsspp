//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//UPSCALING FILTERS:		//To use those, you have to set rendering res to smaller than window/display size(x1 for best results) and screen scaling filter to "nearest"
				//Starting from v1.1.1-28-g70e9979 you can also add Upscaling=True to ini file(check example) to do it automatically
//================
//#define xHQ          0     	//ON:1/OFF:0 same as 4xHQ in PPSSPP, but actually using output resolution
//#define scaleoffset  1.0   	//Default: 1.0 /you can tweek it between 0.5 and 1.5, Note: to little = no effect, to high = ugliness
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//UPSCALING FILTERS:		//To use those, you have to set rendering res to smaller than window/display size(x1 for best results) and screen scaling filter to "nearest"
				//Starting from v1.1.1-28-g70e9979 you can also add Upscaling=True to ini file(check example) to do it automatically
//================
//#define xHQv         0     	//ON:1/OFF:0 same as 4xHQ in PPSSPP, but actually using output resolution
//#define scaleoffsetv 1.0   	//Default: 1.0 /you can tweek it between 0.5 and 1.5, Note: to little = no effect, to high = ugliness
//================
//Approximate performance hit
//Free: 
//Light: 
//Medium: xHQ
//Heavy: xBR, xBRAccuracy
//Extremely Heavy:
//======================================================================================================================================================================
//~packed together, corrected to fit requirements of popular with PPSSPP AMD legacy drivers v11.11(if it works on that, it will on almost anything lol),
// currently meet requirements of GLSL version 100 meaning it will probably run on anything unless the driver is horribly broken
// partially written, ported to glsl where required and tested by LunaMoo (Moon Cow?;p),
// other credits mentioned earlier, any more info / required licenses can be found below.


/*
	4xGLSL HqFilter shader, Modified to use in PPSSPP. Grabbed from:
	http://forums.ngemu.com/showthread.php?t=76098

	by guest(r) (guest.r@gmail.com)
	License: GNU-GPL

	Shader notes: looks better with sprite games
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

const float mx = 0.325;    // start smoothing factor
const float k = -0.250;    // smoothing decrease factor
const float max_w = 0.25;  // max. smoothing weigth
const float min_w =-0.05;  // min smoothing/sharpening weigth

vec3 processxHQ(vec3 color){

	float x,y;

	float scaleoffsetxHQ = u_setting.y;

	x = u_pixelDelta.x*((u_texelDelta.x/u_pixelDelta.x)/2.0)*scaleoffsetxHQ;
	y = u_pixelDelta.y*((u_texelDelta.y/u_pixelDelta.y)/2.0)*scaleoffsetxHQ;

	vec2 dg1 = vec2( x,y);
	vec2 dg2 = vec2(-x,y);
	vec2 sd1 = dg1*0.5;
	vec2 sd2 = dg2*0.5;

	vec3 c  = texture2D(sampler0, v_texcoord0.xy).xyz;
	vec3 i1 = texture2D(sampler0, v_texcoord0.xy - sd1).xyz; 
	vec3 i2 = texture2D(sampler0, v_texcoord0.xy - sd2).xyz; 
	vec3 i3 = texture2D(sampler0, v_texcoord0.xy + sd1).xyz; 
	vec3 i4 = texture2D(sampler0, v_texcoord0.xy + sd2).xyz; 
	vec3 o1 = texture2D(sampler0, v_texcoord0.xy - dg1).xyz; 
	vec3 o3 = texture2D(sampler0, v_texcoord0.xy + dg1).xyz; 
	vec3 o2 = texture2D(sampler0, v_texcoord0.xy - dg2).xyz;
	vec3 o4 = texture2D(sampler0, v_texcoord0.xy + dg2).xyz; 
	vec3 dt = vec3(1.0,1.0,1.0);

	float ko1 = dot(abs(o1-c),dt);
	float ko2 = dot(abs(o2-c),dt);
	float ko3 = dot(abs(o3-c),dt);
	float ko4 = dot(abs(o4-c),dt);
	float s1d = dot(abs(i1-i3),dt);
	float s2d = dot(abs(i2-i4),dt);
	float w1  = step(ko1,ko3)*s2d;
	float w2  = step(ko2,ko4)*s1d;
	float w3  = step(ko3,ko1)*s2d;
	float w4  = step(ko4,ko2)*s1d;

	c = (w1*o1+w2*o2+w3*o3+w4*o4+0.1*c)/(w1+w2+w3+w4+0.1);
	float lc = c.r+c.g+c.b+0.2;

	w1 = (i1.r+i1.g+i1.b+lc)*0.2; 
	w1 = clamp(k*dot(abs(c-i1),dt)/w1+mx,min_w,max_w);
	w2 = (i2.r+i2.g+i2.b+lc)*0.2; 
	w2 = clamp(k*dot(abs(c-i2),dt)/w2+mx,min_w,max_w);
	w3 = (i3.r+i3.g+i3.b+lc)*0.2;
	w3 = clamp(k*dot(abs(c-i3),dt)/w3+mx,min_w,max_w);
	w4 = (i4.r+i4.g+i4.b+lc)*0.2; 
	w4 = clamp(k*dot(abs(c-i4),dt)/w4+mx,min_w,max_w);

	color = w1*i1 + w2*i2 + w3*i3 + w4*i4 + (1.0-w1-w2-w3-w4)*c;
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processxHQ(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
