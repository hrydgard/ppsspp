#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Krzysztof Narkowicz 2015 ACES Filmic Tone Mapping Curve
// Industry-standard cinematic tone mapper, smooth highlight rolloff
vec3 acesFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Soft rim/edge light via multi-tap luminance gradient
// Returns a soft halo rather than a hard white line
float edgeFalloff(sampler2D tex, vec2 uv, vec2 texelD) {
    float c  = luminance(texture2D(tex, uv).rgb);
    float l  = luminance(texture2D(tex, uv + vec2(-texelD.x, 0.0)).rgb);
    float r  = luminance(texture2D(tex, uv + vec2( texelD.x, 0.0)).rgb);
    float u  = luminance(texture2D(tex, uv + vec2(0.0, -texelD.y)).rgb);
    float d  = luminance(texture2D(tex, uv + vec2(0.0,  texelD.y)).rgb);
    // 4-tap soft edge: only the "darker surroundings" side gets the halo
    float diffL = max(c - l, 0.0);
    float diffR = max(c - r, 0.0);
    float diffU = max(c - u, 0.0);
    float diffD = max(c - d, 0.0);
    return max(max(diffL, diffR), max(diffU, diffD));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    // 1. ACES Filmic tone map (Narkowicz 2015)
    //    u_setting.x = strength [0,1]. 0 = passthrough, 1 = full ACES
    color = mix(color, acesFilmic(color), u_setting.x);

    // 2. Rim light using soft luminance gradient
    //    u_setting.y = strength. Modulates color tint, not just adds white.
    //    u_setting.w = quality (0=cheap, 1=normal, 2=high) -> controls texel spread
    if (u_setting.y > 0.0) {
        float quality = u_setting.w;
        vec2 sampleDelta = u_texelDelta * (1.0 + quality * 1.5);
        float edge = edgeFalloff(sampler0, v_texcoord0, sampleDelta);
        // Soft falloff: low power to keep it gentle, no hard "white lines"
        float rim = pow(edge, 0.6) * u_setting.y * 0.45;
        // Warm-tinted rim light (orange/amber), mimics how real backlights look
        vec3 rimColor = vec3(1.05, 0.92, 0.78) * rim;
        // Multiplicative blend: rim light brightens existing color rather than
        // overlaying pure white. This is what gives UE5 the "subtle halo" look.
        color = color + rimColor * (1.0 - color);
    }

    // 3. Color grading (luminance-based, intensity-driven by u_setting.z)
    //    Saturation gain + warm/cool + soft shadow tint.
    if (u_setting.z > 0.0) {
        float lum = luminance(color);
        // Slight saturation boost (UE5 default push)
        vec3 graded = mix(vec3(lum), color, 1.12);
        // Cool-warm balance: push midtones warmer (UE5 default look)
        graded.r += 0.02;
        graded.b -= 0.01;
        // Soft shadow lift: only the darkest 25% gets a slight cyan tint
        float darkMask = 1.0 - smoothstep(0.0, 0.25, lum);
        graded = mix(graded, graded + vec3(-0.005, 0.0, 0.012), darkMask * 0.3);
        color = mix(color, graded, u_setting.z);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
