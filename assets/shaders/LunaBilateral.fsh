//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//DEBANDING FILTERS:		//especially useful for upscaling filters like 5xBR as well as texture scaling like xBRZ, use only one
//================
//#define Bilateral    0     	//ON:1/OFF:0 /Nice, but very expensive, made by mrharicot ~ https://www.shadertoy.com/view/4dfGDH
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//DEBANDING FILTERS:		//especially useful for upscaling filters like 5xBR as well as texture scaling like xBRZ, use only one
//================
//#define Bilateralv   0     	//ON:1/OFF:0 /Nice, but very expensive, made by mrharicot ~ https://www.shadertoy.com/view/4dfGDH
//================
//Approximate performance hit
//Free: 
//Light: 
//Medium: Deband, Frost(fSimple)
//Heavy: Frost
//Extremely Heavy: Bilateral
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


#define SIGMA 10.0
#define BSIGMA 0.1
#define MSIZE 15
float normpdf(float x, float sigma) {
	float vulkanWorkaround = 0.39894;
	if (u_setting.y == 1.0) {
		vulkanWorkaround = 0.39894*10.0;
	}
	return vulkanWorkaround * exp(-0.5 * x * x / (sigma * sigma)) / sigma;
}

float normpdf3(vec3 v, float sigma) {
	float vulkanWorkaround = 0.39894;
	if (u_setting.y == 1.0) {
		vulkanWorkaround = 0.39894*10.0;
	}
	return vulkanWorkaround*exp(-0.5*dot(v,v)/(sigma*sigma))/sigma;
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

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processBilateral(color);
	}

	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
