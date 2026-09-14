#version 450
layout(set = 0, binding = 0, std140) uniform UBO { mat4 MVP; vec4 SourceSize; };
#pragma name LutPass
#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = MVP * Position; vTexCoord = TexCoord; }
#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(binding = 1) uniform sampler2D Source;
layout(binding = 2) uniform sampler2D PaletteLUT;
void main() {
   // Map luminance through the palette LUT -> proves the PNG LUT is bound and sampled.
   vec4 c = texture(Source, vTexCoord);
   float lum = dot(c.rgb, vec3(0.299, 0.587, 0.114));
   vec3 mapped = texture(PaletteLUT, vec2(lum, 0.5)).rgb;
   FragColor = vec4(mapped, c.a);
}
