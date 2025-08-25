// MIT License
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Set precision for OpenGL ES
#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// Texture sampler for input image
uniform sampler2D sampler0;

// Texture coordinate deltas:
// u_texelDelta = 1.0 / input texture size (texel size)
// u_pixelDelta = 1.0 / output resolution (pixel size)
uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;

// Interpolated texture coordinates from vertex shader
varying vec2 v_texcoord0;

// Precision definition for high precision calculations when available
#ifdef GL_FRAGMENT_PRECISION_HIGH
#define HIGHP highp
#else
#define HIGHP mediump
#endif

// ---------------- Luminance ----------------
// Calculate luminance from RGB color using standard weights
// Based on Rec. 709 luma coefficients for perceptual brightness
HIGHP float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// ---------------- Cubic kernel (Catmull-Rom, B=0, C=0.5) ----------------
// Separable cubic weights in 1D, evaluated at fractional position t in [0,1)
// Implements Catmull-Rom spline with B=0, C=0.5 for smooth interpolation
// Returns weights for 4 taps at positions: -1, 0, +1, +2
HIGHP vec4 cubicWeights(HIGHP float t) {
    HIGHP float t2 = t * t;
    HIGHP float t3 = t2 * t;
    
    // Calculate weights for each of the 4 taps using Catmull-Rom formula
    HIGHP float w0 = -0.5 * t + 1.0 * t2 - 0.5 * t3;  // Tap at -1
    HIGHP float w1 =  1.0       - 2.5 * t2 + 1.5 * t3; // Tap at 0
    HIGHP float w2 =  0.5 * t + 2.0 * t2 - 1.5 * t3;   // Tap at +1
    HIGHP float w3 = -0.5 * t2 + 0.5 * t3;             // Tap at +2
    
    return vec4(w0, w1, w2, w3);
}

// ---------------- Neighborhood & gradients ----------------
// Structure to hold a 3x3 neighborhood of color values and luminance
struct N3x3 {
    vec3 c[3][3];      // RGB color values for 3x3 grid
    HIGHP float l[3][3]; // Luminance values for 3x3 grid
};

// Load a 3x3 neighborhood around the given UV coordinate
// Samples 9 texels and calculates their luminance values
N3x3 load3x3(HIGHP vec2 uv) {
    N3x3 nb;
    // Loop through the 3x3 grid centered at uv
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            // Calculate texture coordinate for this grid position
            HIGHP vec2 t = uv + vec2(float(i), float(j)) * u_texelDelta;
            
            // Sample color and calculate luminance
            vec3 s = texture2D(sampler0, t).rgb;
            nb.c[j+1][i+1] = s;
            nb.l[j+1][i+1] = Luma(s);
        }
    }
    return nb;
}

// Calculate gradient using Sobel operator on luminance values
// Returns gradient vector (gx, gy) representing edge direction and magnitude
HIGHP vec2 sobelGrad(in HIGHP float l[3][3]) {
    // Sobel X kernel (horizontal edge detection)
    HIGHP float gx = (l[0][2] + 2.0*l[1][2] + l[2][2]) - (l[0][0] + 2.0*l[1][0] + l[2][0]);
    
    // Sobel Y kernel (vertical edge detection)
    HIGHP float gy = (l[2][0] + 2.0*l[2][1] + l[2][2]) - (l[0][0] + 2.0*l[0][1] + l[0][2]);
    
    return vec2(gx, gy);
}

// ---------------- Anti-ringing clamp vs. base 2x2 ----------------
// Clamp the color to prevent ringing artifacts by constraining it 
// within the range of the base 2x2 pixel block
void clampToBase2x2(inout vec3 color, HIGHP vec2 srcPxCenter) {
    // Find the base pixel coordinates (integer grid)
    HIGHP vec2 ip = floor(srcPxCenter);
    HIGHP vec2 baseUV = ip * u_texelDelta;

    // Sample the four corners of the 2x2 base block
    vec3 c00 = texture2D(sampler0, baseUV).rgb;
    vec3 c10 = texture2D(sampler0, baseUV + vec2(u_texelDelta.x, 0.0)).rgb;
    vec3 c01 = texture2D(sampler0, baseUV + vec2(0.0, u_texelDelta.y)).rgb;
    vec3 c11 = texture2D(sampler0, baseUV + u_texelDelta).rgb;

    // Find the minimum and maximum colors in the 2x2 block
    vec3 lo = min(min(c00, c10), min(c01, c11));
    vec3 hi = max(max(c00, c10), max(c01, c11));
    
    // Clamp the color to be within the range of the base block
    color = clamp(color, lo, hi);
}

// ---------------- Edge-adaptive anisotropic 4x4 reconstruction ----------------
// Main EASU (Edge Adaptive Spatial Upsampling) function
// Performs edge-adaptive upscaling using anisotropic filtering
vec4 FsrEasu(vec2 uvOut) {
    // Calculate input and output dimensions
    HIGHP vec2 inSize  = 1.0 / u_texelDelta;   // Input texture size
    HIGHP vec2 outSize = 1.0 / u_pixelDelta;   // Output resolution
    
    // Map output UV coordinate to input pixel space
    HIGHP vec2 srcPx = uvOut / u_texelDelta;
    
    // Align to pixel centers in input space
    HIGHP vec2 center = floor(srcPx - 0.5) + 0.5;
    HIGHP vec2 phase  = fract(srcPx - 0.5);

    // Load 3x3 neighborhood and calculate local gradients
    N3x3 nb = load3x3(uvOut);
    HIGHP vec2 g = sobelGrad(nb.l);           // Edge gradient
    HIGHP float gLen = length(g);             // Edge strength

    // Calculate edge basis vectors:
    // nrm: Normal to the edge (points toward edge direction)
    // tan: Tangent to the edge (along edge direction)
    HIGHP vec2 nrm = (gLen > 1e-4) ? (g / gLen) : vec2(0.0, 1.0);
    HIGHP vec2 tan = vec2(-nrm.y, nrm.x);

    // Calculate anisotropy based on edge strength:
    // Stronger edges = more anisotropy (directional filtering)
    HIGHP float anis = clamp(gLen * 0.35, 0.0, 1.0);
    HIGHP float rTan = mix(1.0, 1.6, anis);   // Support radius along tangent
    HIGHP float rNrm = mix(1.0, 0.7, anis);   // Support radius along normal

    // Define 4x4 sampling footprint anchored at (center - 1, center - 1)
    HIGHP vec2 basePx = center - 1.0;
    HIGHP vec2 baseUV = basePx * u_texelDelta;

    // Precompute separable cubic weights for the current phase
    HIGHP vec4 wx = cubicWeights(phase.x);
    HIGHP vec4 wy = cubicWeights(phase.y);

    // Initialize accumulator for weighted samples
    vec3 accum = vec3(0.0);
    HIGHP float wsum = 0.0;

    // Process all 16 samples in the 4x4 footprint
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            // Calculate offset from center in pixel units
            HIGHP vec2 d = vec2(float(i) - 1.0 - (1.0 - phase.x),
                                float(j) - 1.0 - (1.0 - phase.y));

            // Project offset onto tangent and normal directions
            HIGHP float dt = dot(d, tan) / rTan;  // Distance along tangent
            HIGHP float dn = dot(d, nrm) / rNrm;  // Distance along normal

            // Calculate anisotropic weights using Catmull-Rom splines
            HIGHP float at = abs(dt);  // Absolute distance along tangent
            HIGHP float an = abs(dn);  // Absolute distance along normal

            // Calculate cubic weights for tangent direction
            HIGHP float at2 = at * at; HIGHP float at3 = at2 * at;
            HIGHP float wt = (at < 1.0)
                ? (1.0 - 2.5*at2 + 1.5*at3)          // For |d| < 1
                : ((at < 2.0) ? (-0.5*at + 2.0*at2 - 1.5*at3) : 0.0);  // For 1 ≤ |d| < 2

            // Calculate cubic weights for normal direction
            HIGHP float an2 = an * an; HIGHP float an3 = an2 * an;
            HIGHP float wn = (an < 1.0)
                ? (1.0 - 2.5*an2 + 1.5*an3)          // For |d| < 1
                : ((an < 2.0) ? (-0.5*an + 2.0*an2 - 1.5*an3) : 0.0);  // For 1 ≤ |d| < 2

            // Combine directional weights
            HIGHP float wAniso = max(0.0, wt) * max(0.0, wn);

            // Calculate baseline separable grid weight
            HIGHP float wSep = wx[i] * wy[j];

            // Blend between separable and anisotropic weights based on edge strength
            HIGHP float kEdge = clamp(gLen * 0.4, 0.0, 1.0);
            HIGHP float w = mix(wSep, wAniso, kEdge);

            // Sample texture and accumulate weighted color
            vec2 uv = baseUV + vec2(float(i) * u_texelDelta.x,
                                    float(j) * u_texelDelta.y);
            vec3 s = texture2D(sampler0, uv).rgb;

            accum += s * w;
            wsum += w;
        }
    }

    // Normalize accumulated color by total weight
    vec3 recon = (wsum > 0.0) ? (accum / wsum) : texture2D(sampler0, uvOut).rgb;

    // Early-out optimization for very flat regions to avoid unnecessary processing
    HIGHP float lMin = min(min(nb.l[0][0], nb.l[0][1]), min(nb.l[0][2], min(min(nb.l[1][0], nb.l[1][1]), min(nb.l[1][2], min(nb.l[2][0], min(nb.l[2][1], nb.l[2][2]))))));
    HIGHP float lMax = max(max(nb.l[0][0], nb.l[0][1]), max(nb.l[0][2], max(max(nb.l[1][0], nb.l[1][1]), max(nb.l[1][2], max(nb.l[2][0], max(nb.l[2][1], nb.l[2][2]))))));
    HIGHP float contrast = lMax - lMin;
    
    // Skip sharpening in low-contrast regions
    if (contrast < 0.02) {
        return vec4(texture2D(sampler0, uvOut).rgb, 1.0);
    }

    // Apply anti-ringing clamp to prevent overshoots
    clampToBase2x2(recon, center);

    // Final clamp to valid color range
    return vec4(clamp(recon, 0.0, 1.0), 1.0);
}

// Main fragment shader entry point
void main() {
    // Apply EASU upscaling to the current fragment
    gl_FragColor = FsrEasu(v_texcoord0);
}
