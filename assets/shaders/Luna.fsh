//Note  : Recommend to use PPSSPP v1.1.1-183-gb411fc0 or newer for full functionality, there were some bugs in earlier versions.
//Note2 : Upscaling, smoothing and sharpening filters are not set to be mixed between each other since the results are pointless(they counter each other).
//        Only last one applies, so pick just one of them, mess around with it's settings and add other effects as needed.
//======================================================================================================================================================================
//SMOOTHING FILTERS:		//If you love smooth graphics ~ those are also great for downscaling - to do that, you need to use higher rendering res than your display res
//================
#define FXAA         0     	//ON:1/OFF:0 /default FXAA, orginal info below
//================
#define GAUSS_SQ     0     	//ON:1/OFF:0 /full square gauss filtering
#define Gsmoothing   3.5   	//Default: 3.5 /increase for smoother(blurry) graphics
//================
#define GAUSS_S      0	   	//ON:1/OFF:0 /simple gauss filtering by Bigpet, slightly different from above /you can find standalone in https://github.com/hrydgard/ppsspp/issues/7242
//================
#define Guest_AA4o   1     	//ON:1/OFF:0 /alternative to FXAA /taken from http://forums.ppsspp.org/showthread.php?tid=6594&pid=124441#pid124441
#define scale        7.0   	//Default: 7.0 / 4.0 -> smooth, 8.0 -> sharp (4.0 + filtro works well also as upscaling filter for 2D games)
#define filtro       0     	//ON:1/OFF:0 /less blurry with smoother settings, but also like 3 times heavier
//================
//SHARPENING FILTERS:		//if you need very sharp image, add lots of aliasing
//================
#define SHARPEN      0     	//ON:1/OFF:0 /a simple sharpen filter, might be counterproductive to FXAA and BLOOM, hence disabled by default
#define value        7.5   	//Default: 7.5 /higher = more visible effect
//================
#define S_COM_V2     0	   	//Sharpen Complex v2 from https://github.com/mpc-hc similar to above in effect, maybe more accurate
#define S_val0       5.0   	//Default: 5.0 /higher ~ increases sharpness /negative ~ can add extra blurr/strange effect
//================
//UPSCALING FILTERS:		//To use those, you have to set rendering res to smaller than window/display size(x1 for best results) and screen scaling filter to "nearest"
				//Starting from v1.1.1-28-g70e9979 you can also add Upscaling=True to ini file(check example) to do it automatically
//================
#define xBR          0     	//ON:1/OFF:0 /5xBR upscale, nice for 2D games especially those that might be buggy with higher rendering res, initially made by Hyllian - license below
#define VariantB     0     	//ON:1/OFF:0 /slightly less effect on fonts, dots and other small details
#define Variantx2    0     	//ON:1/OFF:0 /2xBR aka more blurry version of 5xBR
//================
#define xBRAccuracy  0     	//ON:1/OFF:0 / Hyllian's xBR-lv2 Shader Accuracy (tweak by guest.r) ~ copy of the full license below
#define CornerA      0     	//ON:1/OFF:0 / A, B, C, D are just different variants of corner rounding
#define CornerB      0     	//ON:1/OFF:0 / activate only one
#define CornerD      0     	//ON:1/OFF:0
//	CornerC            	//used as default if no other ones are defined
//================
#define xHQ          0     	//ON:1/OFF:0 same as 4xHQ in PPSSPP, but actually using output resolution
#define scaleoffset  1.0   	//Default: 1.0 /you can tweek it between 0.5 and 1.5, Note: to little = no effect, to high = ugliness
//================
//DEBANDING FILTERS:		//especially useful for upscaling filters like 5xBR as well as texture scaling like xBRZ, use only one
//================
#define Deband       1     	//ON:1/OFF:0 /Blurs
#define DeTol        16.0  	//Default: 16.0 /tolerance, too high will look glitchy
//================
#define Frost        0     	//ON:1/OFF:0 /Dithering ~ adds noise
#define fTol         32.0  	//Default: 32.0 /tolerance, too high will look glitchy
#define fSimple      0     	//ON:1/OFF:0 / simpler(about half as demanding, slightly less smooth) version
//================
#define Bilateral    0     	//ON:1/OFF:0 /Nice, but very expensive, made by mrharicot ~ https://www.shadertoy.com/view/4dfGDH
//================
//OTHER FILTERS:		//Most effects from here on can be fully mixed without loosing previous effects. Exceptions: Natural Colors, Advanced Cartoon
//================
#define BLOOM        1     	//ON:1/OFF:0 /bloom implementation from "my heroics" blog http://myheroics.wordpress.com/2008/09/04/glsl-bloom-shader/
#define MIKU         0     	//Hatsune<3 this is an optional bloom filter for all those pale anime faces which get white otherwise:P tested on Miku in white dress
#define samples      3     	//Default: 4 /higher = more glow, worse performance
#define quality      0.14  	//Default: 0.18 /lower = smaller glow, better quality
#define Bpower       1.0  	//Default: 1.0 /amount of bloom mixed
//================
#define COLORED      0     	//ON:1/OFF:0 /coloring part of KrossX Overlay Bloom shader from here http://www.mediafire.com/krossx#ste5pa5ijfa0o
#define Cpower       0.5   	//Default: 0.5 /strenght of the effect
//================
#define NATURALC     0     	//ON:1/OFF:0 /by popular demand, natular colors, note: this shader can't be fully mixed with smoothing/sharpening/upscaling effects
#define ncpower      0.5   	//Default:0.5 / higher = more natural color, check note line above
//================
#define ACARTOON     0     	//ON:1/OFF:0 Advanced Cartoon from Guest shader pack
#define th           0.10  	//Default: 0.10 /outlines sensitivity, recommended from 0.00...0.50
#define bb           0.45  	//Default: 0.45 /outlines strength,    recommended from 0.10...2.00
#define pp           1.50  	//Default: 1.50 /outlines blackening,  recommended from 0.25...2.00
#define acpower      0.5   	//Default:0.5 / higher = more effect, note: this shader can't be fully mixed with smoothing/sharpening/upscaling effects
//================
#define SHADEBOOST   0     	//ON:1/OFF:0 /color correction from GSdx/pcsx2 plugin, initially taken from http://irrlicht.sourceforge.net/phpBB2/viewtopic.php?t=21057 
#define saturation   1.0   	//Default: 1.0 //negative will look like inverted colors shader
#define brightness   1.0   	//Default: 1.0
#define contrast     1.0   	//Default: 1.0 //negative will be... well negative;p
#define red          1.0   	//Default: 1.0
#define green        1.0   	//Default: 1.0
#define blue         1.0   	//Default: 1.0
//Shadeboost presets:		//Shadeboost must be activated, presets override options above
#define SEPIA        0	   	//Moody coolors:)
#define GRAYSCALE    0	   	//Just for lols?
#define NEGATIVE     0	   	//As above
#define PSPCOLORS    0     	//Makes the colors as on older PSP screens(colder)
//Simple color swap:		//Shadeboost must be activated
#define rgbRBG       0     	//green <-> blue
#define rgbBGR       0     	//red <-> blue
#define rgbBRG       0     	//red -> green -> blue -> red
#define rgbGRB       0     	//red <-> green
#define rgbGBR       0     	//red -> blue -> green -> red
//^All presets and color swap are simple switch ON:1/OFF:0, 
//================
#define GAMMA        1	   	//ON:1/OFF:0 /simple gamma function after reading http://filmicgames.com/archives/299
#define	correction   0.75  	//Default: 1.0
//================
#define SCANLINES    0	   	//Ugly lines which never existed on psp, yet are popular among some people(I had to, sorry:P)
#define SLsize       1     	//Default: 1 /basically sets how wide each line is, from 1 to looks_weird_when_too_high
#define SLcolor      2.8   	//Default: 2.8(1.2 for noGradient) /brightens screen to compensate for dark lines, set to 1.0 for no compensation
#define SLpower      0.4   	//Default: 0.4(0.8 for noGradient) /less/more = darker/brighter lines, 
#define SLV          0     	//ON:1/OFF:0 /vertical scanlines
#define noGradient   0     	//ON:1/OFF:0 /enabling this makes the scanline a simple sharp line
//================
#define LCD3x        0     	//ON:1/OFF:0 //different scanline shader
#define b_sl         16.0  	//Default: 16.0 /horizontal lines brightness
#define b_lcd        4.0   	//Default: 4.0 /vertical lines brightness
#define sl_size      2.0   	//Default: 2.0 /scanline size
//================
#define VIGNETTE     0     	//ON:1/OFF:0 /same as in PPSSPP, just with few settings
#define vsize        1.2   	//Default: 1.2 /winter... I mean darkness is coming ~ with higher values
#define VIpos        0.0   	//Default: 0.0 /position of the effect between 0.0 and less than 0.5(where it disappears completely)
#define deVi         0     	//ON:1/OFF:0 /reverse vignette
//================
#define TEST         0     	//ON:1/OFF:0 /test mode, applies shaders on half of the screen for easy result comparison and tweaking
#define TESTANIM     0 	   	//ON:1/OFF:0 /as above, animation slides shader from left to right and back /use only one
#define testAspeed   1.0   	//animation speed
//================
//Approximate performance hit
//Free: TEST, TESTANIM
//Light: SHARPEN, COLORED, SHADEBOOST, GAMMA, SCANLINES, LCD3x, VIGNETTE, BLOOM(<= 2 samples)
//Medium: FXAA, GAUSS_SQ, GAUSS_S, S_COM_V2, xHQ, Deband, Frost(fSimple), NATURALC, ACARTOON, BLOOM(== 3 samples)
//Heavy: Guest_AA4o, xBR, Frost, BLOOM(>= 4 samples), xBRAccuracy
//Extremely Heavy: Guest_AA4o(filtro), BLOOM(>= 8 samples), Bilateral
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

/*
   AA shader 4.o / AA shader 4.o - filtro
   
   Copyright (C) 2014 guest(r) - guest.r@gmail.com

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
   Hyllian's 5xBR v3.5a Shader
   
   Copyright (C) 2011 Hyllian/Jararaca - sergiogdb@gmail.com
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
   
*/

/*
	4xGLSL HqFilter shader, Modified to use in PPSSPP. Grabbed from:
	http://forums.ngemu.com/showthread.php?t=76098

	by guest(r) (guest.r@gmail.com)
	License: GNU-GPL

	Shader notes: looks better with sprite games
*/

/*
   Hyllian's xBR-lv2 Shader Accuracy (tweak by guest.r)
   
   Copyright (C) 2011-2015 Hyllian - sergiogdb@gmail.com

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.

   Incorporates some of the ideas from SABR shader. Thanks to Joshua Street.
*/

/*
   Advanced Cartoon shader
   
   Copyright (C) 2006 guest(r) - guest.r@gmail.com

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
uniform vec4 u_time;


//===========
// The inverse of the texture dimensions along X and Y
uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
varying vec2 v_texcoord0;

varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;
varying vec2 v_texcoord5;


//===========
//===========
#if(FXAA==1)
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
#endif
#if(GAUSS_SQ==1)
vec3 processGAUSS_SQ(vec3 color){
	float GAUSS_KERNEL_SIZE = 5.0;
	//indices
	//  00 01 02 03 04
	//  05 06 07 08 09
	//  10 11 12 13 14
	//  15 16 17 18 19
	//  20 21 22 23 24

	//filter strength, rather smooth 
	//  01 04 07 04 01
	//  04 16 26 16 04
	//  07 26 41 26 07
	//  04 16 26 16 04
	//  01 04 07 04 01

	vec2 offset = u_pixelDelta*Gsmoothing/GAUSS_KERNEL_SIZE;

	vec3 cGauss0 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0,-2.0)).xyz;
	vec3 cGauss1 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0,-2.0)).xyz;
	vec3 cGauss2 =  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0,-2.0)).xyz;
	vec3 cGauss3 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0,-2.0)).xyz;
	vec3 cGauss4 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0,-2.0)).xyz;
	vec3 cGauss5 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0,-1.0)).xyz;
	vec3 cGauss6 = 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0,-1.0)).xyz;
	vec3 cGauss7 = 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0,-1.0)).xyz;
	vec3 cGauss8 = 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0,-1.0)).xyz;
	vec3 cGauss9 =  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0,-1.0)).xyz;
	vec3 cGauss10=  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0, 0.0)).xyz;
	vec3 cGauss11= 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 0.0)).xyz;
	vec3 cGauss12= 41.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 0.0)).xyz;
	vec3 cGauss13= 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 0.0)).xyz;
	vec3 cGauss14=  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0, 0.0)).xyz;
	vec3 cGauss15=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0, 1.0)).xyz;
	vec3 cGauss16= 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 1.0)).xyz;
	vec3 cGauss17= 26.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 1.0)).xyz;
	vec3 cGauss18= 16.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 1.0)).xyz;
	vec3 cGauss19=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0, 1.0)).xyz;
	vec3 cGauss20=  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0, 2.0)).xyz;
	vec3 cGauss21=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 2.0)).xyz;
	vec3 cGauss22=  7.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 2.0)).xyz;
	vec3 cGauss23=  4.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 2.0)).xyz;
	vec3 cGauss24=  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0, 2.0)).xyz;

	color.xyz = cGauss0 + cGauss1 + cGauss2 + cGauss3 + cGauss4 + cGauss5 + cGauss6 + cGauss7 + cGauss8 + cGauss9 + cGauss10 + cGauss11 + cGauss12 + cGauss13 + cGauss14 + cGauss15 + cGauss16 + cGauss17 + cGauss18 + cGauss19 + cGauss20 + cGauss21 + cGauss22 + cGauss23 + cGauss24;
	color.xyz = color.xyz / 273.0;
	return color;
}
#endif
#if(GAUSS_S==1)
vec3 processGAUSS_S(vec3 color){
	//The parameters are hardcoded for now, but could be
	//made into uniforms to control fromt he program.
	float GAUSSS_SPAN_MAX = 1.5;

	//just a variable to describe the maximu
	float GAUSSS_KERNEL_SIZE = 5.0;
	//indices
	//  XX XX 00 XX XX
	//  XX 01 02 03 XX
	//  04 05 06 07 08
	//  XX 09 10 11 XX
	//  XX XX 12 XX XX

	//filter strength, rather smooth 
	//  XX XX 01 XX XX
	//  XX 03 08 03 XX
	//  01 08 10 08 01
	//  XX 03 08 03 XX
	//  XX XX 01 XX XX

	vec2 offsetS = u_pixelDelta*GAUSSS_SPAN_MAX/GAUSSS_KERNEL_SIZE;

	vec3 cGaussS0 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0,-2.0)).xyz;
	vec3 cGaussS1 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-1.0,-1.0)).xyz;
	vec3 cGaussS2 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0,-1.0)).xyz;
	vec3 cGaussS3 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 1.0,-1.0)).xyz;
	vec3 cGaussS4 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-2.0, 0.0)).xyz;
	vec3 cGaussS5 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-1.0, 0.0)).xyz;
	vec3 cGaussS6 = 10.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0, 0.0)).xyz;
	vec3 cGaussS7 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 1.0, 0.0)).xyz;
	vec3 cGaussS8 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 2.0, 0.0)).xyz;
	vec3 cGaussS9 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2(-1.0, 1.0)).xyz;
	vec3 cGaussS10=  8.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0, 1.0)).xyz;
	vec3 cGaussS11=  3.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 1.0, 1.0)).xyz;
	vec3 cGaussS12=  1.0 * texture2D(sampler0, v_texcoord0.xy + offsetS * vec2( 0.0, 2.0)).xyz;

	color = cGaussS0 + cGaussS1 + cGaussS2 + cGaussS3 + cGaussS4 + cGaussS5 + cGaussS6 + cGaussS7 + cGaussS8 + cGaussS9 + cGaussS10 + cGaussS11 + cGaussS12;
	color = color / 58.0;
	return color;
}
#endif
#if(xBRAccuracy==1 || xBR==1)

#if(xBRAccuracy==1)

const float XBR_SCALE = 3.0;
const float lv2_cf = 2.0;
const vec3  rgbw          = vec3(14.352, 28.176, 5.472);
const vec4  eq_threshold  = vec4(15.0, 15.0, 15.0, 15.0); 

const vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );
const vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );
const vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );
const vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );
const vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );
const vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );
const vec4 Ci = vec4(0.25, 0.25, 0.25, 0.25); 

#else
const float coef = 2.0;
const vec3  rgbw = vec3(16.163, 23.351, 8.4772);

const vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );
const vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );
const vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );
const vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );
const vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );
const vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );

#endif

vec4 df(vec4 A, vec4 B) {
	return abs(A-B);
}
#if(xBRAccuracy==1)

// Compare two vectors and return their components are different.
vec4 diff(vec4 A, vec4 B) {
	return vec4(notEqual(A, B));
}

// Determine if two vector components are equal based on a threshold.
vec4 eq(vec4 A, vec4 B) {
	return (step(df(A, B), eq_threshold));
}

// Determine if two vector components are NOT equal based on a threshold.
vec4 neq(vec4 A, vec4 B) {
	return (vec4(1.0, 1.0, 1.0, 1.0) - eq(A, B));
}
float c_df(vec3 c1, vec3 c2) {
	vec3 df = abs(c1 - c2);
	return df.r + df.g + df.b;
}
#else
bvec4 _and_(bvec4 A, bvec4 B) {
	return bvec4(A.x && B.x, A.y && B.y, A.z && B.z, A.w && B.w);
}
bvec4 _or_(bvec4 A, bvec4 B) {
	return bvec4(A.x || B.x, A.y || B.y, A.z || B.z, A.w || B.w);
} 
bvec4 close(vec4 A, vec4 B) {
	return (lessThan(df(A, B), vec4(15.0)));
}
vec4 weighted_distance(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
	return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));
}
#endif
vec3 processxBR(vec3 color){
	vec2 pS  = 1.0 / u_texelDelta.xy;
	vec2 fp  = fract(v_texcoord0.xy*pS.xy);
	vec2 TexCoord_0 = v_texcoord0.xy-fp*u_pixelDelta.xy;
	vec2 dx  = vec2(u_texelDelta.x,0.0);
	vec2 dy  = vec2(0.0,u_texelDelta.y);
	vec2 y2  = dy + dy; vec2 x2  = dx + dx;

#if(xBRAccuracy==1)
	// px = pixel, edr = edge detection rule
	vec4 edri, edr, edr_l, edr_u, px;
	vec4 irlv0, irlv1, irlv2l, irlv2u;
	vec4 fx, fx_l, fx_u; // inequations of straight lines.
	
	vec4 delta   = vec4(1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE);
	vec4 delta_l = vec4(0.5/XBR_SCALE, 1.0/XBR_SCALE, 0.5/XBR_SCALE, 1.0/XBR_SCALE);
	vec4 delta_u = delta_l.yxwz;
#else
	bvec4 edr, edr_left, edr_up, px; // px = pixel, edr = edge detection rule
	bvec4 interp_restriction_lv1, interp_restriction_lv2_left, interp_restriction_lv2_up;
	bvec4 nc; // new_color
	bvec4 fx, fx_left, fx_up; // inequations of straight lines.
#endif

	vec3 A  = texture2D(sampler0, TexCoord_0 -dx -dy).xyz;
	vec3 B  = texture2D(sampler0, TexCoord_0     -dy).xyz;
	vec3 C  = texture2D(sampler0, TexCoord_0 +dx -dy).xyz;
	vec3 D  = texture2D(sampler0, TexCoord_0 -dx    ).xyz;
	vec3 E  = texture2D(sampler0, TexCoord_0        ).xyz;
	vec3 F  = texture2D(sampler0, TexCoord_0 +dx    ).xyz;
	vec3 G  = texture2D(sampler0, TexCoord_0 -dx +dy).xyz;
	vec3 H  = texture2D(sampler0, TexCoord_0     +dy).xyz;
	vec3 I  = texture2D(sampler0, TexCoord_0 +dx +dy).xyz;
	vec3 A1 = texture2D(sampler0, TexCoord_0 -dx -y2).xyz;
	vec3 C1 = texture2D(sampler0, TexCoord_0 +dx -y2).xyz;
	vec3 A0 = texture2D(sampler0, TexCoord_0 -x2 -dy).xyz;
	vec3 G0 = texture2D(sampler0, TexCoord_0 -x2 +dy).xyz;
	vec3 C4 = texture2D(sampler0, TexCoord_0 +x2 -dy).xyz;
	vec3 I4 = texture2D(sampler0, TexCoord_0 +x2 +dy).xyz;
	vec3 G5 = texture2D(sampler0, TexCoord_0 -dx +y2).xyz;
	vec3 I5 = texture2D(sampler0, TexCoord_0 +dx +y2).xyz;
	vec3 B1 = texture2D(sampler0, TexCoord_0     -y2).xyz;
	vec3 D0 = texture2D(sampler0, TexCoord_0 -x2    ).xyz;
	vec3 H5 = texture2D(sampler0, TexCoord_0     +y2).xyz;
	vec3 F4 = texture2D(sampler0, TexCoord_0 +x2    ).xyz;

	vec4 b  = vec4(dot(B ,rgbw), dot(D ,rgbw), dot(H ,rgbw), dot(F ,rgbw));
	vec4 c  = vec4(dot(C ,rgbw), dot(A ,rgbw), dot(G ,rgbw), dot(I ,rgbw));
	vec4 d  = b.yzwx;
	vec4 e  = vec4(dot(E,rgbw));
	vec4 f  = b.wxyz;
	vec4 g  = c.zwxy;
	vec4 h  = b.zwxy;
	vec4 i  = c.wxyz;
	vec4 i4 = vec4(dot(I4,rgbw), dot(C1,rgbw), dot(A0,rgbw), dot(G5,rgbw));
	vec4 i5 = vec4(dot(I5,rgbw), dot(C4,rgbw), dot(A1,rgbw), dot(G0,rgbw));
	vec4 h5 = vec4(dot(H5,rgbw), dot(F4,rgbw), dot(B1,rgbw), dot(D0,rgbw));
	vec4 f4 = h5.yzwx;

#if(xBRAccuracy==1)
	// These inequations define the line below which interpolation occurs.
	fx   = (Ao*fp.y+Bo*fp.x); 
	fx_l = (Ax*fp.y+Bx*fp.x);
	fx_u = (Ay*fp.y+By*fp.x);

	irlv1 = irlv0 = diff(e,f) * diff(e,h);

#if(CornerA==0) // A takes priority skipping other corners
	#define SMOOTH_TIPS
	// Corner C also default if no other ones used
	irlv1   = (irlv0  * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );
	int select1 = 0;
#if(CornerB==1) // Corner B
	irlv1   = (irlv0 * ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) );
	select1 = 1;
#endif
#if(CornerD==1) // Corner D
	if (select1==0){
		vec4 c1 = i4.yzwx;
		vec4 g0 = i5.wxyz;
		irlv1   = (irlv0  *  ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) * (diff(f,f4) * diff(f,i) + diff(h,h5) * diff(h,i) + diff(h,g) + diff(f,c) + eq(b,c1) * eq(d,g0)));
	}
#endif
#endif

	irlv2l = diff(e,g) * diff(d,g);
	irlv2u = diff(e,c) * diff(b,c);

	vec4 fx45i = clamp((fx   + delta   -Co - Ci)/(2.0*delta  ), 0.0, 1.0);
	vec4 fx45  = clamp((fx   + delta   -Co     )/(2.0*delta  ), 0.0, 1.0);
	vec4 fx30  = clamp((fx_l + delta_l -Cx     )/(2.0*delta_l), 0.0, 1.0);
	vec4 fx60  = clamp((fx_u + delta_u -Cy     )/(2.0*delta_u), 0.0, 1.0);

	vec4 w1, w2;
	w1.x = dot(abs(E-C),rgbw) + dot(abs(E-G),rgbw) + dot(abs(I-H5),rgbw) + dot(abs(I-F4),rgbw) + 4.0*dot(abs(H-F),rgbw);
	w1.y = dot(abs(E-A),rgbw) + dot(abs(E-I),rgbw) + dot(abs(C-F4),rgbw) + dot(abs(C-B1),rgbw) + 4.0*dot(abs(F-B),rgbw);  
	w1.z = dot(abs(E-G),rgbw) + dot(abs(E-C),rgbw) + dot(abs(A-B1),rgbw) + dot(abs(A-D0),rgbw) + 4.0*dot(abs(B-D),rgbw);
	w1.w = dot(abs(E-I),rgbw) + dot(abs(E-A),rgbw) + dot(abs(G-D0),rgbw) + dot(abs(G-H5),rgbw) + 4.0*dot(abs(D-H),rgbw);

	w2.x = dot(abs(H-D),rgbw) + dot(abs(H-I5),rgbw) + dot(abs(F-I4),rgbw) + dot(abs(F-B),rgbw) + 4.0*dot(abs(E-I),rgbw);
	w2.y = dot(abs(F-H),rgbw) + dot(abs(F-C4),rgbw) + dot(abs(B-C1),rgbw) + dot(abs(B-D),rgbw) + 4.0*dot(abs(E-C),rgbw);
	w2.z = dot(abs(B-F),rgbw) + dot(abs(B-A1),rgbw) + dot(abs(D-A0),rgbw) + dot(abs(D-H),rgbw) + 4.0*dot(abs(E-A),rgbw); 
	w2.w = dot(abs(D-B),rgbw) + dot(abs(D-G0),rgbw) + dot(abs(H-G5),rgbw) + dot(abs(H-F),rgbw) + 4.0*dot(abs(E-G),rgbw);

	edri  = step(w1, w2) * irlv0;
	edr   = step(w1 + vec4(0.1, 0.1, 0.1, 0.1), w2) * step(vec4(0.5, 0.5, 0.5, 0.5), irlv1);

	w1.x = dot(abs(F-G),rgbw); w1.y = dot(abs(B-I),rgbw); w1.z = dot(abs(D-C),rgbw); w1.w = dot(abs(H-A),rgbw);
	w2.x = dot(abs(H-C),rgbw); w2.y = dot(abs(F-A),rgbw); w2.z = dot(abs(B-G),rgbw); w2.w = dot(abs(D-I),rgbw);

	edr_l = step( lv2_cf*w1, w2 ) * irlv2l * edr;
	edr_u = step( lv2_cf*w2, w1 ) * irlv2u * edr;

	fx45  = edr   * fx45;
	fx30  = edr_l * fx30;
	fx60  = edr_u * fx60;
	fx45i = edri  * fx45i;

	w1.x = dot(abs(E-F),rgbw); w1.y = dot(abs(E-B),rgbw); w1.z = dot(abs(E-D),rgbw); w1.w = dot(abs(E-H),rgbw);
	w2.x = dot(abs(E-H),rgbw); w2.y = dot(abs(E-F),rgbw); w2.z = dot(abs(E-B),rgbw); w2.w = dot(abs(E-D),rgbw);
	
	px = step(w1, w2);

#ifdef SMOOTH_TIPS
	vec4 maximos = max(max(fx30, fx60), max(fx45, fx45i));
#else
	vec4 maximos = max(max(fx30, fx60), fx45);
#endif

	vec3 res1 = E;
	res1 = mix(res1, mix(H, F, px.x), maximos.x);
	res1 = mix(res1, mix(B, D, px.z), maximos.z);

	vec3 res2 = E;
	res2 = mix(res2, mix(F, B, px.y), maximos.y);
	res2 = mix(res2, mix(D, H, px.w), maximos.w);

	vec3 res = mix(res1, res2, step(c_df(E, res1), c_df(E, res2)));
#else
	// These inequations define the line below which interpolation occurs.
	fx      = greaterThan(Ao*fp.y+Bo*fp.x,Co); 
	fx_left = greaterThan(Ax*fp.y+Bx*fp.x,Cx);
	fx_up   = greaterThan(Ay*fp.y+By*fp.x,Cy);

	if (VariantB==1){
		bvec4 t1        = _and_( notEqual(e, f), notEqual(e, h) );
		bvec4 t2        = _and_( not(close(f, b)), not(close(h, d)) );
		bvec4 t3        = _and_( _and_( close(e, i), not(close(f, i4)) ), not(close(h, i5)) );
		bvec4 t4        = _or_( close(e, g), close(e, c) );
		interp_restriction_lv1        = _and_( t1, _or_( _or_(t2, t3), t4 ) );
	} else {
		interp_restriction_lv1	    = bvec4(vec4(notEqual(e,f))*vec4(notEqual(e,h)));
	}
	interp_restriction_lv2_left = bvec4(vec4(notEqual(e,g))*vec4(notEqual(d,g)));
	interp_restriction_lv2_up   = bvec4(vec4(notEqual(e,c))*vec4(notEqual(b,c)));

	edr      = bvec4(vec4(lessThan(weighted_distance( e, c, g, i, h5, f4, h, f), weighted_distance( h, d, i5, f, i4, b, e, i)))*vec4(interp_restriction_lv1));
	edr_left = bvec4(vec4(lessThanEqual(coef*df(f,g),df(h,c)))*vec4(interp_restriction_lv2_left)); 
	edr_up   = bvec4(vec4(greaterThanEqual(df(f,g),coef*df(h,c)))*vec4(interp_restriction_lv2_up));
	
	nc.x = ( edr.x && (fx.x || edr_left.x && fx_left.x || edr_up.x && fx_up.x) );
	nc.y = ( edr.y && (fx.y || edr_left.y && fx_left.y || edr_up.y && fx_up.y) );
	nc.z = ( edr.z && (fx.z || edr_left.z && fx_left.z || edr_up.z && fx_up.z) );
	nc.w = ( edr.w && (fx.w || edr_left.w && fx_left.w || edr_up.w && fx_up.w) );

	px = lessThanEqual(df(e,f),df(e,h));

	vec3 res;
	if (Variantx2 == 1) {
		vec3 E0 = E.xyz;
		vec3 E1 = E.xyz;
		vec3 E2 = E.xyz;
		vec3 E3 = E.xyz;
		vec3 P[4];
		P[0] = px.x ? F : H;
		P[1] = px.y ? B : F;
		P[2] = px.z ? D : B;
		P[3] = px.w ? H : D;
		if (edr.x) {
			if (edr_left.x && edr_up.x) {
				E3  = mix(E3 , P[0],  0.833333);
				E2  = mix(E2 , P[0],  0.25);
				E1  = mix(E1 , P[0],  0.25);
			} else if (edr_left.x) {
				E3  = mix(E3 , P[0],  0.75);
				E2  = mix(E2 , P[0],  0.25);
			} else if (edr_up.x) {
				E3  = mix(E3 , P[0],  0.75);
				E1  = mix(E1 , P[0],  0.25);
			} else {
				E3  = mix(E3 , P[0],  0.5);
			}
		}
		if (edr.y) {
			if (edr_left.y && edr_up.y) {
				E1  = mix(E1 , P[1],  0.833333);
				E3  = mix(E3 , P[1],  0.25);
				E0  = mix(E0 , P[1],  0.25);
			} else if (edr_left.y) {
				E1  = mix(E1 , P[1],  0.75);
				E3  = mix(E3 , P[1],  0.25);
			} else if (edr_up.y) {
				E1  = mix(E1 , P[1],  0.75);
				E0  = mix(E0 , P[1],  0.25);
			} else {
				E1  = mix(E1 , P[1],  0.5);
			}
		}
		if (edr.z) {
			if (edr_left.z && edr_up.z) {
				E0  = mix(E0 , P[2],  0.833333);
				E1  = mix(E1 , P[2],  0.25);
				E2  = mix(E2 , P[2],  0.25);
			} else if (edr_left.z) {
				E0  = mix(E0 , P[2],  0.75);
				E1  = mix(E1 , P[2],  0.25);
			} else if (edr_up.z) {
				E0  = mix(E0 , P[2],  0.75);
				E2  = mix(E2 , P[2],  0.25);
			} else {
				E0  = mix(E0 , P[2],  0.5);
			}
		}
		if (edr.w) {
			if (edr_left.w && edr_up.w) {
				E2  = mix(E2 , P[3],  0.833333);
				E0  = mix(E0 , P[3],  0.25);
				E3  = mix(E3 , P[3],  0.25);
			} else if (edr_left.w) {
				E2  = mix(E2 , P[3],  0.75);
				E0  = mix(E0 , P[3],  0.25);
			} else if (edr_up.w) {
				E2  = mix(E2 , P[3],  0.75);
				E3  = mix(E3 , P[3],  0.25);
			} else {
				E2  = mix(E2 , P[3],  0.5);
			}
		}
		res = (fp.x < 0.50) ? (fp.y < 0.50 ? E0 : E2) : (fp.y < 0.50 ? E1: E3);
	} else {
		res = nc.x ? px.x ? F : H : nc.y ? px.y ? B : F : nc.z ? px.z ? D : B : nc.w ? px.w ? H : D : E;
	}
#endif
	return res;
}
#endif
#if(BLOOM==1)
vec3 processBLOOM(vec3 color, vec3 colorB, int hatsu){
	#define factor       0.002 	//Default: 0.002 /just an extra tweak for the bloom slightly higher values might look better in some cases, but too much can cause artifacts
	vec4 sum = vec4(0);
	int jb;
	int diffx = samples - 1;
	int ib;
	int diffy = samples;
	vec3 bloom;
	for( ib= -diffy ;ib < diffy; ib++) {
		for (jb = -diffx; jb < diffx; jb++) {
			sum += texture2D(sampler0, v_texcoord0.xy + vec2(jb, ib)*factor) * quality;
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
	color = mix(color, bloom, Bpower);
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
#endif
#if(NATURALC==1)
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

	color=mix(color,YIQtoRGB*c1,ncpower);
	return color;
}
#endif
#if(SCANLINES==1)
vec3 processSCANLINES(vec3 color){
	int vscan = SLV;
	vec3 colorSL=color*SLcolor;
	if(vscan==0){
		float rey = (1.0/float(SLsize)/u_pixelDelta.y);
		float posSy = v_texcoord0.y*rey;
		float lineSy = mod(posSy,2.0);
		if (noGradient==1) {
			bool clSy = lineSy<1.0;
			lineSy = mix(1.0,0.0,float(clSy));
		}
		color=color-lineSy;
	} else {
		float rex = (1.0/float(SLsize)/u_pixelDelta.x);
		float posSx = v_texcoord0.x*(rex/1.0);
		float lineSx = mod(posSx,2.0);
		if (noGradient==1) {
			bool clSx = lineSx<1.0;
			lineSx = mix(1.0,0.0,float(clSx));
		}
		color=color-lineSx;
	}
	if (noGradient==1) {
		color=mix(color,clamp(colorSL,0.0,1.0),SLpower);
	} else {
		color=mix(color,colorSL,SLpower);
	}
	return color;
}
#endif
#if(SHARPEN==1)
vec3 processSHARPEN(vec3 color){
	color -= texture2D(sampler0, v_texcoord0.xy+0.0001).xyz*value;
	color += texture2D(sampler0, v_texcoord0.xy-0.0001).xyz*value;
	return color;
}
#endif
#if(S_COM_V2==1)
vec3 processS_COM_V2(vec3 color){
	float width = (v_texcoord0.x);
	float height = (v_texcoord0.y);
	float px = (u_texelDelta.x);
	float py = (u_texelDelta.y);
	float mean = 0.6;
	float dx = (mean * px);
	float dy = (mean * py);
	float CoefBlur = 2.0;
	float CoefOrig = (1.0 + CoefBlur);
	float Sharpen_val1 = ((S_val0 - 1.0) / 8.0);
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
		corrected = color * S_val0 - (c1 + c2 + c4 + c5 + c6 + c7 ) * Sharpen_val1;
	}
	return corrected;
}
#endif
#if(GAMMA==1)
vec3 processGAMMA(vec3 color){
	vec3 gam=vec3(pow(color.r,1.0/abs(correction)),pow(color.g,1.0/abs(correction)),pow(color.b,1.0/abs(correction)));
	return gam;
}
#endif
#if(SHADEBOOST==1)
vec3 processSHADEBOOST(vec3 color){
	float sat = saturation;
	float brt = brightness;
	float con = contrast;

	// Increase or decrease theese values to adjust r, g and b color channels seperately
	float AvgLumR = red;
	float AvgLumG = green;
	float AvgLumB = blue;
	
	//==========================presets
	if(SEPIA==1) {
		sat = 0.11;
		brt = 0.87;
		con = 1.20;
		AvgLumR = 1.19930;
		AvgLumG = 0.960;
		AvgLumB = 0.6754;
	}
	if(GRAYSCALE==1) {
		sat = 0.0;
		brt = 1.0;
		con = 1.0;
		AvgLumR = 1.0;
		AvgLumG = 1.0;
		AvgLumB = 1.0;
	}
	if(NEGATIVE==1) {
		sat = 1.0;
		brt = 1.0;
		con = -1.0;
		AvgLumR = 1.0;
		AvgLumG = 1.0;
		AvgLumB = 1.0;
	}
	if(PSPCOLORS==1) {
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
	if(rgbRBG==1) {
		color.rgb = color.rbg;
	}
	if(rgbBGR==1) {
		color.rgb = color.bgr;
	}
	if(rgbBRG==1) {
		color.rgb = color.brg;
	}
	if(rgbGRB==1) {
		color.rgb = color.grb;
	}
	if(rgbGBR==1) {
		color.rgb = color.gbr;
	}
	return color;
}
#endif
#if(COLORED==1)
float overlay(float base, float blend) {
	float result = 0.0;
	if( base < 0.5 ) {
		result = 2.0 * base * blend;
	} else {
		result = 1.0 - (1.0 - 2.0*(base-0.5)) * (1.0-blend);
	}
	return result;
}
vec3 processCOLORED(vec3 color){
	vec3 blur = color * 1.22520613262190495;
	vec3 newcolor = vec3(overlay(color.r,blur.r),
		 overlay(color.g,blur.g),
		 overlay(color.b,blur.b));
	color = mix(color, newcolor, Cpower);
	return color;
}
#endif
#if(TEST==1)
vec3 processTEST(vec3 color){
	if(v_texcoord0.x<0.5) {
		color.xyz=color.xyz;
	} else {
		color.xyz=texture2D(sampler0, v_texcoord0.xy).xyz;
	}
	return color;
}
#endif
#if(TESTANIM==1)
vec3 processTESTANIM(vec3 color){
	float anim=(sin(u_time.x*testAspeed)+0.9)*0.55;
	if(v_texcoord0.x<anim) {
		color.xyz=color.xyz;
	} else {
		color.xyz=texture2D(sampler0, v_texcoord0.xy).xyz;
	}
	return color;
}
#endif
#if(VIGNETTE==1)
vec3 processVIGNETTE(vec3 color) {
	float distance = dot(v_texcoord0 - 0.5, v_texcoord0 - 0.5);
	distance = clamp(distance - VIpos,0.0,1.0);
	float vignette;
	if(deVi==1){
		vignette = 1.1 + distance * vsize;
	} else {
		vignette = 1.1 - distance * vsize;
	}
	color = vignette * color;
	return color;
}
#endif
#if(xHQ==1)
const float mx = 0.325;    // start smoothing factor
const float k = -0.250;    // smoothing decrease factor
const float max_w = 0.25;  // max. smoothing weigth
const float min_w =-0.05;  // min smoothing/sharpening weigth

vec3 processxHQ(vec3 color){
	float x,y;
	x = u_pixelDelta.x*((u_texelDelta.x/u_pixelDelta.x)/2.0)*scaleoffset;
	y = u_pixelDelta.y*((u_texelDelta.y/u_pixelDelta.y)/2.0)*scaleoffset;

	vec2 dg1 = vec2( x,y);
	vec2 dg2 = vec2(-x,y);
	vec2 sd1 = dg1*0.5;
	vec2 sd2 = dg2*0.5;

	vec3 c  = texture2D(sampler0, v_texcoord0.xy).xyz;
	vec3 i1 = texture2D(sampler0, v_texcoord0.xy - sd1).xyz; 
	vec3 i2 = texture2D(sampler0, v_texcoord0.xy - sd2).xyz; 
	vec3 i3 = texture2D(sampler0, v_texcoord0.xy + sd1).xyz; 
	vec3 i4 = texture2D(sampler0, v_texcoord0.xy + sd2).xyz; 
	vec3 o1 = texture2D(sampler0, v_texcoord0.xy - dg1).xyz; 
	vec3 o3 = texture2D(sampler0, v_texcoord0.xy + dg1).xyz; 
	vec3 o2 = texture2D(sampler0, v_texcoord0.xy - dg2).xyz;
	vec3 o4 = texture2D(sampler0, v_texcoord0.xy + dg2).xyz; 
	vec3 dt = vec3(1.0,1.0,1.0);

	float ko1 = dot(abs(o1-c),dt);
	float ko2 = dot(abs(o2-c),dt);
	float ko3 = dot(abs(o3-c),dt);
	float ko4 = dot(abs(o4-c),dt);
	float s1d = dot(abs(i1-i3),dt);
	float s2d = dot(abs(i2-i4),dt);
	float w1  = step(ko1,ko3)*s2d;
	float w2  = step(ko2,ko4)*s1d;
	float w3  = step(ko3,ko1)*s2d;
	float w4  = step(ko4,ko2)*s1d;

	c = (w1*o1+w2*o2+w3*o3+w4*o4+0.1*c)/(w1+w2+w3+w4+0.1);
	float lc = c.r+c.g+c.b+0.2;

	w1 = (i1.r+i1.g+i1.b+lc)*0.2; 
	w1 = clamp(k*dot(abs(c-i1),dt)/w1+mx,min_w,max_w);
	w2 = (i2.r+i2.g+i2.b+lc)*0.2; 
	w2 = clamp(k*dot(abs(c-i2),dt)/w2+mx,min_w,max_w);
	w3 = (i3.r+i3.g+i3.b+lc)*0.2;
	w3 = clamp(k*dot(abs(c-i3),dt)/w3+mx,min_w,max_w);
	w4 = (i4.r+i4.g+i4.b+lc)*0.2; 
	w4 = clamp(k*dot(abs(c-i4),dt)/w4+mx,min_w,max_w);

	color = w1*i1 + w2*i2 + w3*i3 + w4*i4 + (1.0-w1-w2-w3-w4)*c;
	return color;
}
#endif
#if(ACARTOON==1)
vec3 processACARTOON(vec3 color){
	float x = u_pixelDelta.x;
	float y = u_pixelDelta.y;
	vec2 dg1 = vec2( x,y);
	vec2 dg2 = vec2(-x,y);
	vec2 acdx  = vec2(x,0.0);
	vec2 acdy  = vec2(0.0,y);

	vec3 c00 = texture2D(sampler0, v_texcoord0.xy - dg1).xyz; 
	vec3 c10 = texture2D(sampler0, v_texcoord0.xy - acdy).xyz; 
	vec3 c20 = texture2D(sampler0, v_texcoord0.xy + dg2).xyz; 
	vec3 c01 = texture2D(sampler0, v_texcoord0.xy - acdx).xyz; 
	vec3 c11 = texture2D(sampler0, v_texcoord0.xy).xyz; 
	vec3 c21 = texture2D(sampler0, v_texcoord0.xy + acdx).xyz; 
	vec3 c02 = texture2D(sampler0, v_texcoord0.xy - dg2).xyz; 
	vec3 c12 = texture2D(sampler0, v_texcoord0.xy + acdy).xyz; 
	vec3 c22 = texture2D(sampler0, v_texcoord0.xy + dg1).xyz; 
	vec3 dt = vec3(1.0,1.0,1.0); 

	float d1=dot(abs(c00-c22),dt);
	float d2=dot(abs(c20-c02),dt);
	float hl=dot(abs(c01-c21),dt);
	float vl=dot(abs(c10-c12),dt);

	c11 = (c11+ 0.5*(c01+c10+c21+c12)+ 0.25*(c00+c22+c20+c02))/4.0;
	float d =bb*pow(max(d1+d2+hl+vl-th,0.0),pp)/(dot(c11,dt)+0.50);
	
	float lc = 5.0*length(c11); 
	lc = 0.2*(floor(lc) + pow(fract(lc),4.0));
	c11 = 4.0*normalize(c11); 
	vec3 frct = fract(c11); frct*=frct;
	c11 = floor(c11) + frct*frct;
	c11 = 0.25*(c11)*lc; lc*=0.577;
	c11 = mix(c11,lc*dt,lc);
	color.xyz = mix(color,(1.15-d)*c11,acpower);
	return color;
}
#endif
#if(LCD3x==1)
vec3 processLCD3x(vec3 color){
	const vec3 offsets = 3.141592654 * vec3(1.0/2.0,1.0/2.0 - 2.0/3.0,1.0/2.0-4.0/3.0);

	vec2 angle = v_texcoord0.xy * v_texcoord5 / sl_size;

	float yfactor = (b_sl + sin(angle.y)) / (b_sl + 1.0);
	vec3 xfactors = (b_lcd + sin(angle.x + offsets)) / (b_lcd + 1.0);

	color.rgb = yfactor * xfactors * color.rgb;
	return color;
}
#endif
#if(Deband==1)
vec3 processDeband(vec3 color) {
	float tolerance = DeTol/1638.4;
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
#endif
#if(Frost==1)
float rand(vec2 co){
	return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}
vec3 processFrost(vec3 color) {
	float tolerance = fTol/1638.4;
	for (int i=1;i<5;i++) {
		float p  =  u_pixelDelta.x * float(i) / rand(vec2(rand(v_texcoord0.xy),rand(v_texcoord0.xy)));
		vec3 c1  =  texture2D(sampler0,v_texcoord0.xy + vec2(  p,  p)).xyz;
		vec3 c1b =  texture2D(sampler0,v_texcoord0.xy + vec2( -p, -p)).xyz;
		vec3 c2  =  texture2D(sampler0,v_texcoord0.xy + vec2( -p,  p)).xyz;
		vec3 c2b =  texture2D(sampler0,v_texcoord0.xy + vec2(  p, -p)).xyz;
		vec3 c3  =  texture2D(sampler0,v_texcoord0.xy + vec2(  p,0.0)).xyz;
		vec3 c3b =  texture2D(sampler0,v_texcoord0.xy + vec2( -p,0.0)).xyz;
		vec3 c4  =  texture2D(sampler0,v_texcoord0.xy + vec2(0.0, -p)).xyz;
		vec3 c4b =  texture2D(sampler0,v_texcoord0.xy + vec2(0.0,  p)).xyz;
		vec3 avg;
		if(fSimple==1) {
			avg = (c1 + c2 + c3 + c4)/4.0;
		} else {
			avg = (c1 + c2 + c3 + c4 + c1b + c2b + c3b + c4b)/8.0;
		}
		vec3 diff = abs(color - avg);
		bool a = diff.r > tolerance || diff.g > tolerance || diff.b > tolerance;
		color = mix(avg, color, float(a));
	}
	return color;
}
#endif
#if(Guest_AA4o==1)
const vec3 dt = vec3(1.0,1.0,1.0);
vec4 yx = vec4(1.0/480.0,1.0/272.0,-1.0/480.0,-1.0/272.0)/scale;
vec4 xy = vec4(2.0/480.0,2.0/272.0,-2.0/480.0,-2.0/272.0)/scale;
vec3 texture2d (vec2 texcoord) {
	vec3 s00 = texture2D(sampler0, texcoord + yx.zw).xyz; 
	vec3 s20 = texture2D(sampler0, texcoord + yx.xw).xyz; 
	vec3 s22 = texture2D(sampler0, texcoord + yx.xy).xyz; 
	vec3 s02 = texture2D(sampler0, texcoord + yx.zy).xyz; 

	float m1=dot(abs(s00-s22),dt)+0.001;
	float m2=dot(abs(s02-s20),dt)+0.001;

	return 0.5*(m2*(s00+s22)+m1*(s02+s20))/(m1+m2);
}
vec3 texture2dd (vec2 texcoord) {

	vec3 c11 = texture2D(sampler0, texcoord        ).xyz; 
	vec3 c00 = texture2D(sampler0, texcoord + xy.zw).xyz; 
	vec3 c20 = texture2D(sampler0, texcoord + xy.xw).xyz; 
	vec3 c22 = texture2D(sampler0, texcoord + xy.xy).xyz; 
	vec3 c02 = texture2D(sampler0, texcoord + xy.zy).xyz; 
	vec3 s00 = texture2D(sampler0, texcoord + yx.zw).xyz; 
	vec3 s20 = texture2D(sampler0, texcoord + yx.xw).xyz; 
	vec3 s22 = texture2D(sampler0, texcoord + yx.xy).xyz; 
	vec3 s02 = texture2D(sampler0, texcoord + yx.zy).xyz; 

	float d1=dot(abs(c00-c22),dt)+0.001;
	float d2=dot(abs(c20-c02),dt)+0.001;
	float m1=dot(abs(s00-s22),dt)+0.001;
	float m2=dot(abs(s02-s20),dt)+0.001;

	vec3 t2=(d1*(c20+c02)+d2*(c00+c22))/(2.0*(d1+d2));
	
	return 0.25*(c11+t2+(m2*(s00+s22)+m1*(s02+s20))/(m1+m2));
}
vec3 processGuestAA4o() {
	// Calculating texel coordinates

	vec2 size     = vec2(480.0,272.0)*scale;
	vec2 inv_size = vec2(1.0/480.0,1.0/272.0)/scale;
	
	vec2 OGL2Pos = v_texcoord0 * size;
	vec2 fp = fract(OGL2Pos);
	vec2 dx = vec2(inv_size.x,0.0);
	vec2 dy = vec2(0.0, inv_size.y);
	vec2 g1 = vec2(inv_size.x,inv_size.y);
	vec2 g2 = vec2(-inv_size.x,inv_size.y);
	
	vec2 pC4 = floor(OGL2Pos) * inv_size + 0.5*inv_size;	
	
	// Reading the texels
	vec3 C0 = texture2d(pC4 - g1); 
	vec3 C1 = texture2d(pC4 - dy);
	vec3 C2 = texture2d(pC4 - g2);
	vec3 C3 = texture2d(pC4 - dx);
	vec3 C4 = texture2d(pC4     );
	vec3 C5 = texture2d(pC4 + dx);
	vec3 C6 = texture2d(pC4 + g2);
	vec3 C7 = texture2d(pC4 + dy);
	vec3 C8 = texture2d(pC4 + g1);
	
	vec3 ul, ur, dl, dr;
	float m1, m2;
	
	m1 = dot(abs(C0-C4),dt)+0.001;
	m2 = dot(abs(C1-C3),dt)+0.001;
	ul = (m2*(C0+C4)+m1*(C1+C3))/(m1+m2);  
	
	m1 = dot(abs(C1-C5),dt)+0.001;
	m2 = dot(abs(C2-C4),dt)+0.001;
	ur = (m2*(C1+C5)+m1*(C2+C4))/(m1+m2);
	
	m1 = dot(abs(C3-C7),dt)+0.001;
	m2 = dot(abs(C6-C4),dt)+0.001;
	dl = (m2*(C3+C7)+m1*(C6+C4))/(m1+m2);
	
	m1 = dot(abs(C4-C8),dt)+0.001;
	m2 = dot(abs(C5-C7),dt)+0.001;
	dr = (m2*(C4+C8)+m1*(C5+C7))/(m1+m2);
	
	vec3 result = vec3(0.5*((dr*fp.x+dl*(1.0-fp.x))*fp.y+(ur*fp.x+ul*(1.0-fp.x))*(1.0-fp.y)));

	if(filtro==1){
		vec3 c11 = 0.5*((dr*fp.x+dl*(1.0-fp.x))*fp.y+(ur*fp.x+ul*(1.0-fp.x))*(1.0-fp.y));

		inv_size = vec2(2.0/480.0,2.0/272.0)/scale;;

		dx  = vec2(inv_size.x,0.0);
		dy  = vec2(0.0,inv_size.y);
		g1  = vec2( inv_size.x,inv_size.y);
		g2  = vec2(-inv_size.x,inv_size.y);
		pC4 = v_texcoord0;
  
		C0 = texture2dd(pC4-g1); 
		C1 = texture2dd(pC4-dy);
		C2 = texture2dd(pC4-g2);
		C3 = texture2dd(pC4-dx);
		C4 = texture2dd(pC4   );
		C5 = texture2dd(pC4+dx);
		C6 = texture2dd(pC4+g2);
		C7 = texture2dd(pC4+dy);
		C8 = texture2dd(pC4+g1);
 
		vec3 mn1 = min(min(C0,C1),C2);
		vec3 mn2 = min(min(C3,C4),C5);
		vec3 mn3 = min(min(C6,C7),C8);
		vec3 mx1 = max(max(C0,C1),C2);
		vec3 mx2 = max(max(C3,C4),C5);
		vec3 mx3 = max(max(C6,C7),C8);
 
		mn1 = min(min(mn1,mn2),mn3);
		mx1 = max(max(mx1,mx2),mx3);
 
		vec3 dif1 = abs(c11-mn1) + 0.0001*dt;
		vec3 dif2 = abs(c11-mx1) + 0.0001*dt;
 
		float filterparam = 2.0; 
 
		dif1=vec3(pow(dif1.x,filterparam),pow(dif1.y,filterparam),pow(dif1.z,filterparam));
		dif2=vec3(pow(dif2.x,filterparam),pow(dif2.y,filterparam),pow(dif2.z,filterparam));
 
		c11.r = (dif1.x*mx1.x + dif2.x*mn1.x)/(dif1.x + dif2.x);
		c11.g = (dif1.y*mx1.y + dif2.y*mn1.y)/(dif1.y + dif2.y);
		c11.b = (dif1.z*mx1.z + dif2.z*mn1.z)/(dif1.z + dif2.z);

		result = c11;
	}
	
	return result;
}
#endif
#if(Bilateral==1)


#define SIGMA 10.0
#define BSIGMA 0.1
#define MSIZE 15
float normpdf(float x, float sigma) {
	return 0.39894 * exp(-0.5 * x * x / (sigma * sigma)) / sigma;
}

float normpdf3(vec3 v, float sigma) {
	return 0.39894*exp(-0.5*dot(v,v)/(sigma*sigma))/sigma;
}

vec3 processBilateral(vec3 color) {
	const int kSize = (MSIZE-1)/2;
	float kernel[MSIZE];
	vec3 final_colour;

	// Create the 1-D kernel
	float Zxx = 0.0;
	for (int j = 0; j <= kSize; ++j) {
		kernel[kSize+j] = kernel[kSize-j] = normpdf(float(j), SIGMA);
	}
	// sigma 10.0, MSIZE 15
	//someone mentioned performance increase with precompiled kernel, but really don't see that
	//const float kernel[MSIZE] = float[MSIZE](0.031225216, 0.033322271, 0.035206333, 0.036826804, 0.038138565, 0.039104044, 0.039695028, 0.039894000, 0.039695028, 0.039104044, 0.038138565, 0.036826804, 0.035206333, 0.033322271, 0.031225216);
	vec3 cc;
	float factorNB;
	float bZ = 1.0/normpdf(0.0, BSIGMA);
	// Read out the texels
	for (int i=-kSize; i <= kSize; ++i) {
		for (int j=-kSize; j <= kSize; ++j) {
			cc = texture2D(sampler0, v_texcoord0.xy+vec2(float(i)*u_pixelDelta.x,float(j)*u_pixelDelta.x)).rgb;
			factorNB = normpdf3(cc-color, BSIGMA)*bZ*kernel[kSize+j]*kernel[kSize+i];
			Zxx += factorNB;
			final_colour += factorNB*cc;
		}
	}
	return final_colour/Zxx;
}
#endif
void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;
	#if(FXAA==1)
		color=processFXAA(color);
	#endif
	#if(GAUSS_SQ==1)
		color=processGAUSS_SQ(color);
	#endif
	#if(GAUSS_S==1)
		color=processGAUSS_S(color);
	#endif
	#if(Guest_AA4o==1)
		color=processGuestAA4o();
	#endif
	#if(SHARPEN==1)
		color=processSHARPEN(color);
	#endif
	#if(S_COM_V2==1)
		color=processS_COM_V2(color);
	#endif
	#if(xHQ==1)
		color=processxHQ(color);
	#endif
	#if(xBRAccuracy==1 || xBR==1)
		color=processxBR(color);
	#endif
	#if(Deband==1)
		color=processDeband(color);
	#endif
	#if(Frost==1)
		color=processFrost(color);
	#endif
	#if(Bilateral==1)
		color=processBilateral(color);
	#endif
	#if(NATURALC==1)
		color=processNATURALC(color);
	#endif
	#if(ACARTOON==1)
		color=processACARTOON(color);
	#endif
	#if(BLOOM==1)
		vec3 colorB=color;
		int hatsu = 0;
	#if(MIKU==1)
		hatsu = 1;
		color=processMIKU(color);
	#endif
		color=processBLOOM(color, colorB, hatsu);
	#endif
	#if(COLORED==1)
		color=processCOLORED(color);
	#endif
	#if(SHADEBOOST==1)
		color = processSHADEBOOST(color);
	#endif
	#if(GAMMA==1)
		color = processGAMMA(color);
	#endif
	#if(SCANLINES==1)
		color = processSCANLINES(color);
	#endif
	#if(LCD3x==1)
		color = processLCD3x(color);
	#endif
	#if(VIGNETTE==1)
		color = processVIGNETTE(color);
	#endif
	#if(TEST==1)
		color = processTEST(color);
	#endif
	#if(TESTANIM==1)
		color = processTESTANIM(color);
	#endif
	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
