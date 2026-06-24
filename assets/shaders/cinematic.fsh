#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    // 1. Chromatic Aberration
    if (u_setting.x > 0.0) {
        float r = texture2D(sampler0, v_texcoord0 + vec2(u_setting.x, 0.0)).r;
        float g = texture2D(sampler0, v_texcoord0).g;
        float b = texture2D(sampler0, v_texcoord0 - vec2(u_setting.x, 0.0)).b;
        color = vec3(r, g, b);
    }

    // 2. Film Grain
    if (u_setting.y > 0.0) {
        float x = dot(v_texcoord0 * 237.0, vec2(12.9898, 78.233));
        float g = fract(sin(x) * 43758.5453);
        color += (g - 0.5) * u_setting.y;
    }

    // 3. Vignette
    if (u_setting.z > 0.0) {
        vec2 center = v_texcoord0 - 0.5;
        float vignette = 1.0 - dot(center, center) * u_setting.z * 2.0;
        color *= clamp(vignette, 0.0, 1.0);
    }

    // 4. Dithering (Bayer 4x4)
    if (u_setting.w > 0.0) {
        vec2 p = floor(gl_FragCoord.xy);
        int n = int(mod(p.x, 4.0)) + int(mod(p.y, 4.0)) * 4;
        float bayer[16] = float[](
            0.0, 8.0, 2.0, 10.0,
            12.0, 4.0, 14.0, 6.0,
            3.0, 11.0, 1.0, 9.0,
            15.0, 7.0, 13.0, 5.0
        );
        float d = bayer[n % 16] / 16.0;
        color = floor(color * 255.0 + d) / 255.0;
    }

    color = clamp(color, 0.0, 1.0);
    gl_FragColor = vec4(color, 1.0);
}
