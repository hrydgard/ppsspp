// Simple false color shader

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

void main() {
  vec3 rgb = texture2D(sampler0, v_texcoord0.xy).xyz;
  float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
  vec3 gray = vec3(luma, luma, luma) - 0.5;
  rgb -= vec3(0.5, 0.5, 0.5);

  gl_FragColor.rgb = mix(rgb, gray, 2.0) + 0.5;
  gl_FragColor.a = 1.0;
}
