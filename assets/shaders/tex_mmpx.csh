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

// Calculate the RGB distance between two ABGR8 colors
float rgb_distance(ABGR8 a, ABGR8 b)
{
    vec4 ca = unpackUnorm4x8(a);
    vec4 cb = unpackUnorm4x8(b);
    return distance(ca.rgb, cb.rgb);
}

// Calculate the normalized luminance difference between two ABGR8 colors. In practice, luma values range from 0.001 to 0.9998.
float luma_distance(ABGR8 a, ABGR8 b)
{
    return abs(luma(a) - luma(b)) * 0.00000509f;  // Multiplicative substitute for division by (255.0 * 256.0);
}

/*=============================================================================
Auxiliary function for 4-pixel cross determination: Scores the number of matches at specific positions in the pattern. Three pattern conditions must be met to achieve 6 points.
                ┌───┬───┬───┐                ┌───┬───┬───┐
                │ A │ B │ C │                │ A │ B │ 1 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ D │ E │ F │       =>   L   │ B │ A │ 2 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ G │ H │ I │                │ 5 │ 4 │ 3 │
                └───┴───┴───┘                └───┴───┴───┘
=============================================================================*/

bool countPatternMatches(ABGR8 LA, ABGR8 LB, ABGR8 L1, ABGR8 L2, ABGR8 L3, ABGR8 L4, ABGR8 L5) {

    int score1 = 0; // Diagonal pattern 1
    int score2 = 0; // Diagonal pattern 2
    int score3 = 0; // Horizontal/vertical line pattern
    int scoreBonus = 0;
    
    // Jointly determine the perceptual difference between two pixels using RGB distance and luminance distance
    float pixelDist = rgb_distance(LA, LB)*0.356822 + luma_distance(LA, LB)*0.382; // Proportions follow the golden ratio

    // Add details for very similar colors, reduce details for highly different colors (font edges)
    if (pixelDist < 0.12) { // Colors are quite similar
        scoreBonus += 1;
    } else if (pixelDist > 0.9) { // High contrast (e.g., black and white)
        scoreBonus -= 1;
    } 

    // Diagonals use a penalty system: deduct points for crosses, add back if conditions are met  
    // 1. Diagonal pattern ╲ (Condition: B = 2 or 4)
    if (LB == L2 || LB == L4) {
        score1 -= int(LB == L2 && LA == L1) * 1;    	// A-1 and B-2 form a cross: deduct points
        score1 -= int(LB == L4 && LA == L5) * 1;   		// A-5 and B-4 form a cross: deduct points

        // Add points if the following triangular patterns are satisfied (canceling previous cross deductions)		
        score1 += int(LB == L1 && L1 == L2) * 1;   		// B-1-2 form a triangle: add points
        score1 += int(LB == L4 && L4 == L5) * 1;   		// B-4-5 form a triangle: add points
        score1 += int(L2 == L3 && L3 == L4) * 1;   		// 2-3-4 form a triangle: add points
        
        score1 += scoreBonus + 6;
    } 

    // 2. Diagonal pattern ╱ (Condition: A = 1 or 5)
    if (LA == L1 || LA == L5) {
        score2 -= int(LB == L2 && LA == L1) * 1;    	// A-1 and B-2 cross: deduct points
        score2 -= int(LB == L4 && LA == L5) * 1;   		// A-5 and B-4 cross: deduct points
        score2 -= int(LA == L3) * 1;    					// A-3 forms a cross: deduct points				

        // Add points if the following triangular patterns are satisfied (canceling previous cross deductions)
        score2 += int(LB == L1 && L1 == L2) * 1;   		// B-1-2 form a triangle: add points
        score2 += int(LB == L4 && L4 == L5) * 1;   		// B-4-5 form a triangle: add points
        score2 += int(L2 == L3 && L3 == L4) * 1;   		// 2-3-4 form a triangle: add points
    
        score2 += scoreBonus + 6;
    } 

    // 3. Horizontal/vertical line patterns (Condition: horizontal continuity) use a scoring system; pass only if conditions are met
    if (LA == L2 || LB == L1 || LA == L4 || LB == L5 || (L1 == L2 && L2 == L3) || (L3 == L4 && L4 == L5)) {
        score3 += int(LA == L2);    	// A matches 2	+1
        score3 += int(LB == L1);    	// B matches 1	+1
        score3 += int(L3 == L4);    	// 3 matches 4	+1
        score3 += int(L4 == L5);    	// 4 matches 5	+1
        score3 += int(L3 == L4 && L4 == L5) ; // 3-4-5 continuous

        score3 += int(LB == L5);    	// B matches 5	+1
        score3 += int(LA == L4);    	// A matches 4	+1
        score3 += int(L2 == L3);    	// 2 matches 3	+1
        score3 += int(L1 == L2);    	// 1 matches 2	+1
        score3 += int(L1 == L2 && L2 == L3) ; // 1-2-3 continuous

        // A x 4 square	
        score3 += int(LA == L2 && L2 == L3 && L3 == L4) * 2;

        // Patch for the above rule to avoid bubble-like cross patterns in large grids. Some games use single-side patterns, so bilateral checks are preferred (work in progress)
        score3 -= int(LB == L1 && L1 == L5 && LA == L2 && L2 == L4)*3 ; 

        score3 -= int(LA == L1 && LA == L5) ; // Deduct points if L1 and L5 are both A to avoid excessive scoring and diagonal pattern misidentification
        
        // Bonus points
        score3 += scoreBonus;	// Experience: Even with very similar colors, avoid over-scoring to prevent bubbles in Z-shaped crosses
    } 

    // Take the maximum of the four scores
    int score = max(max(score1, score2), score3);
    
    return score < 6; // Requires a minimum of 6 points
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


        // Default output (to avoid code duplication)
        ivec2 destXY = ivec2(xy) * 2;
        ABGR8 defaultColor = E;
        writeColorf(destXY, unpackUnorm4x8(defaultColor));
        writeColorf(destXY + ivec2(1, 0), unpackUnorm4x8(defaultColor));
        writeColorf(destXY + ivec2(0, 1), unpackUnorm4x8(defaultColor));
        writeColorf(destXY + ivec2(1, 1), unpackUnorm4x8(defaultColor));

        // Check for "convex" shapes to avoid single-pixel spikes on long straight edges
        if (A == B && B == C && E == H && A != D && C != F && rgb_distance(D, F) < 0.2 && rgb_distance(B, E) > 0.6) return;

        if (A == D && D == G && E == F && A != B && G != H && rgb_distance(B, H) < 0.2 && rgb_distance(D, E) > 0.6) return;

        if (C == F && F == I && E == D && B != C && H != I && rgb_distance(B, H) < 0.2 && rgb_distance(E, F) > 0.6) return;

        if (G == H && H == I && B == E && D != G && F != I && rgb_distance(D, F) < 0.2 && rgb_distance(E, H) > 0.6) return;


        // Check each 4-pixel cross in "田" (field) shape and pass five surrounding pixels for pattern judgment
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
