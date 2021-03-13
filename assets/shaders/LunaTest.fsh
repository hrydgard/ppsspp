//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
//	  Attach to any effect you wish to easily test
//=======================================================================================
//=========================================TEST==========================================
//=======================================================================================
//#define TEST         0     	//ON:1/OFF:0 /test mode, applies shaders on half of the screen for easy result comparison and tweaking
//#define TESTANIM     0 	   	//ON:1/OFF:0 /as above, animation slides shader from left to right and back /use only one
//#define testAspeed   1.0   	//animation speed
//================
//Approximate performance hit
//Free: TEST, TESTANIM
//Light: 
//Medium: 
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
uniform sampler2D sampler1;
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

vec3 processTEST(vec3 color){
	if(v_texcoord0.x<0.5) {
		color.xyz=color.xyz;
	} else {
		color.xyz=texture2D(sampler1, v_texcoord0.xy).xyz;
	}
	return color;
}

vec3 processTESTANIM(vec3 color){
	float anim=(sin(u_time.x*u_setting.y)+0.9)*0.55;
	if(v_texcoord0.x<anim) {
		color.xyz=color.xyz;
	} else {
		color.xyz=texture2D(sampler1, v_texcoord0.xy).xyz;
	}
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==0.0){
		color = processTEST(color);
	} else {
		color = processTESTANIM(color);
	}
	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
