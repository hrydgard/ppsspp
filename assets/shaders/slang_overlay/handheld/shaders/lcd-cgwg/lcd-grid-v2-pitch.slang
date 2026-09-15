#version 450

// lcd-grid-v2-pitch.slang - a fork of handheld/shaders/lcd-cgwg/lcd-grid-v2.slang from the libretro
// slang-shaders pack (commit 4ecd48510e4a0f936617c5e899dd6c4fd50abbd7), by cgwg.
// GPL-2.0-or-later, as the original.
//
// Draws the LCD cell grid coarser than one cell per source pixel, so it stays visible on a physically
// small high-PPI panel. Design notes, including why one cell per PSP pixel washes out at 367 PPI:
// PPSSPP's docs/superpowers/specs/2026-09-15-lcd-grid-hidpi-shader-design.md
//
// Three changes from upstream, and nothing else:
//   1. The grid is keyed to SourceSize, not OriginalSize. OriginalSize is the size of the *chain
//      input*, so upstream keeps drawing a 480x272 grid when a resampling pass is put in front of it,
//      while sampling the resampled image with 480x272 texel steps. SourceSize follows the previous
//      pass, which is what lets that pass set the cell pitch.
//   2. A PITCH parameter: a cell is PITCH source pixels wide, and its colour is the average of the
//      block it covers, fetched with textureLod. PITCH = 1.0 reproduces upstream exactly.
//   3. fetch_offset was a macro reading the global OriginalSize; the grid metrics are locals now, so
//      it became a function taking them as arguments. It also moved into the fragment stage - a real
//      function referencing Source cannot sit in the prologue, which is shared with the vertex stage.
//
// Upstream's formatting (4 spaces, its brace style) is kept deliberately, to keep the diff readable.

layout(push_constant) uniform Push
{
    float RSUBPIX_R;
    float RSUBPIX_G;
    float RSUBPIX_B;
    float GSUBPIX_R;
    float GSUBPIX_G;
    float GSUBPIX_B;
    float BSUBPIX_R;
    float BSUBPIX_G;
    float BSUBPIX_B;
    float gain;
    float gamma;
    float blacklevel;
    float ambient;
    float BGR;
    float PITCH;
} params;

#pragma parameter RSUBPIX_R  "Colour of R subpixel: R" 1.0 0.0 1.0 0.01
#pragma parameter RSUBPIX_G  "Colour of R subpixel: G" 0.0 0.0 1.0 0.01
#pragma parameter RSUBPIX_B  "Colour of R subpixel: B" 0.0 0.0 1.0 0.01
#pragma parameter GSUBPIX_R  "Colour of G subpixel: R" 0.0 0.0 1.0 0.01
#pragma parameter GSUBPIX_G  "Colour of G subpixel: G" 1.0 0.0 1.0 0.01
#pragma parameter GSUBPIX_B  "Colour of G subpixel: B" 0.0 0.0 1.0 0.01
#pragma parameter BSUBPIX_R  "Colour of B subpixel: R" 0.0 0.0 1.0 0.01
#pragma parameter BSUBPIX_G  "Colour of B subpixel: G" 0.0 0.0 1.0 0.01
#pragma parameter BSUBPIX_B  "Colour of B subpixel: B" 1.0 0.0 1.0 0.01
#pragma parameter gain       "Gain"                    1.0 0.5 2.0 0.05
#pragma parameter gamma      "LCD Gamma"               3.0 0.5 5.0 0.1
#pragma parameter blacklevel "Black level"            0.05 0.0 0.5 0.01
#pragma parameter ambient    "Ambient"                 0.0 0.0 0.5 0.01
#pragma parameter BGR        "BGR"                     0 0 1 1
#pragma parameter PITCH      "LCD cell pitch (source px)" 1.0 1.0 3.0 0.25

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
    vec4 OutputSize;
    vec4 OriginalSize;
    vec4 SourceSize;
} global;

#define outgamma 2.2

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

// integral of (1 - x^2 - x^4 + x^6)^2
float coeffs_x[7] = float[](1.0, -2.0/3.0, -1.0/5.0, 4.0/7.0, -1.0/9.0, -2.0/11.0, 1.0/13.0);
// integral of (1 - 2x^4 + x^6)^2
float coeffs_y[7] = float[](1.0,      0.0, -4.0/5.0, 2.0/7.0,  4.0/9.0, -4.0/11.0, 1.0/13.0);

float intsmear_func(float z, float coeffs[7])
{
    float z2 = z*z;
    float zn = z;
    float ret = 0.0;
    for (int i = 0; i < 7; i++) {
        ret += zn*coeffs[i];
        zn *= z2;
    }
    return ret;
}

float intsmear(float x, float dx, float d, float coeffs[7])
{
    float zl = clamp((x-dx*0.5)/d,-1.0,1.0);
    float zh = clamp((x+dx*0.5)/d,-1.0,1.0);
    return d * ( intsmear_func(zh,coeffs) - intsmear_func(zl,coeffs) )/dx;
}

// Upstream's fetch_offset macro. texelSize is one cell rather than one texel now, and lod picks the
// mip level whose texels are that cell, so a cell shows the average of the block it covers instead of
// one pixel out of it. At lod 0 this is exactly upstream's texture() fetch.
vec3 fetch_cell(ivec2 coord, ivec2 offset, vec2 texelSize, float lod)
{
    vec3 c = textureLod(Source, (vec2(coord + offset) + 0.5) * texelSize, lod).rgb;
    return pow(vec3(params.gain) * c + vec3(params.blacklevel), vec3(params.gamma)) + vec3(params.ambient);
}

void main()
{
    float pitch = max(params.PITCH, 1.0);
    vec2 texelSize = global.SourceSize.zw * pitch;
    float lod = log2(pitch);
    /* float2 range = IN.video_size / (IN.output_size * IN.texture_size); */
    vec2 range = global.OutputSize.zw;

    vec3 cred   = pow(vec3(params.RSUBPIX_R, params.RSUBPIX_G, params.RSUBPIX_B), vec3(outgamma));
    vec3 cgreen = pow(vec3(params.GSUBPIX_R, params.GSUBPIX_G, params.GSUBPIX_B), vec3(outgamma));
    vec3 cblue  = pow(vec3(params.BSUBPIX_R, params.BSUBPIX_G, params.BSUBPIX_B), vec3(outgamma));

    ivec2 tli = ivec2(floor(vTexCoord/texelSize-vec2(0.4999)));

    vec3 lcol, rcol;
    float subpix = (vTexCoord.x/texelSize.x - 0.4999 - float(tli.x))*3.0;
    float rsubpix = range.x/texelSize.x * 3.0;

    lcol = vec3(intsmear(subpix+1.0, rsubpix, 1.5, coeffs_x),
                intsmear(subpix    , rsubpix, 1.5, coeffs_x),
                intsmear(subpix-1.0, rsubpix, 1.5, coeffs_x));
    rcol = vec3(intsmear(subpix-2.0, rsubpix, 1.5, coeffs_x),
                intsmear(subpix-3.0, rsubpix, 1.5, coeffs_x),
                intsmear(subpix-4.0, rsubpix, 1.5, coeffs_x));

    if (params.BGR > 0.5) {
        lcol.rgb = lcol.bgr;
        rcol.rgb = rcol.bgr;
    }

    float tcol, bcol;
    subpix = vTexCoord.y/texelSize.y - 0.4999 - float(tli.y);
    rsubpix = range.y/texelSize.y;
    tcol = intsmear(subpix    ,rsubpix, 0.63, coeffs_y);
    bcol = intsmear(subpix-1.0,rsubpix, 0.63, coeffs_y);

    vec3 topLeftColor     = fetch_cell(tli, ivec2(0,0), texelSize, lod) * lcol * vec3(tcol);
    vec3 bottomRightColor = fetch_cell(tli, ivec2(1,1), texelSize, lod) * rcol * vec3(bcol);
    vec3 bottomLeftColor  = fetch_cell(tli, ivec2(0,1), texelSize, lod) * lcol * vec3(bcol);
    vec3 topRightColor    = fetch_cell(tli, ivec2(1,0), texelSize, lod) * rcol * vec3(tcol);

    vec3 averageColor = topLeftColor + bottomRightColor + bottomLeftColor + topRightColor;

    averageColor = mat3(cred, cgreen, cblue) * averageColor;

    FragColor = vec4(pow(averageColor, vec3(1.0/outgamma)),0.0);
}
