#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — Multi-radius edge darkening with scattering (v4)
// =================================================================
// TRUE pseudo-AO without depth buffer. Detects brightness edges,
// then darkens the "dark side" of each edge. Multiple radius rings
// make the darkening scatter outward like real contact shadows.
//
// Algorithm:
//   For each of 3 radius rings (0.33x, 0.66x, 1.0x of max radius):
//     1. Sample 8 neighbors at that ring distance
//     2. Compute average neighbor luminance
//     3. If center is darker than neighbors (dark side of edge):
//        accumulate (avgNeighbor - center) weighted by 1/ring_index
//   The weighted sum across rings creates a "scattering" falloff.
//
// u_setting:
//   .x = Strength  [-1, 2]   default 1.0 (negative = brighten)
//   .y = Radius    [1, 6]    default 3.0
//   .z = Color     [0, 1]    0=black, 1=dark grey  default 0.0
//   .w = Blend     [0, 1]    default 0.7

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

    float strength = u_setting.x;
    if (strength == 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    float maxRadius = max(u_setting.y, 1.0);
    float colorMode = u_setting.z;
    float blend = u_setting.w;

    // 3 radius rings, 8 directions each = 24 taps
    float aoAccum = 0.0;
    float weightAccum = 0.0;
    const float PI_4 = 0.7853981633;

    for (int ring = 0; ring < 3; ring++) {
        float ringFrac = (float(ring) + 1.0) / 3.0;  // 0.33, 0.67, 1.0
        float r = maxRadius * ringFrac;
        // Closer rings weigh more — creates scattering falloff
        float weight = 1.0 / (1.0 + float(ring) * 0.7);

        float sumLum = 0.0;
        for (int i = 0; i < 8; i++) {
            float angle = float(i) * PI_4;
            vec2 dir = vec2(cos(angle), sin(angle));
            vec2 off = dir * u_texelDelta * r;
            sumLum += luminance(texture2D(sampler0, v_texcoord0 + off).rgb);
        }
        float avgLum = sumLum / 8.0;

        // If center is on dark side of edge (darker than neighbors)
        float diff = max(0.0, avgLum - centerLum);
        aoAccum += diff * weight;
        weightAccum += weight;
    }

    float ao = aoAccum / max(weightAccum, 0.001);
    // Sensitive smoothstep for visible effect
    float aoFactor = smoothstep(0.005, 0.15, ao);

    // Quantized AO color
    vec3 aoColor = (colorMode < 0.5) ? vec3(0.0) : vec3(0.08, 0.08, 0.10);

    vec3 result;
    if (strength > 0.0) {
        // Normal: darken toward AO color
        result = mix(color, aoColor, clamp(aoFactor * strength, 0.0, 1.0));
    } else {
        // Negative strength: brighten instead (inverse AO)
        result = mix(color, vec3(1.0), clamp(aoFactor * abs(strength), 0.0, 1.0));
    }
    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
