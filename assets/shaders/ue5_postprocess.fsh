#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// UE5 PostProcess — Unreal Engine 5 style post-processing (v8)
// =================================================================
// Three independent stages, all bypassed when their slider is 0:
//
//   .x = Filmic tone-map strength  [0, 1]     (0 = off)  default 0.3
//   .y = Rim light strength        [0, 0.2]   (0 = off)  default 0.08
//   .z = Color grading strength    [0, 1]     (0 = off)  default 0.2
//   .w = Quality level             [0, 2]     (0=cheap, 1=normal, 2=high)
//
// v8 changes (rim light):
//   - Rim is now gated by the local luminance GRADIENT magnitude, so it
//     appears on actual edges instead of every locally-bright pixel
//     (gradient/edge-based rim, Sobel/Frei-Chen style mask).
//   - The rim tint is shifted by the gradient DIRECTION (horizontal),
//     so top-lit vs side-lit edges read differently — a light-direction
//     aware rim rather than a uniform glow.
//   ACES tone mapping and Lift/Gamma/Gain are unchanged (documented,
//   well-behaved).

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

// Rec.709 luma
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
// as DaVinci Resolve / UE5 / Lightroom).
vec3 liftGammaGain(vec3 color, vec3 lift, vec3 gamma, vec3 gain) {
    vec3 liftMask = vec3(1.0) - smoothstep(0.0, 0.4, color);
    vec3 gainMask = smoothstep(0.0, 0.7, color);
    vec3 gammaMask = vec3(1.0) - abs(color - 0.5) * 2.0;
    gammaMask = clamp(gammaMask, 0.0, 1.0);

    color = color + lift * liftMask * 0.10;
    color = color * mix(vec3(1.0), gain, gainMask);
    color = pow(max(color, vec3(0.0001)), mix(vec3(1.0), 1.0 / gamma, gammaMask));
    return color;
}

// Soft rim-light via local min/max contrast. 5x5 neighborhood.
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
    float range = max(cMax - cMin, 0.0001);
    return clamp((c - cMin) / range, 0.0, 1.0);
}

// Luminance gradient (gx, gy) from 4 diagonal neighbors.
vec2 grad2(sampler2D tex, vec2 uv, vec2 td) {
    float tl = luminance(texture2D(tex, uv + vec2(-td.x, -td.y)).rgb);
    float tr = luminance(texture2D(tex, uv + vec2( td.x, -td.y)).rgb);
    float bl = luminance(texture2D(tex, uv + vec2(-td.x,  td.y)).rgb);
    float br = luminance(texture2D(tex, uv + vec2( td.x,  td.y)).rgb);
    return vec2((tr + br) - (tl + bl), (bl + br) - (tl + tr));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    // ---- 1. Filmic tone mapping (no pre-exposure) ----
    if (u_setting.x > 0.0) {
        color = mix(color, acesFilmic(color), u_setting.x);
    }

    // ---- 2. Rim light (gradient-aware, v8) ----
    if (u_setting.y > 0.0) {
        float quality = u_setting.w;
        vec2 sampleDelta = u_texelDelta * (1.0 + quality * 1.2);
        float rim = softRim(sampler0, v_texcoord0, sampleDelta);
        float rimTerm = pow(rim, 2.0);

        // Edge gate: rim only where there is a real luminance gradient.
        vec2 g = grad2(sampler0, v_texcoord0, sampleDelta);
        float edge = smoothstep(0.02, 0.30, length(g));

        // Direction-aware tint shift (horizontal gradient component).
        float dirTint = clamp(g.x * 0.5 + 0.5, 0.0, 1.0);
        vec3 rimTintShift = mix(vec3(0.96, 1.0, 1.06), vec3(1.04, 1.0, 0.94), dirTint);

        vec3 rimColor = vec3(1.05, 0.92, 0.78);
        // Tighter rim mask: smoothstep(0.50, 0.85) — less aggressive.
        float rimMask = smoothstep(0.50, 0.85, rim);
        // Multiplier reduced from 3.5 to 1.0.
        color = color + rimColor * rimTintShift * rimTerm * rimMask * edge * u_setting.y * 1.0;
    }

    // ---- 3. Color grading (Lift/Gamma/Gain) ----
    if (u_setting.z > 0.0) {
        vec3 lift  = vec3(0.04, 0.05, -0.02);
        vec3 gamma = vec3(1.0);
        vec3 gain  = vec3(1.05, 1.0, 0.95);
        vec3 graded = liftGammaGain(color, lift, gamma, gain);
        float lum = luminance(graded);
        graded = mix(vec3(lum), graded, 1.10);
        color = mix(color, graded, u_setting.z);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
