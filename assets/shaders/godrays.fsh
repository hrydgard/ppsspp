#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// God Rays (Volumetric Light Scattering) - Screen Space
// Based on "Volumetric Light Scattering as a Post-Process"
// (Crytek / GPU Gems 3, Ch. 13) and Erkaman's glsl-godrays reference.
//
// PPSSPP has no depth/occlusion buffer, so we approximate the
// "occlusion texture" by darkening everything that is not a
// bright surface (luminance-thresholded input). The user supplies
// the light position in screen space; light source is the brightest
// pixel near that position.
//
// u_setting:
//   .x = intensity [0,2]            (0 = off)
//   .y = light X in screen UV [0,1] (left=0, right=1)
//   .z = light Y in screen UV [0,1] (top=0,  bottom=1)
//   .w = sample quality  [0,2]      (0=24samp / 1=48samp / 2=80samp)

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Static god-rays scatter. No time animation, no moving rays — the
// rays stay anchored to the screen-space light position. Looks like
// a soft light leak rather than a moving spotlight.
vec3 godrays(vec2 uv, vec2 lightPos, int numSamples) {
    // Per-pixel "occlusion" approximation: bright pixels contribute,
    // dark ones don't. Threshold ~ 0.55 so only the lit area glows.
    const float brightThreshold = 0.55;

    vec2 deltaTextCoord = (uv - lightPos) * (1.0 / float(numSamples)) * 0.95;

    vec2 sampleCoord = uv;
    vec3 fragColor = vec3(0.0);
    float illuminationDecay = 1.0;
    // Tuned constants per Crytek paper
    const float weight = 0.02;
    const float decay   = 0.96;
    const float exposure = 0.34;

    for (int i = 0; i < 80; i++) {
        if (i >= numSamples) break;
        sampleCoord -= deltaTextCoord;
        vec3 samp = texture2D(sampler0, sampleCoord).rgb;
        float lum = luminance(samp);
        // Only bright pixels contribute (simulates an "occlusion" buffer
        // where the light is the only non-black thing).
        samp *= max(lum - brightThreshold, 0.0) * 4.0;
        samp *= illuminationDecay * weight;
        fragColor += samp;
        illuminationDecay *= decay;
    }
    return fragColor * exposure;
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    if (u_setting.x > 0.0) {
        // Quality: 0 -> 24, 1 -> 48, 2 -> 80 samples
        float quality = u_setting.w;
        int numSamples;
        if (quality < 0.5)      numSamples = 24;
        else if (quality < 1.5) numSamples = 48;
        else                    numSamples = 80;

        vec2 lightPos = vec2(u_setting.y, u_setting.z);
        vec3 rays = godrays(v_texcoord0, lightPos, numSamples);

        // Warm sun-light color for the rays (closer to natural sunlight)
        vec3 rayTint = vec3(1.0, 0.94, 0.78);
        color += rays * rayTint * u_setting.x;
    }

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
