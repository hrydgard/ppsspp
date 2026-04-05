/* MMPX.glc
   Copyright 2020 Morgan McGuire & Mara Gagiu.
   Provided under the Open Source MIT license https://opensource.org/licenses/MIT

   See js-demo.html for the commented source code.
   This is an optimized GLSL port of that version
   by Morgan McGuire and Mara Gagiu.
*/

// Performs 2x upscaling.

#define ABGR8 uint

// If we took an image as input, we could use a sampler to do the clamping. But we decode
// low-bpp texture data directly, so...
ABGR8 src(int x, int y) {
    return readColoru(uvec2(clamp(x, 0, params.width - 1), clamp(y, 0, params.height - 1)));
}

uint luma(ABGR8 C) {
    uint alpha = (C & 0xFF000000u) >> 24;
    return (((C & 0x00FF0000u) >> 16) + ((C & 0x0000FF00u) >> 8) + (C & 0x000000FFu) + 1u) * (256u - alpha);
}

bool all_eq2(ABGR8 B, ABGR8 A0, ABGR8 A1) {
    return ((B ^ A0) | (B ^ A1)) == 0u;
}

bool all_eq3(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2) {
    return ((B ^ A0) | (B ^ A1) | (B ^ A2)) == 0u;
}

bool all_eq4(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2, ABGR8 A3) {
    return ((B ^ A0) | (B ^ A1) | (B ^ A2) | (B ^ A3)) == 0u;
}

bool any_eq3(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2) {
    return B == A0 || B == A1 || B == A2;
}

bool none_eq2(ABGR8 B, ABGR8 A0, ABGR8 A1) {
    return (B != A0) && (B != A1);
}

bool none_eq4(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2, ABGR8 A3) {
    return B != A0 && B != A1 && B != A2 && B != A3;
}

void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

    ABGR8 A = src(srcX - 1, srcY - 1), B = src(srcX, srcY - 1), C = src(srcX + 1, srcY - 1);
    ABGR8 D = src(srcX - 1, srcY + 0), E = src(srcX, srcY + 0), F = src(srcX + 1, srcY + 0);
    ABGR8 G = src(srcX - 1, srcY + 1), H = src(srcX, srcY + 1), I = src(srcX + 1, srcY + 1);

    ABGR8 J = E, K = E, L = E, M = E;

    if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) | (H ^ E) | (I ^ E)) != 0u) {
        ABGR8 P = src(srcX, srcY - 2), S = src(srcX, srcY + 2);
        ABGR8 Q = src(srcX - 2, srcY), R = src(srcX + 2, srcY);
        ABGR8 Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);

        // 1:1 slope rules
        if ((D == B && D != H && D != F) && (El >= Dl || E == A) && any_eq3(E, A, C, G) && ((El < Dl) || A != D || E != P || E != Q)) J = D;
        if ((B == F && B != D && B != H) && (El >= Bl || E == C) && any_eq3(E, A, C, I) && ((El < Bl) || C != B || E != P || E != R)) K = B;
        if ((H == D && H != F && H != B) && (El >= Hl || E == G) && any_eq3(E, A, G, I) && ((El < Hl) || G != H || E != S || E != Q)) L = H;
        if ((F == H && F != B && F != D) && (El >= Fl || E == I) && any_eq3(E, C, G, I) && ((El < Fl) || I != H || E != R || E != S)) M = F;

        // Intersection rules
        if ((E != F && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H)) && (F != src(srcX + 3, srcY))) K = M = F;
        if ((E != D && all_eq4(E, A, G, F, R) && all_eq2(D, B, H)) && (D != src(srcX - 3, srcY))) J = L = D;
        if ((E != H && all_eq4(E, G, I, B, P) && all_eq2(H, D, F)) && (H != src(srcX, srcY + 3))) L = M = H;
        if ((E != B && all_eq4(E, A, C, H, S) && all_eq2(B, D, F)) && (B != src(srcX, srcY - 3))) J = K = B;
        if (Bl < El && all_eq4(E, G, H, I, S) && none_eq4(E, A, D, C, F)) J = K = B;
        if (Hl < El && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F)) L = M = H;
        if (Fl < El && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H)) K = M = F;
        if (Dl < El && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H)) J = L = D;

        // 2:1 slope rules
        if (H != B) {
            if (H != A && H != E && H != C) {
                if (all_eq3(H, G, F, R) && none_eq2(H, D, src(srcX + 2, srcY - 1))) L = M;
                if (all_eq3(H, I, D, Q) && none_eq2(H, F, src(srcX - 2, srcY - 1))) M = L;
            }

            if (B != I && B != G && B != E) {
                if (all_eq3(B, A, F, R) && none_eq2(B, D, src(srcX + 2, srcY + 1))) J = K;
                if (all_eq3(B, C, D, Q) && none_eq2(B, F, src(srcX - 2, srcY + 1))) K = J;
            }
        } // H !== B

        if (F != D) {
            if (D != I && D != E && D != C) {
                if (all_eq3(D, A, H, S) && none_eq2(D, B, src(srcX + 1, srcY + 2))) J = L;
                if (all_eq3(D, G, B, P) && none_eq2(D, H, src(srcX + 1, srcY - 2))) L = J;
            }

            if (F != E && F != A && F != G) {
                if (all_eq3(F, C, H, S) && none_eq2(F, B, src(srcX - 1, srcY + 2))) K = M;
                if (all_eq3(F, I, B, P) && none_eq2(F, H, src(srcX - 1, srcY - 2))) M = K;
            }
        } // F !== D
    } // not constant

    // Write four pixels at once.
    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, unpackUnorm4x8(J));
    writeColorf(destXY + ivec2(1, 0), unpackUnorm4x8(K));
    writeColorf(destXY + ivec2(0, 1), unpackUnorm4x8(L));
    writeColorf(destXY + ivec2(1, 1), unpackUnorm4x8(M));
}
