#version 450
layout(set = 0, binding = 0, std140) uniform UBO { mat4 MVP; vec4 SourceSize; };
#pragma name FeedbackPass
#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = MVP * Position; vTexCoord = TexCoord; }
#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(binding = 1) uniform sampler2D Source;                 // current input
layout(binding = 2) uniform sampler2D FeedbackPassFeedback;   // this pass's output, previous frame
void main() {
   // Motion persistence: 0.15 current + 0.85 last frame's output -> visible trailing/ghosting on movement.
   vec4 cur = texture(Source, vTexCoord);
   vec4 prev = texture(FeedbackPassFeedback, vTexCoord);
   FragColor = mix(cur, prev, 0.85);
}
