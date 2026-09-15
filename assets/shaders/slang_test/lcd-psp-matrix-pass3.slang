#version 450
// PSP LCD matrix shader — final pass: RGB subpixel mask + inter-cell grid + display encode.
#pragma parameter SUBPIXEL_STRENGTH "LCD Subpixel Strength" 0.35 0.0 1.0 0.05
#pragma parameter SUBPIXEL_SIZE "LCD Subpixel Width (px)" 3.0 1.0 8.0 1.0
#pragma parameter GRID_STRENGTH     "LCD Grid Strength"     0.7 0.0 1.0 0.05
#pragma parameter SCANLINE_STRENGTH "LCD Scanline Strength" 0.1 0.0 1.0 0.05
#pragma parameter BRIGHTEN          "Brightness Compensation" 1.3 1.0 3.0 0.05
#pragma parameter GLOW_STRENGTH     "LCD Glow Strength" 0.5 0.0 1.0 0.05

layout(push_constant) uniform Push {
   float SUBPIXEL_STRENGTH;
   float SUBPIXEL_SIZE;
   float GRID_STRENGTH;
   float SCANLINE_STRENGTH;
   float BRIGHTEN;
   float GLOW_STRENGTH;
   vec4 OutputSize;
   vec4 OriginalSize;
   vec4 SourceSize;
} registers;
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;      // = CORRECTED (pass1)
layout(set = 0, binding = 3) uniform sampler2D CORRECTED;   // alias of pass1 (same content; bound by name)
layout(set = 0, binding = 4) uniform sampler2D GLOW;

#define INV_DISPLAY_GAMMA (1.0 / 2.2)

void main() {
   vec3 c = texture(CORRECTED, vTexCoord).rgb;

   // Source (PSP) pixel position — used for the inter-cell grid + scanline so one emulated pixel
   // maps to one LCD cell, independent of internal render resolution.
   vec2 sp = vTexCoord * registers.SourceSize.xy;

   // RGB vertical subpixel stripes. The stripe PERIOD is SUBPIXEL_SIZE *display* pixels wide (NOT
   // tied to source-pixel count): a PSP pixel upscaled to the display is only ~1 px per RGB column,
   // too fine to see, so we render visible stripes at a fixed on-screen width instead. Each of the
   // 3 columns emphasizes R/G/B; the others are attenuated by SUBPIXEL_STRENGTH.
   vec2 op = vTexCoord * registers.OutputSize.xy;
   int sub = int(mod(floor(op.x / registers.SUBPIXEL_SIZE), 3.0));
   // Built with selects rather than "mask[sub] = 1.0": a dynamically indexed vector component is
   // not a valid l-value in HLSL SM5, so FXC rejects the cross-compiled D3D11 shader with
   // "error X3500: array reference cannot be used as an l-value; not natively addressable".
   vec3 sel = vec3(float(sub == 0), float(sub == 1), float(sub == 2));
   vec3 mask = mix(vec3(1.0 - registers.SUBPIXEL_STRENGTH), vec3(1.0), sel);

   // Inter-cell grid: darken the gap between source-pixel cells (both axes). fract over the
   // source-pixel grid spans many display pixels at high res, so the grid stays visible.
   vec2 f = fract(sp);
   float gx = 1.0 - registers.GRID_STRENGTH * smoothstep(0.4, 1.0, abs(f.x - 0.5) * 2.0);
   float gy = 1.0 - registers.GRID_STRENGTH * smoothstep(0.4, 1.0, abs(f.y - 0.5) * 2.0);
   float grid = gx * gy;

   // Subtle horizontal scanline modulation over source rows (much milder than a CRT).
   float scan = 1.0 - registers.SCANLINE_STRENGTH * (0.5 - 0.5 * cos(6.28318 * sp.y));

   c = c * mask * grid * scan * registers.BRIGHTEN;
   c += registers.GLOW_STRENGTH * texture(GLOW, vTexCoord).rgb;

   // Encode to display gamma (pass1 left color in ~linear space).
   c = clamp(pow(max(c, vec3(0.0)), vec3(INV_DISPLAY_GAMMA)), 0.0, 1.0);
   FragColor = vec4(c, 1.0);
}
