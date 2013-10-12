// Natural shader, GLSL code adapted from:
// http://forums.ngemu.com/showthread.php?t=76098


uniform sampler2D sampler0;

const mat3 RGBtoYIQ = mat3(0.299, 0.596, 0.212, 
                           0.587,-0.275,-0.523, 
                           0.114,-0.321, 0.311);

const mat3 YIQtoRGB = mat3(1.0, 1.0, 1.0,
                           0.95568806036115671171,-0.27158179694405859326,-1.1081773266826619523,
                           0.61985809445637075388,-0.64687381613840131330, 1.7050645599191817149);

const vec3 val00 = vec3( 1.2, 1.2, 1.2);

void main()
{
vec3 c0,c1;

c0 = texture2D(sampler0,gl_TexCoord[0].xy).xyz;
c0+=(texture2D(sampler0,gl_TexCoord[0].zy).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[0].xw).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[0].zw).xyz)*0.125;

c0+= texture2D(sampler0,gl_TexCoord[1].xy).xyz;
c0+=(texture2D(sampler0,gl_TexCoord[1].zy).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[1].xw).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[1].zw).xyz)*0.125;

c0+= texture2D(sampler0,gl_TexCoord[2].xy).xyz;
c0+=(texture2D(sampler0,gl_TexCoord[2].zy).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[2].xw).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[2].zw).xyz)*0.125;

c0+= texture2D(sampler0,gl_TexCoord[3].xy).xyz;
c0+=(texture2D(sampler0,gl_TexCoord[3].zy).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[3].xw).xyz)*0.25;
c0+=(texture2D(sampler0,gl_TexCoord[3].zw).xyz)*0.125;
c0*=0.153846153846;

c1=RGBtoYIQ*c0;

c1=vec3(pow(c1.x,val00.x),c1.yz*val00.yz);

gl_FragColor.xyz=YIQtoRGB*c1;

}
