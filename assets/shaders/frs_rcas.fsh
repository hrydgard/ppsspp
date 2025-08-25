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

// Texture coordinate delta (1.0 / viewport size)
uniform vec2 u_texelDelta;

// User settings:
// u_setting.x = sharpness value (0.0 to 1.0)
uniform vec4 u_setting;

// Interpolated texture coordinates from vertex shader
varying vec2 v_texcoord0;

// Split-screen divider width for comparison views
const float lineWidth = 0.005;

// Luminance coefficients based on Rec. 709 standard
// Used for converting RGB to perceptually weighted luminance
const vec3 lumCoef = vec3(0.2126, 0.7152, 0.0722);

// RCAS (Robust Contrast Adaptive Sharpening) constants
// Peak negative lobe strength and its inverse
const float rcasPeak = 8.0 - 3.0;      // Peak negative lobe strength
const float rcasInvPeak = 1.0 / rcasPeak; // Inverse of peak strength

// Small epsilon value for numerical stability
// Matches AMD's reference implementation
const float FSR_EPS = 0.0001;

// Cross-shaped sampling pattern offsets (N, W, E, S)
// Used for 4-tap cross sampling around center pixel
const vec2 crossOffsets[4] = vec2[4](
    vec2( 0.0, -1.0),  // North
    vec2(-1.0,  0.0),  // West
    vec2( 1.0,  0.0),  // East
    vec2( 0.0,  1.0)   // South
);

// Vanilla RCAS kernel implementation (no edge-aware weighting)
// Performs contrast adaptive sharpening on the input texture
vec4 FsrRcasVanilla(vec2 uv) {
    // Sample center pixel and convert to luminance
    vec3 C = texture2D(sampler0, uv).rgb;
    float CL = dot(C, lumCoef);

    // Sample the 4 cross neighbors and convert to luminance
    vec3 N = texture2D(sampler0, uv + crossOffsets[0] * u_texelDelta).rgb;  // North
    vec3 W = texture2D(sampler0, uv + crossOffsets[1] * u_texelDelta).rgb;  // West
    vec3 E = texture2D(sampler0, uv + crossOffsets[2] * u_texelDelta).rgb;  // East
    vec3 S = texture2D(sampler0, uv + crossOffsets[3] * u_texelDelta).rgb;  // South

    float NL = dot(N, lumCoef);  // North luminance
    float WL = dot(W, lumCoef);  // West luminance
    float EL = dot(E, lumCoef);  // East luminance
    float SL = dot(S, lumCoef);  // South luminance

    // Calculate adaptive amplification factor to prevent oversharpening
    // Uses min/max range analysis to determine safe sharpening strength
    vec3 minRGB = min(min(min(N, W), min(E, S)), C);  // Minimum RGB in 5-tap neighborhood
    vec3 maxRGB = max(max(max(N, W), max(E, S)), C);  // Maximum RGB in 5-tap neighborhood
    vec3 invMax = 1.0 / (maxRGB + FSR_EPS);          // Inverse of maximum (with epsilon)
    vec3 amp = clamp(min(minRGB, 2.0 - maxRGB) * invMax, 0.0, 1.0);  // Amplification factor
    amp = inversesqrt(amp + FSR_EPS);                // Inverse square root for non-linearity

    // Calculate sharpening weight based on amplification
    float w = -rcasInvPeak / dot(amp, lumCoef);

    // Compute sharpened luminance using contrast adaptive formula
    float sumL = NL + WL + EL + SL;                 // Sum of neighbor luminances
    float invDen = 1.0 / (4.0 * w + 1.0);           // Inverse denominator
    float sharpL = clamp((sumL * w + CL) * invDen, 0.0, 1.0);  // Sharpened luminance

    // Reconstruct color by preserving chroma (hue/saturation)
    // This prevents color shifts during sharpening
    vec3 chroma = C - vec3(CL);                      // Extract chroma (color without brightness)
    vec3 sharpColor = chroma + vec3(sharpL);        // Apply sharpened luminance to chroma

    // Blend between original and sharpened based on user sharpness setting
    // u_setting.x controls the blend: 0.0 = original, 1.0 = fully sharpened
    vec3 outColor = mix(C, sharpColor, u_setting.x);
    
    return vec4(outColor, 1.0);
}

// Main fragment shader entry point
void main() {
    // Apply RCAS sharpening to the current fragment
    gl_FragColor = FsrRcasVanilla(v_texcoord0);
}
