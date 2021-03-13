//Note  : Recommend to use PPSSPP with chain shaders for full functionality.
// This shader has 2 sets of configs, separate for in-game and video
//======================================================================================================================================================================
//=======================================================================================
//========================================IN-GAME========================================
//=======================================================================================
//UPSCALING FILTERS:		//To use those, you have to set rendering res to smaller than window/display size(x1 for best results) and screen scaling filter to "nearest"
				//Starting from v1.1.1-28-g70e9979 you can also add Upscaling=True to ini file(check example) to do it automatically
//================
//#define xBR          0     	//ON:1/OFF:0 /5xBR upscale, nice for 2D games especially those that might be buggy with higher rendering res, initially made by Hyllian - license below
//#define VariantB     0     	//ON:1/OFF:0 /slightly less effect on fonts, dots and other small details
//#define Variantx2    0     	//ON:1/OFF:0 /2xBR aka more blurry version of 5xBR
//================
//#define xBRAccuracy  1     	//ON:1/OFF:0 / Hyllian's xBR-lv2 Shader Accuracy (tweak by guest.r) ~ copy of the full license below
//#define CornerA      0     	//ON:1/OFF:0 / A, B, C, D are just different variants of corner rounding
//#define CornerB      0     	//ON:1/OFF:0 / activate only one
//#define CornerD      0     	//ON:1/OFF:0
//	CornerC            	//used as default if no other ones are defined
//=======================================================================================
//=========================================VIDEO=========================================
//=======================================================================================
//UPSCALING FILTERS:		//To use those, you have to set rendering res to smaller than window/display size(x1 for best results) and screen scaling filter to "nearest"
				//Starting from v1.1.1-28-g70e9979 you can also add Upscaling=True to ini file(check example) to do it automatically
//================
//#define xBRv         0     	//ON:1/OFF:0 /5xBR upscale, nice for 2D games especially those that might be buggy with higher rendering res, initially made by Hyllian - license below
//#define VariantBv    0     	//ON:1/OFF:0 /slightly less effect on fonts, dots and other small details
//#define Variantx2v   0     	//ON:1/OFF:0 /2xBR aka more blurry version of 5xBR
//================
//#define xBRAccuracyv 1     	//ON:1/OFF:0 / Hyllian's xBR-lv2 Shader Accuracy (tweak by guest.r) ~ copy of the full license below
//#define CornerAv     0     	//ON:1/OFF:0 / A, B, C, D are just different variants of corner rounding
//#define CornerBv     0     	//ON:1/OFF:0 / activate only one
//#define CornerDv     0     	//ON:1/OFF:0
//	CornerCv           	//used as default if no other ones are defined
//================
//Approximate performance hit
//Free: 
//Light: 
//Medium: xHQ
//Heavy: xBR, xBRAccuracy
//Extremely Heavy:
//======================================================================================================================================================================
//~packed together, corrected to fit requirements of popular with PPSSPP AMD legacy drivers v11.11(if it works on that, it will on almost anything lol),
// currently meet requirements of GLSL version 100 meaning it will probably run on anything unless the driver is horribly broken
// partially written, ported to glsl where required and tested by LunaMoo (Moon Cow?;p),
// other credits mentioned earlier, any more info / required licenses can be found below.



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

const float lv2_cf = 2.0;
const vec4 Ci = vec4(0.25, 0.25, 0.25, 0.25); 
const float XBR_SCALE = 3.0;
const vec4  eq_threshold  = vec4(15.0, 15.0, 15.0, 15.0); 
const float coef = 2.0;

const vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );
const vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );
const vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );
const vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );
const vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );
const vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );
const vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );


vec4 df(vec4 A, vec4 B) {
	return abs(A-B);
}


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

vec3 processxBR(vec3 color){
	vec2 pS  = 1.0 / u_texelDelta.xy;
	vec2 fp  = fract(v_texcoord0.xy*pS.xy);
	vec2 TexCoord_0 = v_texcoord0.xy-fp*u_pixelDelta.xy;
	vec2 dx  = vec2(u_texelDelta.x,0.0);
	vec2 dy  = vec2(0.0,u_texelDelta.y);
	vec2 y2  = dy + dy; vec2 x2  = dx + dx;

	// px = pixel, edr = edge detection rule
	vec4 edri, edr, edr_l, edr_u, px;
	vec4 irlv0, irlv1, irlv2l, irlv2u;
	vec4 fx, fx_l, fx_u; // inequations of straight lines.
	
	vec4 delta   = vec4(1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE);
	vec4 delta_l = vec4(0.5/XBR_SCALE, 1.0/XBR_SCALE, 0.5/XBR_SCALE, 1.0/XBR_SCALE);
	vec4 delta_u = delta_l.yxwz;

	bvec4 edrBR, edr_left, edr_up, pxBR; // px = pixel, edr = edge detection rule
	bvec4 interp_restriction_lv1, interp_restriction_lv2_left, interp_restriction_lv2_up;
	bvec4 nc; // new_color
	bvec4 fxBR, fx_left, fx_up; // inequations of straight lines.

	vec3 rgbw = vec3(16.163, 23.351, 8.4772);
	if(u_setting.y==2.0) {
		rgbw = vec3(14.352, 28.176, 5.472);
	}

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

	vec3 res;

if(u_setting.y==2.0) {
	// These inequations define the line below which interpolation occurs.
	fx   = (Ao*fp.y+Bo*fp.x); 
	fx_l = (Ax*fp.y+Bx*fp.x);
	fx_u = (Ay*fp.y+By*fp.x);

	irlv1 = irlv0 = diff(e,f) * diff(e,h);

if(u_setting.z!=1.0) { // A takes priority skipping other corners
	#define SMOOTH_TIPS
	// Corner C also default if no other ones used
	irlv1   = (irlv0  * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );
	int select1 = 0;
if(u_setting.z==2.0) {// Corner B
	irlv1   = (irlv0 * ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) );
	select1 = 1;
}
if(u_setting.z==4.0) { // Corner D
	if (select1==0){
		vec4 c1 = i4.yzwx;
		vec4 g0 = i5.wxyz;
		irlv1   = (irlv0  *  ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) * (diff(f,f4) * diff(f,i) + diff(h,h5) * diff(h,i) + diff(h,g) + diff(f,c) + eq(b,c1) * eq(d,g0)));
	}
}
} //Corner A if end

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

	res = mix(res1, res2, step(c_df(E, res1), c_df(E, res2)));
} else { //Version 1
	// These inequations define the line below which interpolation occurs.
	fxBR      = greaterThan(Ao*fp.y+Bo*fp.x,Co); 
	fx_left = greaterThan(Ax*fp.y+Bx*fp.x,Cx);
	fx_up   = greaterThan(Ay*fp.y+By*fp.x,Cy);

	if (u_setting.z==2.0){
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

	edrBR      = bvec4(vec4(lessThan(weighted_distance( e, c, g, i, h5, f4, h, f), weighted_distance( h, d, i5, f, i4, b, e, i)))*vec4(interp_restriction_lv1));
	edr_left = bvec4(vec4(lessThanEqual(coef*df(f,g),df(h,c)))*vec4(interp_restriction_lv2_left)); 
	edr_up   = bvec4(vec4(greaterThanEqual(df(f,g),coef*df(h,c)))*vec4(interp_restriction_lv2_up));
	
	nc.x = ( edrBR.x && (fxBR.x || edr_left.x && fx_left.x || edr_up.x && fx_up.x) );
	nc.y = ( edrBR.y && (fxBR.y || edr_left.y && fx_left.y || edr_up.y && fx_up.y) );
	nc.z = ( edrBR.z && (fxBR.z || edr_left.z && fx_left.z || edr_up.z && fx_up.z) );
	nc.w = ( edrBR.w && (fxBR.w || edr_left.w && fx_left.w || edr_up.w && fx_up.w) );

	pxBR = lessThanEqual(df(e,f),df(e,h));


	if (u_setting.z==4.0) {
		vec3 E0 = E.xyz;
		vec3 E1 = E.xyz;
		vec3 E2 = E.xyz;
		vec3 E3 = E.xyz;
		vec3 P[4];
		P[0] = pxBR.x ? F : H;
		P[1] = pxBR.y ? B : F;
		P[2] = pxBR.z ? D : B;
		P[3] = pxBR.w ? H : D;
		if (edrBR.x) {
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
		if (edrBR.y) {
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
		if (edrBR.z) {
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
		if (edrBR.w) {
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
		res = nc.x ? pxBR.x ? F : H : nc.y ? pxBR.y ? B : F : nc.z ? pxBR.z ? D : B : nc.w ? pxBR.w ? H : D : E;
	}
}
	return res;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;
	
	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processxBR(color);
	}
	
	gl_FragColor.xyz=color;
	gl_FragColor.a = 1.0;
}
