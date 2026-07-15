#version 450
// PSP LCD matrix shader — final pass: RGB subpixel mask + inter-cell grid + display encode.
#pragma parameter SUBPIXEL_STRENGTH "LCD Subpixel Strength" 0.5 0.0 1.0 0.05
#pragma parameter GRID_STRENGTH     "LCD Grid Strength"     0.3 0.0 1.0 0.05
#pragma parameter SCANLINE_STRENGTH "LCD Scanline Strength" 0.1 0.0 1.0 0.05
#pragma parameter BRIGHTEN          "Brightness Compensation" 1.3 1.0 3.0 0.05

layout(push_constant) uniform Push {
   float SUBPIXEL_STRENGTH;
   float GRID_STRENGTH;
   float SCANLINE_STRENGTH;
   float BRIGHTEN;
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

#define INV_DISPLAY_GAMMA (1.0 / 2.2)

void main() {
   vec3 c = texture(CORRECTED, vTexCoord).rgb;

   // Output-pixel coordinate (subpixels render at display density).
   vec2 op = vTexCoord * registers.OutputSize.xy;

   // RGB vertical subpixel mask: each output column emphasizes one of R/G/B.
   // sub in {0,1,2} selects the channel; others attenuated by SUBPIXEL_STRENGTH.
   int sub = int(mod(floor(op.x), 3.0));
   vec3 mask = vec3(1.0 - registers.SUBPIXEL_STRENGTH);
   mask[sub] = 1.0;

   // Inter-cell grid: subtle darkening at cell boundaries (both axes), tunable.
   // Use fract of the per-axis pixel position; darken near the cell edge.
   vec2 f = fract(op);
   float gx = 1.0 - registers.GRID_STRENGTH * smoothstep(0.5, 1.0, abs(f.x - 0.5) * 2.0);
   float gy = 1.0 - registers.GRID_STRENGTH * smoothstep(0.5, 1.0, abs(f.y - 0.5) * 2.0);
   float grid = gx * gy;

   // Subtle horizontal scanline modulation (much milder than a CRT).
   float scan = 1.0 - registers.SCANLINE_STRENGTH * (0.5 - 0.5 * cos(6.28318 * op.y));

   c = c * mask * grid * scan * registers.BRIGHTEN;

   // Encode to display gamma (pass1 left color in ~linear space).
   c = clamp(pow(max(c, vec3(0.0)), vec3(INV_DISPLAY_GAMMA)), 0.0, 1.0);
   FragColor = vec4(c, 1.0);
}
