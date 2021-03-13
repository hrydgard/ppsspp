//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//DEBANDING FILTERS:		//especially useful for upscaling filters like 5xBR as well as texture scaling like xBRZ, use only one
//================
//#define Deband       0     	//ON:1/OFF:0 /Blurs
//#define DeTol        16.0  	//Default: 16.0 /tolerance, too high will look glitchy
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//DEBANDING FILTERS:		//especially useful for upscaling filters like 5xBR as well as texture scaling like xBRZ, use only one
//================
//#define Debandv      0     	//ON:1/OFF:0 /Blurs
//#define DeTolv       16.0  	//Default: 16.0 /tolerance, too high will look glitchy
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

vec3 processDeband(vec3 color) {
	float tolerance = u_setting.y/1638.4;

	for (int i=1;i<5;i++) {
		float p  =  u_texelDelta.x * float(i);
		vec3 c1  =  texture2D(sampler0,v_texcoord0.xy + vec2(  p,  p)).xyz;
		vec3 c2 =  texture2D(sampler0,v_texcoord0.xy + vec2( -p, -p)).xyz;
		vec3 c3  =  texture2D(sampler0,v_texcoord0.xy + vec2( -p,  p)).xyz;
		vec3 c4 =  texture2D(sampler0,v_texcoord0.xy + vec2(  p, -p)).xyz;
		vec3 avg = (c1 + c2 + c3 + c4)/4.0;
		vec3 diff = abs(color - avg);
		//color = mix(avg, color, greaterThan(diff, vec3(tolerance))); //doesn't work on old devices
		bool a = diff.r > tolerance || diff.g > tolerance || diff.b > tolerance; //workaround for the above
		color = mix(avg, color, float(a));
	}
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processDeband(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
