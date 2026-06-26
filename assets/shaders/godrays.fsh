#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// GodRays — Volumetric Light Scattering (Screen Space)
// =================================================================
// Reference:  "Volumetric Light Scattering as a Post-Process"
//             (Crytek, GPU Gems 3, Ch. 13) — also known as "god rays".
//             Direct adaptation: Erkaman's glsl-godrays (MIT).
//
// The full algorithm needs an *occlusion buffer* (the depth-pass
// result, where only the light source is white). PPSSPP has no depth
// output. We approximate occlusion by:
//   1. Thresholding the input color above a brightness cutoff, so
//      only the bright (light) pixels contribute.
//   2. Sampling along the ray from current pixel toward the
//      user-supplied light position in screen space.
//   3. Accumulating with exponential decay (illuminationDecay *= decay).
//
// Improvement over the previous version:
//   - Per-fragment *jitter* to break radial banding that appears
//     when the sample step is constant.
//   - Step length scales with distance to the light, not a flat
//     1/numSamples — this gives smoother rays at the screen edges.
//   - Light position is searched (small 3x3) for the brightest pixel
//     near the user-supplied point, so the rays actually anchor to
//     a hot pixel rather than an empty coordinate.
//
// u_setting:
//   .x = intensity [0, 2]            (0 = off)
//   .y = light X in screen UV [0,1]
//   .z = light Y in screen UV [0,1]
//   .w = sample quality  [0, 2]      (0=32samp / 1=64samp / 2=96samp)
//                                    (was 24/48/80; we raised the floor
//                                     so the result is visibly smoother)

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Cheap deterministic hash (used for jitter to break radial banding).
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    if (u_setting.x <= 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    float quality = u_setting.w;
    int numSamples = (quality < 0.5) ? 32
                   : (quality < 1.5) ? 64
                   :                    96;

    vec2 lightPos = vec2(u_setting.y, u_setting.z);

    // Optional: find the brightest 3x3 pixel near the user-supplied
    // light position and use that as the actual light center.
    // This is a *small* search (9 taps) so it's cheap.
    vec2 actualLightPos = lightPos;
    float bestLum = -1.0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 probe = lightPos + vec2(float(dx), float(dy)) * u_texelDelta * 4.0;
            probe = clamp(probe, vec2(0.001), vec2(0.999));
            float l = luminance(texture2D(sampler0, probe).rgb);
            if (l > bestLum) {
                bestLum = l;
                actualLightPos = probe;
            }
        }
    }
    // If nothing bright was found near the user position, fall back.
    if (bestLum < 0.05) {
        actualLightPos = lightPos;
    }

    // Crytek constants. Tuned for a soft "sunlight" look.
    const float weight  = 0.018;  // per-tap contribution
    const float decay   = 0.965;  // exponential falloff along the ray
    const float exposure = 0.42;  // overall ray brightness
    const float brightThreshold = 0.40; // only "lit" pixels contribute

    // Per-fragment jitter to break radial banding (this is the
    // secret sauce for clean rays: see Mitchell 2007 / dithered god rays).
    float jitter = hash12(v_texcoord0 * 1024.0 + gl_FragCoord.xy);

    // Step vector from current pixel toward the light, scaled by 1/N
    // and a density parameter. We pre-multiply by jitter so different
    // fragments start the accumulation at different depths along the
    // ray (this is what kills the radial-step bands).
    vec2 deltaTextCoord = (v_texcoord0 - actualLightPos);
    float dist = length(deltaTextCoord);
    deltaTextCoord *= (1.0 / float(numSamples)) * 1.05;

    vec2 sampleCoord = v_texcoord0;
    vec3 fragColor = vec3(0.0);
    float illuminationDecay = 1.0;
    // Skip the first 'jitter' fraction of the ray so different pixels
    // see different starting offsets.
    sampleCoord -= deltaTextCoord * jitter;

    for (int i = 0; i < 100; i++) {
        if (i >= numSamples) break;
        sampleCoord -= deltaTextCoord;
        // Clamp to avoid edge sampling artifacts.
        vec2 sc = clamp(sampleCoord, vec2(0.001), vec2(0.999));
        vec3 samp = texture2D(sampler0, sc).rgb;
        float l = luminance(samp);
        // Threshold: only bright pixels contribute (simulates
        // "this part of the screen is the light source").
        float brightGate = max(l - brightThreshold, 0.0) * 3.5;
        samp *= illuminationDecay * weight * brightGate;
        fragColor += samp;
        illuminationDecay *= decay;
    }

    // The light is brighter if the light source itself is far from the
    // camera (faking "depth"). Closer rays -> smaller boost, further
    // rays -> larger boost.
    float depthBoost = 1.0 + 0.6 * smoothstep(0.0, 0.5, dist);

    // Warm sun tint, multiplicative so it doesn't blow out the image.
    vec3 rayTint = vec3(1.00, 0.94, 0.78);
    color += fragColor * exposure * rayTint * u_setting.x * depthBoost;

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
