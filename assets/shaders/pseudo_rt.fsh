#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo RT — Indirect light / Global Illumination simulation (v4)
// =================================================================
// Simulates "ray tracing-like" global illumination: light bounces
// from bright surfaces onto nearby dark areas. NOT a light cone.
//
// Algorithm:
//   1. Sample 8 neighbors at 2 distances (16 taps)
//   2. If neighbor is MUCH brighter than center: accumulate warm spill
//   3. If neighbor is MUCH darker than center: accumulate AO darkening
//   4. Apply both: darken concave edges, brighten near light sources
//
// u_setting:
//   .x = AO Strength      [-1, 2]   default 0.5
//   .y = Indirect Light   [-1, 1]   default 0.3 (negative = cool blue)
//   .z = Radius           [1, 6]    default 3.0
//   .w = Blend            [0, 1]    default 0.6

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

const vec3 AO_COLOR       = vec3(0.04, 0.045, 0.055);
const vec3 INDIRECT_WARM  = vec3(1.0, 0.85, 0.65);
const vec3 INDIRECT_COOL  = vec3(0.65, 0.75, 1.0);

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float aoStrength = u_setting.x;
    float indirectStrength = u_setting.y;
    float maxRadius = max(u_setting.z, 1.0);
    float blend = u_setting.w;

    if (aoStrength == 0.0 && indirectStrength == 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    // 8 directions × 2 distances = 16 taps
    float aoAccum = 0.0;
    vec3 indirectAccum = vec3(0.0);
    float aoWeight = 0.0;
    float indirectWeight = 0.0;
    const float PI_4 = 0.7853981633;

    for (int i = 0; i < 8; i++) {
        float angle = float(i) * PI_4;
        vec2 dir = vec2(cos(angle), sin(angle));

        // Two distances
        for (int d = 0; d < 2; d++) {
            float distFrac = (float(d) + 1.0) / 2.0;  // 0.5, 1.0
            float r = maxRadius * distFrac;
            vec2 off = dir * u_texelDelta * r;
            vec3 samp = texture2D(sampler0, v_texcoord0 + off).rgb;
            float l = luminance(samp);

            float diff = l - centerLum;

            // AO: neighbor is darker than center (center on bright side
            // of edge — still gets darkened for concavity effect)
            if (diff < -0.03) {
                float w = 1.0 / (1.0 + float(d) * 0.5);
                aoAccum += abs(diff) * w;
                aoWeight += w;
            }

            // Indirect light: neighbor is brighter than center
            if (diff > 0.03) {
                float w = 1.0 / (1.0 + float(d) * 0.5);
                indirectAccum += samp * diff * w;
                indirectWeight += w;
            }
        }
    }

    // Normalize
    float aoFactor = smoothstep(0.005, 0.15, aoAccum / max(aoWeight, 0.001));
    vec3 indirectAvg = indirectAccum / max(indirectWeight, 0.001);
    float indirectFactor = smoothstep(0.01, 0.20, luminance(indirectAvg));

    // Apply AO (darken)
    vec3 result = mix(color, AO_COLOR, clamp(aoFactor * aoStrength, 0.0, 1.0));

    // Apply indirect light (brighten with warm or cool tint)
    vec3 indirectTint = (indirectStrength >= 0.0) ? INDIRECT_WARM : INDIRECT_COOL;
    float indirectMag = abs(indirectStrength);
    result += indirectTint * indirectFactor * indirectMag * 0.4;

    // Final blend
    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
