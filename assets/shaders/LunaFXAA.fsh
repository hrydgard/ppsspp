//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define FXAA         0     	//ON:1/OFF:0 /default FXAA, orginal info below
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
//#define FXAAv        0     	//ON:1/OFF:0 /default FXAA, orginal info below
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




/*
  FXAA shader, GLSL code adapted from:
  http://horde3d.org/wiki/index.php5?title=Shading_Technique_-_FXAA
  Whitepaper describing the technique:
  http://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
*/




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

vec3 processFXAA(vec3 color){
	//The parameters are hardcoded for now, but could be
	//made into uniforms to control fromt he program.
	float FXAA_SPAN_MAX = 8.0;
	float FXAA_REDUCE_MUL = 1.0/8.0;
	float FXAA_REDUCE_MIN = (1.0/128.0);

	vec3 rgbNW = texture2D(sampler0, v_texcoord0 + vec2(-1.0, -1.0) * u_texelDelta).xyz;
	//#define Pass_Diffuse(v2TexCoord) texture2D(sampler0, v2TexCoord).xyz
	//vec3 rgbNW = Pass_Diffuse(v2TexCoord + vec2(-u_texelDelta.x, -u_texelDelta.y)).xyz;
	vec3 rgbNE = texture2D(sampler0, v_texcoord0 + vec2(+1.0, -1.0) * u_texelDelta).xyz;
	vec3 rgbSW = texture2D(sampler0, v_texcoord0 + vec2(-1.0, +1.0) * u_texelDelta).xyz;
	vec3 rgbSE = texture2D(sampler0, v_texcoord0 + vec2(+1.0, +1.0) * u_texelDelta).xyz;
	vec3 rgbM  = texture2D(sampler0, v_texcoord0).xyz;

	vec3 luma = vec3(0.299, 0.587, 0.114);
	float lumaNW = dot(rgbNW, luma);
	float lumaNE = dot(rgbNE, luma);
	float lumaSW = dot(rgbSW, luma);
	float lumaSE = dot(rgbSE, luma);
	float lumaM  = dot( rgbM, luma);

	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	
	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);

	float rcpDirMin = 1.0/(min(abs(dir.x), abs(dir.y)) + dirReduce);

	dir = min(vec2(FXAA_SPAN_MAX,  FXAA_SPAN_MAX), 
	max(vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX), dir * rcpDirMin)) * u_texelDelta;

	vec3 rgbA = (1.0/2.0) * (
		texture2D(sampler0, v_texcoord0 + dir * (1.0/3.0 - 0.5)).xyz +
		texture2D(sampler0, v_texcoord0 + dir * (2.0/3.0 - 0.5)).xyz);
	vec3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (
		texture2D(sampler0, v_texcoord0 + dir * (0.0/3.0 - 0.5)).xyz +
		texture2D(sampler0, v_texcoord0 + dir * (3.0/3.0 - 0.5)).xyz);
	float lumaB = dot(rgbB, luma);

	if((lumaB < lumaMin) || (lumaB > lumaMax)){
		color=rgbA;
	} else {
		color=rgbB;
	}
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;


	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processFXAA(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
