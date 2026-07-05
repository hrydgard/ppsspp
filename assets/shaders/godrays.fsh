#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// GodRays — Volumetric light scattering (v5 optimized)
// =================================================================
// Crytek radial sampling (GPU Gems 3, Ch.13) with dithered jitter.
// Lower sample count + better dither = same quality, better perf.
//
// u_setting:
//   .x = Intensity    [-1, 1]  (negative = dark rays)  default 0.2
//   .y = Light Pos X  [0, 1]                            default 0.5
//   .z = Light Pos Y  [0, 1]                            default 0.3
//   .w = Quality      [0, 2]   0=16/1=24/2=32 samples   default 1.0

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;
varying vec2 v_texcoord0;

float lum(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
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

    // ponytail: reduced from 24/48/72 to 16/24/32 — dither covers the gap
    int numSamples = (quality < 0.5) ? 16
                   : (quality < 1.5) ? 24
                   :                    32;

    // Tuned constants (v5): lower threshold, faster decay, higher weight
    const float weight = 0.025;
    const float decay = 0.94;
    const float exposure = 0.40;
    const float brightThreshold = 0.30;

    // Dithered jitter — breaks banding with fewer samples
    float jitter = hash12(v_texcoord0 * 1024.0 + gl_FragCoord.xy);

    vec2 deltaTextCoord = (v_texcoord0 - lightPos);
    deltaTextCoord *= (1.0 / float(numSamples)) * 1.05;

    vec2 sampleCoord = v_texcoord0;
    sampleCoord -= deltaTextCoord * jitter;

    vec3 rayColor = vec3(0.0);
    float illuminationDecay = 1.0;

    for (int i = 0; i < 32; i++) {
        if (i >= numSamples) break;
        sampleCoord -= deltaTextCoord;
        vec2 sc = clamp(sampleCoord, vec2(0.001), vec2(0.999));
        vec3 samp = texture2D(sampler0, sc).rgb;
        float l = lum(samp);
        float gate = max(l - brightThreshold, 0.0) * 3.5;
        rayColor += samp * illuminationDecay * weight * gate;
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
