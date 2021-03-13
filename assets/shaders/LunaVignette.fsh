//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define VIGNETTE     0     	//ON:1/OFF:0 /same as in PPSSPP, just with few settings
//#define vsize        1.2   	//Default: 1.2 /winter... I mean darkness is coming ~ with higher values
//#define VIpos        0.0   	//Default: 0.0 /position of the effect between 0.0 and less than 0.5(where it disappears completely)
//#define deVi         0     	//ON:1/OFF:0 /reverse vignette
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define VIGNETTEv    0     	//ON:1/OFF:0 /same as in PPSSPP, just with few settings
//#define vsizev       1.2   	//Default: 1.2 /winter... I mean darkness is coming ~ with higher values
//#define VIposv       0.0   	//Default: 0.0 /position of the effect between 0.0 and less than 0.5(where it disappears completely)
//#define deViv        0     	//ON:1/OFF:0 /reverse vignette
//================
//Approximate performance hit
//Free: 
//Light: VIGNETTE, BLOOM(<= 2 samples)
//Medium: ACARTOON, BLOOM(== 3 samples)
//Heavy: BLOOM(>= 4 samples)
//Extremely Heavy: BLOOM(>= 8 samples)
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

vec3 processVIGNETTE(vec3 color) {
	float vsizeVIGNETTE = u_setting.y;
	float VIposVIGNETTE = u_setting.z;

	float distance = dot(v_texcoord0 - 0.5, v_texcoord0 - 0.5);
	distance = clamp(distance - VIposVIGNETTE,0.0,1.0);
	float vignette;
	if(u_setting.w==1.0){
		vignette = 1.1 + distance * vsizeVIGNETTE;
	} else {
		vignette = 1.1 - distance * vsizeVIGNETTE;
	}
	color = vignette * color;
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processVIGNETTE(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
