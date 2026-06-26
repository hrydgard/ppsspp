#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — edge darkening with scattering (v5)
// Simple, robust, strong effect. 16 taps.
//
// u_setting:
//   .x = Strength  [-1, 2]  default 1.0
//   .y = Radius    [1, 6]   default 3.0
//   .z = Color     [0, 1]   0=black, 1=dark grey  default 0.0
//   .w = Blend     [0, 1]   default 0.7

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);

    float strength  = u_setting.x;
    float radius    = max(u_setting.y, 1.0);
    float colorMode = u_setting.z;
    float blend     = u_setting.w;

    // Sample 8 neighbors at 2 distances (16 taps total)
    float aoAccum = 0.0;
    int aoCount = 0;

    // 8 directions, 45 degrees apart
    // Use precomputed sin/cos to avoid potential driver issues
    vec2 dir0 = vec2( 1.0000,  0.0000);
    vec2 dir1 = vec2( 0.7071,  0.7071);
    vec2 dir2 = vec2( 0.0000,  1.0000);
    vec2 dir3 = vec2(-0.7071,  0.7071);
    vec2 dir4 = vec2(-1.0000,  0.0000);
    vec2 dir5 = vec2(-0.7071, -0.7071);
    vec2 dir6 = vec2( 0.0000, -1.0000);
    vec2 dir7 = vec2( 0.7071, -0.7071);

    // Ring 1: radius * 0.5 (closer, weight 1.0)
    float r1 = radius * 0.5;
    vec2 off1_0 = dir0 * u_texelDelta * r1;
    vec2 off1_1 = dir1 * u_texelDelta * r1;
    vec2 off1_2 = dir2 * u_texelDelta * r1;
    vec2 off1_3 = dir3 * u_texelDelta * r1;
    vec2 off1_4 = dir4 * u_texelDelta * r1;
    vec2 off1_5 = dir5 * u_texelDelta * r1;
    vec2 off1_6 = dir6 * u_texelDelta * r1;
    vec2 off1_7 = dir7 * u_texelDelta * r1;

    float l1_0 = luminance(texture2D(sampler0, v_texcoord0 + off1_0).rgb);
    float l1_1 = luminance(texture2D(sampler0, v_texcoord0 + off1_1).rgb);
    float l1_2 = luminance(texture2D(sampler0, v_texcoord0 + off1_2).rgb);
    float l1_3 = luminance(texture2D(sampler0, v_texcoord0 + off1_3).rgb);
    float l1_4 = luminance(texture2D(sampler0, v_texcoord0 + off1_4).rgb);
    float l1_5 = luminance(texture2D(sampler0, v_texcoord0 + off1_5).rgb);
    float l1_6 = luminance(texture2D(sampler0, v_texcoord0 + off1_6).rgb);
    float l1_7 = luminance(texture2D(sampler0, v_texcoord0 + off1_7).rgb);

    // Ring 2: radius * 1.0 (farther, weight 0.5)
    float r2 = radius;
    vec2 off2_0 = dir0 * u_texelDelta * r2;
    vec2 off2_1 = dir1 * u_texelDelta * r2;
    vec2 off2_2 = dir2 * u_texelDelta * r2;
    vec2 off2_3 = dir3 * u_texelDelta * r2;
    vec2 off2_4 = dir4 * u_texelDelta * r2;
    vec2 off2_5 = dir5 * u_texelDelta * r2;
    vec2 off2_6 = dir6 * u_texelDelta * r2;
    vec2 off2_7 = dir7 * u_texelDelta * r2;

    float l2_0 = luminance(texture2D(sampler0, v_texcoord0 + off2_0).rgb);
    float l2_1 = luminance(texture2D(sampler0, v_texcoord0 + off2_1).rgb);
    float l2_2 = luminance(texture2D(sampler0, v_texcoord0 + off2_2).rgb);
    float l2_3 = luminance(texture2D(sampler0, v_texcoord0 + off2_3).rgb);
    float l2_4 = luminance(texture2D(sampler0, v_texcoord0 + off2_4).rgb);
    float l2_5 = luminance(texture2D(sampler0, v_texcoord0 + off2_5).rgb);
    float l2_6 = luminance(texture2D(sampler0, v_texcoord0 + off2_6).rgb);
    float l2_7 = luminance(texture2D(sampler0, v_texcoord0 + off2_7).rgb);

    // Average neighbor brightness
    float avgNeighbor = (l1_0 + l1_1 + l1_2 + l1_3 + l1_4 + l1_5 + l1_6 + l1_7
                       + l2_0 + l2_1 + l2_2 + l2_3 + l2_4 + l2_5 + l2_6 + l2_7) / 16.0;

    // AO: how much brighter are neighbors than center?
    float diff = avgNeighbor - centerLum;

    // STRONG smoothstep — low threshold so even small differences trigger
    float aoFactor = smoothstep(0.002, 0.08, diff);

    // Quantized AO color
    vec3 aoColor = (colorMode < 0.5) ? vec3(0.0, 0.0, 0.0) : vec3(0.08, 0.08, 0.10);

    vec3 result;
    if (strength < 0.0) {
        // Negative: brighten
        result = mix(color, vec3(1.0, 1.0, 1.0), clamp(abs(strength) * aoFactor, 0.0, 1.0));
    } else {
        result = mix(color, aoColor, clamp(strength * aoFactor, 0.0, 1.0));
    }

    result = mix(color, result, blend);

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
