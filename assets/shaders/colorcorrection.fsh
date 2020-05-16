// Color correction

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

uniform vec4 u_setting;

void main()
{
  vec3 rgb = texture2D( sampler0, v_texcoord0 ).xyz;
  rgb = vec3(mix(vec3(dot(rgb, vec3(0.299, 0.587, 0.114))), rgb, u_setting.y));
  rgb = (rgb-0.5)*u_setting.z+0.5+u_setting.x-1.0;
  gl_FragColor.rgb = pow(rgb, vec3(1.0/u_setting.w));
  gl_FragColor.a = 1.0;
}
