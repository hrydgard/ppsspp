#version 450
// PSP LCD matrix shader — Pass 1: PSP color correction.
// PSP color-correction ('psp_color') originally by hunterk, modified by Pokefan531,
// released into the public domain; matrix values as used in libretro lcd1x_psp.
#pragma name CORRECTED
#pragma parameter COLOR_CORRECT "PSP Color Correction" 1.0 0.0 1.0 1.0

layout(push_constant) uniform Push {
   float COLOR_CORRECT;
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
layout(set = 0, binding = 2) uniform sampler2D Source;

#define TARGET_GAMMA 2.21
#define CC_R 0.98
#define CC_G 0.795
#define CC_B 0.98
#define CC_RG 0.04
#define CC_RB 0.01
#define CC_GR 0.20
#define CC_GB 0.01
#define CC_BR -0.18
#define CC_BG 0.165

void main() {
   vec3 c = texture(Source, vTexCoord).rgb;
   // Always linearize here (input is ~gamma-encoded): pass 3 is the sole place that re-encodes
   // to display gamma, so the linearize<->encode must balance regardless of COLOR_CORRECT. Gating
   // the linearize behind the toggle would double-encode when correction is off (washed-out image).
   c = pow(c, vec3(TARGET_GAMMA));
   if (registers.COLOR_CORRECT > 0.5) {
      c = mat3(CC_R,  CC_RG, CC_RB,
               CC_GR, CC_G,  CC_GB,
               CC_BR, CC_BG, CC_B) * c;
   }
   // Leave in this (roughly linear) space; pass 3 encodes to display gamma.
   c = clamp(c, 0.0, 1.0);
   FragColor = vec4(c, 1.0);
}
