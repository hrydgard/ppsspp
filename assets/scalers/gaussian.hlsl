#ifdef BLEND_ALPHA
float4 premultiply_alpha(float4 c) { float a = clamp(c.a, 0.0, 1.0); return float4(c.rgb * a, a); }
float4 postdivide_alpha(float4 c) { return c.a < 0.001f? float4(0.0f,0.0f,0.0f,0.0f) : float4(c.rgb / c.a, c.a); }
#else
#define premultiply_alpha(c) (c)
#define postdivide_alpha(c) (c)
#endif

#define sharpness 1.0
#define pi 3.14159265358
#define normalGauss(x) ((exp(-(x)*(x)*0.5))/sqrt(2.0*pi))
#define normalGauss2(x) (normalGauss(x - 0.5) - 0.5)
float normalGaussIntegral(float x)
{
	 float a1 = 0.4361836;
	 float a2 = -0.1201676;
	 float a3 = 0.9372980;
	 float p = 0.3326700;
	 float t = 1.0 / (1.0 + p*abs(x));

	 return (0.5-normalGauss(x) * (t*(a1 + t*(a2 + a3*t))))*sign(x);
}
#define KERNEL(x,b) (normalGaussIntegral(sqrt(2*pi)*b*(x - 0.5)) - normalGaussIntegral(sqrt(2*pi)*b*(x + 0.5)))

float4 tex_sample(float2 coord) {
  float2 offset = frac(coord * u_texSize.xy) - 0.5;
  float4 tempColor = 0.0;
  float4 c;
  float i,j;
  float2 pos;
  for (i = -2.0; i < 2.0; i++){
    pos.x = offset.x - i;
    for (j = -2.0; j< 2.0 ;j++){
      pos.y = offset.y - j;
      c=premultiply_alpha(tex_sample_direct(coord - pos * u_texSize.zw));
      tempColor+=c*KERNEL(pos.x,sharpness)*KERNEL(pos.y,sharpness);
    }
  }
  return postdivide_alpha(tempColor);
}
