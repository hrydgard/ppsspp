#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec4 original = texture2D(sampler0, v_texcoord0);
    vec3 color = original.rgb;
    float centerLum = luminance(color);

    float aoStrength = u_setting.x;
    float indirectStrength = u_setting.y;
    float radiusSetting = u_setting.z;

    int ringCount = 1;
    if (int(radiusSetting) >= 3) ringCount = 3;
    else if (int(radiusSetting) >= 2) ringCount = 2;

    float totalWeight = 0.0;
    float occlusionSum = 0.0;
    vec3 indirectSum = vec3(0.0);

    for (int ring = 1; ring <= 3; ring++) {
        if (ring > ringCount) break;
        float r = float(ring);
        float weight = 1.0 / (r * r);

        for (int i = 0; i < 8; i++) {
            float angle = float(i) * 0.785398; // 45 degrees in radians
            vec2 dir = vec2(cos(angle), sin(angle));
            vec2 offset = dir * u_texelDelta * r;
            vec3 sampleColor = texture2D(sampler0, v_texcoord0 + offset).rgb;
            float sampleLum = luminance(sampleColor);

            // AO: center darker than neighbor means more occlusion
            float diff = centerLum - sampleLum;
            occlusionSum += diff * weight;

            // Indirect light: neighbor brighter than center means light spill
            float brightDiff = sampleLum - centerLum;
            if (brightDiff > 0.0) {
                indirectSum += sampleColor * brightDiff * weight;
            }

            totalWeight += weight;
        }
    }

    float avgDiff = occlusionSum / max(totalWeight, 0.001);
    float aoFactor = 1.0 - smoothstep(0.0, 1.0, (0.5 + avgDiff * 0.5)) * aoStrength;

    float indirectRaw = luminance(indirectSum) / max(totalWeight, 0.001);
    vec3 indirectLight = vec3(1.0, 0.85, 0.7) * indirectRaw * indirectStrength * 0.5;

    vec3 result = color * aoFactor + indirectLight;
    result = mix(color, result, u_setting.w);
    result = clamp(result, 0.0, 1.0);

    gl_FragColor = vec4(result, 1.0);
}
