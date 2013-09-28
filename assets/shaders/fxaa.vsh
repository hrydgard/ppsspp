attribute vec4 a_position;
attribute vec2 a_texcoord0;
uniform mat4 u_viewproj;
varying vec2 v_texcoord0;
void main() {
  v_texcoord0 = a_texcoord0;
  gl_Position = u_viewproj * a_position;
}
