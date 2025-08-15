/* MMPX.glc
   Copyright 2020 Morgan McGuire & Mara Gagiu.
   Provided under the Open Source MIT license https://opensource.org/licenses/MIT

   by Morgan McGuire and Mara Gagiu.
   2025 Enhanced by CrashGG.
*/

// Performs 2x upscaling

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


// This luma is only for brightness decision mechanism. Each alpha point multiplies the final output
//
uint luma(ABGR8 C) {
    uint alpha = (C & 0xFF000000u) >> 24;
    return (((C & 0x00FF0000u) >> 16) + ((C & 0x0000FF00u) >> 8) + (C & 0x000000FFu) + 1u) * (256u - alpha);
}
//
// Modified: Linear brightness decision mechanism
/*
uint luma(ABGR8 C) {
    uint alpha = (C >> 24) & 0xFFu;  // Simplified bit operations
    if (alpha == 0u) return 1530u; // Ensure fully transparent pixels return max value 1530
    uint rgbSum = ((C >> 16) & 0xFFu) + ((C >> 8) & 0xFFu) + (C & 0xFFu);
    float factor = 1.0f + (255.0f - alpha) * 0.00392157f; // Multiplication equivalent to division by 255
    return uint(rgbSum * factor);
}
*/
uvec4 extractPIX(ABGR8 color) {
    uint r = (color >> 0) & 0xFFu;
    uint g = (color >> 8) & 0xFFu;
    uint b = (color >> 16) & 0xFFu;
    uint a = (color >> 24) & 0xFFu;
    return uvec4(r, g, b, a);
}

// RGB approximate equality (RGB Euclidean distance ≈0.00932276 after golden ratio^3), alpha difference within 14.59% (golden ratio^2)

bool same(ABGR8 B, ABGR8 A0) {
    uvec4 b_pix = extractPIX(B);
    uvec4 a0_pix = extractPIX(A0);
    
    // Calculate sum of squared RGB differences
    ivec3 diff = ivec3(b_pix.rgb) - ivec3(a0_pix.rgb);
    
    // Calculate alpha difference (0-255 range)
    uint alphaDiff = abs(int(b_pix.a) - int(a0_pix.a));
    
    // 14.59%≈37.2
    bool alphaDiffCheck = alphaDiff < 38u;
    
    return dot(diff, diff) < 606u && alphaDiffCheck;
}

// Checks exact RGB equality (morphology), ignores alpha
bool fullsame(ABGR8 B, ABGR8 A0){

    return B == A0; //exact RGB match
}

// Full difference including alpha channel
bool notsame(ABGR8 B, ABGR8 A0){
    return (B!=A0);
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
 vec4 testcolor = vec4(1.0, 0.0, 1.0, 1.0);  // Magenta (opaque)
 vec4 testcolor2 = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan (opaque)

float LumaFactor(vec4 col1, vec4 col2) {
    // Calculate brightness difference using RGB average (0-1 range)
    float LumaDiff = abs((col1.r + col1.g + col1.b) - (col2.r + col2.g + col2.b))*0.3333333;
    // Apply golden ratio scaling (0.618 - 1.618)
    float LumaGolden = (1.618034 - LumaDiff) * (1.618034 - LumaDiff); // Squared for dot() operations
    return LumaGolden;
}


vec4 admixC(ABGR8 X1, ABGR8 X2, vec4 rgbaE) {
    // Unpack ABGR8 to RGBA float vector (0.0-1.0 range)
    vec4 rgbaX1 = unpackUnorm4x8(X1);
    vec4 rgbaX2 = unpackUnorm4x8(X2);
    
    // Extract RGB components
    vec3 rgbX1 = rgbaX1.rgb;
    vec3 rgbX2 = rgbaX2.rgb;
    vec3 rgbE = rgbaE.rgb;
    
    // Return early if both sides are dark (RGB magnitude <0.05)
    if (dot(rgbX1, rgbX1) < 0.05 && dot(rgbX2, rgbX2) < 0.05) return rgbaE;

    // Return early if both sides are fully transparent
    if (rgbaX1.a < 0.01 && rgbaX2.a < 0.01) return rgbaE;

    // Calculate squared Euclidean distance to reference color
    float rgbDistX1 = dot(rgbX1 - rgbE, rgbX1 - rgbE);
    float rgbDistX2 = dot(rgbX2 - rgbE, rgbX2 - rgbE);
    
    // Apply brightness factor adjustment
    float rgb_lumaX1 = rgbDistX1 * LumaFactor(rgbaX1, rgbaE);
    float rgb_lumaX2 = rgbDistX2 * LumaFactor(rgbaX2, rgbaE);
    
    // Select closer reference color
    bool useX1 = rgb_lumaX1 < rgb_lumaX2;
    float rgb_luma = useX1 ? rgb_lumaX1 : rgb_lumaX2;
    vec4 rgbaX = useX1 ? rgbaX1 : rgbaX2;

    // Avoid copying X directly if center is transparent (could cause artifacts)
    if (rgbaE.a < 0.01) return rgbaE;
    
    // Blend or retain original color
    return (rgb_luma < 0.75) ? mix(rgbaX, rgbaE, 0.5) : rgbaE;
}

vec4 admixK(ABGR8 X, vec4 rgbaE) {
    // Unpack ABGR8 to RGBA float vector (0.0-1.0 range)
    vec4 rgbaX = unpackUnorm4x8(X);

    // Return E if X is transparent
    if (rgbaX.a < 0.01) return rgbaE;

    // Return X if E is transparent
    if (rgbaE.a < 0.01) return rgbaX;
    
    // Extract RGB components
    vec3 rgbX = rgbaX.rgb;
    vec3 rgbE = rgbaE.rgb;

    // Calculate weighted squared distance with brightness factor
    float rgbDist = dot(rgbX - rgbE, rgbX - rgbE);
    float rgb_luma = rgbDist * LumaFactor(rgbaX, rgbaE);
    
    // Blend based on threshold
    return (rgb_luma < 0.75) ? mix(rgbaX, rgbaE, 0.381966) : rgbaE;
}

vec4 admixL(vec4 rgbaX, vec4 rgbaE) {
    // If E is transparent, return X
    if (rgbaE.a < 0.01) return rgbaX;
 
    // If X is transparent, return E
    if (rgbaX.a < 0.01) return rgbaE;
     
    // Extract RGB components
    vec3 rgbX = rgbaX.rgb;
    vec3 rgbE = rgbaE.rgb;
    
    // Calculate squared RGB distance
    float rgbDist = dot(rgbX - rgbE, rgbX - rgbE);
    
    // Check for nearly identical pixels
    if (rgbDist < 0.00136041) return rgbaE;

    // Apply brightness factor adjustment
    float rgb_luma = rgbDist * LumaFactor(rgbaX, rgbaE);
    
    // Blend result
    return (rgb_luma < 0.75) ? mix(rgbaX, rgbaE, 0.381966) : rgbaX;
}

/* Main corner logic with X-cross detection
                                                   P
                ┌───┬───┬───┐                ┌───┬───┬───┐
                │ A │ B │ C │                │ A │ B2│ 1 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ D │ E │ F │   => L      Q  │ B1│ E │ 2 │
                ├───┼───┼───┤                ├───┼───┼───┤
                │ G │ H │ I │                │ 5 │ 4 │ 3 │
                └───┴───┴───┘                └───┴───┴───┘
    Euclidean Formula: √(ΔR² + ΔG² + ΔB²)
      Black↔White: 1.732, Black↔RGB: 1.0, White↔R↔G↔B: 1.414

    Squared Distance: dot(Δ,Δ) avoids sqrt (10x cheaper)
      Black↔White: 3.0, Black↔RGB: 1.0, White↔R↔G↔B: 2.0
    
    Golden Ratio Thresholds:
      Euclidean: 0.382    0.5      0.618    0.382²
      Squared:  0.4377    0.75     1.1459   0.06386

*/
vec4 admixX(ABGR8 LE, ABGR8 LB1, ABGR8 LB2, ABGR8 LA, ABGR8 L1, ABGR8 L2, ABGR8 L3, ABGR8 L4, ABGR8 L5, ABGR8 LP, ABGR8 LQ) {
    // First check using approximate pixels to prevent false positives on gradient lines
    vec4 rgbaLE = unpackUnorm4x8(LE); 
    if (fullsame(LE,LB1)||fullsame(LE,LB2)) return rgbaLE;
 
    // Unpack neighbor colors
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
    
    // Define LB blend candidate. Handle transparency edge cases

    vec4 rgbaLB = mix(rgbaLB1, rgbaLB2, 0.5);
    if (rgbaLB1.a < 0.01 ) rgbaLB=rgbaLB2;
    if (rgbaLB2.a < 0.01 ) rgbaLB=rgbaLB1;
    // If the RGB difference between the two LBs is large, the alpha of the side with less alpha can be used after mixing to reduce burrs
    if (rgbaLB1.a < rgbaLB.a ) rgbaLB.a=rgbaLB1.a; else rgbaLB.a=rgbaLB2.a;

    // Calculate squared RGB distance and brightness adjustment
    float rgbaDist = dot(rgbaLB - rgbaLE, rgbaLB - rgbaLE);
    float rgb_luma = rgbaDist * LumaFactor(rgbaLB, rgbaLE);
    float alphaDiff = abs((rgbaLB1.a + rgbaLB2.a) - rgbaLE.a * 2 );
    bool LBLEalpha0 = rgbaLB.a < 0.01 || rgbaLE.a < 0.01; // Transparency check

    // X-cross pattern detection core logic
    if (same(LE,LA)) {
        // Weak blend for very similar pixels to avoid artifacts
        if (rgbaDist < 0.06386) return mix(rgbaLE,rgbaLB,0.381966);

        // Special pattern: Tuning fork
        bool same_LB1_L4 = same(LB1,L4);
        bool same_LB1_L5 = same(LB1,L5);
        bool same_LB2_L1 = same(LB2,L1);
        bool same_LB2_L2 = same(LB2,L2);
        if ( same_LB1_L4 && same_LB2_L1 && !same_LB2_L2 || same_LB1_L5 && same_LB2_L2 && !same_LB1_L4 ) return rgbaLE;

        // Special pattern: Large block cross
        bool same_LB2_LP = same(LB2,LP);
        bool same_LB1_LQ = same(LB1,LQ);
        bool same_LE_L2 = same(LE,L2);
        bool same_LE_L4 = same(LE,L4);

    if (same_LB2_L1 && same_LB1_L5 && (same_LB2_LP && same_LE_L2 || same_LB1_LQ && same_LE_L4) ) return rgbaLE;

        // Special pattern: Diagonal cross grid
        bool same_LE_L1 = same(LE,L1);
        bool same_LE_L3 = same(LE,L3);
        bool same_LE_L5 = same(LE,L5);

	// A point
        if (same_LE_L1 && same_LE_L3 && same_LE_L5 && !same_LE_L2 && !same_LE_L4) return rgbaLE;
    bool same_LB1_L3 = same(LB1,L3);
    bool same_LB2_L3 = same(LB2,L3);

	// B point
    if ( same_LB2_L2 && same_LB1_L4 && !same_LB2_L1 && !same_LB1_L5 && !same_LB1_L3&& !same_LB2_L3 ) return rgbaLE;

        // Scoring system for cross patterns
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
    if ( score1<1) {
		if (same_LE_L2 && same_LB2_L1 || same_LE_L4 && same_LB1_L5 ) return rgbaLE;
	}
        score1 += int(same_LB2_L2);
        score1 += int(same_LB1_L4);  
        
        // Straight pattern scoring
        score2 += int(fullsame(LP, LB2) && fullsame(L5, LB1));
        score2 += int(fullsame(LQ, LB1) && fullsame(L1, LB2));

        // Penalize large brightness differences
        float LumaDiff = abs((rgbaLE.r + rgbaLE.g + rgbaLE.b + rgbaLE.a) - (rgbaLB.r + rgbaLB.g + rgbaLB.b + rgbaLB.a))*0.3333333;
        if (LumaDiff > 0.8541 ) scoreBonus -= 1;
        if (alphaDiff > 1.0) scoreBonus -= 1; // Points are deducted when the cross-pixel alpha difference exceeds half
        score1 += scoreBonus;
        score2 += scoreBonus;

        // Blend based on scores and thresholds
        if (rgb_luma < 0.75 && (score1 >= 1 || score2 >= 1)) return (LBLEalpha0) ? rgbaLE : mix(rgbaLE, rgbaLB,0.5);
        if (rgb_luma < 1.1459 && (score1 == 1 && score2 >= 1)) return (LBLEalpha0) ? rgbaLE : mix(rgbaLE, rgbaLB,0.381966);
        if (score1 < 2 && score2 >= 1) return (LBLEalpha0) ? rgbaLE : mix(rgbaLE, rgbaLB,0.145898);

        // Final decision for strong patterns
        return (score1 >= 2) ? rgbaLB : rgbaLE;
    }

    // Non-cross patterns
    if (LBLEalpha0) return rgbaLB;
    if (fullsame(LB1,LB2)) return (rgb_luma < 0.75) ? mix(rgbaLB, rgbaLE,0.381966) : rgbaLB;
    return (rgb_luma < 0.75) ? mix(rgbaLB, rgbaLE,0.5) : rgbaLB;
}


void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

    // Sample 3x3 neighborhood
    ABGR8 A = src(srcX - 1, srcY - 1), B = src(srcX, srcY - 1), C = src(srcX + 1, srcY - 1);
    ABGR8 D = src(srcX - 1, srcY + 0), E = src(srcX, srcY + 0), F = src(srcX + 1, srcY + 0);
    ABGR8 G = src(srcX - 1, srcY + 1), H = src(srcX, srcY + 1), I = src(srcX + 1, srcY + 1);

    // Default output pixels (center color)
    vec4 centerColor = unpackUnorm4x8(E);
    vec4 J = centerColor, K = centerColor, L = centerColor, M = centerColor;

    // Only process if neighborhood isn't uniform
    if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) | (H ^ E) | (I ^ E)) != 0u) {
        // Extended sampling for pattern detection
        ABGR8 P = src(srcX, srcY - 2), S = src(srcX, srcY + 2);
        ABGR8 Q = src(srcX - 2, srcY), R = src(srcX + 2, srcY);
        ABGR8 Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);

        // Main scaling rules (J, K, L, M calculation)
        if ((same(D,B) && none_eq2(D,H,F) && none_eq2(B,H,F))  &&  ((El>=Dl&&El>=Bl) || fullsame(E,A))  &&  any_eq3(E,A,C,G)  &&  ((El<Dl&&El<Bl) || none_eq2(A,D,B) || notsame(E,P) || notsame(E,Q)) ) J=admixX(E,D,B,A,C,F,I,H,G,P,Q);
        if ((same(B,F) && none_eq2(B,D,H) && none_eq2(F,D,H))  &&  ((El>=Bl&&El>=Fl) || fullsame(E,C))  &&  any_eq3(E,A,C,I)  &&  ((El<Bl&&El<Fl) || none_eq2(C,B,F) || notsame(E,P) || notsame(E,R)) ) K=admixX(E,F,B,C,A,D,G,H,I,P,R);
        if ((same(H,D) && none_eq2(H,F,B) && none_eq2(D,F,B))  &&  ((El>=Hl&&El>=Dl) || fullsame(E,G))  &&  any_eq3(E,A,G,I)  &&  ((El<Hl&&El<Dl) || none_eq2(G,H,D) || notsame(E,S) || notsame(E,Q)) ) L=admixX(E,D,H,G,I,F,C,B,A,S,Q);
        if ((same(F,H) && none_eq2(F,B,D) && none_eq2(H,B,D))  &&  ((El>=Fl&&El>=Hl) || fullsame(E,I))  &&  any_eq3(E,C,G,I)  &&  ((El<Fl&&El<Hl) || none_eq2(I,H,F) || notsame(E,R) || notsame(E,S)) ) M=admixX(E,F,H,I,G,D,A,B,C,S,R);

        // Scorpion tail pattern (handles thin diagonal features)
        if ((notsame(E,F) && all_eq2(E,C,I)&&fullsame(E,D)&&fullsame(D,Q) && fullsame(F,B)&&fullsame(B,H)) && notsame(F,src(srcX+3,srcY))) {K=admixK(F,K); M=admixK(F,M);}
        if ((notsame(E,D) && all_eq2(E,A,G)&&fullsame(E,F)&&fullsame(F,R) && fullsame(D,B)&&fullsame(B,H)) && notsame(D,src(srcX-3,srcY))) {J=admixK(D,J); L=admixK(D,L);}
        if ((notsame(E,H) && all_eq2(E,G,I)&&fullsame(E,B)&&fullsame(B,P) && fullsame(H,D)&&fullsame(D,F)) && notsame(H,src(srcX,srcY+3))) {L=admixK(H,L); M=admixK(H,M);}
        if ((notsame(E,B) && all_eq2(E,A,C)&&fullsame(E,H)&&fullsame(H,S) && fullsame(B,D)&&fullsame(D,F)) && notsame(B,src(srcX,srcY-3))) {J=admixK(B,J); K=admixK(B,K);}

        // Anti-aliasing for cross patterns (smooths jagged edges)
        if ( (Bl<El) && full_eq3(E,G,H,I) && same(E,S) && none_eq4(E,A,D,C,F) ) {J=admixC(D,B,J); K=admixC(B,F,K);} // Top
        if ( (Hl<El) && full_eq3(E,A,B,C) && same(E,P) && none_eq4(E,D,G,I,F) ) {L=admixC(D,H,L); M=admixC(F,H,M);} // Bottom
        if ( (Fl<El) && full_eq3(E,A,D,G) && same(E,Q) && none_eq4(E,B,C,I,H) ) {K=admixC(B,F,K); M=admixC(F,H,M);} // Right
        if ( (Dl<El) && full_eq3(E,C,F,I) && same(E,R) && none_eq4(E,B,A,G,H) ) {J=admixC(D,B,J); L=admixC(D,H,L);} // Left

        // L-shaped slope handling (2:1 slopes)
        if (notsame(H,B)) {

      if (notsame(H,A) && notsame(H,E) && notsame(H,C)) {

                       //   Ⓐ B C .
                       // Q D 🄴 🅵 🆁
                       //   🅶 🅷 I
         if (full_eq3(H,G,F,R) && none_eq2(H,D,src(srcX+2,srcY-1))) L=admixL(M,L);

                       // . A B Ⓒ
                       // 🆀 🅳 🄴 F R
                       //   G 🅷 🅸
         if (full_eq3(H,I,D,Q) && none_eq2(H,F,src(srcX-2,srcY-1))) M=admixL(L,M);
      }
                        
      if (notsame(B,I) && notsame(B,G) && notsame(B,E)) {

                       //   🅰️ 🅱 C
                       // Q D 🄴 🅵 🆁
                       //   Ⓖ H I .
         if (full_eq3(B,A,F,R) && none_eq2(B,D,src(srcX+2,srcY+1))) J=admixL(K,J);

                       //   A 🅱 🅲
                       // 🆀 🅳 🄴 F R
                       // . G H Ⓘ 
         if (full_eq3(B,C,D,Q) && none_eq2(B,F,src(srcX-2,srcY+1))) K=admixL(J,K);
      }
   }

   if (notsame(F,D)) {

      if (notsame(D,I) && notsame(D,E) && notsame(D,C)) {

                       //   🅰B Ⓒ
                       // Q 🅳 🄴 F R
                       //   G 🅷 I
                       //     🆂 .
         if (full_eq3(D,A,H,S) && none_eq2(D,B,src(srcX+1,srcY+2))) J=admixL(L,J);

                       //     🅿 .
                       //   A 🅱 C
                       // Q 🅳 🄴 F R
                       //   🅶 H Ⓘ
         if (full_eq3(D,G,B,P) && none_eq2(D,H,src(srcX+1,srcY-2))) L=admixL(J,L);
      }


      if (notsame(F,E) && notsame(F,A) && notsame(F,G)) {
   
                       //   Ⓐ B 🅲   
                       // Q D 🄴 🅵 R 
                       //   G 🅷 I   
                       //   . 🆂   
         if (full_eq3(F,C,H,S) && none_eq2(F,B,src(srcX-1,srcY+2))) K=admixL(M,K);

                       //   . 🅿
                       //   A 🅱 C
                       // Q D 🄴 🅵 R
                       //   Ⓖ H 🅸
         if (full_eq3(F,I,B,P) && none_eq2(F,H,src(srcX-1,srcY-2))) M=admixL(K,M);
      }
   } // F !== D
 } // not constant
    // Write 2x2 upscaled pixels
    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
}
