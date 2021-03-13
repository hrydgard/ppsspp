//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//COLORING FILTERS:
//================
//#define GAMMA        0	   	//ON:1/OFF:0 /simple gamma function after reading http://filmicgames.com/archives/299
//#define	correction   0.75  	//Default: 1.0
//================
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//COLORING FILTERS:
//================
//#define GAMMAv       0	   	//ON:1/OFF:0 /simple gamma function after reading http://filmicgames.com/archives/299
//#define	correctionv  0.75  	//Default: 1.0
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

vec3 processGAMMA(vec3 color){
	float correctionGAMMA = u_setting.y;

	vec3 gam=vec3(pow(color.r,1.0/abs(correctionGAMMA)),pow(color.g,1.0/abs(correctionGAMMA)),pow(color.b,1.0/abs(correctionGAMMA)));
	return gam;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processGAMMA(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
