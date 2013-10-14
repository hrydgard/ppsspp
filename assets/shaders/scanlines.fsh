// Simple Scanlines shader, created to use in PPSSPP.
// Looks good at Internal resolution same as viewport.

#define PI 3.14159

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

float offset = 1.0;
float frequency = 166;

void main()
{
  float pos0 = (v_texcoord0.y + offset) * frequency;
  float pos1 = cos((fract( pos0 ) - 0.5)*PI);
  vec4 pel = texture2D( sampler0, v_texcoord0 );

  gl_FragColor = mix(vec4(0,0,0,0), pel, pos1);
}
