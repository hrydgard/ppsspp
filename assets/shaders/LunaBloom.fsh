//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define BLOOM        0     	//ON:1/OFF:0 /bloom implementation from "my heroics" blog http://myheroics.wordpress.com/2008/09/04/glsl-bloom-shader/
//#define MIKU         0     	//Hatsune<3 this is an optional bloom filter for all those pale anime faces which get white otherwise:P tested on Miku in white dress
//#define samples      3     	//Default: 4 /higher = more glow, worse performance
//#define quality      0.14  	//Default: 0.18 /lower = smaller glow, better quality
//#define Bpower       1.0  	//Default: 1.0 /amount of bloom mixed
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define BLOOMv       0     	//ON:1/OFF:0 /bloom implementation from "my heroics" blog http://myheroics.wordpress.com/2008/09/04/glsl-bloom-shader/
//#define MIKUv        0     	//Hatsune<3 this is an optional bloom filter for all those pale anime faces which get white otherwise:P tested on Miku in white dress
//#define samplesv     3     	//Default: 4 /higher = more glow, worse performance
//#define qualityv     0.14  	//Default: 0.18 /lower = smaller glow, better quality
//#define Bpowerv      1.0  	//Default: 1.0 /amount of bloom mixed
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

vec3 processBLOOM(vec3 color, vec3 colorB, int hatsu){
	#define factor       0.002 	//Default: 0.002 /just an extra tweak for the bloom slightly higher values might look better in some cases, but too much can cause artifacts
	vec4 sum = vec4(0);
	int jb;
	int samplesBloom = int(u_setting.y);
	float qualityBloom = u_setting.z;
	float BpowerBloom = 1.0;

	int diffx = samplesBloom - 1;
	int ib;
	int diffy = samplesBloom;
	vec3 bloom;
	for( ib= -diffy ;ib < diffy; ib++) {
		for (jb = -diffx; jb < diffx; jb++) {
			sum += texture2D(sampler0, v_texcoord0.xy + vec2(jb, ib)*factor) * qualityBloom;
		}
	}
	if(color.r < 0.3 && color.g < 0.3 && color.b < 0.3) {
		bloom = sum.xyz*sum.xyz*0.012 + color;
	} else {
		if(color.r < 0.5 && color.g < 0.5 && color.b < 0.5) {
			bloom = sum.xyz*sum.xyz*0.009 + color;
		} else {
			bloom = sum.xyz*sum.xyz*0.0075 + color;
		}
	}
	color = mix(color, bloom, BpowerBloom);
	if(hatsu==1) {
		color = mix(colorB, color, 0.5);
	}
	return color;
}
vec3 processMIKU(vec3 color){
	vec3 mmm = vec3(0.1, 0.1, 0.1);
	color = mix(color,mmm, 0.9);
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		vec3 colorB=color;
		int hatsu = 0;
		if(u_setting.w==1.0) {
			hatsu = 1;
			color=processMIKU(color);
		}
		color=processBLOOM(color, colorB, hatsu);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
