/* MMPX.glc
   Copyright 2020 Morgan McGuire & Mara Gagiu.
   Provided under the Open Source MIT license https://opensource.org/licenses/MIT

   by Morgan McGuire and Mara Gagiu.
   2025 Enhanced by CrashGG.
*/

// Performs 2x upscaling.

#define ABGR8 uint

/* If we took an image as input, we could use a sampler to do the clamping. But we decode
   low-bpp texture data directly, so...

   readColoru is a built-in function (implementation depends on rendering engine) that reads unsigned integer format color data from texture/framebuffer.
   Normalization loss: Using readColor (without 'u') reads as floats (vec4), mapping integer range (0-255) to [0.0, 1.0], causing precision loss (255→1.0, 1→0.0039215686...).
   The unpackUnorm4x8 function in MMPX converts uint to vec4 (normalized floats) - this step is lossy.
*/

ABGR8 src(int x, int y) {
    return readColoru(uvec2(clamp(x, 0, params.width - 1), clamp(y, 0, params.height - 1)));
}

/*
// Original luminance decision mechanism where small alpha changes cause significant output amplification
uint luma(ABGR8 C) {
    uint alpha = (C & 0xFF000000u) >> 24;
    return (((C & 0x00FF0000u) >> 16) + ((C & 0x0000FF00u) >> 8) + (C & 0x000000FFu) + 1u) * (256u - alpha);
}
*/
//
// Modified ternary alpha segmentation for efficiency optimization (LUT possible)
uint luma(ABGR8 C) {
    uint alpha = (C & 0xFF000000u) >> 24;
    
    uint sum = ((C & 0x00FF0000u) >> 16) + 
               ((C & 0x0000FF00u) >> 8)  + 
               (C & 0x000000FFu) + 1u;

    uint alphafactor = 
        (alpha == 0) ? 7000 :     // Fully transparent
        (alpha > 217) ? 1000 :    // ≈0.8535534×255 (Two golden cuts)
        (alpha > 157) ? 2000 :    // ≈0.618034×255 (One golden cut)
        (alpha > 97) ? 3000 :     // ≈0.381966×255 (Short golden cut)
        (alpha > 37) ? 4000 : 5000; // ≈0.1464466×255 (Two short golden cuts)

    return sum + alphafactor;
}
//

uvec4 extractPIX(ABGR8 color) {
    uint r = (color >> 0) & 0xFFu;
    uint g = (color >> 8) & 0xFFu;
    uint b = (color >> 16) & 0xFFu;
    uint a = (color >> 24) & 0xFFu;
    return uvec4(r, g, b, a);
}

// Approximate RGB equality (Euclidean distance threshold ≈0.00932276) and alpha difference <14.59% (≈37.2/255)
bool same(ABGR8 B, ABGR8 A0) {
    uvec4 b_pix = extractPIX(B);
    uvec4 a0_pix = extractPIX(A0);
    
    // Sum of squared RGB differences
    ivec3 diff = ivec3(b_pix.rgb) - ivec3(a0_pix.rgb);
    uint alphaDiff = abs(int(b_pix.a) - int(a0_pix.a));
    
    bool alphaDiffCheck = alphaDiff < 38u; // 5.57% ≈14.21, 14.59%≈37.2
    return dot(diff, diff) < 606u && alphaDiffCheck;
}

// Full RGBA equality
bool fullsame(ABGR8 B, ABGR8 A0){
    return B == A0;
}

// RGBA inequality
bool notsame(ABGR8 B, ABGR8 A0){
    return B != A0;
}

bool all_eq2(ABGR8 B, ABGR8 A0, ABGR8 A1) {
    return (same(B,A0) && same(B,A1));
}

bool all_eq3(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2) {
    return (same(B,A0) && same(B,A1) && same(B,A2));
}

bool all_eq4(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2, ABGR8 A3) {
    return (same(B,A0) && same(B,A1) && same(B,A2) && same(B,A3));
}

bool full_eq3(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2) {
    return (fullsame(B,A0) && fullsame(B,A1) && fullsame(B,A2));
}

bool any_eq3(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2) {
   return (same(B,A0) || same(B,A1) || same(B,A2));
}

bool none_eq2(ABGR8 B, ABGR8 A0, ABGR8 A1) {
   return (notsame(B,A0) && notsame(B,A1));
}

bool none_eq4(ABGR8 B, ABGR8 A0, ABGR8 A1, ABGR8 A2, ABGR8 A3) {
   return (notsame(B,A0) && notsame(B,A1) && notsame(B,A2) && notsame(B,A3));
}

///////////////////////     Test Colors     ///////////////////////
 //vec4 testcolor = vec4(1.0, 0.0, 1.0, 1.0);  // Magenta (opaque)
 //vec4 testcolor2 = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan (opaque)
 //vec4 testcolor3 = vec4(1.0, 1.0, 0.0, 1.0);  // Yellow (opaque)
 //vec4 testcolor4 = vec4(1.0, 1.0, 1.0, 1.0);  // White (opaque)

float LumaFactor(vec4 col1, vec4 col2) {
    // Calculate luminance difference (0-1) using RGB average
    float LumaDiff = abs((col1.r + col1.g + col1.b) - (col2.r + col2.g + col2.b))*0.3333333;
    float alphaDiff = abs(col1.a - col2.a);
    // Golden ratio scaling (0.618 - 1.618)
    float LumaGolden = (1.618034 - LumaDiff) * (1.618034 - LumaDiff); // Squared for dot() operations
    
    return LumaGolden/(1.0 - alphaDiff); // Amplify result with alpha difference
}

vec4 admixC(ABGR8 X1, ABGR8 X2, vec4 rgbaE) {
    // Type-C: Weak blending dominance. Returns original if conditions unmet to avoid artifacts.
    if (rgbaE.a < 0.01) return rgbaE; // Handle transparent center

    vec4 rgbaX1 = unpackUnorm4x8(X1);
    vec4 rgbaX2 = unpackUnorm4x8(X2);
    
    // Return if both sides are black (avoid blending)
    if (dot(rgbaX1.rgb, rgbaX1.rgb) < 0.05 && dot(rgbaX2.rgb, rgbaX2.rgb) < 0.05) return rgbaE;
    // Return if both sides are transparent
    if (rgbaX1.a < 0.01 && rgbaX2.a < 0.01) return rgbaE;

    // Calculate squared Euclidean distance
    float rgbaDistX1 = dot(rgbaX1 - rgbaE, rgbaX1 - rgbaE);
    float rgbaDistX2 = dot(rgbaX2 - rgbaE, rgbaX2 - rgbaE);
    
    // Apply luminance factor adjustment
    float rgb_lumaX1 = rgbaDistX1 * LumaFactor(rgbaX1, rgbaE);
    float rgb_lumaX2 = rgbaDistX2 * LumaFactor(rgbaX2, rgbaE);
    
    // Select closer reference color
    bool useX1 = rgb_lumaX1 < rgb_lumaX2;
    float rgb_luma = useX1 ? rgb_lumaX1 : rgb_lumaX2;
    vec4 rgbaX = useX1 ? rgbaX1 : rgbaX2;

    // Blend or return original
    return (rgb_luma < 0.75) ? mix(rgbaX, rgbaE, 0.5) : rgbaE;
}

vec4 admixK(ABGR8 X, vec4 rgbaE) {
    vec4 rgbaX = unpackUnorm4x8(X);
    // Type-K: Weak blending. Return original if transparent.
    if (rgbaX.a < 0.01 || rgbaE.a < 0.01) return rgbaE;

    // Weighted dot product difference with luminance
    float rgbaDist = dot(rgbaX - rgbaE, rgbaX - rgbaE);
    float rgb_luma = rgbaDist * LumaFactor(rgbaX, rgbaE);
    
    return (rgb_luma < 0.75) ? mix(rgbaX, rgbaE, 0.381966) : rgbaE;
}

vec4 admixL(vec4 rgbaX, vec4 rgbaE, ABGR8 S) {
    // Type-L: Strong blending dominance
    if (rgbaE.a < 0.01) return rgbaX; // Copy target if E transparent
    if (rgbaX.a < 0.01) return rgbaE; // Return E if X transparent

    float rgbaDist = dot(rgbaX - rgbaE, rgbaX - rgbaE);
    if (rgbaDist < 0.00136041) return rgbaX; // Return if nearly identical

    vec4 rgbaS = unpackUnorm4x8(S);
    float rgbaXSDist = dot(rgbaX - rgbaS, rgbaX - rgbaS);
    if (rgbaXSDist > 0.00136041) return rgbaX; // Return if X differs from S (already blended)

    float rgb_luma = rgbaDist * LumaFactor(rgbaX, rgbaE);
    return (rgb_luma < 0.75) ? mix(rgbaX, rgbaE, 0.381966) : rgbaX;
}

/* Main corner detection with X-cross check
                                                   P
                ┌───┬───┬───┐                ┌───┬───┬───┐
                │ A │ B │ C │                │ A │ B2│ 1 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ D │ E │ F │   => L      Q  │ B1│ E │ 2 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ G │ H │ I │                │ 5 │ 4 │ 3 │
                └───┴───┴───┘                └───┴───┴───┘
    Euclidean: √(ΔR² + ΔG² + ΔB²)
        Black↔White: 1.732, Black↔RGB: 1.0, White↔R/G/B: 1.414
    Dot product: dot(LA-LB, LA-LB) = Euclidean² (faster)
        Black↔White: 3.0, Black↔RGB: 1.0, White↔R/G/B: 2.0
	
    Golden ratio positions (Euclidean): 0.382, 0.5, 0.618, 0.382²
    Corresponding dot thresholds:       0.4377, 0.75, 1.1459, 0.06386
*/
vec4 admixX(ABGR8 LE, ABGR8 LB1, ABGR8 LB2, ABGR8 LA, ABGR8 L1, ABGR8 L2, ABGR8 L3, ABGR8 L4, ABGR8 L5, ABGR8 LP, ABGR8 LQ) {
    vec4 rgbaLE = unpackUnorm4x8(LE); 
    if (fullsame(LE,LB1)||fullsame(LE,LB2)) return rgbaLE; // Early exit if identical
 
    vec4 rgbaLB1 = unpackUnorm4x8(LB1);
    vec4 rgbaLB2 = unpackUnorm4x8(LB2);
    vec4 rgbaLA = unpackUnorm4x8(LA);
    vec4 rgbaL1 = unpackUnorm4x8(L1);
    vec4 rgbaL2 = unpackUnorm4x8(L2);
    vec4 rgbaL3 = unpackUnorm4x8(L3);
    vec4 rgbaL4 = unpackUnorm4x8(L4);
    vec4 rgbaL5 = unpackUnorm4x8(L5);
    vec4 rgbaLP = unpackUnorm4x8(LP);
    vec4 rgbaLQ = unpackUnorm4x8(LQ);
    
    // Define LB blending (handle transparency)
    vec4 rgbaLB = mix(rgbaLB1, rgbaLB2, 0.5);
    if (rgbaLB1.a < 0.01 ) rgbaLB=rgbaLB2;
    if (rgbaLB2.a < 0.01 ) rgbaLB=rgbaLB1;
    if (rgbaLB1.a < rgbaLB.a ) rgbaLB.a=rgbaLB1.a; else rgbaLB.a=rgbaLB2.a; // Reduce artifacts in gradients (e.g., "Jojo's Wall")

    // Calculate RGBA dot product difference
    float rgbaDist = dot(rgbaLB - rgbaLE, rgbaLB - rgbaLE);
    float rgb_luma = rgbaDist * LumaFactor(rgbaLB, rgbaLE);
    float alphaDiff = abs((rgbaLB1.a + rgbaLB2.a) - rgbaLE.a * 2 );
    bool LBLEalpha0 = rgbaLB.a < 0.01 || rgbaLE.a < 0.01; // Transparency flag
  
    // Cross pattern detection (core rule)
    if (same(LE,LA)) {
        if (rgbaDist < 0.06386) return mix(rgbaLE,rgbaLB,0.381966); // Weak blend for near-identical pixels

        // Special pattern: Tuning fork
        bool same_LB1_L4 = same(LB1,L4);
        bool same_LB1_L5 = same(LB1,L5);
        bool same_LB2_L1 = same(LB2,L1);
        bool same_LB2_L2 = same(LB2,L2);
        if ( (same_LB1_L4 && same_LB2_L1 && !same_LB2_L2) || 
             (same_LB1_L5 && same_LB2_L2 && !same_LB1_L4) ) return rgbaLE;

        // Special pattern: Large block cross
        bool same_LE_L2 = same(LE,L2);
        bool same_LE_L4 = same(LE,L4);
        // Special pattern: Large block cross
        if (same_LB2_L1 && same_LB1_L5 && (same(LB2,LP) && same_LE_L2 || same(LB1,LQ) && same_LE_L4)) return rgbaLE;

        // Diagonal grid cross
        bool same_LE_L1 = same(LE,L1);
        bool same_LE_L3 = same(LE,L3);
        bool same_LE_L5 = same(LE,L5);
	// A point
        if ( same_LE_L1 && same_LE_L3 && same_LE_L5 && !same_LE_L2 && !same_LE_L4 ) return rgbaLE;

	// B point
        if ( same_LB2_L2 && same_LB1_L4 && !same_LB2_L1 && !same_LB1_L5 && !same(LB1,L3)&& !same(LB2,L3) ) return rgbaLE;


        // Scoring system for pattern recognition
        int score1 = 0; // Diagonal pattern score
        int score2 = 0; // Straight pattern score
        int scoreBonus = 0;

        // Diagonal pattern scoring
		score1 += int(same_LE_L3);

		score1 += int(same_LE_L1);
		score1 += int(same_LE_L5);  
		score1 += int(same(L2, L4));

		score1 += int(same(LA, LP));  
		score1 += int(same(LA, LQ));
        // Diagonal Y
    if ( score1<1) {
		if (same_LE_L2 && same_LB2_L1 || same_LE_L4 && same_LB1_L5 ) return rgbaLE;
	}
        score1 += int(same_LB2_L2) + int(same_LB1_L4);  

        // Straight pattern scoring
        score2 += int(fullsame(LP, LB2) && fullsame(L5, LB1));
        score2 += int(fullsame(LQ, LB1) && fullsame(L1, LB2));

        // Penalize high luminance/alpha differences
        float LumaDiff = abs((rgbaLE.r+rgbaLE.g+rgbaLE.b+rgbaLE.a)-(rgbaLB.r+rgbaLB.g+rgbaLB.b+rgbaLB.a))*0.3333333;
        if (LumaDiff > 0.8541) scoreBonus -= 1; // High luminance difference penalty
        if (alphaDiff > 1.0) scoreBonus -= 1;   // High alpha difference penalty

        score1 += scoreBonus;
        score2 += scoreBonus;

        // Blend based on scores and thresholds
        if (rgb_luma < 0.75 && (score1 >= 1 || score2 >= 1)) 
            return LBLEalpha0 ? rgbaLE : mix(rgbaLE, rgbaLB, 0.5);
        if (rgb_luma < 1.1459 && (score1 == 1 && score2 >= 1)) 
            return LBLEalpha0 ? rgbaLE : mix(rgbaLE, rgbaLB, 0.381966);
        if (score1 < 2 && score2 >= 1) 
            return LBLEalpha0 ? rgbaLE : mix(rgbaLE, rgbaLB, 0.145898);

        return (score1 >= 2) ? rgbaLB : rgbaLE; // Final decision
    }

    // Non-cross patterns
    if (LBLEalpha0) return rgbaLB; // Handle transparency
    if (notsame(LB1,LB2)) return (rgb_luma < 0.75) ? mix(rgbaLB, rgbaLE, 0.5) : rgbaLB; // Gradient artifact prevention
    if (fullsame(LB1,LA)) return rgbaLB; // Avoid blending on edges
    return (rgb_luma < 0.75) ? mix(rgbaLB, rgbaLE, 0.381966) : rgbaLB; // Default weak blend
}


void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

    // Sample 3x3 neighborhood
    ABGR8 A = src(srcX - 1, srcY - 1), B = src(srcX, srcY - 1), C = src(srcX + 1, srcY - 1);
    ABGR8 D = src(srcX - 1, srcY + 0), E = src(srcX, srcY + 0), F = src(srcX + 1, srcY + 0);
    ABGR8 G = src(srcX - 1, srcY + 1), H = src(srcX, srcY + 1), I = src(srcX + 1, srcY + 1);

    // Default output: center color (E)
    vec4 centerColor = unpackUnorm4x8(E);
    vec4 J = centerColor, K = centerColor, L = centerColor, M = centerColor;

    // Process if neighborhood is non-uniform
    if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) | (H ^ E) | (I ^ E)) != 0u) {
        // Extended sampling for pattern detection
        ABGR8 P = src(srcX, srcY - 2), S = src(srcX, srcY + 2);
        ABGR8 Q = src(srcX - 2, srcY), R = src(srcX + 2, srcY);
        ABGR8 Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);

        // Pattern-based blending rules
        bool same_B_D = same(B,D);
        bool same_B_F = same(B,F);
        bool same_H_D = same(H,D);
        bool same_H_F = same(H,F);
        bool same_E_A = same(E,A);
        bool same_E_C = same(E,C);
        bool same_E_G = same(E,G);
        bool same_E_I = same(E,I);

        // Rule 1: Corner blending (X-pattern)
   if ( (same_B_D && none_eq2(D,H,F) && none_eq2(B,H,F))  &&  ((El>=Dl&&El>=Bl) || fullsame(E,A))  &&  (same_E_A||same_E_C||same_E_G)  &&  ((El<Dl&&El<Bl) || none_eq2(A,D,B) || notsame(E,P) || notsame(E,Q)) ) J=admixX(E,D,B,A,C,F,I,H,G,P,Q);
   if ( (same_B_F && none_eq2(B,D,H) && none_eq2(F,D,H))  &&  ((El>=Bl&&El>=Fl) || fullsame(E,C))  &&  (same_E_A||same_E_C||same_E_I)  &&  ((El<Bl&&El<Fl) || none_eq2(C,B,F) || notsame(E,P) || notsame(E,R)) ) K=admixX(E,F,B,C,A,D,G,H,I,P,R);
   if ( (same_H_D && none_eq2(H,F,B) && none_eq2(D,F,B))  &&  ((El>=Hl&&El>=Dl) || fullsame(E,G))  &&  (same_E_A||same_E_G||same_E_I)  &&  ((El<Hl&&El<Dl) || none_eq2(G,H,D) || notsame(E,S) || notsame(E,Q)) ) L=admixX(E,D,H,G,I,F,C,B,A,S,Q);
   if ( (same_H_F && none_eq2(F,B,D) && none_eq2(H,B,D))  &&  ((El>=Fl&&El>=Hl) || fullsame(E,I))  &&  (same_E_C||same_E_G||same_E_I)  &&  ((El<Fl&&El<Hl) || none_eq2(I,H,F) || notsame(E,R) || notsame(E,S)) ) M=admixX(E,F,H,I,G,D,A,B,C,S,R);


        // Rule 2: K-pattern (Scorpion tail)
   if (notsame(E,F) && same_E_C&&same_E_I&&fullsame(E,D)&&fullsame(D,Q) && fullsame(F,B)&&fullsame(B,H) && notsame(F,src(srcX+3,srcY)) ) {K=admixK(F,K); M=admixK(F,M);}
   if (notsame(E,D) && same_E_A&&same_E_G&&fullsame(E,F)&&fullsame(F,R) && fullsame(D,B)&&fullsame(B,H) && notsame(D,src(srcX-3,srcY)) ) {J=admixK(D,J); L=admixK(D,L);}
   if (notsame(E,H) && same_E_G&&same_E_I&&fullsame(E,B)&&fullsame(B,P) && fullsame(H,D)&&fullsame(D,F) && notsame(H,src(srcX,srcY+3)) ) {L=admixK(H,L); M=admixK(H,M);}
   if (notsame(E,B) && same_E_A&&same_E_C&&fullsame(E,H)&&fullsame(H,S) && fullsame(B,D)&&fullsame(D,F) && notsame(B,src(srcX,srcY-3)) ) {J=admixK(B,J); K=admixK(B,K);}


        // Rule 3: Cross pattern 
        if ( (Bl<El) && full_eq3(E,G,H,I) && same(E,S) && none_eq4(E,A,D,C,F) ) 
            {J=admixC(D,B,J); K=admixC(B,F,K);} // Top
        if ( (Hl<El) && full_eq3(E,A,B,C) && same(E,P) && none_eq4(E,D,G,I,F) ) 
            {L=admixC(D,H,L); M=admixC(F,H,M);} // Bottom
        if ( (Fl<El) && full_eq3(E,A,D,G) && same(E,Q) && none_eq4(E,B,C,I,H) ) 
            {K=admixC(B,F,K); M=admixC(F,H,M);} // Right
        if ( (Dl<El) && full_eq3(E,C,F,I) && same(E,R) && none_eq4(E,B,A,G,H) ) 
            {J=admixC(D,B,J); L=admixC(D,H,L);} // Left


        // L - type

   if (notsame(H,B)) {

      // E over  2:1 ◢ or -2:1 ◣ 
      if (notsame(H,A) && notsame(H,E) && notsame(H,C)) {


         //               Ⓐ B C .
         //             Q D 🄴 🅵 🆁
         //               🅶 🅷 I
         if (none_eq2(H,D,src(srcX+2,srcY-1))) {
			 // Extend the original rule: incorporate the comparison of adjacent pixels in the third step to ensure that secondary mixing does not occur.
			 if (full_eq3(H,G,F,R)) L=admixL(M,L,H);
			 
			 // Extension of the New Rule: New Gradual Edge Morphology Extraction
 				// F -> M
			 else if (!same_H_F && notsame(F,I) && all_eq3(E,C,B,D) && same(F,G) && same(R,H) && !same(E,F) && !same(E,H)) M=unpackUnorm4x8(F);
		 }


         //             . A B Ⓒ
         //             🆀 🅳 🄴 F R
         //               G 🅷 🅸
         if (none_eq2(H,F,src(srcX-2,srcY-1))) {

			 if (full_eq3(H,I,D,Q)) M=admixL(L,M,H);
				// D -> L
			 else if (!same_H_D && notsame(D,G) && all_eq3(E,A,B,F) && same(D,I) && same(Q,H) && !same(E,D) && !same(E,H)) L=unpackUnorm4x8(D);
		 }

  
      }
                        
      // E under 2:1 (◥)  -2:1 (◤) 
      if (notsame(B,I) && notsame(B,G) && notsame(B,E)) {


         //               🅰️🅱C
         //             Q D 🄴 🅵 🆁
         //               Ⓖ H I .
         if (none_eq2(B,D,src(srcX+2,srcY+1))) {

			 if (full_eq3(B,A,F,R)) J=admixL(K,J,B);
				// F -> K
			 else if (!same_B_F && notsame(C,F) && all_eq3(E,D,H,I) && same(A,F) && same(B,R) && !same(E,B) && !same(E,F)) K=unpackUnorm4x8(F);
		 }


         //               A 🅱🅲
         //             🆀 🅳 🄴 F R
         //             . G H Ⓘ 
         if (none_eq2(B,F,src(srcX-2,srcY+1))) {
			 
			 if (full_eq3(B,C,D,Q)) K=admixL(J,K,B);
				// D -> J
			 else if (!same_B_D && notsame(A,D) && all_eq3(E,F,G,H) && same(B,Q) && same(C,D) && !same(E,B) && !same(E,D)) J=unpackUnorm4x8(D);
		 }
	  }
	
   }

   if (notsame(F,D)) {



      if (notsame(D,I) && notsame(D,E) && notsame(D,C)) {

         //               🅰B Ⓒ
         //             Q 🅳 🄴 F R
         //               G 🅷 I
         //                 🆂 .
         if (none_eq2(D,B,src(srcX+1,srcY+2))) {
			 
			 if (full_eq3(D,A,H,S)) J=admixL(L,J,D);
			 		 // H -> L
			 else if (!same_H_D && notsame(G,H) && all_eq3(E,B,F,I) && same(A,H) && same(D,S) && !same(E,D) && !same(E,H)) L=unpackUnorm4x8(H);
		 }

         //                 🅿.
         //               A 🅱C
         //             Q 🅳 🄴 F R
         //               🅶 H Ⓘ
         if (none_eq2(D,H,src(srcX+1,srcY-2))) {
			 
			 if (full_eq3(D,G,B,P)) L=admixL(J,L,D);
			 		 // B -> J
             else if (!same_B_D && notsame(B,A) && all_eq3(E,C,F,H) && same(P,D) && same(B,G) && !same(E,B) && !same(E,D)) J=unpackUnorm4x8(B);
         }

	  }


      if (notsame(F,E) && notsame(F,A) && notsame(F,G)) {
   
         //               Ⓐ B 🅲   
         //             Q D 🄴 🅵 R 
         //               G 🅷 I   
         //               . 🆂   
         if (none_eq2(F,B,src(srcX-1,srcY+2))) {
			 
			 if (full_eq3(F,C,H,S)) K=admixL(M,K,F);
			 		 // H -> M
             else if (!same_H_F && notsame(H,I) && all_eq3(E,B,D,G) && same(C,H) && same(F,S) && !same(E,F) && !same(E,H)) M=unpackUnorm4x8(H);
		 }

         //               . 🅿
         //               A 🅱C
         //             Q D 🄴 🅵 R
         //               Ⓖ H 🅸
         if (none_eq2(F,H,src(srcX-1,srcY-2))) {
			 
			 if (full_eq3(F,I,B,P)) M=admixL(K,M,F);
			 		 // B -> K
             else if (!same_B_F && notsame(B,C) && all_eq3(E,A,D,H) && same(P,F) && same(B,I) && !same(E,B) && !same(E,F)) K=unpackUnorm4x8(B);
		 }

      }
   } // F !== D
 } // not constant

    // Write four pixels at once.
    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
}
