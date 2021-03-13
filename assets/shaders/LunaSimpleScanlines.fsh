//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SCANLINE FILTERS:
//================
//#define SCANLINES    0  	//Ugly lines which never existed on psp, yet are popular among some people(I had to, sorry:P)
//#define SLsize       1     	//Default: 1 /basically sets how wide each line is, from 1 to looks_weird_when_too_high
//#define SLcolor      2.8   	//Default: 2.8(1.2 for noGradient) /brightens screen to compensate for dark lines, set to 1.0 for no compensation
//#define SLpower      0.4   	//Default: 0.4(0.8 for noGradient) /less/more = darker/brighter lines, 
#define SLV          0     	//ON:1/OFF:0 /vertical scanlines
#define SSnoGradient   1     	//ON:1/OFF:0 /enabling this makes the scanline a simple sharp line
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define SCANLINESv   0   	//Ugly lines which never existed on psp, yet are popular among some people(I had to, sorry:P)
//#define SLsizev      1     	//Default: 1 /basically sets how wide each line is, from 1 to looks_weird_when_too_high
//#define SLcolorv     2.8   	//Default: 2.8(1.2 for noGradient) /brightens screen to compensate for dark lines, set to 1.0 for no compensation
//#define SLpowerv     0.4   	//Default: 0.4(0.8 for noGradient) /less/more = darker/brighter lines, 
//#define SLVv         0     	//ON:1/OFF:0 /vertical scanlines
//#define noGradientv  0     	//ON:1/OFF:0 /enabling this makes the scanline a simple sharp line//================
//Approximate performance hit
//Free: 
//Light: SCANLINES, LCD3x
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
uniform vec4 u_time;
uniform float u_video;

uniform vec4 u_setting;

//===========
// The inverse of the texture dimensions along X and Y
uniform vec2 u_pixelDelta;
varying vec2 v_texcoord0;



//===========
//===========

vec3 processSCANLINES(vec3 color){
	int vscan = SLV;
	int noGradient = SSnoGradient;
	vec3 colorSL=color*u_setting.w;

	float SLsizeSCANLINES = u_setting.y;
	float SLpowerSCANLINES = u_setting.z;


	if(vscan==0){
		float rey = (1.0/SLsizeSCANLINES/u_pixelDelta.y);
		float posSy = v_texcoord0.y*rey;
		float lineSy = mod(posSy,2.0);
		if (noGradient==1) {
			bool clSy = lineSy<1.0;
			lineSy = mix(1.0,0.0,float(clSy));
		}
		color=color-lineSy;
	} else {
		float rex = (1.0/SLsizeSCANLINES/u_pixelDelta.x);
		float posSx = v_texcoord0.x*(rex/1.0);
		float lineSx = mod(posSx,2.0);
		if (noGradient==1) {
			bool clSx = lineSx<1.0;
			lineSx = mix(1.0,0.0,float(clSx));
		}
		color=color-lineSx;
	}
	if (noGradient==1) {
		color=mix(color,clamp(colorSL,0.0,1.0),SLpowerSCANLINES);
	} else {
		color=mix(color,colorSL,SLpowerSCANLINES);
	}
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processSCANLINES(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
