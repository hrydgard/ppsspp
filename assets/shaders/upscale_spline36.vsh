attribute vec4 a_position;
attribute vec2 a_texcoord0;
uniform mat4 u_viewproj;

varying vec2 v_position;

void main()
{
  gl_Position = u_viewproj * a_position;
      
  v_position = a_texcoord0;
}
