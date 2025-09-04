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

uniform sampler2D sampler0;
uniform vec2 u_texelDelta;   // = 1 / textureSize
uniform vec2 u_pixelDelta;   // not used

varying vec2 v_texcoord0;

#ifdef GL_FRAGMENT_PRECISION_HIGH
#define HIGHP highp
#else
#define HIGHP mediump
#endif

// ---------- FSR-EASU core (unchanged) ----------
void FsrEasuCon(
    out vec4 con0, out vec4 con1, out vec4 con2, out vec4 con3,
    vec2  inputViewportInPixels,
    vec2  inputSizeInPixels,
    vec2  outputSizeInPixels)
{
    con0 = vec4(
        inputViewportInPixels.x / outputSizeInPixels.x,
        inputViewportInPixels.y / outputSizeInPixels.y,
        0.5 * inputViewportInPixels.x / outputSizeInPixels.x - 0.5,
        0.5 * inputViewportInPixels.y / outputSizeInPixels.y - 0.5);

    con1 = vec4(1.0, 1.0, 1.0, -1.0) / inputSizeInPixels.xyxy;
    con2 = vec4(-1.0,  2.0,  1.0,  2.0) / inputSizeInPixels.xyxy;
    con3 = vec4( 0.0,  4.0,  0.0,  0.0) / inputSizeInPixels.xyxy;
}

void FsrEasuTapF(
    inout vec3 aC, inout float aW,
    vec2  off,
    vec2  dir, vec2 len,
    float lob, float clp,
    vec3  c)
{
    vec2 v = vec2(dot(off, dir), dot(off, vec2(-dir.y, dir.x))) * len;
    float d2 = min(dot(v, v), clp);
    float wB = 0.4 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB; wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

void FsrEasuSetF(
    inout vec2 dir, inout float len,
    float w,
    float lA, float lB, float lC, float lD, float lE)
{
    float lenX = max(abs(lD - lC), abs(lC - lB));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) / (lenX + 1e-5), 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;

    float lenY = max(abs(lE - lC), abs(lC - lA));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) / (lenY + 1e-5), 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

vec3 FsrEasuF(vec2 ip, vec4 con0, vec4 con1, vec4 con2, vec4 con3)
{
    vec2 pp = ip * con0.xy + con0.zw;
    vec2 fp = floor(pp);
    pp -= fp;

    vec2 p0 = fp * con1.xy + con1.zw;
    vec2 p1 = p0 + con2.xy;
    vec2 p2 = p0 + con2.zw;
    vec2 p3 = p0 + con3.xy;

    vec4 off = vec4(-0.5, 0.5, -0.5, 0.5) * con1.xxyy;

    vec3 bC = texture2D(sampler0, p0 + off.xw).rgb; float bL = bC.g + 0.5*(bC.r + bC.b);
    vec3 cC = texture2D(sampler0, p0 + off.yw).rgb; float cL = cC.g + 0.5*(cC.r + cC.b);
    vec3 iC = texture2D(sampler0, p1 + off.xw).rgb; float iL = iC.g + 0.5*(iC.r + iC.b);
    vec3 jC = texture2D(sampler0, p1 + off.yw).rgb; float jL = jC.g + 0.5*(jC.r + jC.b);
    vec3 fC = texture2D(sampler0, p1 + off.yz).rgb; float fL = fC.g + 0.5*(fC.r + fC.b);
    vec3 eC = texture2D(sampler0, p1 + off.xz).rgb; float eL = eC.g + 0.5*(eC.r + eC.b);
    vec3 kC = texture2D(sampler0, p2 + off.xw).rgb; float kL = kC.g + 0.5*(kC.r + kC.b);
    vec3 lC = texture2D(sampler0, p2 + off.yw).rgb; float lL = lC.g + 0.5*(lC.r + lC.b);
    vec3 hC = texture2D(sampler0, p2 + off.yz).rgb; float hL = hC.g + 0.5*(hC.r + hC.b);
    vec3 gC = texture2D(sampler0, p2 + off.xz).rgb; float gL = gC.g + 0.5*(gC.r + gC.b);
    vec3 oC = texture2D(sampler0, p3 + off.yz).rgb; float oL = oC.g + 0.5*(oC.r + oC.b);
    vec3 nC = texture2D(sampler0, p3 + off.xz).rgb; float nL = nC.g + 0.5*(nC.r + nC.b);

    vec2  dir = vec2(0.0);
    float len = 0.0;

    FsrEasuSetF(dir, len, (1.0 - pp.x)*(1.0 - pp.y), bL, eL, fL, gL, jL);
    FsrEasuSetF(dir, len,        pp.x *(1.0 - pp.y), cL, fL, gL, hL, kL);
    FsrEasuSetF(dir, len, (1.0 - pp.x)*       pp.y , fL, iL, jL, kL, nL);
    FsrEasuSetF(dir, len,        pp.x *       pp.y , gL, jL, kL, lL, oL);

    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < 1.0/32768.0;
    dirR = inversesqrt(dirR);
    dirR = zro ? 1.0 : dirR;
    dir  = zro ? vec2(1.0, 0.0) : (dir * dirR);

    len = len * 0.5;
    len *= len;

    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    vec2  len2 = vec2(1.0 + (stretch - 1.0)*len,
                      1.0 - 0.5*len);

    float lob = 0.5 - 0.29*len;
    float clp = 1.0/lob;

    vec3  aC = vec3(0.0);
    float aW = 0.0;

    FsrEasuTapF(aC, aW, vec2( 0,-1)-pp, dir, len2, lob, clp, bC);
    FsrEasuTapF(aC, aW, vec2( 1,-1)-pp, dir, len2, lob, clp, cC);
    FsrEasuTapF(aC, aW, vec2(-1, 1)-pp, dir, len2, lob, clp, iC);
    FsrEasuTapF(aC, aW, vec2( 0, 1)-pp, dir, len2, lob, clp, jC);
    FsrEasuTapF(aC, aW, vec2( 0, 0)-pp, dir, len2, lob, clp, fC);
    FsrEasuTapF(aC, aW, vec2(-1, 0)-pp, dir, len2, lob, clp, eC);
    FsrEasuTapF(aC, aW, vec2( 1, 1)-pp, dir, len2, lob, clp, kC);
    FsrEasuTapF(aC, aW, vec2( 2, 1)-pp, dir, len2, lob, clp, lC);
    FsrEasuTapF(aC, aW, vec2( 2, 0)-pp, dir, len2, lob, clp, hC);
    FsrEasuTapF(aC, aW, vec2( 1, 0)-pp, dir, len2, lob, clp, gC);
    FsrEasuTapF(aC, aW, vec2( 1, 2)-pp, dir, len2, lob, clp, oC);
    FsrEasuTapF(aC, aW, vec2( 0, 2)-pp, dir, len2, lob, clp, nC);

    vec3 min4 = min(min(fC, gC), min(jC, kC));
    vec3 max4 = max(max(fC, gC), max(jC, kC));
    return min(max4, max(min4, aC / aW));
}

// ---------- entry point ----------
void main() {
    vec2 texSize   = vec2(1.0) / u_texelDelta;  // texture resolution
    vec2 outSize   = vec2(1.0) / u_texelDelta;  // if canvas == texture, same

    vec4 con0, con1, con2, con3;
    FsrEasuCon(con0, con1, con2, con3,
               texSize, texSize, outSize);

    // Convert 0-1 texcoord to integer pixel in output space
    vec2 ip = v_texcoord0 * outSize - vec2(0.5);

    vec3 rgb = FsrEasuF(ip, con0, con1, con2, con3);
    gl_FragColor = vec4(rgb, 1.0);
}
