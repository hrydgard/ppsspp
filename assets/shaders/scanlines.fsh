// Advanced Scanlines (CRT) shader, created to use in PPSSPP.

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

float amount = 1.0; // suitable range = 0.0 - 1.0
float intensity = 0.5; // suitable range = 0.0 - 1.0

void main()
{
  float pos0 = ((v_texcoord0.y + 1.0) * 170.0*amount);
  float pos1 = cos((fract( pos0 ) - 0.5)*3.1415926*intensity)*1.5;
  vec4 rgb = texture2D( sampler0, v_texcoord0 );
  
  // slight contrast curve
  vec4 color = rgb*0.5+0.5*rgb*rgb*1.2;
  
  // color tint
  color *= vec4(0.9,1.0,0.7, 0.0);
  
  // vignette
  color *= 1.1 - 0.6 * (dot(v_texcoord0 - 0.5, v_texcoord0 - 0.5) * 2.0);

  gl_FragColor.rgba = mix(vec4(0,0,0,0), color, pos1);
}
