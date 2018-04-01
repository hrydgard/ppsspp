// PPSSPP: Simple Gauss filter
// Made by Bigpet

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;

// The inverse of the texture dimensions along X and Y
uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
varying vec2 v_texcoord0;

void main() {
  // The parameters are hardcoded for now, but could be
  // made into uniforms to control fromt he program.
  float GAUSS_SPAN_MAX = 1.5;

  //just a variable to describe the maximu
  float GAUSS_KERNEL_SIZE = 5.0;
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

  vec2 offset = u_pixelDelta*GAUSS_SPAN_MAX/GAUSS_KERNEL_SIZE;
  
  vec3 rgbSimple0 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0,-2.0)).xyz;
  vec3 rgbSimple1 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0,-1.0)).xyz;
  vec3 rgbSimple2 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0,-1.0)).xyz;
  vec3 rgbSimple3 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0,-1.0)).xyz;
  vec3 rgbSimple4 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-2.0, 0.0)).xyz;
  vec3 rgbSimple5 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 0.0)).xyz;
  vec3 rgbSimple6 = 10.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 0.0)).xyz;
  vec3 rgbSimple7 =  8.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 0.0)).xyz;
  vec3 rgbSimple8 =  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 2.0, 0.0)).xyz;
  vec3 rgbSimple9 =  3.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2(-1.0, 1.0)).xyz;
  vec3 rgbSimple10=  8.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 1.0)).xyz;
  vec3 rgbSimple11=  3.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 1.0, 1.0)).xyz;
  vec3 rgbSimple12=  1.0 * texture2D(sampler0, v_texcoord0.xy + offset * vec2( 0.0, 2.0)).xyz;
  //vec3 rgbSimple10= vec3(1.0,0.0,0.0);
  //vec3 rgbSimple11= vec3(1.0,0.0,0.0);
  //vec3 rgbSimple12= vec3(1.0,0.0,0.0);
  
  vec3 rgb =  rgbSimple0 + 
              rgbSimple1 +
              rgbSimple2 +
              rgbSimple3 +
              rgbSimple4 +
              rgbSimple5 +
              rgbSimple6 +
              rgbSimple7 +
              rgbSimple8 +
              rgbSimple9 +
              rgbSimple10 +
              rgbSimple11 +
              rgbSimple12;
  rgb = rgb / 58.0;
  gl_FragColor.xyz=rgb;
  gl_FragColor.a = 1.0;
}

