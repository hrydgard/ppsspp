/* MMPX.glc
   Copyright 2020 Morgan McGuire & Mara Gagiu.
   Provided under the Open Source MIT license https://opensource.org/licenses/MIT

   See js-demo.html for the commented source code.
   This is an optimized GLSL port of that version
   by Morgan McGuire and Mara Gagiu.
*/

// Performs 2x upscaling

#define ABGR8 uint

// If we took an image as input, we could use a sampler to do the clamping. But we decode
// low-bpp texture data directly, so...

ABGR8 src(int x, int y) {
    return readColoru(uvec2(clamp(x, 0, params.width - 1), clamp(y, 0, params.height - 1)));
}

uint luma(ABGR8 C) {
    uint alpha = (C >> 24) & 0xFFu;  // Simplified bit operations
    if (alpha == 0u) return 1530u;   // Ensure fully transparent pixels return max value 1530
    uint rgbSum = ((C >> 16) & 0xFFu) + ((C >> 8) & 0xFFu) + (C & 0xFFu);
    float factor = 1.0f + (255.0f - alpha) * 0.00392157f; // Division by 255 alternative
    return uint(rgbSum * factor);
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

// Two-stage weak blending (blend/no-blend)
ABGR8 admix2d(ABGR8 a, ABGR8 d) {
    vec4 ca = unpackUnorm4x8(a);
    vec4 cd = unpackUnorm4x8(d);
    float rgbDist = dot(ca - cd, ca - cd);
    if (dot(ca, ca) < 0.01) return d; // Return d for pure black
    
    vec4 result;
    if (rgbDist < 1.0) {
        // Close colors: Linear blend RGB and Alpha
        result = (ca + cd) * 0.5;
    } else {
        // Distant colors: Return b
        result = cd;
    }
    
    // Repack
    return packUnorm4x8(result);
}

/*=============================================================================
Helper function for 4-pixel cross pattern detection: Scores matches at 
morphologically significant positions. Requires 6 points to pass.

                ┌───┬───┬───┐                ┌───┬───┬───┐
                │ A │ B │ C │                │ A │ B │ 1 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ D │ E │ F │       =>   L   │ B │ A │ 2 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ G │ H │ I │                │ 5 │ 4 │ 3 │
                └───┴───┴───┘                └───┴───┴───┘
=============================================================================*/

bool countPatternMatches(ABGR8 LA, ABGR8 LB, ABGR8 L1, ABGR8 L2, ABGR8 L3, ABGR8 L4, ABGR8 L5) {
    int score1 = 0; // Diagonal pattern 1 (╲)
    int score2 = 0; // Diagonal pattern 2 (╱)
    int score3 = 0; // Horizontal/vertical line pattern
    int scoreBonus = 0;

    vec4 ca = unpackUnorm4x8(LA);
    vec4 cb = unpackUnorm4x8(LB);
    // Use dot product instead of Euclidean distance to save sqrt cost
    float rgbDist = dot(ca - cb, ca - cb);

    // Enhance detail for very similar colors, reduce for high-contrast edges
    if (rgbDist < 0.06386) { // two golden sections
        scoreBonus += 1;
    } else if (rgbDist > 2.18847) { // High-contrast threshold (two golden sections)
        scoreBonus -= 1;
    } 

    // Diagonal pattern 1 (╲): Penalty system
    if (LB == L2 || LB == L4) {
        score1 -= int(LB == L2 && LA == L1) * 1;  // Penalize A-1, B-2 crossing
        score1 -= int(LB == L4 && LA == L5) * 1;  // Penalize A-5, B-4 crossing
        
        // Compensate with triangle patterns
        score1 += int(LB == L1 && L1 == L2) * 1;  // B-1-2 triangle
        score1 += int(LB == L4 && L4 == L5) * 1;  // B-4-5 triangle
        score1 += int(L2 == L3 && L3 == L4) * 1;  // 2-3-4 triangle
        
        score1 += scoreBonus + 6;
    } 

    // Diagonal pattern 2 (╱): Penalty system
    if (LA == L1 || LA == L5) {
        score2 -= int(LB == L2 && LA == L1) * 1;  // Penalize A-1, B-2 crossing
        score2 -= int(LB == L4 && LA == L5) * 1;  // Penalize A-5, B-4 crossing
        score2 -= int(LA == L3) * 1;              // Penalize A-3 crossing
        
        // Compensate with triangle patterns
        score2 += int(LB == L1 && L1 == L2) * 1;  // B-1-2 triangle
        score2 += int(LB == L4 && L4 == L5) * 1;  // B-4-5 triangle
        score2 += int(L2 == L3 && L3 == L4) * 1;  // 2-3-4 triangle
    
        score2 += scoreBonus + 6;
    } 

    // Horizontal/vertical line pattern: Reward system
    if (LA == L2 || LB == L1 || LA == L4 || LB == L5 || (L1 == L2 && L2 == L3) || (L3 == L4 && L4 == L5)) {
        score3 += int(LA == L2);     // +1: A matches 2
        score3 += int(LB == L1);     // +1: B matches 1
        score3 += int(L3 == L4);     // +1: 3 matches 4
        score3 += int(L4 == L5);     // +1: 4 matches 5
        score3 += int(L3 == L4 && L4 == L5); // +1: 3-4-5 continuity

        score3 += int(LB == L5);     // +1: B matches 5
        score3 += int(LA == L4);     // +1: A matches 4
        score3 += int(L2 == L3);     // +1: 2 matches 3
        score3 += int(L1 == L2);     // +1: 1 matches 2
        score3 += int(L1 == L2 && L2 == L3); // +1: 1-2-3 continuity

        // 2x2 uniform block bonus
        score3 += int(LA == L2 && L2 == L3 && L3 == L4) * 2;

        // Patch for grid artifacts
        score3 -= int(LB == L1 && L1 == L5 && LA == L2 && L2 == L4) * 3; 
        score3 -= int(LA == L1 && LA == L5); // Penalize diagonal conflict
        
        score3 += scoreBonus; // Conservative bonus
    } 

    // Take maximum score
    int score = max(max(score1, score2), score3);
    return score < 6; // Require 6 points to pass
}

void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

    // Sample 3x3 neighborhood
    ABGR8 A = src(srcX - 1, srcY - 1), B = src(srcX, srcY - 1), C = src(srcX + 1, srcY - 1);
    ABGR8 D = src(srcX - 1, srcY + 0), E = src(srcX, srcY + 0), F = src(srcX + 1, srcY + 0);
    ABGR8 G = src(srcX - 1, srcY + 1), H = src(srcX, srcY + 1), I = src(srcX + 1, srcY + 1);

    ABGR8 J = E, K = E, L = E, M = E;

    if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) | (H ^ E) | (I ^ E)) != 0u) {
        // Sample extended neighborhood
        ABGR8 P = src(srcX, srcY - 2), S = src(srcX, srcY + 2);
        ABGR8 Q = src(srcX - 2, srcY), R = src(srcX + 2, srcY);
        ABGR8 Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);

        // Default output (centered)
        ivec2 destXY = ivec2(xy) * 2;
        vec4 centerColor = unpackUnorm4x8(E);
        writeColorf(destXY, centerColor);
        writeColorf(destXY + ivec2(1, 0), centerColor);
        writeColorf(destXY + ivec2(0, 1), centerColor);
        writeColorf(destXY + ivec2(1, 1), centerColor);

        // 4-pixel grid pattern detection
        if (A == E && B == D && A != B && countPatternMatches(A, B, C, F, I, H, G)) return;
        if (C == E && B == F && C != B && countPatternMatches(C, B, A, D, G, H, I)) return;
        if (G == E && D == H && G != H && countPatternMatches(G, H, I, F, C, B, A)) return;
        if (I == E && F == H && I != H && countPatternMatches(I, H, G, D, A, B, C)) return;
       
        // Original MMPX logic

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

        // Weak blending for edge artifacts
        if (Bl < El && all_eq4(E, G, H, I, S) && none_eq4(E, A, D, C, F)) {J=admix2d(B,J); K=admix2d(B,K);}
        if (Hl < El && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F)) {L=admix2d(H,L); M=admix2d(H,M);}
        if (Fl < El && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H)) {K=admix2d(F,K); M=admix2d(F,M);}
        if (Dl < El && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H)) {J=admix2d(D,J); L=admix2d(D,L);}

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
        }

        if (F != D) {
            if (D != I && D != E && D != C) {
                if (all_eq3(D, A, H, S) && none_eq2(D, B, src(srcX + 1, srcY + 2))) J = L;
                if (all_eq3(D, G, B, P) && none_eq2(D, H, src(srcX + 1, srcY - 2))) L = J;
            }

            if (F != E && F != A && F != G) {
                if (all_eq3(F, C, H, S) && none_eq2(F, B, src(srcX - 1, srcY + 2))) K = M;
                if (all_eq3(F, I, B, P) && none_eq2(F, H, src(srcX - 1, srcY - 2))) M = K;
            }
        }
    }

    // Write 2x2 upscaled block
    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, unpackUnorm4x8(J));
    writeColorf(destXY + ivec2(1, 0), unpackUnorm4x8(K));
    writeColorf(destXY + ivec2(0, 1), unpackUnorm4x8(L));
    writeColorf(destXY + ivec2(1, 1), unpackUnorm4x8(M));
}
