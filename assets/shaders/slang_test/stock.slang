#version 450

layout(set = 0, binding = 0, std140) uniform UBO
{
   mat4 MVP;
   vec4 SourceSize;
   float ColorMod;
};

#pragma name StockShader
#pragma parameter ColorMod "Color intensity" 1.0 0.1 2.0 0.1

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main()
{
   gl_Position = MVP * Position;
   vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(binding = 1) uniform sampler2D Source;
void main()
{
   // Minimal slang test fixture: sample the source and scale by the ColorMod parameter.
   // With the default ColorMod = 1.0 this is an identity pass-through; adjusting the
   // parameter (or editing this shader) is a quick way to confirm the slang chain runs.
   FragColor = texture(Source, vTexCoord) * ColorMod;
}
