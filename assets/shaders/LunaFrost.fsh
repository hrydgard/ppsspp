//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//DEBANDING FILTERS:		//especially useful for upscaling filters like 5xBR as well as texture scaling like xBRZ, use only one
//================
//#define Frost        0     	//ON:1/OFF:0 /Dithering ~ adds noise
//#define fTol         32.0  	//Default: 32.0 /tolerance, too high will look glitchy
//#define fSimple      0     	//ON:1/OFF:0 / simpler(about half as demanding, slightly less smooth) version
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//DEBANDING FILTERS:		//especially useful for upscaling filters like 5xBR as well as texture scaling like xBRZ, use only one
//================
//#define Frostv       0     	//ON:1/OFF:0 /Dithering ~ adds noise
//#define fTolv        32.0  	//Default: 32.0 /tolerance, too high will look glitchy
//#define fSimplev     1     	//ON:1/OFF:0 / simpler(about half as demanding, slightly less smooth) version
//================
//Approximate performance hit
//Free: 
//Light: 
//Medium: Deband, Frost(fSimple)
//Heavy: Frost
//Extremely Heavy: Bilateral
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

float rand(vec2 co){
	return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}
vec3 processFrost(vec3 color) {
	float tolerance = u_setting.y/1638.4;

	for (int i=1;i<5;i++) {
		float p  =  u_pixelDelta.x * float(i) / rand(vec2(rand(v_texcoord0.xy),rand(v_texcoord0.xy)));
		vec3 c1  =  texture2D(sampler0,v_texcoord0.xy + vec2(  p,  p)).xyz;
		vec3 c1b =  texture2D(sampler0,v_texcoord0.xy + vec2( -p, -p)).xyz;
		vec3 c2  =  texture2D(sampler0,v_texcoord0.xy + vec2( -p,  p)).xyz;
		vec3 c2b =  texture2D(sampler0,v_texcoord0.xy + vec2(  p, -p)).xyz;
		vec3 c3  =  texture2D(sampler0,v_texcoord0.xy + vec2(  p,0.0)).xyz;
		vec3 c3b =  texture2D(sampler0,v_texcoord0.xy + vec2( -p,0.0)).xyz;
		vec3 c4  =  texture2D(sampler0,v_texcoord0.xy + vec2(0.0, -p)).xyz;
		vec3 c4b =  texture2D(sampler0,v_texcoord0.xy + vec2(0.0,  p)).xyz;
		vec3 avg = vec3(0.0, 0.0, 0.0);
		if(u_setting.w!=1.0 || mod(v_texcoord0.x / u_pixelDelta.x , 2.0) >= 1.0) {
			if(u_setting.z==1.0) {
				avg = (c1 + c2 + c3 + c4)/4.0;
			} else {
				avg = (c1 + c2 + c3 + c4 + c1b + c2b + c3b + c4b)/8.0;
			}
		}
		vec3 diff = abs(color - avg);
		bool a = diff.r > tolerance || diff.g > tolerance || diff.b > tolerance;
		color = mix(avg, color, float(a));
	}
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processFrost(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
