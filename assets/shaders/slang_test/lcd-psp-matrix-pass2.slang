#version 450
// PSP LCD matrix shader — Pass 2: backlight glow (soft blur of the corrected image).
#pragma name GLOW
#pragma parameter GLOW_RADIUS "LCD Glow Radius" 1.0 0.5 4.0 0.1

layout(push_constant) uniform Push {
   float GLOW_RADIUS;
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
layout(set = 0, binding = 2) uniform sampler2D Source;     // = CORRECTED (pass1)
layout(set = 0, binding = 3) uniform sampler2D CORRECTED;

void main() {
   // 3x3 gaussian-ish blur, radius scaled by GLOW_RADIUS in source texels.
   vec2 px = registers.SourceSize.zw * registers.GLOW_RADIUS;  // 1/size * radius
   vec3 sum = vec3(0.0);
   float wsum = 0.0;
   for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
         float w = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0); // 4/2/1 gaussian
         sum += w * texture(CORRECTED, vTexCoord + vec2(float(x), float(y)) * px).rgb;
         wsum += w;
      }
   }
   FragColor = vec4(sum / wsum, 1.0);
}
