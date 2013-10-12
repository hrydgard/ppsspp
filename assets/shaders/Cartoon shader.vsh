attribute vec4 a_position;
attribute vec2 a_texcoord0;
uniform mat4 u_viewproj;

float size = 2.0; //edge detection offset, 2.0-5.0 suitable range

void main()

{
float x = (0.125/480.0)*size;
float y = (0.25/272.0)*size;
vec2 dg1 = vec2( x,y);
vec2 dg2 = vec2(-x,y);
vec2 dx  = vec2(x,0.0);
vec2 dy  = vec2(0.0,y);
gl_Position = u_viewproj * a_position;
gl_TexCoord[0]=a_texcoord0.xyxy;
gl_TexCoord[1].xy = gl_TexCoord[0].xy - dy;
gl_TexCoord[2].xy = gl_TexCoord[0].xy + dy;
gl_TexCoord[3].xy = gl_TexCoord[0].xy - dx;
gl_TexCoord[4].xy = gl_TexCoord[0].xy + dx;
gl_TexCoord[5].xy = gl_TexCoord[0].xy - dg1;
gl_TexCoord[6].xy = gl_TexCoord[0].xy + dg1;
gl_TexCoord[1].zw = gl_TexCoord[0].xy - dg2;
gl_TexCoord[2].zw = gl_TexCoord[0].xy + dg2;

}
