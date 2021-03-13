//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define GAUSS_S      1	   	//ON:1/OFF:0 /simple gauss filtering by Bigpet, slightly different from above /you can find standalone in https://github.com/hrydgard/ppsspp/issues/7242
//================
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define GAUSS_Sv     0	   	//ON:1/OFF:0 /simple gauss filtering by Bigpet, slightly different from above /you can find standalone in https://github.com/hrydgard/ppsspp/issues/7242
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

vec3 processGAUSS_S(vec3 color){
	//The parameters are hardcoded for now, but could be
	//made into uniforms to control fromt he program.
	float GAUSSS_SPAN_MAX = 1.5;

	//just a variable to describe the maximu
	float GAUSSS_KERNEL_SIZE = 5.0;
	//indices
	//  XX XX 00 XX XX
	//  XX 01 02 03 XX
	//  04 05 06 07 08
	//  XX 09 10 11 XX
	//  XX XX 12 XX XX

	//filter strength, rather smooth 
	//  XX XX 01 XX XX
	//  XX 03 08 03 XX
	//  01 08 10 08 01
	//  XX 03 08 03 XX
	//  XX XX 01 XX XX

	vec2 offsetS = u_pixelDelta*GAUSSS_SPAN_MAX/GAUSSS_KERNEL_SIZE;

	vec3 cGaussS0 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0,-2.0)).xyz;
	vec3 cGaussS1 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-1.0,-1.0)).xyz;
	vec3 cGaussS2 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0,-1.0)).xyz;
	vec3 cGaussS3 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 1.0,-1.0)).xyz;
	vec3 cGaussS4 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-2.0, 0.0)).xyz;
	vec3 cGaussS5 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-1.0, 0.0)).xyz;
	vec3 cGaussS6 = 10.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0, 0.0)).xyz;
	vec3 cGaussS7 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 1.0, 0.0)).xyz;
	vec3 cGaussS8 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 2.0, 0.0)).xyz;
	vec3 cGaussS9 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-1.0, 1.0)).xyz;
	vec3 cGaussS10=  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0, 1.0)).xyz;
	vec3 cGaussS11=  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 1.0, 1.0)).xyz;
	vec3 cGaussS12=  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0, 2.0)).xyz;

	color = cGaussS0 + cGaussS1 + cGaussS2 + cGaussS3 + cGaussS4 + cGaussS5 + cGaussS6 + cGaussS7 + cGaussS8 + cGaussS9 + cGaussS10 + cGaussS11 + cGaussS12;
	color = color / 58.0;
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processGAUSS_S(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
