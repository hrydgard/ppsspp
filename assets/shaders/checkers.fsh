
#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;

// The inverse of the texture dimensions along X and Y
uniform vec2 u_texelDelta;
varying vec2 v_texcoord0;

void main() {
  vec2 coord = v_texcoord0 / u_texelDelta;
  int x = int(floor(coord.x));
  int y = int(floor(coord.y));

  int a = (x ^ y) & 1;

  gl_FragColor.xyz = vec3(float(a));
  gl_FragColor.a = 1.0;
}
