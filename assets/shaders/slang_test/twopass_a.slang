#version 450
layout(set = 0, binding = 0, std140) uniform UBO { mat4 MVP; vec4 SourceSize; };
#pragma name FirstPass
#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = MVP * Position; vTexCoord = TexCoord; }
#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(binding = 1) uniform sampler2D Source;
void main() {
   // Pass 0: grayscale, so a visible transform proves the pass ran and pass 1 read its output.
   vec4 c = texture(Source, vTexCoord);
   float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));
   FragColor = vec4(g, g, g, c.a);
}
