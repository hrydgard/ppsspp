
// generate the value of a Mitchell-Netravali scaling spline at distance d, with parameters A and B
// B=1 C=0   : cubic B spline (very smooth)
// B=C=1/3   : recommended for general upscaling
// B=0 C=1/2 : Catmull-Rom spline (sharp, ringing)
// see Mitchell & Netravali, \Reconstruction Filters in Computer Graphics\

//#define BSPLINE
#ifdef BSPLINE
  static const float B = 1.0f;
  static const float C = 0.0f;
#else
  static const float B = 1.0f / 3.0f;
  static const float C = 1.0f / 3.0f;
#endif

float mitchell_0_1(float x) {
	return ((12 - 9 * B - 6 * C)*(x*x*x) + (-18 + 12 * B + 6 * C)*(x*x) + (6 - 2 * B)) / 6.0f;
}

float mitchell_1_2(float x) {
	return ((-B - 6 * C)*(x*x*x) + (6 * B + 30 * C)*(x*x) + (-12 * B - 48 * C)*x + (8 * B + 24 * C)) / 6.0f;
}

float mitchell(float2 pos) {
  float x = sqrt(dot(pos, pos));
//  return lerp(mitchell_0_1(x), mitchell_1_2(x), step(1.0,x)) * step(x, 2.0);
  if (x < 1.0)
    return mitchell_0_1(x);
  if (x < 2.0)
    return mitchell_1_2(x);
  return 0.0;
}
float4 tex_sample(float2 coord) {
  float2 offset = frac(coord * u_texSize.xy) - 0.5;
  float4 tempColor = 0.0;
  float4 c;
  float i,j;
  float2 pos;
  for (i = -2.0; i < 3.0; i++){
    pos.x = offset.x - i;
    for (j = -2.0; j < 3.0 ;j++){
      pos.y = offset.y - j;
      c=premultiply_alpha(tex_sample_direct(coord - pos * u_texSize.zw));
      tempColor+=c*mitchell(pos);
    }
  }
  return postdivide_alpha(tempColor);
}
