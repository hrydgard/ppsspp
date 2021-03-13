//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SCANLINE FILTERS:
//================
//#define LCD3x        0     	//ON:1/OFF:0 //different scanline shader
//#define b_sl         16.0  	//Default: 16.0 /horizontal lines brightness
//#define b_lcd        4.0   	//Default: 4.0 /vertical lines brightness
//#define sl_size      2.0   	//Default: 2.0 /scanline size
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//OTHER FILTERS:
//================
//#define LCD3xv       0     	//ON:1/OFF:0 //different scanline shader
//#define b_slv        16.0  	//Default: 16.0 /horizontal lines brightness
//#define b_lcdv       4.0   	//Default: 4.0 /vertical lines brightness
//#define sl_sizev     2.0   	//Default: 2.0 /scanline size
//================
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
uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
varying vec2 v_texcoord0;

varying vec2 v_texcoord1;


//===========
//===========

vec3 processLCD3x(vec3 color){
	const vec3 offsets = 3.141592654 * vec3(1.0/2.0,1.0/2.0 - 2.0/3.0,1.0/2.0-4.0/3.0);

	float b_slLCD3x = u_setting.z;
	float b_lcdLCD3x = u_setting.w;
	float sl_sizeLCD3x = u_setting.y;

	vec2 angle = v_texcoord0.xy * v_texcoord1 / sl_sizeLCD3x;

	float yfactor = (b_slLCD3x + sin(angle.y)) / (b_slLCD3x + 1.0);
	vec3 xfactors = (b_lcdLCD3x + sin(angle.x + offsets)) / (b_lcdLCD3x + 1.0);

	color.rgb = yfactor * xfactors * color.rgb;
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processLCD3x(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
