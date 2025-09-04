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

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

// ------------------------------------------------------------------
//  Texture & uniforms
// ------------------------------------------------------------------
#ifdef SHADER_API_D3D11
uniform sampler2D sampler0 : register(s0);
#else
uniform sampler2D sampler0;
#endif

uniform vec2  u_texelDelta;    // 1.0 / viewportSize
uniform vec4  u_setting;       // x = user sharpness (0…1)

varying vec2  v_texcoord0;

// ------------------------------------------------------------------
//  Constants
// ------------------------------------------------------------------
const vec3  lumCoef    = vec3(0.2126, 0.7152, 0.0722);   // Rec.709
const float rcasPeak   = 8.0 - 3.0;                      // = 5.0
const float rcasInvP   = 1.0 / rcasPeak;
const float FSR_EPS    = 0.0001;

// Offsets for 4-tap cross pattern
const vec2 offN = vec2( 0.0,-1.0);
const vec2 offW = vec2(-1.0, 0.0);
const vec2 offE = vec2( 1.0, 0.0);
const vec2 offS = vec2( 0.0, 1.0);

// ------------------------------------------------------------------
//  RCAS kernel – unchanged logic
// ------------------------------------------------------------------
vec4 FsrRcasVanilla(vec2 uv)
{
    vec3 C = texture2D(sampler0, uv).rgb;
    float CL = dot(C, lumCoef);

    vec3 N = texture2D(sampler0, uv + offN * u_texelDelta).rgb;
    vec3 W = texture2D(sampler0, uv + offW * u_texelDelta).rgb;
    vec3 E = texture2D(sampler0, uv + offE * u_texelDelta).rgb;
    vec3 S = texture2D(sampler0, uv + offS * u_texelDelta).rgb;

    float NL = dot(N, lumCoef);
    float WL = dot(W, lumCoef);
    float EL = dot(E, lumCoef);
    float SL = dot(S, lumCoef);

    // Adaptive range
    vec3 minRGB = min(min(min(N, W), min(E, S)), C);
    vec3 maxRGB = max(max(max(N, W), max(E, S)), C);
    vec3 invMax = 1.0 / (maxRGB + FSR_EPS);
    vec3 amp    = clamp(min(minRGB, 2.0 - maxRGB) * invMax, 0.0, 1.0);
    amp         = inversesqrt(amp + FSR_EPS);

    float w = -rcasInvP / dot(amp, lumCoef);

    float sumL   = NL + WL + EL + SL;
    float invDen = 1.0 / (4.0 * w + 1.0);
    float sharpL = clamp((sumL * w + CL) * invDen, 0.0, 1.0);

    vec3  chroma      = C - vec3(CL);
    vec3  sharpColor  = chroma + vec3(sharpL);

    vec3  outColor    = mix(C, sharpColor, u_setting.x);
    return vec4(outColor, 1.0);
}

// ------------------------------------------------------------------
void main()
{
    gl_FragColor = FsrRcasVanilla(v_texcoord0);
}
