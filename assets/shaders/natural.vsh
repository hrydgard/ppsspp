uniform vec4 u_texcoordDelta;

attribute vec4 a_position;
attribute vec2 a_texcoord0;
uniform mat4 u_viewproj;

varying vec4 v_texcoord0;
varying vec4 v_texcoord1;
varying vec4 v_texcoord2;
varying vec4 v_texcoord3;

void main()
{
  gl_Position=u_viewproj * a_position;
      
  v_texcoord0=a_texcoord0.xyxy+vec4(-0.5,-0.5,-1.5,-1.5)*u_texcoordDelta.xyxy;
  v_texcoord1=a_texcoord0.xyxy+vec4( 0.5,-0.5, 1.5,-1.5)*u_texcoordDelta.xyxy;
  v_texcoord2=a_texcoord0.xyxy+vec4(-0.5, 0.5,-1.5, 1.5)*u_texcoordDelta.xyxy;
  v_texcoord3=a_texcoord0.xyxy+vec4( 0.5, 0.5, 1.5, 1.5)*u_texcoordDelta.xyxy;
}
