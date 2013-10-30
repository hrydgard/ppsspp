// Simple sharpen shader; created to use in PPSSPP

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

float amount = 1.5;

void main()
{
  vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;
  color -= texture2D(sampler0, v_texcoord0.xy+0.0001).xyz*7.0*amount;
  color += texture2D(sampler0, v_texcoord0.xy-0.0001).xyz*7.0*amount;
  gl_FragColor.rgb = color;
}
