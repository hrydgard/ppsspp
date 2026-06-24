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

vec3 uncharted2Tonemap(vec3 x) {
    float A = 0.15, B = 0.50, C = 0.10;
    float D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 filmicTonemap(vec3 color) {
    vec3 curr = uncharted2Tonemap(color * 2.0);
    float W = 11.2;
    float exposureBias = uncharted2Tonemap(vec3(W)).r;
    return curr / exposureBias;
}

float sobelEdge(sampler2D tex, vec2 uv, vec2 texelD) {
    float tl = luminance(texture2D(tex, uv + vec2(-texelD.x, -texelD.y)).rgb);
    float tc = luminance(texture2D(tex, uv + vec2(0.0, -texelD.y)).rgb);
    float tr = luminance(texture2D(tex, uv + vec2(texelD.x, -texelD.y)).rgb);
    float ml = luminance(texture2D(tex, uv + vec2(-texelD.x, 0.0)).rgb);
    float mr = luminance(texture2D(tex, uv + vec2(texelD.x, 0.0)).rgb);
    float bl = luminance(texture2D(tex, uv + vec2(-texelD.x, texelD.y)).rgb);
    float bc = luminance(texture2D(tex, uv + vec2(0.0, texelD.y)).rgb);
    float br = luminance(texture2D(tex, uv + vec2(texelD.x, texelD.y)).rgb);
    float gx = -tl + tr - 2.0 * ml + 2.0 * mr - bl + br;
    float gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;
    return sqrt(gx * gx + gy * gy);
}

void main() {
    vec3 color = texture2D(sampler0, v_texcoord0).rgb;

    // 1. Uncharted 2 Filmic Tonemap
    color = mix(color, filmicTonemap(color), u_setting.x);

    // 2. Rim Light with Sobel edge detection
    float quality = u_setting.w;
    vec2 sobelDelta = u_texelDelta * (1.0 + quality * 0.5);
    float edge = sobelEdge(sampler0, v_texcoord0, sobelDelta);
    float rimStrength = u_setting.y;
    float rimThreshold = 0.2;
    float rim = smoothstep(rimThreshold * 0.5, rimThreshold * 1.5, edge) * rimStrength * 0.8;
    vec3 rimColor = vec3(rim * 0.8, rim * 0.9, rim * 1.0);
    color += rimColor;

    // 3. Color grading
    float lum = luminance(color);
    float warmCool = 0.0;
    float saturationGain = 1.0;
    float shadowLift = 0.0;

    // Saturation
    vec3 graded = mix(vec3(lum), color, saturationGain);

    // Warm/Cool
    graded.r += warmCool * 0.05;
    graded.b -= warmCool * 0.05;

    // Shadow lift
    float shadowMask = smoothstep(0.0, 0.3, lum);
    shadowMask = 1.0 - shadowMask;
    graded += vec3(shadowLift * shadowMask);

    color = mix(color, graded, u_setting.z);
    color = clamp(color, 0.0, 1.0);

    gl_FragColor = vec4(color, 1.0);
}
