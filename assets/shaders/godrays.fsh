#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// GodRays — Volumetric light scattering (v8)
// =================================================================
// Crytek radial sampling (GPU Gems 3, Ch.13) with improvements:
//
//   * Jitter now uses Interleaved Gradient Noise (IGN, Castaño /
//     Call of Duty: Advanced Warfare) instead of a white hash. IGN is
//     temporally coherent and lets us CUT samples (12/16/24) while
//     keeping the same quality as the old 16/24/32.
//   * Anisotropic horizontal stretch (delta.x *= 2.5) makes sun shafts
//     read as cinematic light beams rather than a uniform radial blur.
//   * Per-channel offset sampling adds cheap RGB chromatic dispersion.
//
// u_setting:
//   .x = Intensity    [-1, 1]  (negative = dark rays)  default 0.2
//   .y = Light Pos X  [0, 1]                            default 0.5
//   .z = Light Pos Y  [0, 1]                            default 0.3
//   .w = Quality      [0, 2]   0=12/1=16/2=24 samples   default 1.0

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;
varying vec2 v_texcoord0;

// Rec.709 luma
float lum(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Interleaved Gradient Noise (Castaño, CoD:AW). ES 1.00 safe.
float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(floor(p), vec2(0.06711056, 0.00583715))));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float intensity = u_setting.x;

    // Avoid float equality — use small epsilon
    if (abs(intensity) < 0.001) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    vec2 lightPos = vec2(u_setting.y, u_setting.z);
    float quality = u_setting.w;

    // IGN compensates for the lower counts (12/16/24 vs 16/24/32).
    int numSamples = (quality < 0.5) ? 12
                   : (quality < 1.5) ? 16
                   :                    24;

    // Tuned constants: lower threshold, faster decay, higher weight
    const float weight = 0.025;
    const float decay = 0.94;
    const float exposure = 0.40;
    const float brightThreshold = 0.30;

    // Coherent IGN jitter (replaces hash12)
    float jitter = ign(gl_FragCoord.xy);

    vec2 deltaTextCoord = (v_texcoord0 - lightPos);
    deltaTextCoord.x *= 2.5;                 // anisotropic horizontal stretch
    deltaTextCoord *= (1.0 / float(numSamples)) * 1.05;

    vec2 sampleCoord = v_texcoord0;
    sampleCoord -= deltaTextCoord * jitter;

    // tiny per-channel offset for chromatic dispersion
    vec2 disp = deltaTextCoord * 0.15;

    vec3 rayColor = vec3(0.0);
    float illuminationDecay = 1.0;

    // Constant loop bound (32) + break — ES 1.00 safe.
    for (int i = 0; i < 32; i++) {
        if (i >= numSamples) break;
        sampleCoord -= deltaTextCoord;
        vec2 sc = clamp(sampleCoord, vec2(0.001), vec2(0.999));
        // RGB sampled at slightly offset positions -> colored rays
        float lr = lum(texture2D(sampler0, clamp(sc + disp, vec2(0.001), vec2(0.999))).rgb);
        float lg = lum(texture2D(sampler0, sc).rgb);
        float lb = lum(texture2D(sampler0, clamp(sc - disp, vec2(0.001), vec2(0.999))).rgb);
        vec3 samp = vec3(
            max(lr - brightThreshold, 0.0),
            max(lg - brightThreshold, 0.0),
            max(lb - brightThreshold, 0.0)
        ) * 3.5;
        rayColor += samp * illuminationDecay * weight;
        illuminationDecay *= decay;
    }

    vec3 rayTint = vec3(1.00, 0.94, 0.78);

    if (intensity > 0.0) {
        color += rayColor * exposure * rayTint * intensity;
    } else {
        color -= rayColor * exposure * rayTint * abs(intensity);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
