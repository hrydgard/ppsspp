#ifdef BLEND_ALPHA
float4 premultiply_alpha(float4 c) { float a = clamp(c.a, 0.0, 1.0); return float4(c.rgb * a, a); }
float4 postdivide_alpha(float4 c) { return c.a < 0.001f? float4(0.0f,0.0f,0.0f,0.0f) : float4(c.rgb / c.a, c.a); }
#else
#define premultiply_alpha(c) (c)
#define postdivide_alpha(c) (c)
#endif

float c_df(float4 c1, float4 c2) {
	float3 df = abs(c1.rgb - c2.rgb);
	return df.r + df.g + df.b;
}
static const  float4 Ai  = float4( 1.0, -1.0, -1.0,  1.0);
static const  float4 B45 = float4( 1.0,  1.0, -1.0, -1.0);
static const  float4 C45 = float4( 1.5,  0.5, -0.5,  0.5);
static const  float4 M45 = float4(0.4, 0.4, 0.4, 0.4);
static const  float4 M30 = float4(0.2, 0.4, 0.2, 0.4);
static const  float3 lum = float3(0.21, 0.72, 0.07);

float lum_to(float4 v) {
  return dot(lum, v.rgb);
}
float4 lum_to(float4 v0, float4 v1, float4 v2, float4 v3) {
  return float4(lum_to(v0), lum_to(v1), lum_to(v2), lum_to(v3));
}


float4 tex_sample(float2 coord)
{
/*
	Mask for algorithm
	+-----+-----+-----+
	|     |  7  |     |
	+-----+-----+-----+
	| 11  | 12  | 13  |
	+-----+-----+-----+
	|     | 17  |     |
	+-----+-----+-----+
*/
// Store mask values
  float4 P07 = premultiply_alpha(tex_sample_direct(coord + u_texSize.zw * float2( 0.0, -1.0)));
  float4 P11 = premultiply_alpha(tex_sample_direct(coord + u_texSize.zw * float2(-1.0,  0.0)));
  float4 P12 = premultiply_alpha(tex_sample_direct(coord + u_texSize.zw * float2( 0.0,  0.0)));
  float4 P13 = premultiply_alpha(tex_sample_direct(coord + u_texSize.zw * float2( 1.0,  0.0)));
  float4 P17 = premultiply_alpha(tex_sample_direct(coord + u_texSize.zw * float2( 0.0,  1.0)));

  // Store luminance values of each point
  float4 p7  = lum_to(P07, P11, P17, P13);
  float4 p12 = lum_to(P12);
  float4 p13 = p7.wxyz; // P13, P7,  P11, P17
  float4 p17 = p7.zwxy; // P11, P17, P13, P7

  float2 fp = frac(coord * u_texSize.xy);
  float4 ma45 = smoothstep(C45 - M45, C45 + M45, Ai * fp.y + B45 * fp.x);
  float4 px = step(abs(p12 - p17), abs(p12 - p13));

  float4 res1 = P12;
  res1 = lerp(res1, lerp(P13, P17, px.x), ma45.x);
  res1 = lerp(res1, lerp(P07, P13, px.y), ma45.y);
  res1 = lerp(res1, lerp(P11, P07, px.z), ma45.z);
  res1 = lerp(res1, lerp(P17, P11, px.w), ma45.w);

  float4 res2 = P12;
  res2 = lerp(res2, lerp(P17, P11, px.w), ma45.w);
  res2 = lerp(res2, lerp(P11, P07, px.z), ma45.z);
  res2 = lerp(res2, lerp(P07, P13, px.y), ma45.y);
  res2 = lerp(res2, lerp(P13, P17, px.x), ma45.x);

  float4 res = lerp(res1, res2, step(c_df(P12, res1), c_df(P12, res2)));
  return postdivide_alpha(res);
}
