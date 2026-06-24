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

    if (u_setting.x > 0.0) {
        vec2 lightPos = vec2(u_setting.y, u_setting.z);
        float quality = u_setting.w;
        int samples = (quality < 0.5) ? 8 : (quality < 1.5) ? 16 : 32;
        vec2 dir = lightPos - v_texcoord0;
        float dist = length(dir);
        dir = normalize(dir);

        vec3 rayColor = vec3(0.0);
        float decay = 0.95;
        float exposure = 0.3;
        float stepSize = dist / float(samples);

        for (int i = 0; i < 32; i++) {
            if (i >= samples) break;
            float t = float(i) * stepSize;
            vec2 sampleUV = v_texcoord0 + dir * t;
            vec3 sampleColor = texture2D(sampler0, sampleUV).rgb;
            float lum = dot(sampleColor, vec3(0.299, 0.587, 0.114));
            float bright = max(lum - 0.3, 0.0);
            rayColor += vec3(bright) * pow(decay, float(i)) * exposure;
        }

        color += rayColor * u_setting.x / float(samples);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
