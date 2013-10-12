// Simple grayscale shader

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

void main() {
  vec3 rgb = texture2D(sampler0, v_texcoord0.xy).xyz;
  float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
  gl_FragColor.rgb = vec3(luma, luma, luma);
  gl_FragColor.a = 1.0;
}
