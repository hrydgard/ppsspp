// Simple vignette shader - darker towards the corners like in the unprocessed output of a real camera.

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

uniform vec4 u_setting;

void main() {
  vec2 diff = v_texcoord0 - 0.5;
  diff.x *= u_setting.y;
  diff.y /= u_setting.y;
  float vignette = 1.0 - min(1.0, u_setting.x * (dot(diff, diff) * 2.0));
  vec3 rgb = texture2D(sampler0, v_texcoord0.xy).xyz;
  rgb *= vignette;
  gl_FragColor.rgb = vignette * rgb;
  gl_FragColor.a = 1.0;
}
