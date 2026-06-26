#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// GodRays — Multi-Directional Volumetric Light Scattering (v3)
// =================================================================
// Automatically detects bright areas in the frame and casts god rays
// from multiple light source directions. The number of light sources
// is user-controllable (1-3). Uses Crytek radial sampling with
// per-fragment jitter for anti-banding.
//
// Reference: "Volumetric Light Scattering as a Post-Process"
//            (Crytek, GPU Gems 3, Ch. 13)
//
// u_setting:
//   .x = Intensity    [0, 2]   (0 = off)    default 0.6
//   .y = Ray Sources  [1, 3]   (integer)    default 2
//   .z = Light Pos X  [0, 1]   hint for primary light  default 0.5
//   .w = Quality      [0, 2]   0=32samp/1=64samp/2=96samp  default 1.0

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

// Baked defaults.
const float DEFAULT_INTENSITY = 0.6;
const float DEFAULT_SOURCES   = 2.0;
const float DEFAULT_LIGHT_X   = 0.5;
const float DEFAULT_QUALITY   = 1.0;

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
    if (intensity <= 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    float numSourcesF = (u_setting.y > 0.0) ? u_setting.y : DEFAULT_SOURCES;
    numSourcesF = clamp(numSourcesF, 1.0, 3.0);
    int numSources = int(numSourcesF + 0.5);

    float userLightX = (u_setting.z > 0.0) ? u_setting.z : DEFAULT_LIGHT_X;
    float quality = (u_setting.w > 0.0) ? u_setting.w : DEFAULT_QUALITY;

    int numSamples = (quality < 0.5) ? 32
                   : (quality < 1.5) ? 64
                   :                    96;
    int samplesPerSource = numSamples / numSources;

    // ---- PROBE PHASE: sample 8 fixed screen positions ----
    // 3x3 grid minus center: 8 positions at
    // (0.1,0.1),(0.5,0.1),(0.9,0.1),(0.1,0.5),(0.9,0.5),
    // (0.1,0.9),(0.5,0.9),(0.9,0.9)
    float topLum1 = -1.0;
    float topLum2 = -1.0;
    float topLum3 = -1.0;
    vec2  topPos1 = vec2(0.5);
    vec2  topPos2 = vec2(0.5);
    vec2  topPos3 = vec2(0.5);

    for (int i = 0; i < 8; i++) {
        // Compute probe position from index.
        float col, row;
        if (i < 3) {
            col = float(i);
            row = 0.0;
        } else if (i < 5) {
            col = float((i - 3) * 2);
            row = 1.0;
        } else {
            col = float(i - 5);
            row = 2.0;
        }
        float px = col * 0.4 + 0.1;  // 0->0.1, 1->0.5, 2->0.9
        float py = row * 0.4 + 0.1;
        vec2 probeUV = vec2(px, py);

        float pl = luminance(texture2D(sampler0, probeUV).rgb);

        // Insertion sort for top 3 brightest.
        if (pl > topLum1) {
            topLum3 = topLum2; topPos3 = topPos2;
            topLum2 = topLum1; topPos2 = topPos1;
            topLum1 = pl;       topPos1 = probeUV;
        } else if (pl > topLum2) {
            topLum3 = topLum2; topPos3 = topPos2;
            topLum2 = pl;       topPos2 = probeUV;
        } else if (pl > topLum3) {
            topLum3 = pl;       topPos3 = probeUV;
        }
    }

    // ---- RAY CASTING: Crytek radial sampling toward each light source ----
    const float weight          = 0.018;
    const float decay           = 0.965;
    const float exposure        = 0.42;
    const float brightThreshold = 0.40;

    float jitter = hash12(v_texcoord0 * 1024.0 + gl_FragCoord.xy);

    vec3 fragColor = vec3(0.0);
    vec2 primaryLightPos = vec2(0.5);

    for (int src = 0; src < 3; src++) {
        if (src >= numSources) break;

        // Select light position from top-N brightest probes.
        vec2 lightPos;
        if (src == 0)      lightPos = topPos1;
        else if (src == 1) lightPos = topPos2;
        else               lightPos = topPos3;

        // 3x3 refinement: find the brightest pixel near the probe position.
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

        // For primary source, blend user X hint with auto-detected position.
        if (src == 0) {
            if (bestLum < 0.05) {
                // Nothing bright found — fall back to user hint.
                actualLightPos = vec2(userLightX, 0.5);
            } else {
                // Blend user X hint with auto-detected X.
                actualLightPos = vec2(
                    mix(userLightX, actualLightPos.x, 0.5),
                    actualLightPos.y
                );
            }
            primaryLightPos = actualLightPos;
        }

        // Crytek radial sampling from current pixel toward the light.
        vec2 deltaTextCoord = (v_texcoord0 - actualLightPos);
        deltaTextCoord *= (1.0 / float(samplesPerSource)) * 1.05;

        vec2 sampleCoord = v_texcoord0;
        float illuminationDecay = 1.0;
        // Skip first 'jitter' fraction for anti-banding.
        sampleCoord -= deltaTextCoord * jitter;

        for (int i = 0; i < 96; i++) {
            if (i >= samplesPerSource) break;
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
    }

    // Depth boost: rays are brighter when the light source is far.
    float dist = length(v_texcoord0 - primaryLightPos);
    float depthBoost = 1.0 + 0.6 * smoothstep(0.0, 0.5, dist);

    // Warm sun tint.
    vec3 rayTint = vec3(1.00, 0.94, 0.78);
    color += fragColor * exposure * rayTint * intensity * depthBoost;

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
