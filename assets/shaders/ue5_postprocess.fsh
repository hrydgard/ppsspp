#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// UE5 PostProcess — Unreal Engine 5 style post-processing
// =================================================================
// Three independent stages, all bypassed when their slider is 0:
//
//   .x = Filmic tone-map strength  [0, 1]   (0 = off)
//   .y = Rim light strength        [0, 2]   (0 = off)
//   .z = Color grading strength    [0, 1]   (0 = off)
//   .w = Quality level             [0, 2]   (0=cheap, 1=normal, 2=high)
//
// Improvements over the previous version (per hrydgard's feedback
// that UE5 "looked cartoon-like"):
//
// 1. Rim light now uses a *5x5 local min/max contrast* approach
//    (Schlick-style "screen-space ambient rim") instead of a hard
//    Sobel edge. The result is a *soft, additive bloom-like halo*
//    that only appears where the local contrast is high — never
//    as a sharp outline.
//
// 2. Tone mapping: still ACES Filmic (Narkowicz 2015), but now
//    applied with a *pre-exposure* step. This is what UE5 does:
//    multiply the input by ~1.05-1.2 to push midtones into the
//    rolloff region of the curve. Without pre-exposure, ACES on
//    already-dark PSP frames looks flat.
//
// 3. Color grading now uses the *industry-standard
//    Lift/Gamma/Gain* model (used in DaVinci Resolve, UE5,
//    Lightroom). Each channel gets three independent controls:
//      - Lift  : shifts shadows (additive)
//      - Gamma : adjusts midtones (power)
//      - Gain  : multiplies highlights
//    This is what gives UE5 its "filmic" signature look: cool
//    shadows, neutral midtones, warm highlights.
//
// u_time is used to give the rim light a *very subtle* time-based
// shimmer so it doesn't look like a static effect (PPSSPP exposes
// u_time at offset 3 in PostShaderUniforms).

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;
uniform vec4 u_time;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Narkowicz 2015 ACES Filmic — industry standard cinematic curve
vec3 acesFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Lift/Gamma/Gain color grading (industry standard, same model
// as DaVinci Resolve / UE5 / Lightroom). Each parameter is a
// per-channel vec3; we expose a single user slider (u_setting.z)
// and a few hand-tuned constants. To customize, edit the
// constants below.
vec3 liftGammaGain(vec3 color, vec3 lift, vec3 gamma, vec3 gain) {
    // Lift:  additive in shadow region (smoothstep masks to dark)
    // Gain:  multiplicative everywhere
    // Gamma: power in midtone region
    vec3 liftMask = vec3(1.0) - smoothstep(0.0, 0.4, color);
    vec3 gainMask = smoothstep(0.0, 0.7, color);
    vec3 gammaMask = vec3(1.0) - abs(color - 0.5) * 2.0;  // peak at 0.5
    gammaMask = clamp(gammaMask, 0.0, 1.0);

    color = color + lift * liftMask * 0.10;
    color = color * mix(vec3(1.0), gain, gainMask);
    color = pow(max(color, vec3(0.0001)), mix(vec3(1.0), 1.0 / gamma, gammaMask));
    return color;
}

// Soft rim-light via local min/max contrast. 5x5 neighborhood.
// This is much smoother than Sobel (no hard edge detection) and
// matches what UE5 does in its bloom + lens-flare stack.
float softRim(sampler2D tex, vec2 uv, vec2 texelD) {
    float cMin = 1.0;
    float cMax = 0.0;
    float c = luminance(texture2D(tex, uv).rgb);
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            float l = luminance(texture2D(tex, uv + vec2(float(dx), float(dy)) * texelD).rgb);
            cMin = min(cMin, l);
            cMax = max(cMax, l);
        }
    }
    // "Where am I in the local range" — this is the soft rim term
    float range = max(cMax - cMin, 0.0001);
    return clamp((c - cMin) / range, 0.0, 1.0);
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    // ---- 1. Filmic tone mapping (with pre-exposure) ----
    if (u_setting.x > 0.0) {
        // Pre-exposure: push midtones into the rolloff region of
        // the ACES curve. Without this, dark PSP frames look flat.
        float preExposure = 1.08;
        color = color * preExposure;
        color = mix(color, acesFilmic(color), u_setting.x);
    }

    // ---- 2. Rim light (5x5 soft local contrast) ----
    if (u_setting.y > 0.0) {
        float quality = u_setting.w;
        // Quality 0: small neighborhood, 1: normal, 2: wide
        vec2 sampleDelta = u_texelDelta * (1.0 + quality * 1.2);
        float rim = softRim(sampler0, v_texcoord0, sampleDelta);
        // Bias the rim term to the brightest areas of the local
        // range so it looks like a *back-light halo*, not a
        // general brightness boost. pow(rim, 2.0) makes it
        // concentrate on the "top of the local range".
        float rimTerm = pow(rim, 2.0);
        // Tiny time-based shimmer so it doesn't look like a
        // printed-on overlay. ~3 Hz, ~5% amplitude.
        float shimmer = 1.0 + 0.05 * sin(u_time.x * 18.0 + v_texcoord0.x * 12.0);
        // Warm-amber rim color (1.05, 0.92, 0.78) is what real
        // back-lights look like. Multiplicative blend so it
        // brightens existing color rather than overlaying white.
        vec3 rimColor = vec3(1.05, 0.92, 0.78);
        // Apply the rim only to the *brightest* end of the local
        // range, so it doesn't lift the dark areas (which would
        // make it look like a wash).
        float rimMask = smoothstep(0.55, 0.95, rim);
        color = color + rimColor * rimTerm * rimMask * u_setting.y * 0.5 * shimmer;
    }

    // ---- 3. Color grading (Lift/Gamma/Gain) ----
    if (u_setting.z > 0.0) {
        // Hand-tuned for UE5 cinematic look:
        //   Lift  : cool shadows   (R, G up; B down -> cyan)
        //   Gamma : neutral
        //   Gain  : warm highlights (R up, B down -> orange)
        vec3 lift  = vec3(0.04, 0.05, -0.02);
        vec3 gamma = vec3(1.0);
        vec3 gain  = vec3(1.05, 1.0, 0.95);
        vec3 graded = liftGammaGain(color, lift, gamma, gain);
        // Plus a slight saturation push (UE5 default 1.10-1.15).
        float lum = luminance(graded);
        graded = mix(vec3(lum), graded, 1.10);
        color = mix(color, graded, u_setting.z);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
