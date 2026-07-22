#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — multi-radius cross-bilateral contact-shadow AO (v8)
// =================================================================
// Replaces the old single-radius 8-tap edge darkening. No depth buffer
// is available, so AO is estimated from screen-space luminance only,
// following the "luminance-gradient AO" family (Kronnect LBAO,
// McGuire & Mara "Scalable Ambient Obscurance") with a joint-bilateral
// (cross-bilateral) range weight to avoid bleeding across edges
// (Kopf et al. "Joint Bilateral Upsampling").
//
// For each of 8 directions we sample 2 radii (r, 2r) = 16 taps. A
// neighbor brighter than the center indicates an occluder behind the
// center, so we accumulate occlusion weighted by a range weight
// w = 1/(1 + (dL^2)/sigma) (luminance similarity) and a 1/dist spatial
// weight. This yields soft, multi-scale contact shadows instead of a
// single hard edge darken.
//
// u_setting:
//   .x = Strength  [-1, 2]  default 1.0
//   .y = Radius    [1, 6]   default 3.0
//   .z = AO Color  [0, 1]   0=black, 1=dark grey  default 0.0
//   .w = Blend     [0, 1]   default 0.7

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;
varying vec2 v_texcoord0;

// Rec.709 luma
float lum(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float c = lum(color);

    float strength  = u_setting.x;
    float radius    = max(u_setting.y, 1.0);
    float colorMode = u_setting.z;
    float blend     = u_setting.w;

    const float PI_4  = 0.7853981633;
    const float SIGMA = 0.15;   // range (luminance) weight falloff

    float occ  = 0.0;
    float wsum = 0.0;

    for (int i = 0; i < 8; i++) {
        float angle = float(i) * PI_4;
        vec2 dir = vec2(cos(angle), sin(angle));
        for (int r = 1; r <= 2; r++) {
            float dist = float(r);
            vec2 off = dir * u_texelDelta * (radius * dist);
            float l = lum(texture2D(sampler0, v_texcoord0 + off).rgb);
            float dL = l - c;                 // >0: neighbor brighter (occluder behind)
            float w = 1.0 / (1.0 + (dL * dL) / SIGMA) * (1.0 / dist);
            if (dL > 0.0) {
                occ  += dL * w;
                wsum += w;
            }
        }
    }
    float ao = (wsum > 0.0) ? occ / wsum : 0.0;

    // Wider lower edge keeps us clear of mediump precision floor (0.001).
    float aoAmt = smoothstep(0.01, 0.25, ao);
    float amt = clamp(strength * aoAmt, 0.0, 1.0);

    if (strength < 0.0) {
        amt = clamp(abs(strength) * aoAmt, 0.0, 1.0);
        color = mix(color, vec3(1.0), amt);
    } else {
        // Quantized AO color (black / dark grey) per user preference.
        vec3 aoColor = vec3(0.0);
        if (colorMode > 0.5) {
            aoColor = vec3(0.08, 0.08, 0.10);
        }
        color = mix(color, aoColor, amt);
    }

    color = mix(texture2D(sampler0, v_texcoord0).rgb, color, blend);
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
