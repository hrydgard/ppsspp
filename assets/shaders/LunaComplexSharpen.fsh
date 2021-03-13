//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SHARPENING FILTERS:		//if you need very sharp image, add lots of aliasing
//================
//#define S_COM_V2     1	   	//ON:1/OFF:0 /Sharpen Complex v2 from https://github.com/mpc-hc similar to above in effect, maybe more accurate
//#define S_val0       5.0   	//Default: 5.0 /higher ~ increases sharpness /negative ~ can add extra blurr/strange effect
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//SHARPENING FILTERS:		//if you need very sharp image, add lots of aliasing
//================
//#define S_COM_V2v    1	   	//ON:1/OFF:0 /Sharpen Complex v2 from https://github.com/mpc-hc similar to above in effect, maybe more accurate
//#define S_val0v      5.0   	//Default: 5.0 /higher ~ increases sharpness /negative ~ can add extra blurr/strange effect
//================
//Approximate performance hit
//Free: 
//Light: SHARPEN
//Medium: S_COM_V2
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

//===========
//===========

vec3 processS_COM_V2(vec3 color){
	float S_val0S_COM_V2 = u_setting.y;

	float width = (v_texcoord0.x);
	float height = (v_texcoord0.y);
	float px = (u_texelDelta.x);
	float py = (u_texelDelta.y);
	float mean = 0.6;
	float dx = (mean * px);
	float dy = (mean * py);
	float CoefBlur = 2.0;
	float CoefOrig = (1.0 + CoefBlur);
	float Sharpen_val1 = ((S_val0S_COM_V2 - 1.0) / 8.0);
	vec3 S_Edge = vec3(0.2,0.2,0.2);
	// get original pixel / color

	// compute blurred image (gaussian filter)
	vec3 c1 = texture2D(sampler0, v_texcoord0 + vec2(-dx, -dy)).xyz;
	vec3 c2 = texture2D(sampler0, v_texcoord0 + vec2(  0, -dy)).xyz;
	vec3 c3 = texture2D(sampler0, v_texcoord0 + vec2( dx, -dy)).xyz;
	vec3 c4 = texture2D(sampler0, v_texcoord0 + vec2(-dx,   0)).xyz;
	vec3 c5 = texture2D(sampler0, v_texcoord0 + vec2( dx,   0)).xyz;
	vec3 c6 = texture2D(sampler0, v_texcoord0 + vec2(-dx,  dy)).xyz;
	vec3 c7 = texture2D(sampler0, v_texcoord0 + vec2(  0,  dy)).xyz;
	vec3 c8 = texture2D(sampler0, v_texcoord0 + vec2( dx,  dy)).xyz;

	// gaussian filter
	// [ 1, 2, 1 ]
	// [ 2, 4, 2 ]
	// [ 1, 2, 1 ]
	// to normalize the values, we need to divide by the coeff sum
	// 1 / (1+2+1+2+4+2+1+2+1) = 1 / 16 = 0.0625
	vec3 flou = (c1 + c3 + c6 + c8 + 2.0 * (c2 + c4 + c5 + c7) + 4.0 * color) * 0.0625;

	// substract blurred image from original image
	vec3 corrected = CoefOrig * color - CoefBlur * flou;

	// edge detection
	// Get neighbor points
	// [ c1,    c2, c3 ]
	// [ c4, color, c5 ]
	// [ c6,    c7, c8 ]
	c1 = texture2D(sampler0, v_texcoord0 + vec2(-px, -py)).xyz;
	c2 = texture2D(sampler0, v_texcoord0 + vec2(  0, -py)).xyz;
	c3 = texture2D(sampler0, v_texcoord0 + vec2( px, -py)).xyz;
	c4 = texture2D(sampler0, v_texcoord0 + vec2(-px,   0)).xyz;
	c5 = texture2D(sampler0, v_texcoord0 + vec2( px,   0)).xyz;
	c6 = texture2D(sampler0, v_texcoord0 + vec2(-px,  py)).xyz;
	c7 = texture2D(sampler0, v_texcoord0 + vec2(  0,  py)).xyz;
	c8 = texture2D(sampler0, v_texcoord0 + vec2( px,  py)).xyz;

	// using Sobel filter
	// horizontal gradient
	// [ -1, 0, 1 ]
	// [ -2, 0, 2 ]
	// [ -1, 0, 1 ]
	vec3 delta1 = (c3 + 2.0 * c5 + c8) - (c1 + 2.0 * c4 + c6);

	// Save some arithmetic operations to ensure PS2 compatibility
	c1 += c3;
	c6 += c8;
	// vertical gradient
	// [ -1, - 2, -1 ]
	// [  0,   0,  0 ]
	// [  1,   2,  1 ]
	vec3 delta2 = (c6 + 2.0 * c7 ) - (c1 + 2.0 * c2 );
	vec3 checkshc = sqrt(delta1 * delta1 + delta2 * delta2);
	// computation
	if (checkshc.x > S_Edge.x || checkshc.y > S_Edge.y || checkshc.z > S_Edge.z) {
		// if we have an edge, use sharpen
		corrected = color * S_val0S_COM_V2 - (c1 + c2 + c4 + c5 + c6 + c7 ) * Sharpen_val1;
	}
	return corrected;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processS_COM_V2(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
