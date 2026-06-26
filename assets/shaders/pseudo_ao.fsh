#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — Multi-radius edge darkening with scattering (v4)
// =================================================================
// For each pixel, checks if it's on the "dark side" of an edge
// (darker than its neighbors). If so, darken it. Uses 3 radius
// rings so the darkening "scatters" outward like real contact
// shadows.
//
// 24 taps: 3 rings x 8 directions.
//
// u_setting:
//   .x = Strength  [-1, 2]  (negative = brighten)  default 1.0
//   .y = Radius    [1, 6]   texel distance          default 3.0
//   .z = Color     [0, 1]   0=black, 1=dark grey    default 0.0
//   .w = Blend     [0, 1]                           default 0.7

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float strength  = u_setting.x;
    float radius    = max(u_setting.y, 1.0);
    float colorMode = u_setting.z;
    float blend     = u_setting.w;

    // If strength is exactly 0, no effect.
    if (strength == 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    const float PI_4 = 0.7853981633;

    float aoAccum = 0.0;
    float totalWeight = 0.0;

    // 3 radius rings: radius * 0.333, 0.666, 1.0
    for (int r = 0; r < 3; r++) {
        float ringFrac = (float(r) + 1.0) / 3.0;
        float ringRadius = radius * ringFrac;
        // Closer rings weigh more — scattering falloff.
        float ringWeight = 1.0 / (1.0 + float(r) * 0.7);

        // Sample 8 neighbors at this ring distance.
        float neighborSum = 0.0;
        for (int i = 0; i < 8; i++) {
            float angle = float(i) * PI_4;
            vec2 dir = vec2(cos(angle), sin(angle));
            vec2 off = dir * u_texelDelta * ringRadius;
            float l = luminance(texture2D(sampler0, v_texcoord0 + off).rgb);
            neighborSum += l;
        }

        float avgNeighbor = neighborSum / 8.0;

        // If center is on the dark side of the edge (neighbors brighter),
        // accumulate occlusion.
        if (avgNeighbor > centerLum) {
            float diff = avgNeighbor - centerLum;
            aoAccum += diff * ringWeight;
        }
        totalWeight += ringWeight;
    }

    // Normalize by total weight.
    float ao = aoAccum / max(totalWeight, 0.001);

    // Sensitive smoothstep — catches small differences.
    float aoFactor = smoothstep(0.005, 0.15, ao);

    // Quantized AO color: pure black or dark grey.
    vec3 aoColor = (colorMode < 0.5) ? vec3(0.0) : vec3(0.08, 0.08, 0.10);

    vec3 result;
    if (strength < 0.0) {
        // Negative strength: brighten instead of darken.
        result = mix(color, vec3(1.0), clamp(abs(strength) * aoFactor, 0.0, 1.0));
    } else {
        result = mix(color, aoColor, clamp(strength * aoFactor, 0.0, 1.0));
    }

    // Blend with original by user blend factor.
    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
