#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;
uniform vec4 u_setting;


vec3 processAspectRatio() {

	float zoomByX = 16.0 / u_setting.y;
	float zoomByY = 9.0 / u_setting.z;
	float zoomByXY = 1.0;
	
	if (zoomByX < zoomByY) {
		zoomByXY = zoomByX;
	} else {
		zoomByXY = zoomByY;
	}

	float xdivision = 1.0 / (u_setting.y / 16.0 * zoomByXY);
	float ydivision = 1.0 / (u_setting.z / 9.0 * zoomByXY);

	vec3 color = texture2D(sampler0, vec2(v_texcoord0.x * xdivision - (0.5 + xdivision / 2.0 - 1.0), v_texcoord0.y * ydivision - (0.5 + ydivision / 2.0 - 1.0))).xyz;

	// Remove color bleed outside of the game scene caused by shrinking
	float rangex = (0.5 + (xdivision / 2.0 - 1.0)) / xdivision;
	float rangey = (0.5 + (ydivision / 2.0 - 1.0)) / ydivision;
	if (v_texcoord0.x < rangex || v_texcoord0.x > 1.0 - rangex || v_texcoord0.y < rangey || v_texcoord0.y > 1.0 - rangey) {
		color.xyz = vec3(0.0,0.0,0.0).xyz;
	}

	return color.xyz;
}
	
void main() {
	vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;

	if(u_setting.x==2.0 || (u_setting.x==0.0 && u_video==0.0) || (u_setting.x==1.0 && u_video==1.0)) {
		color=processAspectRatio();
	}

	gl_FragColor.xyz=color.xyz;
	gl_FragColor.a = 1.0;
}
