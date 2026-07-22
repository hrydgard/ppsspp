#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// UE4 Filmic Tonemap — revived as a proper, documented tone mapper.
// =================================================================
// Previously this was an orphan (no ini entry) and used `varying vec4`
// which mismatched the vertex shader. It also used a non-standard
// "ACES-like" curve (color/(color+0.155)*1.2) that is NOT ACES.
//
// v8: replaced with John Hable's Filmic Tonemapping
// (GDC "Filmic Tonemapping for Realtime Rendering"), the actual UE4
// filmic curve. Fixed `varying vec4` -> `vec2`. Luminance uses Rec.709.
//
//   .x = Tone Strength   [0, 1]   (0 = off)   default 0.3
//   .y = Contrast         [0.5, 2]             default 1.0
//   .z = Saturation       [0, 2]               default 0.0
//   .w = Shadow Lift      [0, 1]               default 0.0

uniform sampler2D sampler0;
uniform vec4 u_setting;
varying vec2 v_texcoord0;

// Rec.709 luma
float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Hable Filmic curve (John Hable). Constants A..F are the standard set.
vec3 hableFilmic(vec3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    float toneStrength = u_setting.x;  // 0..1
    float contrast     = u_setting.y;  // 0.5..2.0
    float saturation   = u_setting.z;  // 0..2.0
    float lift         = u_setting.w;  // 0..1.0

    // ---- 1. Filmic tone mapping (Hable) ----
    if (toneStrength > 0.0) {
        float exposure = 1.0;
        float white = 11.2;                 // Hable reference white point
        vec3 tonemapped = hableFilmic(color * exposure) / hableFilmic(vec3(white));
        color = mix(color, clamp(tonemapped, 0.0, 1.0), toneStrength);
    }

    // ---- 2. Contrast (UE4-style power curve) ----
    float contrastPower = mix(1.0, 1.2, (contrast - 0.5) * 2.0);
    color = pow(clamp(color, 0.0, 1.0), vec3(contrastPower));

    // ---- 3. Saturation ----
    if (saturation > 0.0) {
        float lum = luminance(color);
        float satMul = 1.0 + saturation * 0.5;
        color = mix(vec3(lum), color, satMul);
    }

    // ---- 4. Shadow lift (UE4 cinematic look, shadows only) ----
    if (lift > 0.0) {
        vec3 lifted = color + lift * 0.3 * (1.0 - color);
        float darkMask = 1.0 - smoothstep(0.0, 0.5, luminance(color));
        color = mix(color, lifted, darkMask * lift);
    }

    color = clamp(color, 0.0, 1.0);
    gl_FragColor = vec4(color, 1.0);
}
