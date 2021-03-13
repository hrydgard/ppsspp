#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;
uniform vec4 u_setting;
uniform vec2 u_pixelDelta;


vec3 processColorReduction(vec3 color) {

	vec3 colorRes = vec3(u_setting.y, u_setting.z, u_setting.w);
	color = floor(color.rgb * colorRes) / (colorRes - 1.0);
	color = min(color, 1.0);
	return color;
}

void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processColorReduction(color);
	}

	gl_FragColor.xyz=color.xyz;
	gl_FragColor.a = 1.0;
}
