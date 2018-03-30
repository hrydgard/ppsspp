/*
   Hyllian's xBR-lv2 Shader

   Copyright (C) 2011-2015 Hyllian - sergiogdb@gmail.com

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons to whom the Software is furnished to do so, subject to the
   following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

   Incorporates some of the ideas from SABR shader. Thanks to Joshua Street.
*/

#define XBR_Y_WEIGHT          48.0 //  0.0 .. 100.0
#define XBR_EQ_THRESHOLD      15.0 //  0.0 ..  50.0
#define XBR_LV1_COEFFICIENT    0.5 //  0.0 ..  30.0
#define XBR_LV2_COEFFICIENT    2.0 //  1.0 ..   3.0
//#define SMALL_DETAILS

#define CORNER_TYPE 3 // 1 .. 4
#define XBR_SCALE 4.0

static const float coef           = 2.0;
static const float3  rgbw         = float3(14.352, 28.176, 5.472);
static const float4  eq_threshold = float4(15.0, 15.0, 15.0, 15.0);

static const float4 delta   = float4(1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE);
static const float4 delta_l = float4(0.5/XBR_SCALE, 1.0/XBR_SCALE, 0.5/XBR_SCALE, 1.0/XBR_SCALE);
static const float4 delta_u = delta_l.yxwz;

static const float4 Ao = float4( 1.0, -1.0, -1.0, 1.0 );
static const float4 Bo = float4( 1.0,  1.0, -1.0,-1.0 );
static const float4 Co = float4( 1.5,  0.5, -0.5, 0.5 );
static const float4 Ax = float4( 1.0, -1.0, -1.0, 1.0 );
static const float4 Bx = float4( 0.5,  2.0, -0.5,-2.0 );
static const float4 Cx = float4( 1.0,  1.0, -0.5, 0.0 );
static const float4 Ay = float4( 1.0, -1.0, -1.0, 1.0 );
static const float4 By = float4( 2.0,  0.5, -2.0,-0.5 );
static const float4 Cy = float4( 2.0,  0.0, -1.0, 0.5 );
static const float4 Ci = float4(0.25, 0.25, 0.25, 0.25);

static const float3 Y = float3(0.2126, 0.7152, 0.0722);

// Difference between vector components.
float4 df(float4 A, float4 B) { return abs(A-B); }

// Compare two vectors and return their components are different.
float4 diff(float4 A, float4 B) { return step(0.001, df(A, B)); }

// Determine if two vector components are equal based on a threshold.
float4 eq(float4 A, float4 B) { return (step(df(A, B), XBR_EQ_THRESHOLD)); }

// Determine if two vector components are NOT equal based on a threshold.
float4 neq(float4 A, float4 B) { return (1.0 - eq(A, B)); }

// Weighted distance.
float4 wd(float4 a, float4 b, float4 c, float4 d, float4 e, float4 f, float4 g, float4 h) {
  return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));
}

float4 weighted_distance(float4 a, float4 b, float4 c, float4 d, float4 e, float4 f, float4 g, float4 h, float4 i, float4 j, float4 k, float4 l) {
	return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + df(i,j) + df(k,l) + 2.0*df(g,h));
}

float c_df(float3 c1, float3 c2) {
  float3 df = abs(c1 - c2);
  return df.r + df.g + df.b;
}

float4 tex_sample(float2 coord) {
  float dx = u_texSize.z;
  float dy = u_texSize.w;

  float4 t1 = coord.xxxy + float4( -dx, 0, dx,-2.0*dy); // A1 B1 C1
  float4 t2 = coord.xxxy + float4( -dx, 0, dx,    -dy); //  A  B  C
  float4 t3 = coord.xxxy + float4( -dx, 0, dx,      0); //  D  E  F
  float4 t4 = coord.xxxy + float4( -dx, 0, dx,     dy); //  G  H  I
  float4 t5 = coord.xxxy + float4( -dx, 0, dx, 2.0*dy); // G5 H5 I5
  float4 t6 = coord.xyyy + float4(-2.0*dx,-dy, 0,  dy); // A0 D0 G0
  float4 t7 = coord.xyyy + float4( 2.0*dx,-dy, 0,  dy); // C4 F4 I4
  float4 edri, edr, edr_l, edr_u, px; // px = pixel, edr = edge detection rule
  float4 irlv0, irlv1, irlv2l, irlv2u, block_3d;
  float4 fx, fx_l, fx_u; // inequations of straight lines.

  float2 fp  = frac(coord*u_texSize.xy);

  float4 A1 = premultiply_alpha(tex_sample_direct(t1.xw));
  float4 B1 = premultiply_alpha(tex_sample_direct(t1.yw));
  float4 C1 = premultiply_alpha(tex_sample_direct(t1.zw));
  float4 A  = premultiply_alpha(tex_sample_direct(t2.xw));
  float4 B  = premultiply_alpha(tex_sample_direct(t2.yw));
  float4 C  = premultiply_alpha(tex_sample_direct(t2.zw));
  float4 D  = premultiply_alpha(tex_sample_direct(t3.xw));
  float4 E  = premultiply_alpha(tex_sample_direct(t3.yw));
  float4 F  = premultiply_alpha(tex_sample_direct(t3.zw));
  float4 G  = premultiply_alpha(tex_sample_direct(t4.xw));
  float4 H  = premultiply_alpha(tex_sample_direct(t4.yw));
  float4 I  = premultiply_alpha(tex_sample_direct(t4.zw));
  float4 G5 = premultiply_alpha(tex_sample_direct(t5.xw));
  float4 H5 = premultiply_alpha(tex_sample_direct(t5.yw));
  float4 I5 = premultiply_alpha(tex_sample_direct(t5.zw));
  float4 A0 = premultiply_alpha(tex_sample_direct(t6.xy));
  float4 D0 = premultiply_alpha(tex_sample_direct(t6.xz));
  float4 G0 = premultiply_alpha(tex_sample_direct(t6.xw));
  float4 C4 = premultiply_alpha(tex_sample_direct(t7.xy));
  float4 F4 = premultiply_alpha(tex_sample_direct(t7.xz));
  float4 I4 = premultiply_alpha(tex_sample_direct(t7.xw));

  float4 b  = float4(dot(B.rgb ,rgbw), dot(D.rgb ,rgbw), dot(H.rgb ,rgbw), dot(F.rgb ,rgbw));
  float4 c  = float4(dot(C.rgb ,rgbw), dot(A.rgb ,rgbw), dot(G.rgb ,rgbw), dot(I.rgb ,rgbw));
  float4 d  = b.yzwx;
  float4 e  = dot(E.rgb,rgbw);
  float4 f  = b.wxyz;
  float4 g  = c.zwxy;
  float4 h  = b.zwxy;
  float4 i  = c.wxyz;

  float4 i4, i5, h5, f4;

  float y_weight = XBR_Y_WEIGHT;
#ifdef SMALL_DETAILS
  i4 = mul(float4x3(I4.rgb, C1.rgb, A0.rgb, G5.rgb), y_weight * Y);
  i5 = mul(float4x3(I5.rgb, C4.rgb, A1.rgb, G0.rgb), y_weight * Y);
  h5 = mul(float4x3(H5.rgb, F4.rgb, B1.rgb, D0.rgb), y_weight * Y);
#else
  i4 = float4(dot(I4.rgb,rgbw), dot(C1.rgb,rgbw), dot(A0.rgb,rgbw), dot(G5.rgb,rgbw));
  i5 = float4(dot(I5.rgb,rgbw), dot(C4.rgb,rgbw), dot(A1.rgb,rgbw), dot(G0.rgb,rgbw));
  h5 = float4(dot(H5.rgb,rgbw), dot(F4.rgb,rgbw), dot(B1.rgb,rgbw), dot(D0.rgb,rgbw));
#endif
  f4 = h5.yzwx;

  // These inequations define the line below which interpolation occurs.
  fx   = (Ao*fp.y+Bo*fp.x);
  fx_l = (Ax*fp.y+Bx*fp.x);
  fx_u = (Ay*fp.y+By*fp.x);

  irlv1 = irlv0 = diff(e,f) * diff(e,h);

#if CORNER_TYPE == 1
#define SMOOTH_TIPS
#elif CORNER_TYPE == 2
  irlv1      = (irlv0 * ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) );
#elif CORNER_TYPE == 3
  irlv1     = (irlv0  * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );
#else // CORNER_TYPE == 4
  float4 c1 = i4.yzwx;
  float4 g0 = i5.wxyz;
  irlv1     = (irlv0  *  ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) * (diff(f,f4) * diff(f,i) + diff(h,h5) * diff(h,i) + diff(h,g) + diff(f,c) + eq(b,c1) * eq(d,g0)));
#endif

  irlv2l = diff(e,g) * diff(d,g);
  irlv2u = diff(e,c) * diff(b,c);

  float4 fx45i = clamp((fx   + delta   -Co - Ci)/(2.0*delta  ), 0.0, 1.0);
  float4 fx45  = clamp((fx   + delta   -Co     )/(2.0*delta  ), 0.0, 1.0);
  float4 fx30  = clamp((fx_l + delta_l -Cx     )/(2.0*delta_l), 0.0, 1.0);
  float4 fx60  = clamp((fx_u + delta_u -Cy     )/(2.0*delta_u), 0.0, 1.0);

  float4 wd1, wd2;
#ifdef SMALL_DETAILS
  wd1 = weighted_distance( e, c, g, i, f4, h5, h, f, b, d, i4, i5);
  wd2 = weighted_distance( h, d, i5, f, b, i4, e, i, g, h5, c, f4);
#else
  wd1 = wd( e, c,  g, i, h5, f4, h, f);
  wd2 = wd( h, d, i5, f, i4,  b, e, i);
#endif

  edri  = step(wd1, wd2) * irlv0;
  edr   = step(wd1 + float4(0.1, 0.1, 0.1, 0.1), wd2) * step(float4(0.5, 0.5, 0.5, 0.5), irlv1);
  edr_l = step( XBR_LV2_COEFFICIENT*df(f,g), df(h,c) ) * irlv2l * edr;
  edr_u = step( XBR_LV2_COEFFICIENT*df(h,c), df(f,g) ) * irlv2u * edr;

  fx45  = edr   * fx45;
  fx30  = edr_l * fx30;
  fx60  = edr_u * fx60;
  fx45i = edri  * fx45i;

  px = step(df(e,f), df(e,h));

#ifdef SMOOTH_TIPS
  float4 maximos = max(max(fx30, fx60), max(fx45, fx45i));
#else
  float4 maximos = max(max(fx30, fx60), fx45);
#endif

  float4 res1 = E;
  res1 = lerp(res1, lerp(H, F, px.x), maximos.x);
  res1 = lerp(res1, lerp(B, D, px.z), maximos.z);

  float4 res2 = E;
  res2 = lerp(res2, lerp(F, B, px.y), maximos.y);
  res2 = lerp(res2, lerp(D, H, px.w), maximos.w);

  float4 res = lerp(res1, res2, step(c_df(E.rgb, res1.rgb), c_df(E.rgb, res2.rgb)));
  return postdivide_alpha(res);
}
