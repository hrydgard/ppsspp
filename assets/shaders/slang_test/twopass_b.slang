#version 450
layout(set = 0, binding = 0, std140) uniform UBO { mat4 MVP; vec4 SourceSize; };
#pragma name SecondPass
#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = MVP * Position; vTexCoord = TexCoord; }
#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(binding = 1) uniform sampler2D Source;          // pass 0 output (grayscale)
layout(binding = 2) uniform sampler2D OriginalHistory1; // previous frame's input (color)
void main() {
   // Blend this frame's grayscale (pass0) with last frame's color input -> proves PassOutput chain + history.
   vec4 gray = texture(Source, vTexCoord);
   vec4 prev = texture(OriginalHistory1, vTexCoord);
   FragColor = mix(gray, prev, 0.5);
}
