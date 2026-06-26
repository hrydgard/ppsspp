#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// PseudoRT — Indirect light GI simulation (v4)
// =================================================================
// Simulates global illumination (light bouncing). For each pixel,
// checks 8 directions x 2 distances (16 taps):
//   - Brighter neighbors  → warm indirect light spill
//   - Darker neighbors    → AO edge darkening
//
// u_setting:
//   .x = AO Strength      [-1, 2]  (negative = clamped to 0)  default 0.5
//   .y = Indirect Light   [-1, 1]  (negative = cool blue)     default 0.3
//   .z = Radius           [1, 6]   texel distance             default 3.0
//   .w = Blend            [0, 1]                              default 0.6

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

const vec3 AO_COLOR         = vec3(0.04, 0.045, 0.055);
const vec3 INDIRECT_TINT_WARM = vec3(1.0, 0.85, 0.65);
const vec3 INDIRECT_TINT_COOL = vec3(0.65, 0.75, 1.0);

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float aoStrength       = u_setting.x;
    float indirectStrength = u_setting.y;
    float radius           = max(u_setting.z, 1.0);
    float blend            = u_setting.w;

    // If both AO and indirect are 0, pass through.
    if (aoStrength == 0.0 && indirectStrength == 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    const float PI_4 = 0.7853981633;

    float aoAccum = 0.0;
    float indirectAccum = 0.0;

    // 8 directions x 2 distances = 16 taps.
    for (int i = 0; i < 8; i++) {
        float angle = float(i) * PI_4;
        vec2 dir = vec2(cos(angle), sin(angle));

        // Distance 1: radius * 0.5 (closer, weight = 1.0)
        vec2 off1 = dir * u_texelDelta * (radius * 0.5);
        float l1 = luminance(texture2D(sampler0, v_texcoord0 + off1).rgb);
        float diff1 = l1 - centerLum;
        if (diff1 > 0.03) {
            indirectAccum += diff1 * 1.0;
        } else if (diff1 < -0.03) {
            aoAccum += (-diff1) * 1.0;
        }

        // Distance 2: radius * 1.0 (farther, weight = 0.5)
        vec2 off2 = dir * u_texelDelta * radius;
        float l2 = luminance(texture2D(sampler0, v_texcoord0 + off2).rgb);
        float diff2 = l2 - centerLum;
        if (diff2 > 0.03) {
            indirectAccum += diff2 * 0.5;
        } else if (diff2 < -0.03) {
            aoAccum += (-diff2) * 0.5;
        }
    }

    // Smoothstep falloff.
    float aoFactor = smoothstep(0.005, 0.15, aoAccum);
    float indirectFactor = smoothstep(0.01, 0.20, indirectAccum);

    // AO darkening (clamped — negative AO strength = no effect).
    float aoAmt = clamp(aoStrength * aoFactor, 0.0, 1.0);
    vec3 result = mix(color, AO_COLOR, aoAmt);

    // Indirect light spill.
    // Negative indirect = cool blue tint instead of warm.
    vec3 indirectTint = (indirectStrength < 0.0) ? INDIRECT_TINT_COOL : INDIRECT_TINT_WARM;
    float indirectAmt = abs(indirectStrength);
    result += indirectTint * indirectFactor * indirectAmt * 0.4;

    // Blend with original by user blend factor.
    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
