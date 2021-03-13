//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//COLORING FILTERS:
//================
//#define NATURALC     0     	//ON:1/OFF:0 /by popular demand, natular colors, note: this shader can't be fully mixed with smoothing/sharpening/upscaling effects
//#define ncpower      1.0   	//Default: 1.0 / reduce for more subtle effect
//================
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//COLORING FILTERS:
//================
//#define NATURALCv    0     	//ON:1/OFF:0 /by popular demand, natular colors, note: this shader can't be fully mixed with smoothing/sharpening/upscaling effects
//#define ncpowerv     1.0   	//Default: 1.0 / reduce for more subtle effect
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

varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;


//===========
//===========

const mat3 RGBtoYIQ = mat3(0.299, 0.596, 0.212, 
                           0.587,-0.275,-0.523, 
                           0.114,-0.321, 0.311);

const mat3 YIQtoRGB = mat3(1.0, 1.0, 1.0,
                           0.95568806036115671171,-0.27158179694405859326,-1.1081773266826619523,
                           0.61985809445637075388,-0.64687381613840131330, 1.7050645599191817149);

const vec3 val00 = vec3( 1.2, 1.2, 1.2);
vec3 processNATURALC(vec3 color){
	vec3 c0,c1;

	c0 = texture2D(sampler0,v_texcoord1.xy).xyz;
	c0+=(texture2D(sampler0,v_texcoord1.zy).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord1.xw).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord1.zw).xyz)*0.125;

	c0+= texture2D(sampler0,v_texcoord2.xy).xyz;
	c0+=(texture2D(sampler0,v_texcoord2.zy).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord2.xw).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord2.zw).xyz)*0.125;

	c0+= texture2D(sampler0,v_texcoord3.xy).xyz;
	c0+=(texture2D(sampler0,v_texcoord3.zy).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord3.xw).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord3.zw).xyz)*0.125;

	c0+= texture2D(sampler0,v_texcoord4.xy).xyz;
	c0+=(texture2D(sampler0,v_texcoord4.zy).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord4.xw).xyz)*0.25;
	c0+=(texture2D(sampler0,v_texcoord4.zw).xyz)*0.125;
	c0*=0.153846153846;

	c1=RGBtoYIQ*c0;

	c1=vec3(pow(c1.x,val00.x),c1.yz*val00.yz);
	
	float ncpowerNATURALC = u_setting.y;

	color=mix(color,YIQtoRGB*c1,ncpowerNATURALC);
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processNATURALC(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
