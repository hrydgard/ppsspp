// Test shader for constant buffers in texture upscalers

#extension GL_EXT_scalar_block_layout : enable

layout(scalar, set = CBUFFER_SET, binding = CBUFFER_BINDING) uniform PixelData {
    uint pixels[256];
};

void applyScaling(uvec2 origxy) {
    // 1. Flatten 2D coordinates to 1D index
    // Use bitwise & 15 to wrap/clamp to the 16x16 bounds
    uint x = origxy.x & 15u;
    uint y = origxy.y & 15u;
    uint index = (y * 16u) + x;

    // 2. Access the scalar array
    uint color = pixels[index];
    
    // 3. Unpack (Note: unpackUnorm4x8 expects a uint)
    vec4 rgba = unpackUnorm4x8(color);

    // Assuming destXY is derived from origxy (e.g., origxy * 2)
    ivec2 destXY = ivec2(origxy) * 2;

    // 4. Output calls
    writeColorf(destXY, rgba);
    writeColorf(destXY + ivec2(1, 0), rgba);
    writeColorf(destXY + ivec2(0, 1), rgba);
    writeColorf(destXY + ivec2(1, 1), rgba);
}
