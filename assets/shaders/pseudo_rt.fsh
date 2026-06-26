#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// PseudoRT — Screen-space pseudo ray tracing.
// Two effects (independently toggleable, see u_setting):
//   .x = AO strength             [0, 1]   (0 = off)
//   .y = Indirect light spill     [0, 1]   (0 = off)
//   .z = Sampling radius         [0.5, 3] (1 = 8 taps, 2 = 16, 3 = 24)
//   .w = Final mix with original [0, 1]
//
// The AO here is "luminance-driven screen-space ambient occlusion".
// We sample a wide neighbourhood along multiple short arcs and
// measure how much darker it is on average than the current pixel.
// This captures both:
//   - corner / crevice darkening (bright outside, dim inside)
//   - contact shadow (lit object against a dark background)
// It is not real SSAO (no depth) but produces a soft, natural
// looking edge shadow that scales with u_setting.x and u_setting.z.

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Sample a small ring of neighbours. Each ring uses a different
// angle offset so we get 24 unique samples when 3 rings are on.
vec3 sampleWithOffset(vec2 uv, vec2 offset, float lumCenter) {
    vec3 s = texture2D(sampler0, uv + offset).rgb;
    float lumS = luminance(s);
    // Returns vec3(R=positive difference, G=negative difference,
// B=neighbour luminance). Caller picks what it wants.
    return vec3(max(lumS - lumCenter, 0.0),    // brighter than center
                max(lumCenter - lumS, 0.0),    // darker than center
                lumS);
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float aoStrength       = u_setting.x;
    float indirectStrength = u_setting.y;
    float radiusSetting    = u_setting.z;
    float blend            = u_setting.w;

    // Scale the sample radius in screen pixels. 0.5 -> 1 pixel,
    // 1.0 -> 2 pixels, 2.0 -> 4, 3.0 -> 6. Wider radius = bigger
    // shadow halos.
    float radius = radiusSetting * 2.0;

    // Number of rings based on radius. At radius 0.5 we only do
    // 1 ring (8 taps, cheap). At radius 3 we do 3 rings (24 taps).
    int ringCount = 1;
    if (radiusSetting > 2.5)      ringCount = 3;
    else if (radiusSetting > 1.5) ringCount = 2;

    float darkenSum   = 0.0;  // how much darker are neighbours
    float brightenSum = 0.0;  // how much brighter are neighbours
    float brightLumSum = 0.0; // accumulated brightness (for indirect light)
    float totalWeight = 0.0;
    float brightWeight = 0.0;

    // Use a non-axis-aligned start angle so the 8-tap ring doesn't
    // always sample on the same texel rows/cols.
    const float baseAngle = 0.39269908; // ~22.5 degrees

    for (int ring = 1; ring <= 3; ring++) {
        if (ring > ringCount) break;
        float r = float(ring);
        // Outer rings contribute less to occlusion (close neighbours
        // are more important for shadow).
        float w = 1.0 / r;
        // Scale the ring radius. Outer rings sit further out, but not
        // as far as r*radius would give (avoids huge sampling distances
        // at radius=3). We use sqrt to grow the radius sublinearly.
        float rDist = sqrt(float(ring)) * radius;

        for (int i = 0; i < 8; i++) {
            float angle = baseAngle + float(i) * 0.7853981633; // +22.5 deg per tap
            vec2 dir = vec2(cos(angle), sin(angle));
            vec2 offset = dir * u_texelDelta * rDist;
            vec3 sd = sampleWithOffset(v_texcoord0, offset, centerLum);
            darkenSum   += sd.y * w;
            brightenSum += sd.x * w;
            if (sd.x > 0.0) {
                brightLumSum += sd.z * w;
                brightWeight += w;
            }
            totalWeight += w;
        }
    }

    // AO: how much to darken the pixel. smoothstep gives a soft falloff
    // so the effect looks like ambient shadow, not a black line.
    // avgDarken is the average luminance drop of neighbours [0, 0.5+].
    float avgDarken = darkenSum / max(totalWeight, 0.001);
    // 0.4 means even a 40% brightness drop between us and neighbours
    // gives full AO. Smaller -> more sensitive.
    float aoFactor = 1.0 - smoothstep(0.0, 0.4, avgDarken) * aoStrength;
    aoFactor = clamp(aoFactor, 0.0, 1.0);

    // Indirect light: warm sun-like spill from brighter neighbours.
    // We only let *significantly* brighter pixels contribute, so the
    // average picture isn't pulled up.
    float brightAvg = brightLumSum / max(brightWeight, 0.001);
    float spillStrength = (brightenSum / max(totalWeight, 0.001));
    // Only the strongest 25% of bright pixels contribute -> soft halo
    float spill = smoothstep(0.0, 0.25, spillStrength) * brightAvg;
    // Warm color, very gentle
    vec3 indirectColor = vec3(1.0, 0.88, 0.72) * spill * indirectStrength * 0.5;

    vec3 result = color * aoFactor + indirectColor;
    // Blend toward the final result by the user-controlled factor.
    result = mix(color, result, blend);
    result = clamp(result, 0.0, 1.0);

    gl_FragColor = vec4(result, 1.0);
}
