#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;
uniform vec4 u_setting;

vec3 processPixelate() {

	float factorX = 1.0 / (480.0 / u_setting.y);
	float factorY = 1.0 / (272.0 / u_setting.y);

	float x = floor(v_texcoord0.x / factorX) * factorX;
	float y = floor(v_texcoord0.y / factorY) * factorY;

	vec3 color = texture2D(sampler0, vec2(x, y)).xyz;
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processPixelate();
	}

	gl_FragColor.xyz=color.xyz;
	gl_FragColor.a = 1.0;
}
