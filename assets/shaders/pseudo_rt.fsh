#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// PseudoRT — Higher-quality horizon-based AO with indirect light (v3)
// =================================================================
// Same horizon-based occlusion as pseudo_ao, but with 12 directions
// and 3 distances per direction (36 taps). Also accumulates indirect
// light from brighter neighbors for a warm spill effect.
//
// u_setting:
//   .x = AO Strength      [0, 1]   (0 = off)    default 0.6
//   .y = Indirect Light   [0, 1]   (0 = off)    default 0.25
//   .z = Sample Radius    [1, 6]   texel count  default 4.0
//   .w = Blend            [0, 1]   default 0.9

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

// Baked defaults.
const float DEFAULT_STRENGTH = 0.6;
const float DEFAULT_INDIRECT = 0.25;
const float DEFAULT_RADIUS   = 4.0;
const float DEFAULT_BLEND    = 0.9;

// Fixed AO color for the "premium" version — dark grey, not user-controllable.
const vec3 AO_COLOR      = vec3(0.04, 0.045, 0.055);
const vec3 INDIRECT_TINT = vec3(1.0, 0.88, 0.72);

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float aoStrength       = u_setting.x;
    float indirectStrength = u_setting.y;
    float radius           = max(u_setting.z, 1.0);
    float blend            = (u_setting.w > 0.0) ? u_setting.w : DEFAULT_BLEND;

    // If both AO and indirect light are off, pass through.
    if (aoStrength <= 0.0 && indirectStrength <= 0.0) {
        gl_FragColor = vec4(color, 1.0);
        return;
    }

    // 12 directions: 0, 30, 60, ..., 330 degrees.
    const float PI_6 = 0.5235987755;

    float maxOcclusion = 0.0;

    // Indirect light accumulation.
    vec3 indirectSpill = vec3(0.0);

    for (int i = 0; i < 12; i++) {
        float angle = float(i) * PI_6;
        vec2 dir = vec2(cos(angle), sin(angle));

        // Track the maximum luminance (horizon) in this direction.
        float horizon = 0.0;

        // 3 distances: radius*0.333, radius*0.666, radius*1.0
        for (int d = 0; d < 3; d++) {
            float distFrac = (float(d) + 1.0) / 3.0; // 0.333, 0.666, 1.0
            vec2 off = dir * u_texelDelta * (radius * distFrac);
            float l = luminance(texture2D(sampler0, v_texcoord0 + off).rgb);
            horizon = max(horizon, l);
        }

        // AO: occlusion from this direction — if neighbor is brighter,
        // the current pixel is "occluded" from that direction.
        float occ = max(0.0, horizon - centerLum);
        maxOcclusion = max(maxOcclusion, occ);

        // Indirect light: if this direction has a brighter neighbor by >0.04,
        // accumulate warm spill.
        if (horizon > centerLum + 0.04) {
            float spill = horizon - centerLum;
            indirectSpill += INDIRECT_TINT * spill * indirectStrength * 0.3;
        }
    }

    // AO: smooth the occlusion into a soft falloff.
    float aoFactor = smoothstep(0.03, 0.35, maxOcclusion);

    // Blend toward fixed AO color.
    vec3 aoDarkened = mix(color, AO_COLOR, aoFactor * aoStrength);

    // Combine: AO darkens, indirect light adds warm spill.
    vec3 result = aoDarkened + indirectSpill;
    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
