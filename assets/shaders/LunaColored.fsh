//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//COLORING FILTERS:
//================
//#define COLORED      0     	//ON:1/OFF:0 /coloring part of KrossX Overlay Bloom shader from here http://www.mediafire.com/krossx#ste5pa5ijfa0o
//#define Cpower       0.5   	//Default: 0.5 /strenght of the effect
//================
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//COLORING FILTERS:
//================
//#define COLOREDv     0     	//ON:1/OFF:0 /coloring part of KrossX Overlay Bloom shader from here http://www.mediafire.com/krossx#ste5pa5ijfa0o
//#define Cpowerv      0.5   	//Default: 0.5 /strenght of the effect
//================
//Approximate performance hit
//Free:
//Light: COLORED, SHADEBOOST, GAMMA
//Medium: NATURALC
//Heavy: 
//Extremely Heavy: 
//======================================================================================================================================================================
//~packed together, corrected to fit requirements of popular with PPSSPP AMD legacy drivers v11.11(if it works on that, it will on almost anything lol),
// currently meet requirements of GLSL version 100 meaning it will probably run on anything unless the driver is horribly broken
// partially written, ported to glsl where required and tested by LunaMoo (Moon Cow?;p),
// other credits mentioned earlier, any more info / required licenses can be found below.





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

float overlay(float base, float blend) {
	float result = 0.0;
	if( base < 0.5 ) {
		result = 2.0 * base * blend;
	} else {
		result = 1.0 - (1.0 - 2.0*(base-0.5)) * (1.0-blend);
	}
	return result;
}
vec3 processCOLORED(vec3 color){
	float CpowerCOLORED = u_setting.y;

	vec3 blur = color * 1.22520613262190495;
	vec3 newcolor = vec3(overlay(color.r,blur.r),
		 overlay(color.g,blur.g),
		 overlay(color.b,blur.b));
	color = mix(color, newcolor, CpowerCOLORED);
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processCOLORED(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
