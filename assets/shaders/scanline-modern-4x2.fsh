/*
================================================================================
Scanline Modern 4x2

This shader is not designed to simply simulate the scanline + aperture grille 
effect of old CRT monitors. Instead, it aims to combine the advantages of sharp 
clarity on modern displays with retro games, enabling better pixel-level scaling.

The generation intensity of scanlines is dynamically quantized and adjusted based 
on the human eye's perceptual curve for chromatic brightness, rather than using 
rigid stripe overlay.

Core Features:
- Lossless RGB Chromaticity and Luminance: Implements a symmetric brightness 
  compensation loop where the quantization attenuation applied to dark sectors 
  is dynamically counterbalanced and redistributed within the bright cycles. 
  This fundamentally resolves the inherent chromaticity degradation and exposure 
  loss typical of traditional scanline shaders caused by naive stripe overlaying.
- Fixed vertical scanline period to 4 pixels and horizontal mask to 2 pixels 
  to establish a native physical grid on modern 2K/4K displays. This efficiently 
  masks and embellishes the glaring grid unevenness caused by non-integer scaling 
  when rendering high-res PSX low-poly/pixel titles.
- Leverages sub-pixel edge detection to smoothly transition and camouflage 
  misalignment artifacts where the virtual game pixels fail to align 
  pixel-to-pixel with the modern LCD hardware.
- Optimized scanline performance based on human eye brightness sensitivity curve: 
  scanlines are prominent in medium brightness areas and weakened in extreme 
  brightness areas.

(C) 2025-2026 by crashGG.
================================================================================
*/

#ifdef GL_ES
precision mediump float;
#endif


uniform sampler2D u_tex0;
uniform vec2 u_texelDelta;
uniform vec4 u_setting;

varying vec2 v_texcoord0;

void main() {

	// --- UI Parameters ---
	float compY = u_setting.y;
	float compX = u_setting.x;

    // 5-tap sampling: center plus down, up, left, right neighboring pixels
    vec3 texel = texture2D(u_tex0, v_texcoord0).rgb;
    vec3 tex_D = texture2D(u_tex0, v_texcoord0 + vec2(0.0, u_texelDelta.y)).rgb;
    vec3 tex_U = texture2D(u_tex0, v_texcoord0 + vec2(0.0, -u_texelDelta.y)).rgb;
    vec3 tex_L = texture2D(u_tex0, v_texcoord0 + vec2(-u_texelDelta.x, 0.0)).rgb;
    vec3 tex_R = texture2D(u_tex0, v_texcoord0 + vec2(u_texelDelta.x, 0.0)).rgb;

    // Calculate luminance (r+g+b) and normalize to 1.0
    float lumaC = dot(texel, vec3(0.3333333));
    float lumaD = dot(tex_D, vec3(0.3333333));
    float lumaU = dot(tex_U, vec3(0.3333333));
    float lumaL = dot(tex_L, vec3(0.3333333));
    float lumaR = dot(tex_R, vec3(0.3333333));

    // Square wave generator: 1-pixel phase shift on Y-axis to form a -1, +1, +1, -1 pattern
    vec2 freq = vec2(0.5, 0.25);
    vec2 phase = fract((gl_FragCoord.xy + vec2(0.0, 1.0)) * freq + 0.01); // +0.01 to avoid precision drift at boundaries
    vec2 wave = step(0.5, phase) * 2.0 - 1.0;

	//	Scanline edge detection
    float diffU = (lumaC - lumaU) * sign(wave.y);
    float diffD = (lumaC - lumaD) * sign(wave.y);
    float diffL = (lumaC - lumaL) * sign(wave.x);
    float diffR = (lumaC - lumaR) * sign(wave.x);

    // Modulate square wave amplitude via component-wise multiplication
	// During the dark cycle, the center pixel cannot be darker than its sides (by compXY threshold); 
	// during the light cycle, the center pixel cannot be brighter than its sides (by compXY threshold). 
	// Otherwise, fallback to the original pixel.
    float modY = compY * wave.y * float(diffU < compY && diffD < compY);
    float modX = compX * wave.x * float(diffL < compX && diffR < compX);


    // Dynamic quantization core logic: distance from each of the RGB channels to the median value 0.5 (vec3 [0.0 - 1.0])
    // The wave perturbation peaks around mid-tones (0.5) where dist approaches 0, and dampens at extreme brightness levels.
    vec3 dist = abs(texel - 0.5) * 2.0;
	// Combined scanline lighting pass synthesis:
	// Overlay scanline oscillation onto the baseline brightness (1.0), then multiply back by the source texture.
    vec3 final_brightness = 1.0 + (modX + modY) * (1.0 - dist);

    // final out 
    gl_FragColor = vec4(texel * final_brightness, 1.0);
}
