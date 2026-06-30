#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// GodRays — Stable single-source volumetric light scattering (v4)
// =================================================================
// Single user-fixed light source with Crytek radial sampling.
// No auto-detection, no probing, no refinement — just stable rays.
// Per-fragment jitter breaks radial banding.
//
// Reference: "Volumetric Light Scattering as a Post-Process"
//            (Crytek, GPU Gems 3, Ch. 13)
//
// u_setting:
//   .x = Intensity    [-1, 1]  (negative = dark rays)  default 0.15
//   .y = Light Pos X  [0, 1]                            default 0.5
//   .z = Light Pos Y  [0, 1]                            default 0.3
//   .w = Quality      [0, 2]   0=24/1=48/2=72 samples   default 1.0

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Cheap deterministic hash for jitter (breaks radial banding).
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    float intensity = u_setting.x;
    // If intensity is exactly 0, no effect.
    if (intensity == 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    // Fixed user light position — no auto-detection.
    vec2 lightPos = vec2(u_setting.y, u_setting.z);
    float quality = u_setting.w;

    int numSamples = (quality < 0.5) ? 24
                   : (quality < 1.5) ? 48
                   :                    72;

    // Crytek radial sampling parameters.
    const float weight          = 0.018;
    const float decay           = 0.965;
    const float exposure        = 0.42;
    const float brightThreshold = 0.40;

    // Per-fragment jitter for anti-banding.
    float jitter = hash12(v_texcoord0 * 1024.0 + gl_FragCoord.xy);

    // Crytek radial sampling from current pixel toward the light.
    vec2 deltaTextCoord = (v_texcoord0 - lightPos);
    deltaTextCoord *= (1.0 / float(numSamples)) * 1.05;

    vec2 sampleCoord = v_texcoord0;
    float illuminationDecay = 1.0;
    // Skip first 'jitter' fraction for anti-banding.
    sampleCoord -= deltaTextCoord * jitter;

    vec3 fragColor = vec3(0.0);

    for (int i = 0; i < 72; i++) {
        if (i >= numSamples) break;
        sampleCoord -= deltaTextCoord;
        vec2 sc = clamp(sampleCoord, vec2(0.001), vec2(0.999));
        vec3 samp = texture2D(sampler0, sc).rgb;
        float l = luminance(samp);
        // Brightness threshold gating: only bright pixels contribute.
        float brightGate = max(l - brightThreshold, 0.0) * 3.5;
        samp *= illuminationDecay * weight * brightGate;
        fragColor += samp;
        illuminationDecay *= decay;
    }

    // Warm sun tint.
    vec3 rayTint = vec3(1.00, 0.94, 0.78);

    if (intensity > 0.0) {
        // Bright rays (normal god rays).
        color += fragColor * exposure * rayTint * intensity;
    } else {
        // Negative intensity: dark rays (shadow streaks).
        color -= fragColor * exposure * rayTint * abs(intensity);
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
