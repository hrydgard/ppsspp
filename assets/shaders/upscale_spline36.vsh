attribute vec4 a_position;
attribute vec2 a_texcoord0;

varying vec2 v_position;

void main()
{
  gl_Position = a_position;
      
  v_position = a_texcoord0;
}
