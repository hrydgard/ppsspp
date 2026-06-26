#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Pseudo AO — edge darkening (v6)
// Uses 8-direction average comparison to detect edges.
// If center is on dark side of edge, apply black/dark-grey.
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

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;
    float centerLum = luminance(color);
    float strength = u_setting.x;
    float radius = max(u_setting.y, 1.0);
    float colorMode = u_setting.z;
    float blend = u_setting.w;

    // 8-direction sampling at radius distance
    float sumLum = 0.0;
    int count = 0;

    for (int i = 0; i < 8; i++) {
        float angle = float(i) * 0.7854;
        vec2 off = vec2(cos(angle), sin(angle)) * u_texelDelta * radius;
        sumLum += luminance(texture2D(sampler0, v_texcoord0 + off).rgb);
        count++;
    }

    float avgNeighbor = sumLum / float(count);
    float diff = avgNeighbor - centerLum;
    float aoFactor = smoothstep(0.001, 0.06, diff);

    vec3 aoColor = (colorMode < 0.5) ? vec3(0.0) : vec3(0.08, 0.08, 0.10);
    vec3 result;

    if (strength < 0.0) {
        result = mix(color, vec3(1.0), clamp(abs(strength) * aoFactor, 0.0, 1.0));
    } else {
        result = mix(color, aoColor, clamp(strength * aoFactor, 0.0, 1.0));
    }

    result = mix(color, result, blend);
    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
