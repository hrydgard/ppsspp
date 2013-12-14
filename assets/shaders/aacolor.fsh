// AA-Color shader, Modified to use in PPSSPP. Grabbed from:
// http://forums.ngemu.com/showthread.php?t=76098

// by guest(r) (guest.r@gmail.com)
// license: GNU-GPL

// Color variables

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

const vec3 c_ch = vec3(1.0,1.0,1.0);  //  rgb color channel intensity
const float   a = 1.20 ;              //  saturation 
const float   b = 1.00 ;              //  brightness 
const float   c = 1.25 ;              //  contrast   

// you can use contrast1,contrast2...contrast4 (or contrast0 for speedup)

float contrast0(float x)
{ return x; }

float contrast1(float x)
{ x = x*1.1547-1.0;
  return sign(x)*pow(abs(x),1.0/c)*0.86 +  0.86;}

float contrast2(float x) 
{ return normalize(vec2(pow(x,c),pow(0.86,c))).x*1.72;}

float contrast3(float x)
{ return 1.73*pow(0.57735*x,c); }

float contrast4(float x)
{ return clamp(0.866 + c*(x-0.866),0.05, 1.73); }

uniform sampler2D sampler0;

varying vec4 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;
varying vec4 v_texcoord4;
varying vec4 v_texcoord5;
varying vec4 v_texcoord6;

void main()
{
  vec3 c10 = texture2D(sampler0, v_texcoord1.xy).xyz; 
  vec3 c01 = texture2D(sampler0, v_texcoord4.xy).xyz; 
  vec3 c11 = texture2D(sampler0, v_texcoord0.xy).xyz; 
  vec3 c21 = texture2D(sampler0, v_texcoord5.xy).xyz; 
  vec3 c12 = texture2D(sampler0, v_texcoord2.xy).xyz; 

  vec3 dt = vec3(1.0,1.0,1.0);
  float k1=dot(abs(c01-c21),dt);
  float k2=dot(abs(c10-c12),dt);

  vec3 color = (k1*(c10+c12)+k2*(c01+c21)+0.001*c11)/(2.0*(k1+k2)+0.001);

  float x = sqrt(dot(color,color));

  color.r = pow(color.r+0.001,a);
  color.g = pow(color.g+0.001,a);
  color.b = pow(color.b+0.001,a);

  gl_FragColor.rgb = contrast4(x)*normalize(color*c_ch)*b;
  gl_FragColor.a = 1.0;
}
