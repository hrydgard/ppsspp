#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// PseudoRT — Indirect light GI simulation (v8)
// =================================================================
// Simulates global illumination (light bouncing). For each pixel we
// sample 8 directions x 2 distances (16 taps):
//   - Brighter neighbors  -> warm (or cool) indirect light spill
//   - Darker neighbors    -> AO edge darkening
//
// v8 changes (fake GI / irradiance, guest.r RetroArch-style single-pass
// approach + joint-bilateral weighting):
//   - Hard `if diff > 0.03` threshold replaced by a smooth knee
//     (smoothstep), removing the binary on/off popping.
//   - A cross-bilateral range weight w = 1/(1+(dL^2)/sigma)*(1/dist)
//     stops the GI/AO from bleeding across bright/dark edges (haloing).
//   - A tiny self-bounce floor lifts pure-black areas slightly so
//     shadows read as "filled" rather than crushed.
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

const vec3 AO_COLOR          = vec3(0.04, 0.045, 0.055);
const vec3 INDIRECT_TINT_WARM = vec3(1.0, 0.85, 0.65);
const vec3 INDIRECT_TINT_COOL = vec3(0.65, 0.75, 1.0);

// Rec.709 luma
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

    const float PI_4  = 0.7853981633;
    const float SIGMA = 0.10;   // range (luminance) weight falloff

    float aoAccum = 0.0, indirectAccum = 0.0;
    float wsumAO = 0.0, wsumI = 0.0;

    // 8 directions x 2 distances = 16 taps.
    for (int i = 0; i < 8; i++) {
        float angle = float(i) * PI_4;
        vec2 dir = vec2(cos(angle), sin(angle));
        for (int r = 1; r <= 2; r++) {
            float dist = float(r);
            vec2 off = dir * u_texelDelta * (radius * dist);
            float l = luminance(texture2D(sampler0, v_texcoord0 + off).rgb);
            float diff = l - centerLum;
            float w = 1.0 / (1.0 + (diff * diff) / SIGMA) * (1.0 / dist);
            if (diff > 0.0) {
                // soft knee instead of binary threshold
                float k = smoothstep(0.0, 0.25, diff);
                indirectAccum += k * w;
                wsumI += w;
            } else if (diff < 0.0) {
                float k = smoothstep(0.0, 0.25, -diff);
                aoAccum += k * w;
                wsumAO += w;
            }
        }
    }

    float aoFactor       = (wsumAO > 0.0) ? aoAccum / wsumAO : 0.0;
    float indirectFactor = (wsumI  > 0.0) ? indirectAccum / wsumI : 0.0;

    // AO darkening (clamped — negative AO strength = no effect).
    float aoAmt = clamp(aoStrength * aoFactor, 0.0, 1.0);
    vec3 result = mix(color, AO_COLOR, aoAmt);

    // Indirect light spill.
    // Negative indirect = cool blue tint instead of warm.
    vec3 indirectTint = (indirectStrength < 0.0) ? INDIRECT_TINT_COOL : INDIRECT_TINT_WARM;
    float indirectAmt = abs(indirectStrength);
    result += indirectTint * indirectFactor * indirectAmt * 0.4;
    // Subtle self-bounce floor so dark areas are not crushed to black.
    result += indirectTint * 0.02 * indirectAmt * (1.0 - centerLum);

    // Blend with original by user blend factor.
    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
