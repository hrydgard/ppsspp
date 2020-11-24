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

#define CornerA      0     	//ON:1/OFF:0 / A, B, C, D are just different variants of corner rounding
#define CornerB      0     	//ON:1/OFF:0 / activate only one
#define CornerD      0     	//ON:1/OFF:0
//	CornerC                 //used as default if none of the above is defined

const float XBR_SCALE = 3.0;
const float lv2_cf    = 2.0;

const float coef          = 2.0;
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

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
varying vec2 v_texcoord0;

// Difference between vector components.
vec4 df(vec4 A, vec4 B) {
	return vec4(abs(A-B));
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

void main() {

 bool upscale = u_texelDelta.x > (1.6 * u_pixelDelta.x);
 vec3 res = texture2D(sampler0, v_texcoord0.xy).xyz;

 // Let's skip the whole scaling if output size smaller than 1.6x of input size
 if (upscale) {

	vec4 edri, edr, edr_l, edr_u, px; // px = pixel, edr = edge detection rule
	vec4 irlv0, irlv1, irlv2l, irlv2u;
	vec4 fx, fx_l, fx_u; // inequations of straight lines.

	vec2 pS  = 1.0 / u_texelDelta.xy;
	vec2 fp  = fract(v_texcoord0.xy*pS.xy);
	vec2 TexCoord_0 = v_texcoord0.xy-fp*u_pixelDelta.xy;
	vec2 dx  = vec2(u_texelDelta.x,0.0);
	vec2 dy  = vec2(0.0,u_texelDelta.y);
	vec2 y2  = dy + dy; vec2 x2  = dx + dx;

	vec4 delta   = vec4(1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE);
	vec4 delta_l = vec4(0.5/XBR_SCALE, 1.0/XBR_SCALE, 0.5/XBR_SCALE, 1.0/XBR_SCALE);
	vec4 delta_u = delta_l.yxwz;

	vec3 A  = texture2D(sampler0, TexCoord_0 -dx -dy	).xyz;
	vec3 B  = texture2D(sampler0, TexCoord_0	 -dy	).xyz;
	vec3 C  = texture2D(sampler0, TexCoord_0 +dx -dy	).xyz;
	vec3 D  = texture2D(sampler0, TexCoord_0 -dx		).xyz;
	vec3 E  = texture2D(sampler0, TexCoord_0		).xyz;
	vec3 F  = texture2D(sampler0, TexCoord_0 +dx		).xyz;
	vec3 G  = texture2D(sampler0, TexCoord_0 -dx +dy	).xyz;
	vec3 H  = texture2D(sampler0, TexCoord_0	 +dy	).xyz;
	vec3 I  = texture2D(sampler0, TexCoord_0 +dx +dy	).xyz;
	vec3 A1 = texture2D(sampler0, TexCoord_0	 -dx -y2).xyz;
	vec3 C1 = texture2D(sampler0, TexCoord_0	 +dx -y2).xyz;
	vec3 A0 = texture2D(sampler0, TexCoord_0 -x2	     -dy).xyz;
	vec3 G0 = texture2D(sampler0, TexCoord_0 -x2	     +dy).xyz;
	vec3 C4 = texture2D(sampler0, TexCoord_0 +x2	     -dy).xyz;
	vec3 I4 = texture2D(sampler0, TexCoord_0 +x2	     +dy).xyz;
	vec3 G5 = texture2D(sampler0, TexCoord_0	 -dx +y2).xyz;
	vec3 I5 = texture2D(sampler0, TexCoord_0	 +dx +y2).xyz;
	vec3 B1 = texture2D(sampler0, TexCoord_0	     -y2).xyz;
	vec3 D0 = texture2D(sampler0, TexCoord_0 -x2		).xyz;
	vec3 H5 = texture2D(sampler0, TexCoord_0	     +y2).xyz;
	vec3 F4 = texture2D(sampler0, TexCoord_0 +x2		).xyz;

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
	if (select1==0) {
		vec4 c1 = i4.yzwx;
		vec4 g0 = i5.wxyz;
		irlv1   = (irlv0  *  ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) * (diff(f,f4) * diff(f,i) + diff(h,h5) * diff(h,i) + diff(h,g) + diff(f,c) + eq(b,c1) * eq(d,g0)));
	}
#endif
#endif
	irlv2l = diff(e,g) * diff(d,g);
	irlv2u = diff(e,c) * diff(b,c);

	vec4 fx45i = clamp((fx   + delta   -Co - Ci) / (2.0*delta  ), 0.0, 1.0);
	vec4 fx45  = clamp((fx   + delta   -Co     ) / (2.0*delta  ), 0.0, 1.0);
	vec4 fx30  = clamp((fx_l + delta_l -Cx     ) / (2.0*delta_l), 0.0, 1.0);
	vec4 fx60  = clamp((fx_u + delta_u -Cy     ) / (2.0*delta_u), 0.0, 1.0);
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
 }
	gl_FragColor.xyz = res;
	gl_FragColor.a = 1.0;
}
