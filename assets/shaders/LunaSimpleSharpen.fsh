//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SHARPENING FILTERS:		//if you need very sharp image, add lots of aliasing
//================
//#define SHARPEN      0     	//ON:1/OFF:0 /a simple sharpen filter, might be counterproductive to FXAA and BLOOM, hence disabled by default
//#define value        7.5   	//Default: 7.5 /higher = more visible effect
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//SHARPENING FILTERS:		//if you need very sharp image, add lots of aliasing
//================
//#define SHARPENv     0     	//ON:1/OFF:0 /a simple sharpen filter, might be counterproductive to FXAA and BLOOM, hence disabled by default
//#define valuev       7.5   	//Default: 7.5 /higher = more visible effect
//================
//Approximate performance hit
//Free: 
//Light: SHARPEN
//Medium: S_COM_V2
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

vec3 processSHARPEN(vec3 color){
	float valueSHARPEN = u_setting.y;
	color -= texture2D(sampler0, v_texcoord0.xy+0.0001).xyz*valueSHARPEN;
	color += texture2D(sampler0, v_texcoord0.xy-0.0001).xyz*valueSHARPEN;
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processSHARPEN(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
