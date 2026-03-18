/* MMPX.glc
   Copyright 2020 Morgan McGuire & Mara Gagiu.
   Provided under the Open Source MIT license https://opensource.org/licenses/MIT

   by Morgan McGuire and Mara Gagiu.
   2025 Enhanced by CrashGG.
*/

// Performs 2x upscaling.

/* If we took an image as input, we could use a sampler to do the clamping. But we decode
   low-bpp texture data directly, so...

   use readColoru to read uint format color data from texture/framebuffer.
   in ppsspp ,readColorf(p) eq unpackUnorm4x8(readColoru(p)),It is not actually read directly.
*/

uint src(int x, int y) {

	// Old clamp method: always return center pixel when coordinates are out of bounds
    //return readColoru(uvec2(clamp(x, 0, params.width - 1), clamp(y, 0, params.height - 1)));

	// Better out-of-bounds check: test if coordinates are valid first. Return transparent if out of bounds.
    return (x >= 0 && x < params.width && y >= 0 && y < params.height) 
           ? readColoru(uvec2(x, y)) 
           : 0u;
}


// RGB perceptual weight + alpha tiered segmentation
float luma(vec4 col) {

	// Use CRT-era BT.601 standard. Result divided by 10, range [0.0 - 0.1]
    float rgbsum =dot(col.rgb, vec3(0.0299, 0.0587, 0.0114));

	// Remove alpha weighting by taking fractional part and *10 later
    float alphafactor = 
        (col.a > 0.854102) ? 1.0 :		// Upper segment of two short golden ratio splits
        (col.a > 0.618034) ? 2.0 :		// One golden ratio split
        (col.a > 0.381966) ? 3.0 :		// One short golden ratio split
        (col.a > 0.145898) ? 4.0 :		// Two short golden ratio splits
        (col.a > 0.002) ? 5.0 : 8.0;	// Fully transparent

    return rgbsum + alphafactor;

}

/* Constant definitions:
0.145898	:			Two short golden ratio splits of 1.0
0.0638587	:		Squared Euclidean RGB distance after two short golden ratio splits
0.024391856	:		Squared Euclidean RGB distance after two short + one golden ratio split
0.00931686	:		Squared Euclidean RGB distance after three short golden ratio splits
0.001359312	:		Squared Euclidean RGB distance after four short golden ratio splits
0.4377		:		Squared Euclidean RGB distance after one short golden ratio split
0.75			:		Squared half Euclidean RGB distance
*/
// Pixel similarity check - Level 1
// pin zz
bool sim1(vec4 col1, vec4 col2) {

    vec4 diff = col1 - col2;
    vec4 absdiff = abs(diff);

    // 1. Fast component difference check
    if ( absdiff.r > 0.1 || absdiff.g > 0.1 || absdiff.b > 0.1 || absdiff.a > 0.145898 ) return false;

    // 2. Fast squared distance check
	float dot_diff = dot(diff.rgb, diff.rgb);
    if (dot_diff < 0.001359312) return true;

    // 3. Gradient pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.096 ) return false;    // Reject if difference exceeds int24
    if ( max_diff-min_diff<0.024 && dot_diff<0.024391856)  return true;  // Treat as gradient if difference <= int6, wider threshold

	// 4. Grayscale pixel check
    float sum1 = dot(col1.rgb, vec3(1.0));  // Sum RGB channels
    float sum2 = dot(col2.rgb, vec3(1.0));
    float avg1 = sum1 * 0.3333333;
    float avg2 = sum2 * 0.3333333;

	vec3 graydiff1 = col1.rgb - vec3(avg1);
	vec3 graydiff2 = col2.rgb - vec3(avg2);
	float dotgray1 = dot(graydiff1,graydiff1);
	float dotgray2 = dot(graydiff2,graydiff2);
    // 0.002: Allow single-channel diff up to int13 when avg=20. 0.0004: Allow single-channel diff up to int6, dual-channel int3+4
	float tolerance1 = avg1<0.08 ? 0.002 : 0.0004;
	float tolerance2 = avg2<0.08 ? 0.002 : 0.0004;
    // 0.078: Max green channel = 19, perceptible limit to human eye
    bool Col1isGray = sum1<0.078||dotgray1<tolerance1;
    bool Col2isGray = sum2<0.078||dotgray2<tolerance2;

	// If both are grayscale, relax threshold to Level 2
    if ( Col1isGray && Col2isGray && dot_diff<0.024391856 ) return true;

	// Reject if only one is grayscale
    if ( Col1isGray != Col2isGray ) return false;

    // Accumulate positive/negative values using max/min
    float team_pos = abs(dot(max(diff.rgb, 0.0), vec3(1.0)));
    float team_neg = abs(dot(min(diff.rgb, 0.0), vec3(1.0)));
    // Find opposing channel
    float team_rebel = min(team_pos, team_neg);
    // Empirically requires at least 3x opposing channel value (1x neutralizes, 2x creates upward trend)
    return dot_diff + team_rebel*team_rebel*3.0 < 0.00931686;

}

float rgbDist(vec3 col1, vec3 col2) {

    // 1. Clamp dark regions
    vec3 clampCol1 = max(col1, vec3(0.078));
    vec3 clampCol2 = max(col2, vec3(0.078));

    vec3 clampdiff = clampCol1 - clampCol2;

    return dot(clampdiff, clampdiff);
}

// Pixel similarity check - Level 2 / Level 3
bool vi_sim2(vec4 colC1, uint C1, uint C2) {
    if (C1==C2) return true;
	vec4 colC2 = unpackUnorm4x8(C2);
	// Ignore RGB if both pixels are near-transparent
	if ( colC1.a < 0.381966 && colC2.a < 0.381966 ) return true;
	// Alpha difference must not exceed 0.382
	if ( abs(colC1.a-colC2.a)>0.381966) return false;

    return rgbDist(colC1.rgb, colC2.rgb) < 0.024391856;
}

bool sim2(vec4 col1, vec4 col2) {
	// Ignore RGB if both pixels are near-transparent
	if ( col1.a < 0.381966 && col2.a < 0.381966 ) return true;
	// Alpha difference must not exceed 0.382
	if ( abs(col1.a-col2.a)>0.381966) return false;
    return rgbDist(col1.rgb, col2.rgb) < 0.024391856;
}

bool sim3(vec4 col1, vec4 col2) {
	// Ignore RGB if both pixels are near-transparent
	if ( col1.a < 0.145898 && col2.a < 0.145898 ) return true;
	// Alpha difference must not exceed 0.382
	if ( abs(col1.a-col2.a)>0.381966) return false;
    return rgbDist(col1.rgb, col2.rgb) < 0.0638587;
}

bool mixcheck(vec4 col1, vec4 col2) {

	// Transparent pixels filtered out externally
	vec4 diff = col1 - col2;
	// Reject mixing if alpha difference exceeds half range
	if ( abs(diff.a) > 0.5 ) return false;

    // Gradient pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.618034 ) return false;

	float dot_diff = dot(diff.rgb, diff.rgb);
    if( max_diff-min_diff<0.024 && dot_diff<0.75)  return true;  // 0.020 <= int5, 0.024 <= int6

    // Accumulate positive/negative values using max/min
    float team_pos = abs(dot(max(diff.rgb, 0.0), vec3(1.0)));
    float team_neg = abs(dot(min(diff.rgb, 0.0), vec3(1.0)));
    // Add opposing channel to squared distance before final check
    float team_rebel = min(team_pos, team_neg);
    // Empirically requires at least 3x opposing channel value (1x neutralizes, 2x creates upward trend)
    return dot_diff + team_rebel*team_rebel*3.0 < 0.4377;
}

// RGB must match, minor alpha differences allowed
bool eq(uint C1, uint C2){
    if (C1 == C2) return true;

	uint rgbC1 = C1 & 0x00FFFFFFu;
	uint rgbC2 = C2 & 0x00FFFFFFu;

	if (rgbC1 != rgbC2) return false;

    uint alphaC1 = C1 >> 24;
    uint alphaC2 = C2 >> 24;

    // Note: uint cannot use abs(alphaC1-alphaC2)!
    uint alphaDiff = (alphaC1 > alphaC2) ? (alphaC1 - alphaC2) : (alphaC2 - alphaC1);

    return alphaDiff < 38u;	// Two short golden ratio splits of 255u

}

#define noteq(a,b) (a!=b)

bool vec_noteq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);
	// Allow total RGB diff <= int2, alpha diff <= int5
    return dot(diff.rgb, vec3(1.0)) > 0.008 || diff.a > 0.021286;
}

bool all_eq2(uint B, uint A0, uint A1) {
    return (eq(B,A0) && eq(B,A1));
}

bool any_eq2(uint B, uint A0, uint A1) {
   return (eq(B,A0) || eq(B,A1));
}

bool any_eq3(uint B, uint A0, uint A1, uint A2) {
   return (eq(B,A0) || eq(B,A1) || eq(B,A2));
}

bool none_eq2(uint B, uint A0, uint A1) {
   return (noteq(B,A0) && noteq(B,A1));
}

// Pre-define
//#define testcolor vec4(1.0, 0.0, 1.0, 1.0)  // Magenta
//#define testcolor2 vec4(0.0, 1.0, 1.0, 1.0)  // Cyan
//#define testcolor3 vec4(1.0, 1.0, 0.0, 1.0)  // Yellow
//#define testcolor4 vec4(1.0, 1.0, 1.0, 1.0)  // White

#define slopeBAD vec4(2.0)
#define theEXIT vec4(4.0)
#define slopOFF vec4(8.0)
#define Mix382 mix(colX,colE,0.381966)
#define Mix618 mix(colX,colE,0.618034)
#define Mix854 mix(colX,colE,0.8541)
#define Mix382off Mix382+slopOFF
#define Mix618off Mix618+slopOFF
#define Mix854off Mix854+slopOFF
#define Xoff colX+slopOFF
#define checkblack(col) ((col).g < 0.078 && (col).r < 0.1 && (col).b < 0.1)
#define diffEB abs(El-Bl)
#define diffED abs(El-Dl)
#define writeJKLM writeColorf(destXY,J);writeColorf(destXY+ivec2(1,0),K);writeColorf(destXY+ivec2(0,1),L);writeColorf(destXY+ivec2(1,1),M)

//pin zz
// Concave + Cross pattern - Weak blending (weak/none)
vec4 admixC(vec4 colX, vec4 colE) {
	// Transparent pixels filtered out in main logic

	bool mixok = mixcheck(colX, colE);

	return mixok ? Mix618 : colE;

}

// K-pattern - Forced weak blending (weaker)
vec4 admixK(vec4 colX, vec4 colE) {
	// Transparent pixels filtered out in main logic

	bool mixok = mixcheck(colX, colE);

	return mixok ? Mix618 : Mix854;

}

// L-pattern - 2:1 slope - Main corner extension
// Practical rule: This requires 4 pixels on strict slope to be identical, otherwise severe aliasing occurs!
vec4 admixL(vec4 colX, vec4 colE, vec4 colS) {

    // Original eq(X,E) check caught many duplicate pixels; now filtered by slopeok in main logic

	// Copy directly if target X differs from sample S (already blended once), no re-blend
	if (vec_noteq(colX, colS)) return colX;

	bool mixok = mixcheck(colX, colE);

    return mixok ? Mix382 : colX;
}

/**************************************************************************************************************************************
 * 												main slope + X cross-processing mechanism						                *
 ******************************************************************************************************************************** zz  */
vec4 admixX(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I,
			uint P, uint PA, uint PC, uint Q, uint QA, uint QG, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint AA, uint CC, uint GG,
			float El, float Bl, float Dl, float Fl, float Hl,
			vec4 colE, vec4 colB, vec4 colD, vec4 colC, vec4 colG,
			bool eq_B_D, bool eq_F_H, bool eq_E_A, bool eq_E_C, bool eq_E_G, bool eq_E_I, bool eq_E_F, bool eq_E_H) {


	bool eq_B_C = eq(B, C);
	bool eq_D_G = eq(D, G);

    // Exit if enclosed by dual straight walls
    if (eq_B_C && eq_D_G) return slopeBAD;

	// Pre-declare variables
	bool eq_A_B;		bool eq_A_D;		bool eq_A_P;		bool eq_A_Q;
	bool eq_B_P;		bool eq_B_PA;	bool eq_B_PC;
	bool eq_D_Q;		bool eq_D_QA;	bool eq_D_QG;
	bool B_slope;	bool B_tower;	bool B_wall;
    bool D_slope;	bool D_tower;	bool D_wall;
	vec4 colX;		bool Xisblack;
    bool mixok;
	bool comboA3;    bool En3;
	
    #define En4square  En3 && eq_E_I

	// E is near-transparent (not fully) - essentially an edge pixel
	bool EalphaL = colE.a >0.002 && colE.a <0.381966;

	// Remove alpha channel weighting
	Bl = fract(Bl) *10.0;
	Dl = fract(Dl) *10.0;
	El = fract(El) *10.0;
	Fl = fract(Fl) *10.0;
	Hl = fract(Hl) *10.0;

/*=========================================
                    B != D
  ==================================== zz */
if (!eq_B_D){

	// Exit if E == A (violates expected logic)
	if (eq_E_A) return slopeBAD;

	// Exit if B and D differ more than either differs from center E
	float diffBD = abs(Bl-Dl);
	if (diffBD > diffEB || diffBD > diffED) return slopeBAD;

	// Prevent single-pixel font edges from being crushed by black background (luminance diff > 0.5)
	// Note: If B != D, use both black checks, not average
	Xisblack = checkblack(colB) && checkblack(colD);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

	// Exclusion rule before original logic (triangular vertices cannot protrude)
	eq_A_B = eq(A,B);
	if ( !Xisblack && eq_A_B && eq_D_G && eq(B,P) ) return slopeBAD;

	eq_A_D = eq(A,D);
	if ( !Xisblack && eq_A_D && eq_B_C && eq(D,Q) ) return slopeBAD;

    // B and D are isolated? Not applicable here (fixes some artifacts but loses shapes, e.g. Double Dragon attract mode character art)

	// X is B/D 50/50 blend
	colX = mix(colB, colD, 0.5);
	colX.a = min(colB.a, colD.a);

	mixok = E!=0u && mixcheck(colX,colE);

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	comboA3 = eq_A_P && eq_A_Q;
	// High priority: 3-in-a-row on A side
    if (comboA3) return mixok ? Mix382off : Xoff;

    // Original official rule
    if ( eq_E_C || eq_E_G ) return mixok ? Mix382off : Xoff;

    // Original rule enhancement 1 - Catches trends well, enhancement 2 disabled (wall bypass issue)
    if ( !eq_D_G&&eq(E,QG)&&sim2(colE,colG) || !eq_B_C&&eq(E,PC)&&sim2(colE,colC) ) return mixok ? Mix382off : Xoff;


    // Exclude 3-pixel single-side wall cases
    if (!Xisblack){
        if ( eq_A_B&&eq_B_C || eq_A_D&&eq_D_G ) return slopeBAD;
    }

    if (EalphaL) return mixok ? Mix382off : Xoff;

    // F-H inline trend (includes En3), blocked by 3-pixel wall rule so placed after
    if ( eq_F_H ) return mixok ? Mix382off : Xoff;

    // Abort remaining 2-pixel walls and random single pixels
    return slopeBAD;
} // B != D

/*******  B == D prepare *******/
  
	// Prevent font edges from being crushed on 3 sides by black background
	Xisblack = checkblack(colB);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

    colX = colB;
	colX.a = min(colB.a , colD.a);

	float distEC = float(!eq_E_C);
	float distEG = float(!eq_E_G);

	if (!eq_E_C) {
		if ( colE.a<0.381966 && colC.a<0.381966 ) {	// Ignore RGB if both near-transparent
			distEC = 0.0;
		} else if (abs(colE.a-colC.a)<0.381966) distEC = rgbDist(colE.rgb, colC.rgb);
	}
	if (!eq_E_G) {
		if ( colE.a<0.381966 && colG.a<0.381966 ) {	// Ignore RGB if both near-transparent
			distEC = 0.0;
		} else if (abs(colE.a-colG.a)<0.381966) distEG = rgbDist(colE.rgb, colG.rgb);
	}

	bool sim_EC = distEC < 0.024391856;
	bool sim_EG = distEG < 0.024391856;

	bool comboE3 = eq_E_C && eq_E_G;
	bool ThickBorder;


/*===============================================
                 Original main rules	sim2 enhanced
  ========================================== zz */
if ( (sim_EC || sim_EG) && !eq_E_A ){

/* Logic:
    1. Skip blending for continuous border shapes
    2. Special handling for long slopes
    3. Original rules
    4. Handle En4/En3/F-H inline patterns, remaining lines and single pixels
    5. Remove L-notch inner lines and outer lines
    6. Default fallback return
*/

	eq_A_B = eq(B,A);
	eq_B_P = eq(B,P);
    eq_B_PC = eq(B,PC);
    eq_B_PA = eq(B,PA);
	eq_D_Q = eq(D,Q);
    eq_D_QG = eq(D,QG);
    eq_D_QA = eq(D,QA);
	B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;


    // Strong shape: B/D both extend patterns
    if ( (B_slope||B_tower) && (D_slope||D_tower) && !eq_A_B) return Xoff;

	mixok = E!=0u && mixcheck(colX,colE);

	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);
	comboA3 = eq_A_P && eq_A_Q;

    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));

	if (ThickBorder && !Xisblack) mixok=false;

    // 3-in-a-row on A side
    if (comboA3) {
        if (!eq_A_B) return Xoff;	// Hollow: return strong shape
        else mixok=false;			// Solid: disable blending
    }

	// 3-in-a-row on E side
	if (comboE3) return mixok ? Mix382off : Xoff;


	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;
	D_wall = eq_D_G && !eq_D_QG && !eq_D_Q;

    // Clear long slope (non-thick border edge - strong trend!)
    // Special condition for long slopes
	if ( B_wall && D_tower ) {
        if (eq_E_G || sim_EG&&eq(E,QG) ) {   // Original + enhanced rule
            if (eq_A_B) return mixok ? Mix382 : colX;    // Has thickness
            return colX;                               // Hollow
        }
        if (eq_A_B) return slopeBAD;
        // 2-pixel with long slope continuation
        if (eq_E_F ) return colX;
        // 1-pixel without long slope
        return Xoff;
    }

	if ( B_tower && D_wall ) {
        if (eq_E_C || sim_EC&&eq(E,PC) ) {   // Original + enhanced rule
            if (eq_A_B) return mixok ? Mix382 : colX;    // Has thickness
            return colX;                                // Hollow
        }
        if (eq_A_B) return slopeBAD;
        // 2-pixel with long slope continuation
        if (eq_E_H ) return colX;
        // 1-pixel without long slope
        return Xoff;
    }


    // Official original rules (placed after special shapes - they specify no blending!)
	// Fixes for original rules
    if (eq_E_C ) {
		if (eq_A_B && eq_B_PA && !eq_B_PC && eq_E_F && eq(E,P)) return theEXIT;
		return mixok ? Mix382 : colX;
	}

	if (eq_E_G) {
		if (eq_A_B && eq_D_QA && !eq_D_QG && eq_E_H && eq(E,Q)) return theEXIT;
		return mixok ? Mix382 : colX;
	}

    // Original rule enhancement 1
    if (sim_EG&&!eq_D_G&&eq(E,QG) || sim_EC&&!eq_B_C&&eq(E,PC)) return mixok ? Mix382off : Xoff;

    // Original rule enhancement 2
    if (sim_EC && sim_EG) return mixok ? Mix382off : Xoff;


    // F-H inline trend (skip En4/En3)
    if ( eq_F_H )  return mixok ? Mix382off : Xoff;

	// Cleanup final long slopes (non-clear shapes)
    // Practical: This section cleans slopes differently than F-H - default neutral unless cube
	if (eq_B_C && eq_D_Q) {
        // Double cube: exit
		if (eq_B_P && eq_B_PC && eq_A_B && eq_D_QA && !eq_D_QG && eq_E_F && eq(H,I) ) return theEXIT;

		return mixok ? Mix382off : Xoff;
	}

	if ( eq_D_G && eq_B_P) {
        // Double cube: exit
		if (eq_D_Q && eq_D_QG && eq_A_B && eq_B_PA && !eq_B_PC && eq_E_H && eq(F,I) ) return theEXIT;

		return mixok ? Mix382off : Xoff;
	}


    // Exit for clear L-notch inner parallel line
    if (eq_A_B && !ThickBorder && !eq_E_I ) {
	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}

    // Early return if colors are similar
    if (mixok) return Mix382off;
    if (EalphaL) return mixok ? Mix382off : Xoff;

    // Exit for hollow L-corner outer line (fixes font edge artifacts)
    if ( !eq_A_B && (eq_E_F||eq_E_H) && !eq_E_I) {
        if (B_tower && !eq_D_Q && !eq_D_QG) return theEXIT;
        if (D_tower && !eq_B_P && !eq_B_PC) return theEXIT;
    }

    // Fallback handler
    return mixok ? Mix382off : Xoff;

} // sim2 base


/*===================================================
                    E - A Cross
  ============================================== zz */
if (eq_E_A) {

	// Cross checks need "region" and "trend" concepts - tighter conditions per region

    // B/D isolated exit? Not needed here!!!


    En3 = eq_E_F&&eq_E_H;

	// Special shape: Square (En4square)
	if ( En4square ) {
        if( noteq(G,H) && noteq(C,F)                      // Independent clear 4-pixel square / 6-pixel rectangle (both sides valid)
		&& (eq(H,S) == eq(I,SI) && eq(F,R) == eq(I,RI)) ) return theEXIT;
        //else return mixok ? mix(X,E,0.381966) : X;    Do NOT return directly - enter checkerboard rule (adjacent B/D may form bubbles)
    }

    // Special shape: Dithering pattern
	// Practical 1: Use !eq_E_F / !eq_E_H (not !sim) to preserve shapes
	// Practical 2: Force blending to preserve shapes (Karnov's Revenge, Muscle Bomber 2)

	bool Eisblack = checkblack(colE);
	mixok = E!=0u && mixcheck(colX,colE);

	// 1. Dithering center
    if ( comboE3 && eq_E_I && !eq_E_F && !eq_E_H ) {

		// Exit if center E is black (KOF '96 power gauge, Punisher belt) - avoid high-contrast blending
		if (Eisblack) return theEXIT;
		// Practical 1: Skip black B checks (normal logic entry)
		// Practical 2: No need for eq_F_H check (>95% chance) + gradient blending inside gauge, 0.5 fallback
		return mixok ? Mix382off : Mix618off;
	}

    eq_B_PA = eq(B,PA);
	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	comboA3 = eq_A_P && eq_A_Q;

	// 2. Dithering edge
    if ( comboA3 && eq(A,AA) && noteq(A,PA) && noteq(A,QA) )  {	
		if (Eisblack) return theEXIT;

        // Strong blend for gradient edges
		if ( !eq_B_PA && eq(PA,QA) ) return mixok ? Mix382off : Mix618off;
        // Remainder: perfect cross = dithering edge, weak blend
        // Default weak blend - no explicit gauge border handling needed
		return mixok ? Mix618off : Mix854off;
	}

	// Early return X if center fully transparent
	if (E==0u) return colX;

    eq_D_QA = eq(D,QA);
    eq_D_QG = eq(D,QG);
    eq_B_PC = eq(B,PC);

	// 3. Half-dithering (usually silhouette shading) - weak blend

	if ( comboE3 && comboA3 &&
		(eq_B_PC || eq_D_QG) && eq_D_QA && eq_B_PA) {
        return mixok ? Mix618off : Mix854off;
	}

    // 4. Quarter-dithering (prevents ugly "finger" artifacts - SF2 Guile plane, Cadillacs and Dinosaurs character select)

	if ( comboE3 && eq_A_P
		 && eq_B_PA &&eq_D_QA && eq_D_QG
		 && eq_E_H
		) return mixok ? Mix618off : Mix854off;

	if ( comboE3 && eq_A_Q
		 && eq_B_PA &&eq_D_QA && eq_B_PC
		 && eq_E_F
		) return mixok ? Mix618off : Mix854off;

    // High priority: A-side 3-in-a-row (after dithering rules)
	if (comboA3) return Xoff;

    // E-side 3-in-a-row (after comboA3)
    if (comboE3) return mixok ? Mix382off : Xoff;


    // B-D long slope components
	eq_B_P = eq(B,P);
	eq_D_Q = eq(D,Q);
	B_wall = eq_B_C && !eq_B_P;
	B_tower = eq_B_P && !eq_B_C;
	D_tower = eq_D_Q && !eq_D_G;
	D_wall = eq_D_G && !eq_D_Q;

    int scoreE = 0; int scoreB = 0; int scoreD = 0; int scoreZ = 0;


// E/B/D Zone Checkerboard Scoring Rules

//	E Zone
    if (En3) {
        scoreE += 1;
        if (B_wall || B_tower || D_tower || D_wall) scoreZ = 1;
    }

    if (eq_E_C) {
		scoreE += 1;
		scoreE += int(eq_E_F);
	}

    if (eq_E_G) {
        scoreE += 1;
		scoreE += int(eq_E_H);

    }

	if (scoreE==0) {
        // Single line
        if (eq_E_F ||eq_E_H) return theEXIT;
    }

    if ( eq_F_H ) {
		scoreE += 1;
        if ( scoreZ==0 && B_wall && (eq(F,R) || eq(G,H) || eq(F,I)) ) scoreZ = 1;
        if ( scoreZ==0 && D_wall && (eq(C,F) || eq(H,S) || eq(F,I)) ) scoreZ = 1;
    }


	#define Bn3  eq_B_P&&eq_B_C
	#define Dn3  eq_D_G&&eq_D_Q

//	B Zone
	scoreB -= int(Bn3);
	scoreB -= int(eq(C,P));
    if (scoreB < 0) scoreZ = 0;

    if (eq_B_PA) {
		scoreB -= 1;
		scoreB -= int(eq_B_P);    // Replace eq(P,PA)
	}

//        D Zone
	scoreD -= int(Dn3);
	scoreD -= int(eq(G,Q));
    if (scoreD < 0) scoreZ = 0;

    if (eq_D_QA) {
		scoreD -= 1;
		scoreD -= int(eq_D_Q);    // Replace eq(Q,QA)
	}

    int scoreFinal = scoreE + scoreB + scoreD + scoreZ ;

    if (scoreE >= 1 && scoreB >= 0 && scoreD >=0) scoreFinal += 1;

    if (scoreFinal >= 2) return colX;

    if (scoreFinal == 1) return mixok ? Mix382 : colX;

    // Final: Zero score, no B/D penalties = long slope
    if (scoreB >= 0 && scoreD >=0) {
        if (B_wall&&D_tower) return colX;
        if (B_tower&&D_wall) return colX;
    }

    return slopeBAD;

}	// eq_E_A


/*=========================================================
                   F - H / B+ D+ Extension Rules
  ==================================================== zz */

// This section is logically walled off from sim section by center E/en4square/BD
// So detection rules differ from sim side

	eq_B_P = eq(B, P);
    eq_B_PC = eq(B, PC);
	eq_D_Q = eq(D, Q);
    eq_D_QG = eq(D, QG);

    // Exit if B/D fully isolated (practical: required for this section)
    if ( !eq_B_C && !eq_B_P && !eq_B_PC && !eq_D_G && !eq_D_Q && !eq_D_QG ) return slopeBAD;

	mixok = E!=0u && mixcheck(colX,colE);

	// Exit if center E is isolated high-contrast pixel
	// Tighten threshold if E is bright
	float E_lumDiff = El > 0.92 ? 0.145898 : 0.381966;
	// Large difference from neighbors
    if ( !mixok && !eq_E_I && !EalphaL && distEC>0.0638587 && distEG>0.0638587&& abs(El-Fl)>E_lumDiff && abs(El-Hl)>E_lumDiff ) return slopeBAD;


    eq_A_B = eq(A, B);
	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);
    eq_B_PA = eq(B,PA);
    eq_D_QA = eq(D,QA);
	comboA3 = eq_A_P && eq_A_Q;
    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));

	if (ThickBorder && !Xisblack) mixok=false;

	B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;

    if (!eq_A_B) {
        // B + D + dual extension patterns
        // Practical 1: Clear shape on one side = relaxed on other
        // Practical 2: "厂" shape: flatten outer, not inner (tower allowed, wall not)
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;

        // High priority: A-side 3-in-a-row
        // Note: comboA3 only valid with !eq_A_B in this section
        if (comboA3) return Xoff;

        // 2x2 combo supplement
        if ( B_slope && eq_A_P ) return mixok ? Mix382off : Xoff;
        if ( D_slope && eq_A_Q ) return mixok ? Mix382off : Xoff;
    }

	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Clear long slope (non-solid edge - strong trend!)
    // Special condition for long slopes
	if ( B_wall && D_tower ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_F ) return colX;  // WIP: Test direct X no blend
        return mixok ? Mix382off : Xoff;
    }

	D_wall = eq_D_G && !eq_D_QG&& !eq_D_Q;

	if ( B_tower && D_wall ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_H ) return colX;
        return mixok ? Mix382off : Xoff;
    }

	// No need for hollow E processing beyond this point
	if (E==0u) return theEXIT;
	// sim3 has no fully transparent input
    bool sim_X_E = sim3(colX,colE);
    bool eq_G_H = eq(G, H);
    bool eq_C_F = eq(C, F);
    bool eq_H_S = eq(H, S);
    bool eq_F_R = eq(F, R);

    En3 = eq_E_F&&eq_E_H;

    // Wall-enclosed 4-pixel rectangle (En4square)
	if ( En4square ) {  // Square check MUST come after previous rule
		if (sim_X_E) return mixok ? Mix382off : Xoff;
        // Exit solid L-enclosure (fixes font corners & unrounded building edges - Mega Man 7)
        if ( (eq_B_C || eq_D_G) && eq_A_B ) return theEXIT;
        // Independent clear 4/6-pixel rectangle (check rectangle edges)
        if ( ( eq_B_C&&!eq_G_H || eq_D_G&&!eq_C_F || !eq_G_H&&!eq_C_F&&diffEB>0.5) && (eq_H_S == eq(I, SI) && eq_F_R == eq(I, RI)) ) return theEXIT;

        return mixok ? Mix382off : Xoff;
    }

    // Wall-enclosed triangle
 	if ( En3 ) {
		if (sim_X_E) return mixok ? Mix382off : Xoff;
        if (eq_H_S && eq_F_R) return theEXIT; // Dual extension (building edges)
       // Inner bend
        if (eq_B_C || eq_D_G) return mixok ? Mix382off : Xoff;
		// Return directly if thick (Z-snake flattens below outer bends)
        if (eq_A_B) return mixok ? Mix382off : Xoff;
        // Outer bend
        if (eq_B_P || eq_D_Q) return theEXIT;

        return mixok ? Mix382off : Xoff;
        // Empirical rules: connect inner L bends, not outer (Double Dragon Jimmy eyebrows)
	}

    // F - H
	// Rule: connect inner L bends, not outer
	if ( eq_F_H ) {
    	if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;
		// Exit solid L-enclosure (prevents dual symmetric crushing of single pixels)
		if ( eq_B_C && eq_A_B && (eq_G_H||!eq_F_R) &&eq(F, I) ) return slopeBAD;
		if ( eq_D_G && eq_A_B && (eq_C_F||!eq_H_S) &&eq(F, I) ) return slopeBAD;

		// Inner bend
        if (eq_B_C && (eq_F_R||eq_G_H||eq(F, I))) return mixok ? Mix382off : Xoff;
        if (eq_D_G && (eq_C_F||eq_H_S||eq(F, I))) return mixok ? Mix382off : Xoff;
		// Break trend: E-I F-H cross
		if (eq_E_I) return slopeBAD;
        // Z-shape outer
		if (eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
		// Outer bend unless opposite forms long L-trend
		if (eq_B_P && (eq_C_F&&eq_H_S)) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && (eq_F_R&&eq_G_H)) return mixok ? Mix382off : Xoff;

        return slopeBAD;
	}


	// Final long slope cleanup (non-clear shapes)
	// Note: Different cleanup from sim2 section - exit solid corners
	if ( eq_B_C && eq_D_Q || eq_D_G && eq_B_P) {
        // Critical: exit eq_A_B first (prevents corner pixel clipping - Eternal Secrets)
		if (eq_A_B) return theEXIT;
		return mixok ? Mix382off : Xoff;
	}

	// Final B+D dual extension catch (higher priority than L inner/outer lines)
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;


    // Exit for clear L-notch inner line (not even sim_X_E exempt)
    if (eq_A_B && !ThickBorder && !eq_E_I ) {

	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}

if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;
    // Exit for hollow L-corner outer line (connect inner, not outer)
	// Practical: Prevents font edge clipping (Cadillacs and Dinosaurs)
	if ( (B_tower || D_tower) && (eq_E_F||eq_E_H) && !eq_A_B && !eq_E_I) return theEXIT;

    // Final B/D single extension checks

    // Max slope range
    if ( B_slope && !eq_A_B && eq(PC,CC) && noteq(PC,RC)) return mixok ? Mix382off : Xoff;
    if ( D_slope && !eq_A_B && eq(QG,GG) && noteq(QG,SG)) return mixok ? Mix382off : Xoff;

    // Relaxed slope check if X/E similar (with internal constraints)
  	if ( mixok && !eq_A_B ) {
        if ( B_slope && (!eq_C_F||eq(F,RC)) ) return Mix382off;
        if ( D_slope && (!eq_G_H||eq(H,SG)) ) return Mix382off;
    }

    // Exclude single lines before Z-snake (required)
    if ((eq_E_F||eq_E_H) && !eq_E_I ) return theEXIT;

    // Z-snake (thick eq_A_B, one side tower/wall, other not)
    if ( eq_B_P && !eq_B_PA && !eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
    if ( eq_D_Q && !eq_D_QA && !eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;

	return theEXIT;

}	// admixX

vec4 admixS(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint II, bool eq_B_D, bool eq_E_D, float El, float Bl, vec4 colE, vec4 colF) {

			//                                    A B C  .
			//                                  Q D 🄴 🅵 🅁       Zone 4
			//					                🅶 🅷 I
			//					                  Ｓ
    // Practical 1: sim E/B(C) follows original logic - no jarring saw insertion on single-pixel E
    // Practical 2: E can equal I? YES!

    if (any_eq2(F,C,I)) return colE;
    //if (any_eq3(F,A,C,I)) return colE;

    if (eq(R, RI) && noteq(R,I)) return colE;
    if (eq(H, S) && noteq(H,I)) return colE;

    if ( eq(R, RC) || eq(G,SG) ) return colE;

	// Remove alpha channel weighting
	Bl = fract(Bl) *10.0;
	El = fract(El) *10.0;

    if ( ( eq_B_D&&eq(B,C)&&diffEB<0.381966 || eq_E_D&&vi_sim2(colE,E,C) ) &&
    (any_eq3(I,H,S,RI) || eq(SI,RI)&&noteq(I,II)) ) return colF;

    return colE;
}


//////////////////////////////////////////// main line /////////////////////////////////////////////// pin zz

void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);
    ivec2 destXY = ivec2(xy) * 2;

    uint E = src(srcX, srcY);
    uint B = src(srcX, srcY-1);
    uint D = src(srcX-1, srcY);
    uint F = src(srcX+1, srcY);
    uint H = src(srcX, srcY+1);

    vec4 colE = unpackUnorm4x8(E);
    vec4 J = colE, K = colE, L = colE, M = colE;

    bool eq_E_D = eq(E,D);
    bool eq_E_F = eq(E,F);
    bool eq_E_B = eq(E,B);
    bool eq_E_H = eq(E,H);

// Skip horizontal/vertical 3x1 lines
if ( eq_E_D && eq_E_F || eq_E_B && eq_E_H) {writeJKLM;return;}

    bool eq_B_H = eq(B,H);
    bool eq_D_F = eq(D,F);
// Skip if center enclosed by mirror pixels
if ( eq_B_H && eq_D_F ) {writeJKLM;return;}

    // Fetch 5x5 neighborhood
    uint A = src(srcX-1, srcY-1);
    uint C = src(srcX+1, srcY-1);
    uint G = src(srcX-1, srcY+1);
    uint I = src(srcX+1, srcY+1);
    uint P = src(srcX, srcY-2);
    uint Q = src(srcX-2, srcY);
    uint R = src(srcX+2, srcY);
    uint S = src(srcX, srcY+2);


    uint PA = src(srcX-1, srcY-2);
    uint PC = src(srcX+1, srcY-2);
    uint QA = src(srcX-2, srcY-1);
    uint QG = src(srcX-2, srcY+1); //             AA    PA    [P]   PC    CC
    uint RC = src(srcX+2, srcY-1); //                ┌──┬──┬──┐
    uint RI = src(srcX+2, srcY+1); //             QA │  A │  B │ C  │ RC
    uint SG = src(srcX-1, srcY+2); //                ├──┼──┼──┤
    uint SI = src(srcX+1, srcY+2); //            [Q] │  D │  E │ F  │ [R]
    uint AA = src(srcX-2, srcY-2); //                ├──┼──┼──┤
    uint CC = src(srcX+2, srcY-2); //             QG │  G │  H │ I  │ RI
    uint GG = src(srcX-2, srcY+2); //                └──┴──┴──┘
    uint II = src(srcX+2, srcY+2); //             GG    SG    [S]   SI    II


// Unpack 8 neighbors
    vec4 colA = unpackUnorm4x8(A);
    vec4 colC = unpackUnorm4x8(C);
    vec4 colG = unpackUnorm4x8(G);
    vec4 colI = unpackUnorm4x8(I);
    vec4 colB = unpackUnorm4x8(B);
    vec4 colD = unpackUnorm4x8(D);
    vec4 colF = unpackUnorm4x8(F);
    vec4 colH = unpackUnorm4x8(H);

// Precompute luminance
    float Bl = luma(colB);
    float Dl = luma(colD);
    float El = luma(colE);
    float Fl = luma(colF);
    float Hl = luma(colH);


// 	Pre-calculate
    bool eq_B_D = eq(B,D);
    bool eq_B_F = eq(B,F);
    bool eq_D_H = eq(D,H);
    bool eq_F_H = eq(F,H);
	bool eq_E_A = eq(E, A);
	bool eq_E_C = eq(E, C);
	bool eq_E_G = eq(E, G);
	bool eq_E_I = eq(E, I);

    // Any mirror enclosure of center
    bool oppoPix =  eq_B_H || eq_D_F;
	// Flags for 1:1 slope processing
    bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;
	// Valid standard pixel return from 1:1 slope
    bool slope1ok = false;  bool slope2ok = false;  bool slope3ok = false;  bool slope4ok = false;
	// slopeBAD: entered admixX but returned E (at least one of JKLM)
    // slopOFF: returned with OFF flag - skip long slope processing

    // B - D
	if ( (!eq_E_B&&!eq_E_D&&!oppoPix) && (!eq_D_H&&!eq_B_F) && (B!=0u&&D!=0u) &&
		(eq_E_A || El>=Dl&&El>=Bl) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || noteq(E,P) || noteq(E,Q) ) && 
		( eq_B_D&&(eq_F_H||eq_E_A||eq(B,PC)||eq(D,QG))  ||  (eq_B_D||sim1(colB,colD))&&(eq_E_C||eq_E_G||sim2(colE,colC)||sim2(colE,colG)) ) ) {
		J=admixX(A,B,C,D,E,F,G,H,I,
				P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG,
				El, Bl, Dl, Fl, Hl,
				colE, colB, colD, colC, colG,
				eq_B_D, eq_F_H, eq_E_A, eq_E_C, eq_E_G, eq_E_I, eq_E_F, eq_E_H);
		slope1 = true;
		if (J.b > 1.0 ) {
            if (J.b > 7.0 ) J=J-8.0; 	// slopOFF
			if (J.b == 4.0 ) {
					J=colE;
					writeJKLM;
					return;
			}
			if (J.b == 2.0 ) J=colE;		// slopeBAD
		} else slope1ok = true;
	}
    // B - F
	if ( !slope1 && 
		(!eq_E_B&&!eq_E_F&&!oppoPix) && (!eq_B_D&&!eq_F_H) && (B!=0u&&F!=0u) && 
		(eq_E_C || El>=Bl&&El>=Fl) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || noteq(E,P) || noteq(E,R) ) && 
		( eq_B_F&&(eq_D_H||eq_E_C||eq(B,PA)||eq(F,RI)) ||  ( eq_B_F||sim1(colB,colF))&&(eq_E_A||eq_E_I||sim2(colE,colA)||sim2(colE,colI)) ) ) {
		K=admixX(C,F,I,B,E,H,A,D,G,
				R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA,
				El,Fl,Bl,Hl,Dl,
				colE,colF,colB,colI,colA,
				eq_B_F, eq_D_H, eq_E_C, eq_E_I, eq_E_A, eq_E_G, eq_E_H, eq_E_D);
		slope2 = true;
		if (K.b > 1.0 ) {
            if (K.b > 7.0 ) K=K-8.0;
			if (K.b == 4.0 ) {
					K=colE;
					writeJKLM;
					return;
			}
			if (K.b == 2.0 ) K=colE;
		} else {slope2ok = true;}
	}
    // D - H
	if ( !slope1 && 
		(!eq_E_D&&!eq_E_H&&!oppoPix) && (!eq_F_H&&!eq_B_D) && (D!=0u&&H!=0u) && 
		(eq_E_G || El>=Hl&&El>=Dl)  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || noteq(E,S) || noteq(E,Q))  &&  
		( eq_D_H&&(eq_B_F||eq_E_G||eq(D,QA)||eq(H,SI)) ||  ( eq_D_H||sim1(colD,colH))&&(eq_E_A||eq_E_I||sim2(colE,colA)||sim2(colE,colI)) ) ) {
		L=admixX(G,D,A,H,E,B,I,F,C,
				Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II,
				El,Dl,Hl,Bl,Fl,
				colE,colD,colH,colA,colI,
				eq_D_H, eq_B_F, eq_E_G, eq_E_A, eq_E_I, eq_E_C, eq_E_B, eq_E_F);
		slope3 = true;
		if (L.b > 1.0 ) {
            if (L.b > 7.0 ) L=L-8.0;
			if (L.b == 4.0 ) {
					L=colE;
					writeJKLM;
					return;
			}
			if (L.b == 2.0 ) L=colE;
		} else {slope3ok = true;}
	}
    // F - H
	if ( !slope2 && !slope3 && 
		(!eq_E_F&&!eq_E_H&&!oppoPix) && (!eq_B_F&&!eq_D_H) && (F!=0u&&H!=0u) && 
		(eq_E_I || El>=Fl&&El>=Hl)  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || noteq(E,R) || noteq(E,S))  &&  
		( eq_F_H&&(eq_B_D||eq_E_I||eq(F,RC)||eq(H,SG)) ||  ( eq_F_H||sim1(colF,colH))&&(eq_E_C||eq_E_G||sim2(colE,colC)||sim2(colE,colG)) ) ) {
		M=admixX(I,H,G,F,E,D,C,B,A,
				S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC,
				El,Hl,Fl,Dl,Bl,
				colE,colH,colF,colG,colC,
				eq_F_H, eq_B_D, eq_E_I, eq_E_G, eq_E_C, eq_E_A, eq_E_D, eq_E_B);
		slope4 = true;
		if (M.b > 1.0 ) {
            if (M.b > 7.0 ) M=M-8.0;
			if (M.b == 4.0 ) {
					M=colE;
					writeJKLM;
					return;
			}
			if (M.b == 2.0 ) M=colE;
		} else {slope4ok = true;}
	}


//  Long gentle 2:1 slope  (P100)

	bool longslope = false;

    if (slope4ok && eq_F_H) { // Zone 4 long slope
        // Original rule ext 1: Pass adjacent pixel to admixL to prevent double-blend
        // Original rule ext 2: No L-shape within opposite pixel gap unless wall formed
        if (eq(G,H) && eq(F,R) && noteq(R, RC) && (noteq(Q,G)||eq(Q, QA))) {L=admixL(M,L,colH); longslope = true;}
        // Vertical
		if (eq(C,F) && eq(H,S) && noteq(S, SG) && (noteq(P,C)||eq(P, PA))) {K=admixL(M,K,colF); longslope = true;}
    }


    if (slope3ok && eq_D_H) { // Zone 3 long slope
        // Horizontal
        if (eq(D,Q) && eq(H,I) && noteq(Q, QA) && (noteq(R,I)||eq(R, RC))) {M=admixL(L,M,colH); longslope = true;}
        // Vertical
		if (eq(A,D) && eq(H,S) && noteq(S, SI) && (noteq(A,P)||eq(P, PC))) {J=admixL(L,J,colD); longslope = true;}
    }

    if (slope2ok && eq_B_F) { // Zone 2 long slope
        // Horizontal
        if (eq(A,B) && eq(F,R) && noteq(R, RI) && (noteq(A,Q)||eq(Q, QG))) {J=admixL(K,J,colB); longslope = true;}
        // Vertical
		if (eq(F,I) && eq(B,P) && noteq(P, PA) && (noteq(I,S)||eq(S, SG))) {M=admixL(K,M,colF); longslope = true;}
    }

    if (slope1ok && eq_B_D) { // Zone 1 long slope
        // Horizontal
        if (eq(B,C) && eq(D,Q) && noteq(Q, QG) && (noteq(C,R)||eq(R, RI))) {K=admixL(J,K,colB); longslope = true;}
        // Vertical
		if (eq(D,G) && eq(B,P) && noteq(P, PC) && (noteq(G,S)||eq(S, SI))) {L=admixL(J,L,colD); longslope = true;}
    }

// Long slope formed = exit (rare diagonal sawslope)
bool skiprest = longslope;

bool slopeok = slope1ok||slope2ok||slope3ok||slope4ok;


// Note: sawslope cannot exclude slopeOFF (minor) / slopeBAD (rare), but CAN exclude slopeok (strong shapes)
if (!skiprest && !oppoPix && !slopeok) {


        // Horizontal bottom
		if (!eq_E_H && none_eq2(H,A,C)) {

			//                                    A B Ｃ ・
			//                                  Q D 🄴 🅵 🅁       Zone 4
			//					                🅶 🅷 I
			//					                  Ｓ
			// (!slope3 && !eq_D_H) chain is elegant
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H)  && !eq_F_H && F!=0u &&
                !eq_E_F && (eq_B_D || eq_E_D) && eq(R,H) && eq(F,G) ) {
                M = admixS(A,B,C,D,E,F,G,H,I,R,RC,RI,S,SG,SI,II,eq_B_D,eq_E_D,El,Bl,colE,colF);
                skiprest = true;}

			//                                  ・  A Ｂ C
			//                                  🆀 🅳 🄴 Ｆ R       Zone 3
			//                                     G 🅷 🅸
			//					                   Ｓ
			if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H && D!=0u &&
                 !eq_E_D && (eq_B_F || eq_E_F) && eq(Q,H) && eq(D,I) ) {
                L = admixS(C,B,A,F,E,D,I,H,G,Q,QA,QG,S,SI,SG,GG,eq_B_F,eq_E_F,El,Bl,colE,colD);
                skiprest = true;}
		}

        // Horizontal up
		if ( !skiprest && !eq_E_B && none_eq2(B,G,I)) {

			//					                   Ｐ
			//                                    🅐 🅑 Ｃ
			//                                  ＱＤ 🄴 🅵 🅁       Zone 2
			//                                    Ｇ H  I  .
			if ( (!slope1 && !eq_B_D)  && (!slope4 && !eq_F_H) && !eq_B_F && F!=0u &&
				  !eq_E_F && (eq_D_H || eq_E_D) && eq(B,R) && eq(A,F) ) {
                K = admixS(G,H,I,D,E,F,A,B,C,R,RI,RC,P,PA,PC,CC,eq_D_H,eq_E_D,El,Hl,colE,colF);
                skiprest = true;}

			//					                  Ｐ
			//                                    A 🅑 🅲
			//                                 🆀 🅳 🄴 Ｆ R        Zone 1
			//                                  . G Ｈ I
			if ( !skiprest && (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_B_D && D!=0u &&
				 !eq_E_D && (eq_F_H || eq_E_F) && eq(B,Q) && eq(C,D) ) {
                J = admixS(I,H,G,F,E,D,C,B,A,Q,QG,QA,P,PC,PA,AA,eq_F_H,eq_E_F,El,Hl,colE,colD);
                skiprest = true;}

		}

        // Vertical left
        if ( !skiprest && !eq_E_D && none_eq2(D,C,I) ) {

			//                                    🅐 B Ｃ
			//                                  Q 🅳 🄴 Ｆ R
			//                                    Ｇ 🅷 I        Zone 3
			//                                       🆂 ・
            if ( (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H && H!=0u &&
				  !eq_E_H && (eq_B_F || eq_E_B) && eq(D,S) && eq(A,H) ) {
                L = admixS(C,F,I,B,E,H,A,D,G,S,SI,SG,Q,QA,QG,GG,eq_B_F,eq_E_B,El,Fl,colE,colH);
                skiprest = true;}

			//                                      🅟 ・
			//                                    A 🅑 C
			//                                  Q 🅳 🄴 F R       Zone 1
			//                                    🅶 ＨＩ
			if ( !skiprest && (!slope3 && !eq_D_H) && (!slope2 && !eq_B_F) && !eq_B_D && B!=0u &&
				  !eq_E_B && (eq_F_H || eq_E_H) && eq(P,D) && eq(B,G) ) {
                J = admixS(I,F,C,H,E,B,G,D,A,P,PC,PA,Q,QG,QA,AA,eq_F_H,eq_E_H,El,Fl,colE,colB);
                skiprest = true;}

		}

        // Vertical right
		if ( !skiprest && !eq_E_F && none_eq2(F,A,G) ) { // right

			//                                    A B 🅲
			//                                  Q D 🄴 🅵 R
			//                                    G 🅷 I        Zone 4
			//                                    . 🆂
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_F_H && H!=0u &&
				  !eq_E_H && (eq_B_D || eq_E_B) && eq(S,F) && eq(H,C) ) {
                M = admixS(A,D,G,B,E,H,C,F,I,S,SG,SI,R,RC,RI,II,eq_B_D,eq_E_B,El,Dl,colE,colH);
                skiprest = true;}

			//                                    ・ 🅟
			//                                    A 🅑 C
			//                                  Q D 🄴 🅵 R        Zone 2
			//                                    G H 🅸
			if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_B_F && B!=0u &&
				 !eq_E_B && (eq_D_H || eq_E_H) && eq(P,F) && eq(B,I) ) {
                K = admixS(G,D,A,H,E,B,I,F,C,P,PA,PC,R,RI,RC,CC,eq_D_H,eq_E_H,El,Dl,colE,colB);
                skiprest = true;}

		} // vertical right
} // sawslope

// sawslope formed = exit; legacy: skiprest||slopeBAD (uses slopeOFF/ok with weak effect)
skiprest = skiprest||slope1||slope2||slope3||slope4||E==0u||B==0u||D==0u||F==0u||H==0u;

/**************************************************
       Concave + Cross pattern	（P100）	   
 *************************************************/
// Cross distant uses similar pixels - useful for horizontal lines + aliasing / gradient text
// e.g. SF2III intro glowing text, SFZ3mix Japanese houses, Garou intro

	vec4 colX;		// Temporary X

    if (!skiprest &&
        Bl<El && !eq_E_D && !eq_E_F && eq_E_H && !eq_E_A && !eq_E_C && all_eq2(G,H,I) && vi_sim2(colE,E,S) ) { // TOP

        if (eq_B_D||eq_B_F) { J=admixK(colB,J);    K=J;
            if (eq_D_F) { L=mix(J,L, 0.61804);   M=L; }
        } else { colX = El-Bl < abs(El-Dl) ? colB : colD;  J=admixC(colX,J);
			if (eq_D_F) { K=J;  L=mix(J,L, 0.61804);    M=L; }
			else {colX = El-Bl < abs(El-Fl) ? colB : colF; 		K=admixC(colX,K); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Hl<El && !eq_E_D && !eq_E_F && eq_E_B && !eq_E_G && !eq_E_I && all_eq2(A,B,C) && vi_sim2(colE,E,P) ) { // BOTTOM

        if (eq_D_H||eq_F_H) { L=admixK(colH,L);    M=L;
            if (eq_D_F) { J=mix(L,J, 0.61804);   K=J; }
        } else { colX = El-Hl < abs(El-Dl) ? colH : colD;  L=admixC(colX,L);
			if (eq_D_F) { M=L;  J=mix(L,J, 0.61804);    K=J; }
			else { colX = El-Hl < abs(El-Fl) ? colH : colF;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

   if (!skiprest &&
		Fl<El && !eq_E_B && !eq_E_H && eq_E_D && !eq_E_C && !eq_E_I && all_eq2(A,D,G) && vi_sim2(colE,E,Q) ) { // RIGHT

        if (eq_B_F||eq_F_H) { K=admixK(colF,K);    M=K;
            if (eq_B_H) { J=mix(K,J, 0.61804);   L=J; }
        } else { colX = El-Fl < abs(El-Bl) ? colF : colB;  K=admixC(colX,K);
			if (eq_B_H) { M=K;  J=mix(K,J, 0.61804);    L=J; }
			else { colX = El-Fl < abs(El-Hl) ? colF : colH;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Dl<El && !eq_E_B && !eq_E_H && eq_E_F && !eq_E_A && !eq_E_G && all_eq2(C,F,I) && vi_sim2(colE,E,R) ) { // LEFT

        if (eq_B_D||eq_D_H) { J=admixK(colD,J);    L=J;
            if (eq_B_H) { K=mix(J,K, 0.61804);   M=K; }
        } else { colX = El-Dl < abs(El-Bl) ? colD : colB;  J=admixC(colX,J);
			if (eq_B_H) { L=J;   K=mix(J,K, 0.61804);    M=K; }
			else { colX = El-Dl < abs(El-Hl) ? colD : colH;    L=admixC(colX,L); }
            }

	   skiprest = true;
	}

/*
	    ✕О
    ООО✕
   	    ✕О    Scorpion pattern (P99). Resembles Matrix tracker bugs. Flattens regular dithered pixels.
*/
// Practical: 1. Scorpion claws use similarity? Causes graphical artifacts
// Practical: 2. Shorten scorpion tail by 1 pixel to catch more shapes
// Scorpion is exclusive - won't trigger if any prior rule ran

   if (!skiprest && !eq_E_F &&eq_E_D&&eq_B_F&&eq_F_H && eq_E_C && eq_E_I && noteq(F,src(srcX+3, srcY)) ) {K=admixK(colF,K); M=K;J=mix(K,J, 0.61804); L=J;skiprest=true;}	// RIGHT
   if (!skiprest && !eq_E_D &&eq_E_F&&eq_B_D&&eq_D_H && eq_E_A && eq_E_G && noteq(D,src(srcX-3, srcY)) ) {J=admixK(colD,J); L=J;K=mix(J,K, 0.61804); M=K;skiprest=true;}	// LEFT
   if (!skiprest && !eq_E_H &&eq_E_B&&eq_D_H&&eq_F_H && eq_E_G && eq_E_I && noteq(H,src(srcX, srcY+3)) ) {L=admixK(colH,L); M=L;J=mix(L,J, 0.61804); K=J;skiprest=true;}	// BOTTOM
   if (!skiprest && !eq_E_B &&eq_E_H&&eq_B_D&&eq_B_F && eq_E_A && eq_E_C && noteq(B,src(srcX, srcY-3)) ) {J=admixK(colB,J); K=J;L=mix(J,L, 0.61804); M=L;}				// TOP

	// Final Exit
	writeJKLM;

}
