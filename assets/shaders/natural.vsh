attribute vec4 a_position;
attribute vec2 a_texcoord0;
uniform mat4 u_viewproj;
varying vec2 v_texcoord0;
void main()
{
gl_Position=u_viewproj * a_position;
		
gl_TexCoord[0]=a_texcoord0.xyxy;
gl_TexCoord[1]=a_texcoord0.xyxy;
gl_TexCoord[2]=a_texcoord0.xyxy;
gl_TexCoord[3]=a_texcoord0.xyxy;

}