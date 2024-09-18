#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
uniform sampler2D sampler2;
uniform vec4 u_setting;
varying vec2 v_texcoord0;

void main() {
  gl_FragColor.rgb = mix(texture2D(sampler0, v_texcoord0.xy).rgb, texture2D(sampler2, v_texcoord0.xy).rgb, u_setting.x);
  gl_FragColor.a = 1.0;
}
