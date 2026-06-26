#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// GodRays — Stable single-source volumetric light (v4)
// =================================================================
// Fixed single light source — NO auto-detection, NO multi-direction.
// This prevents the flickering that v3's auto-detection caused.
//
// Uses Crytek radial sampling (GPU Gems 3, Ch. 13) with per-fragment
// jitter for anti-banding. User controls light position directly.
//
// u_setting:
//   .x = Intensity  [-1, 1]   default 0.15 (negative = dark rays)
//   .y = Light Pos X [0, 1]   default 0.5
//   .z = Light Pos Y [0, 1]   default 0.3 (upper area)
//   .w = Quality     [0, 2]   0=24samp/1=48samp/2=72samp  default 1.0

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    float intensity = u_setting.x;
    if (intensity == 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    vec2 lightPos = vec2(u_setting.y, u_setting.z);
    float quality = u_setting.w;

    int numSamples = (quality < 0.5) ? 24
                   : (quality < 1.5) ? 48
                   :                    72;

    // Crytek constants
    const float weight = 0.024;
    const float decay = 0.965;
    const float brightThreshold = 0.40;

    // Per-fragment jitter for anti-banding
    float jitter = hash12(v_texcoord0 * 1024.0 + gl_FragCoord.xy);

    // Radial sampling from current pixel toward light source
    vec2 deltaTextCoord = (v_texcoord0 - lightPos);
    deltaTextCoord *= (1.0 / float(numSamples)) * 1.05;

    vec2 sampleCoord = v_texcoord0;
    sampleCoord -= deltaTextCoord * jitter;

    vec3 fragColor = vec3(0.0);
    float illuminationDecay = 1.0;

    for (int i = 0; i < 72; i++) {
        if (i >= numSamples) break;
        sampleCoord -= deltaTextCoord;
        vec2 sc = clamp(sampleCoord, vec2(0.001), vec2(0.999));
        vec3 samp = texture2D(sampler0, sc).rgb;
        float l = luminance(samp);
        float brightGate = max(l - brightThreshold, 0.0) * 3.5;
        fragColor += samp * illuminationDecay * weight * brightGate;
        illuminationDecay *= decay;
    }

    // Depth boost
    float dist = length(v_texcoord0 - lightPos);
    float depthBoost = 1.0 + 0.4 * smoothstep(0.0, 0.5, dist);

    // Warm sun tint
    vec3 rayTint = vec3(1.00, 0.94, 0.78);

    if (intensity > 0.0) {
        // Normal: add light rays
        color += fragColor * intensity * depthBoost * rayTint * 1.2;
    } else {
        // Negative: subtract (dark rays / shadow streaks)
        color -= fragColor * abs(intensity) * depthBoost * rayTint * 1.2;
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
