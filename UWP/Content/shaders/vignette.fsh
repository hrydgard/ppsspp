// Simple vignette shader - darker towards the corners like in the unprocessed output of a real camera.

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

void main() {
  float vignette = 1.1 - 0.6 * (dot(v_texcoord0 - 0.5, v_texcoord0 - 0.5) * 2.0);
  vec3 rgb = texture2D(sampler0, v_texcoord0.xy).xyz;
  gl_FragColor.rgb = vignette * rgb;
  gl_FragColor.a = 1.0;
}
