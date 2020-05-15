// Simple sharpen shader; created to use in PPSSPP

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

uniform float u_setting1; float amount = 1.5;

void main()
{
  vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;
  color -= texture2D(sampler0, v_texcoord0.xy+0.0001).xyz*7.0*u_setting1;
  color += texture2D(sampler0, v_texcoord0.xy-0.0001).xyz*7.0*u_setting1;
  gl_FragColor.rgb = color;
}
