// Simple fake reflection shader; created to use in PPSSPP.
// Without excessive samples and complex nested loops
// (to make it compatible with low-end GPUs and to ensure ps_2_0 compatibility).

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;	

uniform vec4 u_setting;

void main()
{
  //get the pixel color
  vec3 color = texture2D(sampler0, v_texcoord0.xy).rgb;
  float gray = (color.r + color.g + color.b) / 3.0;
  float saturation = (abs(color.r - gray) + abs(color.g - gray) + abs(color.b - gray)) / 3.0;

  //persistent random offset to hide that the reflection is wrong
  float rndx = mod(v_texcoord0.x + gray, 0.03) + mod(v_texcoord0.y + saturation, 0.05);
  float rndy = mod(v_texcoord0.y + saturation, 0.03) + mod(v_texcoord0.x + gray, 0.05);

  //show the effect mainly on the bottom part of the screen
  float step = (max(gray, saturation) + 0.1) * v_texcoord0.y;

  //the fake reflection is just a watered copy of the frame moved slightly lower
  vec3 reflection = texture2D(sampler0, v_texcoord0 + vec2(rndx, rndy - min(v_texcoord0.y, 0.25)) * step).rgb;

  //apply parameters and mix the colors
  reflection *= 4.0 * (1.0 - gray) * u_setting.x;
  reflection *= reflection * step * u_setting.y;
  gl_FragColor = vec4(color + reflection, 1.0);
}
