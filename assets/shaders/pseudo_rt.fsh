#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// PseudoRT — Screen-space pseudo ray tracing
// =================================================================
// This is a *luminance-based* approximation of SSAO. Real SSAO needs
// the depth buffer; PPSSPP doesn't expose it to its post-shader.
// What we *can* do, based purely on the color image:
//
//   1. Estimate local luminance variance (std-dev) over a wide disk.
//   2. If neighbours are highly varied AND we're darker than the
//      mean, we're in a "concavity" -> darken (AO).
//   3. If neighbours are bright and we are *also* bright, this is an
//      "exposed" surface -> add a small warm indirect-light spill.
//
// This matches the "screen-space ambient occlusion" intuition even
// though we never touch depth. AO color defaults to neutral gray
// (configurable) so it can be made warmer / cooler.
//
// u_setting:
//   .x = AO strength             [0, 1]   (0 = off)
//   .y = Indirect light spill     [0, 1]   (0 = off)
//   .z = Sampling radius         [1, 6]   texel count of the outermost ring
//   .w = Final mix with original [0, 1]
//
// Additional tunables baked in (changes by request were minimal
// here, so we keep them hard-coded):
//   AO_COLOR = (0.06, 0.06, 0.07)  — neutral dark gray, biases toward black
//   INDIRECT_TINT = (1.0, 0.88, 0.72) — warm sun-like bounce

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

const vec3 AO_COLOR        = vec3(0.04, 0.045, 0.055);
const vec3 INDIRECT_TINT   = vec3(1.00, 0.88, 0.72);
const float INV_SQRT_2PI   = 0.39894228; // for gaussian weight

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float aoStrength       = u_setting.x;
    float indirectStrength = u_setting.y;
    float radiusSetting    = u_setting.z;
    float blend            = u_setting.w;

    // The user-controlled radius becomes a "tap distance" in texels.
    // 1 -> 1px, 6 -> 6px. Bigger radius = wider, softer AO.
    float R = max(radiusSetting, 1.0);

    // Sample on three concentric rings with poisson-like offsets.
    // Total taps: 24 (1 ring) / 48 (2) / 72 (3), depending on R.
    int ringCount = 1;
    if (R > 3.5)      ringCount = 3;
    else if (R > 2.0) ringCount = 2;

    // Accumulate (sum, sum of squares) for mean and variance.
    float sum = 0.0;
    float sumSq = 0.0;
    float totalWeight = 0.0;
    // Accumulate the *bright* neighbour contribution for indirect light.
    float brightAccum = 0.0;
    float brightWeight = 0.0;

    // 12 angle offsets (pi/6 apart), used at every ring.
    // Taps per ring = 12, total = 12 * ringCount.
    for (int ring = 1; ring <= 3; ring++) {
        if (ring > ringCount) break;
        // Each ring is a different radius (R * ring / ringCount).
        float rDist = R * (float(ring) / float(ringCount));
        // Gaussian weight by ring distance: closer neighbours matter more.
        float ringW = exp(-0.5 * rDist * rDist / (R * R));

        for (int i = 0; i < 12; i++) {
            // Stagger angles per ring so taps don't overlap.
            float angle = float(i) * 0.523598775 + float(ring) * 0.17;
            vec2 dir = vec2(cos(angle), sin(angle));
            vec2 offset = dir * u_texelDelta * rDist;
            vec3 s = texture2D(sampler0, v_texcoord0 + offset).rgb;
            float l = luminance(s);

            sum    += l * ringW;
            sumSq  += l * l * ringW;
            totalWeight += ringW;

            if (l > centerLum + 0.04) {
                // Neighbour is meaningfully brighter -> contributes to indirect light
                float contribution = (l - centerLum) * ringW;
                brightAccum += contribution;
                brightWeight += ringW;
            }
        }
    }

    // Mean and variance
    float mean = sum / max(totalWeight, 0.0001);
    float variance = max((sumSq / max(totalWeight, 0.0001)) - mean * mean, 0.0);
    float stddev = sqrt(variance);

    // AO factor:
    //   - baseline: if I'm dim relative to mean AND variance is low (uniform
    //     dim surround), I'm in a "concavity" -> darken.
    //   - if I'm bright relative to mean, do not darken (exposed surface).
    //   - variance acts as a confidence term: high variance means the
    //     scene has a clear edge, AO should be strong there.
    float dim = clamp((mean - centerLum) / max(mean, 0.15), 0.0, 1.0);
    // variance is roughly 0..0.25. Map to 0..1.
    float varNorm = clamp(variance * 4.0, 0.0, 1.0);
    float aoMask = dim * (0.35 + 0.65 * varNorm);

    // Smoothed AO factor
    float aoFactor = 1.0 - aoMask * aoStrength;
    aoFactor = clamp(aoFactor, 0.0, 1.0);

    // Indirect light: only meaningful if there are bright neighbours.
    float brightAvg = brightAccum / max(brightWeight, 0.0001);
    // soft gate so the average picture doesn't get pulled up.
    float spill = smoothstep(0.0, 0.35, brightAvg);
    vec3 indirect = INDIRECT_TINT * spill * indirectStrength * 0.35;

    // AO darkens toward AO_COLOR (configurable gray) instead of pure
    // black, which gives a softer, more "shadowy" look (you can change
    // AO_COLOR at the top of the file if you want a tint).
    vec3 aoTinted = mix(color, AO_COLOR, aoStrength * aoMask * 0.85);

    // Combine: AO darkens color, indirect light adds on top.
    vec3 result = aoTinted + indirect;
    result = mix(color, result, blend);
    result = clamp(result, 0.0, 1.0);

    gl_FragColor = vec4(result, 1.0);
}
