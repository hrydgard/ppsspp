// Side by side stereo, useful for old 3D TVs.
//
// NOTE: Will only be compiled for Vulkan so doesn't follow all the usual conventions.

uniform sampler2DArray sampler0;
varying vec2 v_texcoord0;

uniform vec4 u_setting;

void main() {
	if (v_texcoord0.x < 0.5) {
	  gl_FragColor.rgb = texture(sampler0, vec3(v_texcoord0.x * 2.0, v_texcoord0.y, 0.0)).xyz;
	} else {
	  gl_FragColor.rgb = texture(sampler0, vec3((v_texcoord0.x - 0.5) * 2.0, v_texcoord0.y, 1.0)).xyz;
	}

	gl_FragColor.a = 1.0;
}
