#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — Horizon-based luminance occlusion (v3)
// =================================================================
// Detects where the current pixel is DARKER than its surroundings
// in 8 directions. If a neighbor is brighter, the current pixel is
// "occluded" from that direction. Uses max (not sum) of directional
// occlusions to prevent over-darkening at edge intersections.
//
// 16 taps: 8 directions x 2 distances.
//
// u_setting:
//   .x = AO Strength   [0, 1]   (0 = off)    default 0.6
//   .y = AO Radius     [1, 4]   texel distance, default 2.5
//   .z = AO Color Mode [0, 1]   0=black, 1=dark grey (step=1.0)  default 0.0
//   .w = AO Blend      [0, 1]   default 0.8

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

// Baked defaults — used when u_setting components are 0/unset.
const float DEFAULT_STRENGTH   = 0.6;
const float DEFAULT_RADIUS     = 2.5;
const float DEFAULT_COLOR_MODE = 0.0;
const float DEFAULT_BLEND      = 0.8;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float strength = u_setting.x;
    // strength = 0 means explicitly "off" — no AO at all.
    if (strength <= 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    float radius    = max(u_setting.y, 1.0);
    float colorMode = u_setting.z;
    float blend     = (u_setting.w > 0.0) ? u_setting.w : DEFAULT_BLEND;

    // 8 directions: 0, 45, 90, ..., 315 degrees.
    const float PI_4 = 0.7853981633;

    // Track the maximum occlusion across all 8 directions.
    float maxOcclusion = 0.0;

    for (int i = 0; i < 8; i++) {
        float angle = float(i) * PI_4;
        vec2 dir = vec2(cos(angle), sin(angle));

        // Sample at 2 distances: radius*0.5 and radius*1.0.
        // Track the maximum luminance (the "horizon") in this direction.
        float horizon = 0.0;

        // Distance 1: radius * 0.5
        vec2 off1 = dir * u_texelDelta * (radius * 0.5);
        float l1 = luminance(texture2D(sampler0, v_texcoord0 + off1).rgb);
        horizon = max(horizon, l1);

        // Distance 2: radius * 1.0
        vec2 off2 = dir * u_texelDelta * radius;
        float l2 = luminance(texture2D(sampler0, v_texcoord0 + off2).rgb);
        horizon = max(horizon, l2);

        // Occlusion: if the neighbor is brighter than center, we're occluded.
        float occ = max(0.0, horizon - centerLum);
        // Use max across directions to prevent over-darkening at intersections.
        maxOcclusion = max(maxOcclusion, occ);
    }

    // Smooth the occlusion into a soft falloff.
    float aoFactor = smoothstep(0.03, 0.35, maxOcclusion);

    // Quantized AO color: pure black or dark grey.
    vec3 aoColor = (colorMode < 0.5) ? vec3(0.0) : vec3(0.08, 0.08, 0.10);

    // Blend toward AO color, then mix with original by blend factor.
    vec3 result = mix(color, aoColor, aoFactor * strength);
    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
