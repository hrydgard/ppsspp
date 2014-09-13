// PPSSPP: Grabbed from Processing and slightly modified.

// FXAA shader, GLSL code adapted from:
// http://horde3d.org/wiki/index.php5?title=Shading_Technique_-_FXAA
// Whitepaper describing the technique:
// http://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf

//======================================================================================================================================================================
//Extra options:
#define FXAA         1     	//ON:1/OFF:0 /default FXAA, info above
//there's no extra config for fxaa here since from what I checked this simple fxaa version offers only more blur or less aa;c, you can check it yourself below
//================
#define MIKU         0		//Hatsune<3 this is an optional filter for all those pale anime faces which get white with bloom/colored:P tested on Miku in white dress
//================
#define BLOOM        1     	//ON:1/OFF:0 /bloom implementation from "my heroics" blog http://myheroics.wordpress.com/2008/09/04/glsl-bloom-shader/
#define samples      4     	//Default: 4 /higher = more glow, worser performance
#define quality      0.25  	//Default: 0.25 /lower = smaller glow, better quality
#define factor       0.002 	//Default: 0.002 /just an extra tweak for the bloom slightly higher values might look better in some cases, but too much can cause artifacts
#define Bpower       0.5	//Default: 0.5 /amount of bloom mixed
//================
#define COLORED      1     	//ON:1/OFF:0 /coloring part of KrossX Overlay Bloom shader from here http://www.mediafire.com/krossx#ste5pa5ijfa0o
#define Cpower       0.5	//Default: 0.5 /amount of effect mixed
//================
#define SHARPEN      0    	//ON:1/OFF:0 /a simple sharpen filter, might be counterproductive to FXAA and BLOOM, hence disabled by default
#define value        7.5   	//Default: 7.5 /higher = more visible effect
//================
#define S_COM_V2     0		//Sharpen Complex v2 from https://github.com/mpc-hc similar to above in effect, maybe more accurate
#define S_val0       5.0	//Default: 5.0 /higher ~ increases sharpness /negative ~ can add extra blurr/strange effect
//================
#define SHADEBOOST   0     	//ON:1/OFF:0 /color correction from GSdx/pcsx2 plugin, initially taken from http://irrlicht.sourceforge.net/phpBB2/viewtopic.php?t=21057 
#define saturation   1.0  	//Default: 1.0 //negative will look like inverted colors shader
#define brightness   1.0  	//Default: 1.0
#define contrast     1.0  	//Default: 1.0 //negative will be... well negative;p
#define red          1.0   	//Default: 1.0
#define green        1.0  	//Default: 1.0
#define blue         1.0   	//Default: 1.0
//Shadeboost presets:		//Shadeboost must be activated, presets override options above
int SEPIA =          0;		//Moody coolors:)
int GRAYSCALE =      0;		//Just for lols?
int NEGATIVE =       0;		//As above
int PSPCOLORS =      0; 	//Makes the colors as on older PSP screens(colder)
//All presets are simple switch ON:1/OFF:0, 
//================
#define GAMMA        0		//simple gamma function after reading http://filmicgames.com/archives/299
#define	correction   1.0	//Default: 1.0
//======================================================================================================================================================================
//~packed together, corrected to fit requirements of popular with PPSSPP AMD legacy drivers v11.11, partially written and tested by LunaMoo (Moon Cow?;p)
//other credits mentioned earlier.

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;

// The inverse of the texture dimensions along X and Y
uniform vec2 u_texelDelta;
varying vec2 v_texcoord0;


//===========
float overlay(float base, float blend)
{
	float result = 0.0;
	
	if( base < 0.5 ) 
		result = 2.0 * base * blend;
	else   
		result = 1.0 - (1.0 - 2.0*(base-0.5)) * (1.0-blend);
		
	return result;
}
//===========
void main() {
  vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;
#if(FXAA==1)
  // The parameters are hardcoded for now, but could be
  // made into uniforms to control fromt he program.
  float FXAA_SPAN_MAX = 8.0;
  float FXAA_REDUCE_MUL = 1.0/8.0;
  float FXAA_REDUCE_MIN = (1.0/128.0);

  vec3 rgbNW = texture2D(sampler0, v_texcoord0.xy + (vec2(-1.0, -1.0) * u_texelDelta)).xyz;
  vec3 rgbNE = texture2D(sampler0, v_texcoord0.xy + (vec2(+1.0, -1.0) * u_texelDelta)).xyz;
  vec3 rgbSW = texture2D(sampler0, v_texcoord0.xy + (vec2(-1.0, +1.0) * u_texelDelta)).xyz;
  vec3 rgbSE = texture2D(sampler0, v_texcoord0.xy + (vec2(+1.0, +1.0) * u_texelDelta)).xyz;
  vec3 rgbM  = texture2D(sampler0, v_texcoord0.xy).xyz;
	
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
              texture2D(sampler0, v_texcoord0.xy + dir * (1.0/3.0 - 0.5)).xyz +
              texture2D(sampler0, v_texcoord0.xy + dir * (2.0/3.0 - 0.5)).xyz);
  vec3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (
              texture2D(sampler0, v_texcoord0.xy + dir * (0.0/3.0 - 0.5)).xyz +
              texture2D(sampler0, v_texcoord0.xy + dir * (3.0/3.0 - 0.5)).xyz);
  float lumaB = dot(rgbB, luma);
  
  if((lumaB < lumaMin) || (lumaB > lumaMax)){
	color=rgbA;
  } else {
    color=rgbB;
  }
#endif
vec3 colorB=color;
int hatsu = 0;
#if(MIKU==1)
	vec3 mmm = vec3(0.1, 0.1, 0.1);
	hatsu = 1;
	color = mix(color,mmm, 0.9);
#endif
//#if(PSPCOLORS==1)
//	color.r = color.r * 0.9593;
//	color.g = color.g * 1.0739;
//	color.b = color.b * 1.4604;
//#endif
#if(COLORED==1)
    vec3 blur = color * 1.22520613262190495;	
	vec3 newcolor = vec3(overlay(color.r,blur.r),
							 overlay(color.g,blur.g),
							 overlay(color.b,blur.b));
	color = mix(color, newcolor, Cpower);
#endif
#if(BLOOM==1)
   vec4 sum = vec4(0);
   int j;
   int diffx = samples - 1;
   int i;
   int diffy = samples;
   vec3 bloom;
   for( i= -diffy ;i < diffy; i++)
   {
        for (j = -diffx; j < diffx; j++)
        {
            sum += texture2D(sampler0, v_texcoord0.xy + vec2(j, i)*factor) * quality;
        }
   }
    if(color.r < 0.3 && color.g < 0.3 && color.b < 0.3)// (texture2D(sampler0, v_texcoord0.xy).r < 0.3)
    {
       bloom = sum.xyz*sum.xyz*0.012 + color;//texture2D(sampler0, v_texcoord0.xy).xyz;
    }
    else
    {
        if(color.r < 0.5 && color.g < 0.5 && color.b < 0.5)// (texture2D(sampler0, v_texcoord0.xy).r < 0.5)
        {
            bloom = sum.xyz*sum.xyz*0.009 + color;//texture2D(sampler0, v_texcoord0.xy).xyz;
        }
        else
        {
            bloom = sum.xyz*sum.xyz*0.0075 + color;//texture2D(sampler0, v_texcoord0.xy).xyz;
        }
    }							 
	color = mix(color, bloom, Bpower);
	if(hatsu==1){
		color = mix(colorB, color, 0.5);
	}	
#endif
#if(SHARPEN==1)
	color -= texture2D(sampler0, v_texcoord0.xy+0.0001).xyz*value;
	color += texture2D(sampler0, v_texcoord0.xy-0.0001).xyz*value;
#endif
#if(S_COM_V2==1)
	#define width  (v_texcoord0.x)
	#define height (v_texcoord0.y)
	#define px (u_texelDelta.x)
	#define py (u_texelDelta.y)
	#define dx (mean * px)
	#define dy (mean * py)
	#define mean 0.6
	#define CoefOrig (1 + CoefBlur)
	#define CoefBlur 2
	#define Sharpen_val1 ((S_val0 - 1) / 8.0)
	#define S_Edge 0.2
	// get original pixel
	vec3 orig = texture2D(sampler0, v_texcoord0).xyz;

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
	vec3 flou = (c1 + c3 + c6 + c8 + 2 * (c2 + c4 + c5 + c7) + 4 * orig) * 0.0625;

	// substract blurred image from original image
	vec3 corrected = CoefOrig * orig - CoefBlur * flou;

	// edge detection
	// Get neighbor points
	// [ c1,   c2, c3 ]
	// [ c4, orig, c5 ]
	// [ c6,   c7, c8 ]
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
	vec3 delta1 = (c3 + 2 * c5 + c8) - (c1 + 2 * c4 + c6);

	// Save some arithmetic operations to ensure PS2 compatibility
	c1 += c3;
	c6 += c8;
	// vertical gradient
	// [ -1, - 2, -1 ]
	// [  0,   0,  0 ]
	// [  1,   2,  1 ]
	vec3 delta2 = (c6 + 2 * c7 /*+ c8*/) - (c1 + 2 * c2 /*+ c3*/);

	// computation
	if (sqrt(mul(delta1, delta1) + mul(delta2, delta2)) > S_Edge) {
		// if we have an edge, use sharpen
		vec3 scomplexv2 = orig * S_val0 - (c1 + c2 /*+ c3*/ + c4 + c5 + c6 + c7 /*+ c8*/) * Sharpen_val1;
		color=mix(color,scomplexv2,0.5);
	} else {
		// else return corrected image
		vec3 scomplexv2 = corrected;
		color=mix(color,scomplexv2,1.0);
	}
#endif
#if(SHADEBOOST==1)
	float sat = saturation;
	float brt = brightness;
	float con = contrast;

	// Increase or decrease theese values to adjust r, g and b color channels seperately
	float AvgLumR = red;
	float AvgLumG = green;
	float AvgLumB = blue;
	
	//==========================presets
	if(SEPIA==1)
	{
		sat = 0.01;
		brt = 1.75;
		con = 1.0;
		AvgLumR = 0.44;
		AvgLumG = 0.26;
		AvgLumB = 0.08;
	}
	if(GRAYSCALE==1)
	{
		sat = 0.0;
		brt = 1.0;
		con = 1.0;
		AvgLumR = 1.0;
		AvgLumG = 1.0;
		AvgLumB = 1.0;
	}
	if(NEGATIVE==1)
	{
		sat = 1.0;
		brt = 1.0;
		con = -1.0;
		AvgLumR = 1.0;
		AvgLumG = 1.0;
		AvgLumB = 1.0;
	}
	if(PSPCOLORS==1)
	{
		sat = 1.0;
		brt = 1.0;
		con = 1.0;
		AvgLumR = 0.9593;
		AvgLumG = 1.0739;
		AvgLumB = 1.4604;
	}
	//==========================	
	const vec3 LumCoeff = vec3(0.2125, 0.7154, 0.0721);
	
	vec3 AvgLumin = vec3(AvgLumR, AvgLumG, AvgLumB);
	vec3 conRGB = vec3(0.5, 0.5, 0.5);
	vec3 brtColor = color.rgb * brt;
	vec3 intensity = vec3((brtColor.r*LumCoeff.r)+(brtColor.g*LumCoeff.g)+(brtColor.b*LumCoeff.b)); //workaround for AMD legacy drivers could be just dot(brtColor,LumCoeff) ;c
	vec3 satColor = mix(intensity, brtColor, sat);
	vec3 conColor = mix(conRGB, satColor, con);
	vec3 mixColor = AvgLumin * conColor;

	color.rgb = mixColor;
#endif
#if(GAMMA==1)
	vec3 gam=vec3(pow(color.r,1.0/abs(correction)),pow(color.g,1.0/abs(correction)),pow(color.b,1.0/abs(correction)));
	color=mix(color,gam,1);
#endif
  gl_FragColor.xyz=color;
  gl_FragColor.a = 1.0;
}

