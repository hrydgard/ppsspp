#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — Sobel edge darkening (v7)
// 8-tap luminance gradient → darken dark side of edges.
// Pattern follows bloom.fsh (simplest working PPSSPP shader).
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

float lum(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float c = lum(color);

    float strength = u_setting.x;
    float radius = max(u_setting.y, 1.0);
    float colorMode = u_setting.z;
    float blend = u_setting.w;

    vec2 t = u_texelDelta * radius;

    // 8 neighbors at radius distance
    float l = lum(texture2D(sampler0, v_texcoord0 + vec2(-t.x, -t.y)).rgb);
    float r = lum(texture2D(sampler0, v_texcoord0 + vec2( t.x, -t.y)).rgb);
    float u = lum(texture2D(sampler0, v_texcoord0 + vec2(-t.x,  t.y)).rgb);
    float d = lum(texture2D(sampler0, v_texcoord0 + vec2( t.x,  t.y)).rgb);
    float ll = lum(texture2D(sampler0, v_texcoord0 + vec2(-t.x, 0.0)).rgb);
    float rr = lum(texture2D(sampler0, v_texcoord0 + vec2( t.x, 0.0)).rgb);
    float uu = lum(texture2D(sampler0, v_texcoord0 + vec2(0.0, -t.y)).rgb);
    float dd = lum(texture2D(sampler0, v_texcoord0 + vec2(0.0,  t.y)).rgb);

    float avg = (l + r + u + d + ll + rr + uu + dd) * 0.125;

    // Dark side: center is darker than neighbors
    float diff = avg - c;

    // Aggressive smoothstep — catches tiny differences
    float ao = smoothstep(0.001, 0.05, diff);

    // Apply strength (negative = brighten)
    float amt = clamp(strength * ao, 0.0, 1.0);
    if (strength < 0.0) {
        amt = clamp(abs(strength) * ao, 0.0, 1.0);
        color = mix(color, vec3(1.0), amt);
    } else {
        // Quantized AO color
        vec3 aoColor = vec3(0.0);
        if (colorMode > 0.5) {
            aoColor = vec3(0.08, 0.08, 0.10);
        }
        color = mix(color, aoColor, amt);
    }

    color = mix(texture2D(sampler0, v_texcoord0).rgb, color, blend);
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
