//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define GAUSS_SQ     0     	//ON:1/OFF:0 /full square gauss filtering
//#define Gsmoothing   3.5   	//Default: 3.5 /increase for smoother(blurry) graphics
//================
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define GAUSS_SQv    0     	//ON:1/OFF:0 /full square gauss filtering
//#define Gsmoothingv  3.5   	//Default: 3.5 /increase for smoother(blurry) graphics
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

vec3 processGAUSS_SQ(vec3 color){
	float GAUSS_KERNEL_SIZE = 5.0;
	//indices
	//  00 01 02 03 04
	//  05 06 07 08 09
	//  10 11 12 13 14
	//  15 16 17 18 19
	//  20 21 22 23 24

	//filter strength, rather smooth 
	//  01 04 07 04 01
	//  04 16 26 16 04
	//  07 26 41 26 07
	//  04 16 26 16 04
	//  01 04 07 04 01

	vec2 offset = u_pixelDelta*u_setting.y/GAUSS_KERNEL_SIZE;
	if (u_video==1.0){
		offset = u_pixelDelta*u_setting.y/GAUSS_KERNEL_SIZE;
	}

	vec3 cGauss0 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0,-2.0)).xyz;
	vec3 cGauss1 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0,-2.0)).xyz;
	vec3 cGauss2 =  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0,-2.0)).xyz;
	vec3 cGauss3 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0,-2.0)).xyz;
	vec3 cGauss4 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0,-2.0)).xyz;
	vec3 cGauss5 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0,-1.0)).xyz;
	vec3 cGauss6 = 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0,-1.0)).xyz;
	vec3 cGauss7 = 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0,-1.0)).xyz;
	vec3 cGauss8 = 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0,-1.0)).xyz;
	vec3 cGauss9 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0,-1.0)).xyz;
	vec3 cGauss10=  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0, 0.0)).xyz;
	vec3 cGauss11= 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 0.0)).xyz;
	vec3 cGauss12= 41.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 0.0)).xyz;
	vec3 cGauss13= 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 0.0)).xyz;
	vec3 cGauss14=  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0, 0.0)).xyz;
	vec3 cGauss15=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0, 1.0)).xyz;
	vec3 cGauss16= 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 1.0)).xyz;
	vec3 cGauss17= 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 1.0)).xyz;
	vec3 cGauss18= 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 1.0)).xyz;
	vec3 cGauss19=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0, 1.0)).xyz;
	vec3 cGauss20=  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0, 2.0)).xyz;
	vec3 cGauss21=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 2.0)).xyz;
	vec3 cGauss22=  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 2.0)).xyz;
	vec3 cGauss23=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 2.0)).xyz;
	vec3 cGauss24=  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0, 2.0)).xyz;

	color.xyz = cGauss0 + cGauss1 + cGauss2 + cGauss3 + cGauss4 + cGauss5 + cGauss6 + cGauss7 + cGauss8 + cGauss9 + cGauss10 + cGauss11 + cGauss12 + cGauss13 + cGauss14 + cGauss15 + cGauss16 + cGauss17 + cGauss18 + cGauss19 + cGauss20 + cGauss21 + cGauss22 + cGauss23 + cGauss24;
	color.xyz = color.xyz / 273.0;
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processGAUSS_SQ(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
