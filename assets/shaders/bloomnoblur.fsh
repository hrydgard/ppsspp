// Simple Bloom shader; created to use in PPSSPP.
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
  vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;
  gl_FragColor.rgb = color;
  float gray = (color.r + color.g + color.b) / 3.0;
  float saturation = (abs(color.r - gray) + abs(color.g - gray) + abs(color.b - gray)) / 3.0;
  
  //show the effect mainly on bright parts of the screen
  float step = 0.001 * gray / max(saturation, 0.25) * u_setting.x;

  //sum the neightbor pixels
  vec3 sum = vec3(0);
  for (int x = -3; x <= 3; x += 2)
  {
    for (int y = -3; y <= 3; y += 2)
    {
      color = texture2D(sampler0, v_texcoord0 + vec2(x, y)*step).xyz;
      gray = (color.r + color.g + color.b) / 3.0;
      saturation = (abs(color.r - gray) + abs(color.g - gray) + abs(color.b - gray)) / 3.0;
      sum += color * gray * gray / max(saturation, 0.25);
    }
  }
  sum /= 16.0;

  //mix the color
  gl_FragColor.rgb += sum * u_setting.y;
  gl_FragColor.a = 1.0;
}
