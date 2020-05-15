// Color correction

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

uniform float u_setting1; // brightness - default: 1.0, range = 0.1 - 2.0
uniform float u_setting2; // saturation - default: 1.0, range = 0.0 - 2.0
uniform float u_setting3; // contrast - default: 1.0, range = 0.1 - 3.0
uniform float u_setting4; // gamma - default: 1.0, range = 0.1 - 2.0

void main()
{
  vec3 rgb = texture2D( sampler0, v_texcoord0 ).xyz;
  rgb = vec3(mix(vec3(dot(rgb, vec3(0.299, 0.587, 0.114))), rgb, u_setting2));
  rgb = (rgb-0.5)*u_setting3+0.5+u_setting1-1.0;
  gl_FragColor.rgb = pow(rgb, vec3(1.0/u_setting4));
  gl_FragColor.a = 1.0;
}
