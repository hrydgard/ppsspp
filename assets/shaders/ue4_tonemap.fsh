#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform vec4 u_setting;
uniform sampler2D sampler0;
varying vec4 v_texcoord0;

// 计算亮度（ITU-R BT.601）
float luminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

// 简化版 ACES tone mapping
vec3 aceTonemap(vec3 color) {
    // 简化 ACES 曲线: color / (color + 0.155) * 1.2
    return color / (color + vec3(0.155)) * 1.2;
}

void main() {
    // 采样原始颜色
    vec3 color = texture2D(sampler0, v_texcoord0.xy).xyz;
    
    // 获取用户参数
    float toneStrength = u_setting.x;  // 色调映射强度 (0.0 ~ 1.0)
    float contrast = u_setting.y;      // 对比度 (0.5 ~ 2.0)
    float saturation = u_setting.z;    // 饱和度 (0.0 ~ 2.0)
    float lift = u_setting.w;          // 暗部提亮 (0.0 ~ 1.0)
    
    // 保存原始颜色用于混合
    vec3 originalColor = color;
    
    // === UE4 风格色调映射 ===
    
    // 1. ACES tone mapping（简化版）
    vec3 tonemapped = aceTonemap(color);
    color = mix(color, tonemapped, toneStrength);
    
    // 2. 对比度增强（UE4 风格）
    // 将对比度参数映射到合理的范围
    float contrastPower = mix(1.0, 1.2, (contrast - 0.5) * 2.0);
    color = pow(color, vec3(contrastPower));
    
    // 3. 饱和度调整（UE4 风格）
    float lum = luminance(color);
    vec3 gray = vec3(lum);
    float satMultiplier = 1.0 + saturation * 0.5;
    color = mix(gray, color, satMultiplier);
    
    // 4. 暗部提亮 + 高光压制（UE4 Cinematic Look）
    if (lift > 0.0) {
        // 暗部提亮：使用平滑曲线
        vec3 lifted = color + lift * 0.3 * (1.0 - color);
        // 只影响暗部，不影响亮部（基于亮度判断）
        float lumForMask = luminance(color);
        float darkMask = 1.0 - smoothstep(0.0, 0.5, lumForMask);
        color = mix(color, lifted, darkMask * lift);
    }
    
    // 5. 最终颜色修正和钳位
    color = clamp(color, 0.0, 1.0);
    
    // 输出最终颜色
    gl_FragColor = vec4(color, 1.0);
}
