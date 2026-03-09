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

	// Legacy clamp approach: return center pixel consistently when coordinates out of bounds
    //return readColoru(uvec2(clamp(x, 0, params.width - 1), clamp(y, 0, params.height - 1)));

	// Improved out-of-bounds detection: check if coordinates are within valid range first.
    // Return transparent color immediately if out of bounds.
    return (x >= 0 && x < params.width && y >= 0 && y < params.height) 
           ? readColoru(uvec2(x, y)) 
           : 0u;
}


// RGB perceptual weight + alpha segmentation
float luma(vec4 col) {

	// BT.601 standard (CRT era) - result divided by 10, range [0.0 - 0.1]
    float rgbsum =dot(col.rgb, vec3(0.0299, 0.0587, 0.0114));

	// Subsequent fractional extraction *10 removes alpha weighting
    float alphafactor = 
        (col.a > 0.854102) ? 1.0 :		// 2x short golden ratio (larger segment)
        (col.a > 0.618034) ? 2.0 :		// 1x golden ratio
        (col.a > 0.381966) ? 3.0 :		// 1x short golden ratio
        (col.a > 0.145898) ? 4.0 :		// 2x short golden ratio
        (col.a > 0.002) ? 5.0 : 8.0;	// Fully transparent

    return rgbsum + alphafactor;

}

/* Constant explanations:
0.145898	：			2x short golden ratio of 1.0
0.0638587	：		Squared value after 2x short golden ratio on RGB Euclidean distance
0.024391856	：		Squared value after 2x short golden ratio + 1x golden ratio on RGB Euclidean distance
0.00931686	：		Squared value after 3x short golden ratio on RGB Euclidean distance
0.001359312	：		Squared value after 4x short golden ratio on RGB Euclidean distance
0.4377		：		Squared value after 1x short golden ratio on RGB Euclidean distance
0.75			：		Squared value after halving RGB Euclidean distance
*/
// Pixel similarity check LV1
bool sim1(vec4 col1, vec4 col2) {

    vec4 diff = col1 - col2;
    vec4 absdiff = abs(diff);

    // 1. Fast component difference check
    if ( absdiff.r > 0.1 || absdiff.g > 0.1 || absdiff.b > 0.1 || absdiff.a > 0.145898 ) return false;	// xxx.alpha

    // 2. Fast squared distance check
	float dot_diff = dot(diff.rgb, diff.rgb);		// xxx.alpha
    if (dot_diff < 0.001359312) return true;

    // 3. Gradual pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.096 ) return false;    // Exit if difference exceeds int24
    if ( max_diff-min_diff<0.024 && dot_diff<0.024391856)  return true;  //  Consider gradual pixel if difference ≤ int6. Relax threshold by one level.

	// 4. Grayscale pixel check
    float sum1 = dot(col1.rgb, vec3(1.0));  // Sum of RGB channels	//xxx.alpha
    float sum2 = dot(col2.rgb, vec3(1.0));						//xxx.alpha
    float avg1 = sum1 * 0.3333333;
    float avg2 = sum2 * 0.3333333;

	vec3 graydiff1 = col1.rgb - vec3(avg1);	//xxx.alpha
	vec3 graydiff2 = col2.rgb - vec3(avg2);	//xxx.alpha
	float dotgray1 = dot(graydiff1,graydiff1);
	float dotgray2 = dot(graydiff2,graydiff2);
    // 0.002: Max single-channel difference allowed is int13 when avg=20. 
    // 0.0004: Max single-channel difference allowed is int6, or int3+4 for dual channels
	float tolerance1 = avg1<0.08 ? 0.002 : 0.0004;
	float tolerance2 = avg2<0.08 ? 0.002 : 0.0004;
    // 0.078 limits max green channel value to 19 (perceptible threshold to human eye)
    bool Col1isGray = sum1<0.078||dotgray1<tolerance1;
    bool Col2isGray = sum2<0.078||dotgray2<tolerance2;

	// Relax standard to Lv2 if both are grayscale
    if ( Col1isGray && Col2isGray && dot_diff<0.024391856 ) return true;

	// Exit if only one is grayscale
    if ( Col1isGray != Col2isGray ) return false;

    // Accumulate positive/negative values separately using max/min
    float team_pos = abs(dot(max(diff.rgb, 0.0), vec3(1.0)));	//xxx.alpha
    float team_neg = abs(dot(min(diff.rgb, 0.0), vec3(1.0)));	//xxx.alpha
    // Find opposing channels and add to squared distance for judgment
    float team_rebel = min(team_pos, team_neg);
    // Empirical: At least 3x opposing channel value needs to be added. (+1x to break even, +2x to form increasing trend)
    return dot_diff + team_rebel*team_rebel*3.0 < 0.00931686;

}

// Pixel similarity check LV2 and LV3
bool sim2n3(vec4 col1, vec4 col2, int Lv) {

	// Ignore RGB if both points are nearly transparent	//xxx.alpha
	if ( col1.a < 0.381966 && col2.a < 0.381966 ) return true;
	// Alpha channel difference cannot exceed 0.382	//xxx.alpha
	if ( abs(col1.a-col2.a)>0.381966) return false;

    // 1. Clamp RGB dark areas
    vec3 clampCol1 = max(col1.rgb, vec3(0.078));	//xxx.alpha
    vec3 clampCol2 = max(col2.rgb, vec3(0.078));	//xxx.alpha

    vec3 clampdiff = clampCol1 - clampCol2;

	// 2x short + 1x, 2x short golden ratio of RGB Euclidean distance between two points
    float dotdist = Lv==2 ? 0.024391856 : 0.0638587;

    return dot(clampdiff, clampdiff) < dotdist;
}

bool sim2(vec4 colC1, uint C1, uint C2) {
    if (C1==C2) return true;
    return sim2n3(colC1, unpackUnorm4x8(C2), 2);
}

bool sim3(vec4 col1, vec4 col2) {
    return sim2n3(col1, col2, 3);
}

bool mixcheck(vec4 col1, vec4 col2) {

	// Do not mix if either is transparent		//xxx.alpha
	bool col1alpha0 = col1.a < 0.003;
	bool col2alpha0 = col2.a < 0.003;
	if (col1alpha0!=col2alpha0) return false;

	// Cannot mix if transparency difference exceeds 50%	//xxx.alpha
	if (abs(col1.a - col2.a)>0.5) return false;

    vec3 diff = col1.rgb - col2.rgb;

    // Gradual pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.618034 ) return false;

	float dot_diff = dot(diff, diff);
    if( max_diff-min_diff<0.024 && dot_diff<0.75)  return true;  //  0.020 ≤ int5    0.024 ≤ int6

    // Accumulate positive/negative values separately using max/min
    float team_pos = abs(dot(max(diff, 0.0), vec3(1.0)));
    float team_neg = abs(dot(min(diff, 0.0), vec3(1.0)));
    // Find opposing channels and add to squared distance for judgment
    float team_rebel = min(team_pos, team_neg);
    // Empirical: At least 3x opposing channel value needs to be added. (+1x to break even, +2x to form increasing trend)
    return dot_diff + team_rebel*team_rebel*3.0 < 0.4377;
}

//	RGB must be identical, small alpha channel difference allowed
bool eq(uint C1, uint C2){
    if (C1 == C2) return true;

	uint rgbC1 = C1 & 0x00FFFFFFu;
	uint rgbC2 = C2 & 0x00FFFFFFu;

	if (rgbC1 != rgbC2) return false;

    uint alphaC1 = C1 >> 24;
    uint alphaC2 = C2 >> 24;

    // Note: abs(alphaC1-alphaC2) cannot be used with uint!
    uint alphaDiff = (alphaC1 > alphaC2) ? (alphaC1 - alphaC2) : (alphaC2 - alphaC1);

    return alphaDiff < 38u;	//2x short golden ratio of 255u

}

#define noteq(a,b) (a!=b)

bool vec_noteq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);
	// Allow total RGB channel difference of int2, alpha channel difference of int5
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
// Concave + Cross shape - weak blending (weak/none)
vec4 admixC(vec4 colX, vec4 colE) {
	// Transparent pixels already filtered in main line

	bool mixok = mixcheck(colX, colE);

	return mixok ? Mix618 : colE;

}

// K shape - forced weak blending (weak/weaker)
vec4 admixK(vec4 colX, vec4 colE) {
	// Transparent pixels already filtered in main line

	bool mixok = mixcheck(colX, colE);

	return mixok ? Mix618 : Mix854;

}

// L shape 2:1 slope - extension of main corner
// Note: This rule requires all 4 pixels on the slope to be identical. Otherwise, various visual artifacts will appear!
vec4 admixL(vec4 colX, vec4 colE, vec4 colS) {

    // Check eqX,E: originally captured large number of duplicate pixels, now filtered via slopeok in main line.

	// If target X differs from reference S (sample), it means blending has already occurred once.
    // Copy directly without re-blending
	if (vec_noteq(colX, colS)) return colX;

	bool mixok = mixcheck(colX, colE);

    return mixok ? Mix382 : colX;
}

/********************************************************************************************************************************************
 *              												main slope + X cross-processing mechanism					                *
 *******************************************************************************************************************************************/
vec4 admixX(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint P, uint PA, uint PC, uint Q, uint QA, uint QG, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint AA, uint CC, uint GG, float El, float Bl, float Dl, float Fl, float Hl, vec4 colE, vec4 colB, vec4 colD) {

	// xxx.alpha
	// 1. Normal rule: center E has higher luma - transparency in BD is illogical
	// 2. B/D may be transparent when E-A are identical, but using transparent B/D to cut solid crossed E-A violates design
	if (colB.a<0.002||colD.a<0.002) return slopeBAD;

	bool eq_B_C = eq(B, C);
	bool eq_D_G = eq(D, G);

    // Exit if surrounded by straight walls on both sides
    if (eq_B_C && eq_D_G) return slopeBAD;

	//Pre-declare
	bool eq_A_B;		bool eq_A_D;		bool eq_A_P;	bool eq_A_Q;
	bool eq_B_P;		bool eq_B_PA;	bool eq_B_PC;
	bool eq_D_Q;		bool eq_D_QA;	bool eq_D_QG;
    bool eq_E_F;		bool eq_E_H;		bool eq_E_I;    bool En3;
	bool B_slope;	bool B_tower;	bool B_wall;
    bool D_slope;	bool D_tower;	bool D_wall;
	vec4 colX;		bool Xisblack;
    bool mixok;

	bool eq_B_D = eq(B,D);
    bool eq_E_A = eq(E,A);
	
    #define comboA3  eq_A_P && eq_A_Q
    #define En4square  En3 && eq_E_I

	// E is nearly transparent but not fully transparent - mostly edges	//xxx.alpha
	bool EalphaL = colE.a <0.381966 && colE.a >0.002;

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

	// Exit if E-A equality violates preset logic
	if (eq_E_A) return slopeBAD;

	// Exit if B/D differ significantly (greater than E's difference with either side)
	float diffBD = abs(Bl-Dl);
	if (diffBD > diffEB || diffBD > diffED) return slopeBAD;

	// Avoid single-pixel font edges being squeezed by black background on both sides
	// (Brightness difference between font and background usually exceeds 0.5)
	// Note: If BD are not equal, cannot use average for judgment - both must meet black condition
	Xisblack = checkblack(colB) && checkblack(colD);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

//  Exclusion rule before original logic (triangle vertices cannot protrude)
	eq_A_B = eq(A,B);
	if ( !Xisblack && eq_A_B && eq_D_G && eq(B,P) ) return slopeBAD;

	eq_A_D = eq(A,D);
	if ( !Xisblack && eq_A_D && eq_B_C && eq(D,Q) ) return slopeBAD;

    // B/D unconnected to any? Not applicable here (eliminates some artifacts but loses shapes, 
    // especially non-native pixel art like Double Dragon attract mode character sprites)

	// X is blend of B/D
	colX = mix(colB, colD, 0.5);
	colX.a = min(colB.a, colD.a);	// xxx.alpha

	mixok = mixcheck(colX,colE);

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	// A-side triple consecutive (eq_A_P && eq_A_Q) - high priority
    if (comboA3) return mixok ? Mix382off : Xoff;

    // Official original rule
    if ( eq(E,C) || eq(E,G) ) return mixok ? Mix382off : Xoff;

    // Enhanced original rule 1 - good at capturing trends, but enhanced 2 cannot be used (wall bypass issue)
    if ( !eq_D_G&&eq(E,QG)&&sim2(colE,E,G) || !eq_B_C&&eq(E,PC)&&sim2(colE,E,C) ) return mixok ? Mix382off : Xoff;


    // Exclude "3-pixel single-side wall" case
    if (!Xisblack){
        if ( eq_A_B&&eq_B_C || eq_A_D&&eq_D_G ) return slopeBAD;
    }

	//xxx.alpha
    if (EalphaL) return mixok ? Mix382off : Xoff;

    // F-H inline trend (includes En3) - placed after wall rules since blocked by 3-pixel wall rule
    if ( eq(F,H) ) return mixok ? Mix382off : Xoff;

    // Remaining "2-pixel single-side wall" and unlogical single pixels - abandon
    return slopeBAD;
} // B != D

/*******  B == D prepare *******/
  
	// Avoid single-pixel font edges being squeezed by black background on both sides
	Xisblack = checkblack(colB);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

    colX = colB;
	colX.a = min(colB.a , colD.a);

    bool eq_E_C = eq(E,C);
    bool eq_E_G = eq(E,G);
	bool sim_EC = eq_E_C || sim2n3(colE,unpackUnorm4x8(C),2);
	bool sim_EG = eq_E_G || sim2n3(colE,unpackUnorm4x8(G),2);

	bool ThickBorder;


/*===============================================
                 Original main rule (sim2 enhanced)
  ========================================== zz */
if ( (sim_EC || sim_EG) && !eq_E_A ){

/* Logic:
    1. Handle non-blending for continuous border shapes
    2. Special handling for long slopes
    3. Original rules
    4. Handle E-area inline (En4, En3, F-H etc.), remaining single strip and single pixel
    5. Handle L-shaped inner single strip and outer single strip
    5. Default fallback return
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


    // B+ D+ each extend shape (strong shape) - no blending, return Xoff
    if ( (B_slope||B_tower) && (D_slope||D_tower) && !eq_A_B) return Xoff;


	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);
	mixok = mixcheck(colX,colE);
    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));
	if (ThickBorder && !Xisblack) mixok=false;

    // A-side triple consecutive (eq_A_P && eq_A_Q)
    if (comboA3) {
        if (!eq_A_B) return Xoff;
        else mixok=false;
    }

    // XE_messL B-D-E L-shape with sim2 high similarity		WIP 
    // bool XE_messL = (eq_B_C && !sim_EG || eq_D_G && !sim_EC) ;

    eq_E_F = eq(E, F);
	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Long slope (clear, non-thick solid edge - strong trend!)
    // Special condition for long slopes
	if ( B_wall && D_tower ) {
        if (eq_E_G || sim_EG&&eq(E,QG) ) {   // Original rule + enhanced original
            if (eq_A_B) return mixok ? Mix382 : colX;    // Has thickness
            return colX;                               // Hollow
        }
        // Clear Z-shaped snake - exclude eq_A_B + XE_messL ???
        //if (eq_A_B  && !XE_messL) return ; // Poor effect
        if (eq_A_B) return slopeBAD;
        // 2-pixel with subsequent long slope
        if (eq_E_F ) return colX;
        // 1-pixel without subsequent long slope
        return Xoff;
    }

    eq_E_H = eq(E, H);
	D_wall = eq_D_G && !eq_D_QG && !eq_D_Q;

	if ( B_tower && D_wall ) {
        if (eq_E_C || sim_EC&&eq(E,PC) ) {   // Original rule + enhanced original
            if (eq_A_B) return mixok ? Mix382 : colX;    // Has thickness
            return colX;                                // Hollow
        }
        // Clear Z-shaped snake - exclude eq_A_B + XE_messL ???
        //if (eq_A_B  && !XE_messL) return ; // Poor effect
        if (eq_A_B) return slopeBAD;
        // 2-pixel with subsequent long slope
        if (eq_E_H ) return colX;
        // 1-pixel without subsequent long slope
        return Xoff;
    }


    // Official original rule (placed after special shapes - special shapes have designated no-blending!)
    if (eq_E_C || eq_E_G) return mixok ? Mix382 : colX;

    // Enhanced original rule 1
    if (sim_EG&&!eq_D_G&&eq(E,QG) || sim_EC&&!eq_B_C&&eq(E,PC)) return mixok ? Mix382off : Xoff;

    // Enhanced original rule 2
    if (sim_EC && sim_EG) return mixok ? Mix382off : Xoff;


    // F-H inline trend (skip En4, En3)
    if ( eq(F,H) )  return mixok ? Mix382off : Xoff;

	// Relaxed rule for final long slope cleanup (non-clear shapes)
    // Empirical: This block handles different slopes than F-H. Default to break-even unless cube shape.
	if (eq_B_C && eq_D_Q) {
        // Exit for double cube
		if (eq_B_P && eq_B_PC && eq_A_B && eq_D_QA && !eq_D_QG && eq_E_F && eq(H,I) ) return theEXIT;

		return mixok ? Mix382off : Xoff;
	}

	if ( eq_D_G && eq_B_P) {
        // Exit for double cube
		if (eq_D_Q && eq_D_QG && eq_A_B && eq_B_PA && !eq_B_PC && eq_E_H && eq(F,I) ) return theEXIT;

		return mixok ? Mix382off : Xoff;
	}

    eq_E_I = eq(E, I);

    // Exit for clear L-shaped corner with inner parallel single strip
    if (eq_A_B && !ThickBorder && !eq_E_I ) {
	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}

    // Early return if colors are similar (skip next step)
    if (mixok) return Mix382off;
    if (EalphaL) return mixok ? Mix382off : Xoff;	//xxx.alpha

    // Exit for hollow L-shaped corner with outer single strip (any direction) - fixes font edge issues
    if ( !eq_A_B && (eq_E_F||eq_E_H) && !eq_E_I) {
        if (B_tower && !eq_D_Q && !eq_D_QG) return theEXIT;
        if (D_tower && !eq_B_P && !eq_B_PC) return theEXIT;
    }

    // Fallback handling
    return mixok ? Mix382off : Xoff;

} // sim2 base


/*===================================================
                    E - A cross
  ============================================== zz */
if (eq_E_A) {

	// Cross ✕ judgment requires "region" and "trend" concepts - tighter conditions for different regions

    // No need to exit for unconnected B/D! ! !

    eq_E_F = eq(E, F);
    eq_E_H = eq(E, H);
    eq_E_I = eq(E, I);

    En3 = eq_E_F&&eq_E_H;

	// Special shape: square (En3 && eq_E_I)
	if ( En4square ) {
        if( noteq(G,H) && noteq(C,F)                      //  Independent clear 4-pixel square / 6-pixel rectangle (both sides meet)
		&& (eq(H,S) == eq(I,SI) && eq(F,R) == eq(I,RI)) ) return theEXIT;
        //else return mixok ? mix(X,E,0.381966) : X;    Note: Do not return directly. Need to enter chessboard scoring rule,
        // as adjacent B/D areas may also form bubbles with squares
    }

    //  Special shape: dot pattern
	//	Empirical 1: !eq_E_F, !eq_E_H - do not use !sim (loses some shapes)
    //  Empirical 2: Force blending (otherwise lose shapes - Horse from Gouketsuji Ichizoku, Saturday Night Slam Masters 2)

	bool Eisblack = checkblack(colE);
	mixok = mixcheck(colX,colE);

	//  1. Dot pattern center
    if ( eq_E_C && eq_E_G && eq_E_I && !eq_E_F && !eq_E_H ) {

		// Exit if center E is black (KOF '96 power bar, Punisher's belt) - avoid over-blending with high contrast
		if (Eisblack) return theEXIT;
		// Empirical 1: Do not capture black B points (normal logic entry)
		// Empirical 2: No need to check eq_F_H separately (95%+ probability) + layered gradient in power bar, 0.5 fallback blending
		return mixok ? Mix382off : Mix618off;
	}

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
    eq_B_PA = eq(B,PA);

	//  2. Dot pattern edge
    if ( eq_A_P && eq_A_Q && eq(A,AA) && noteq(A,PA) && noteq(A,QA) )  {
		if (Eisblack) return theEXIT;

        // Layered gradient edge - use strong blending
		if ( !eq_B_PA && eq(PA,QA) ) return mixok ? Mix382off : Mix618off;
        // Remaining perfect cross - definitely dot pattern edge, use weak blending
        // Fallback weak blending - no need to specify power bar border case separately
		return mixok ? Mix618off : Mix854off;
	}

	// xxx.alpha
	if (colE.a<0.002) return colX;

    eq_D_QA = eq(D,QA);
    eq_D_QG = eq(D,QG);
    eq_B_PC = eq(B,PC);

  //   3. Half dot pattern - usually shadow expression on outline edges, use weak blending
	if ( eq_E_C && eq_E_G && eq_A_P && eq_A_Q &&
		(eq_B_PC || eq_D_QG) &&
		 eq_D_QA && eq_B_PA) {
		//if (Eisblack) return ; Unnecessary

        return mixok ? Mix618off : Mix854off;
		}

    //   4. Quarter dot pattern - prone to ugly "pinkie effect" (Guile's Sonic Boom in SF2, Dino Crisis select screen)

	if ( eq_E_C && eq_E_G && eq_A_P
		 && eq_B_PA &&eq_D_QA && eq_D_QG
		 && eq_E_H
		) return mixok ? Mix618off : Mix854off;

	if ( eq_E_C && eq_E_G && eq_A_Q
		 && eq_B_PA &&eq_D_QA && eq_B_PC
		 && eq_E_F
		) return mixok ? Mix618off : Mix854off;


    // E-side triple consecutive (must come after dot pattern)
    if ( eq_E_C && eq_E_G ) return Xoff;

    // A-side triple consecutive (eq_A_P && eq_A_Q) - must come after dot pattern. 
    // Prefer blending over direct copy since E-A are identical
	if (comboA3) return mixok ? Mix382off : Xoff;


    // B-D part of long slope
	eq_B_P = eq(B,P);
	eq_D_Q = eq(D,Q);
	B_wall = eq_B_C && !eq_B_P;
	B_tower = eq_B_P && !eq_B_C;
	D_tower = eq_D_Q && !eq_D_G;
	D_wall = eq_D_G && !eq_D_Q;

    int scoreE = 0; int scoreB = 0; int scoreD = 0; int scoreZ = 0;


// E B D area chessboard scoring rule

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
        // Single strip
        if (eq_E_F ||eq_E_H) return theEXIT;
    }

    if ( eq(F,H) ) {
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
		scoreB -= int(eq_B_P);    //Replace eq(P,PA)
	}

//        D Zone
	scoreD -= int(Dn3);
	scoreD -= int(eq(G,Q));
    if (scoreD < 0) scoreZ = 0;

    if (eq_D_QA) {
		scoreD -= 1;
		scoreD -= int(eq_D_Q);    //Replace eq(Q,QA)
	}

    int scoreFinal = scoreE + scoreB + scoreD + scoreZ ;

    if (scoreE >= 1 && scoreB >= 0 && scoreD >=0) scoreFinal += 1;

    if (scoreFinal >= 2) return colX;

    if (scoreFinal == 1) return mixok ? Mix382 : colX;

    // Final supplement: total score 0, no deductions in B/D areas - forms long slope shape
    if (scoreB >= 0 && scoreD >=0) {
        if (B_wall&&D_tower) return colX;
        if (B_tower&&D_wall) return colX;
    }

    return slopeBAD;

}	// eq_E_A


/*=========================================================
                   F - H / B+ D+ extension new rule
  ==================================================== zz */

// This block differs from sim block - center point and related En4square/BD are naturally isolated by a wall
// Therefore judgment rules differ from sim side

	eq_B_P = eq(B, P);
    eq_B_PC = eq(B, PC);
	eq_D_Q = eq(D, Q);
    eq_D_QG = eq(D, QG);

    // Exit if B/D unconnected to any (Empirical: required for this branch block)
    if ( !eq_B_C && !eq_B_P && !eq_B_PC && !eq_D_G && !eq_D_Q && !eq_D_QG ) return slopeBAD;

	mixok = mixcheck(colX,colE);
    eq_E_I = eq(E, I);

	// Exit if center E is single high-contrast pixel
	// Tighten threshold if E is highlight
	float E_lumDiff = El > 0.92 ? 0.145898 : 0.381966;
	// Large difference with surroundings
    if ( !mixok && !eq_E_I && !EalphaL && abs(El-Fl)>E_lumDiff && abs(El-Hl)>E_lumDiff ) return slopeBAD;	//xxx.alpha


    eq_A_B = eq(A, B);
	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);
    eq_B_PA = eq(B,PA);
    eq_D_QA = eq(D,QA);

    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));

	if (ThickBorder && !Xisblack) mixok=false;

	B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;

    if (!eq_A_B) {
        // B + D + each extend shape
        // Empirical 1: Looser conditions for one side if the other has clear shape
        // Empirical 2: Outer side of "厂" shape break-even, inner side not break-even (can be tower but not wall)
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;

        // A-side triple consecutive (eq_A_P && eq_A_Q) - high priority
        if (comboA3) return Xoff;

        // combo 2x2 as supplement to above
        if ( B_slope && eq_A_P ) return mixok ? Mix382off : Xoff;
        if ( D_slope && eq_A_Q ) return mixok ? Mix382off : Xoff;
    }

    eq_E_F = eq(E, F);
	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Long slope (clear, non-solid edge - strong trend!)
    // Special condition for long slopes
	if ( B_wall && D_tower ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_F ) return colX;  //wip: Test direct X no blending
        return mixok ? Mix382off : Xoff;
    }

    eq_E_H = eq(E, H);
	D_wall = eq_D_G && !eq_D_QG&& !eq_D_Q;

	if ( B_tower && D_wall ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_H ) return colX;
        return mixok ? Mix382off : Xoff;
    }


    bool sim_X_E = sim3(colX,colE);
    bool eq_G_H = eq(G, H);
    bool eq_C_F = eq(C, F);
    bool eq_H_S = eq(H, S);
    bool eq_F_R = eq(F, R);

    En3 = eq_E_F&&eq_E_H;

    // 4-pixel rectangle inside wall (En3 && eq_E_I)
	if ( En4square ) {  // This square detection must come after previous rule
		if (sim_X_E) return mixok ? Mix382off : Xoff;
        // Exit for solid L-shaped inner wrap (fixes font edge corners and non-rounded building corners (Mega Man 7))
        if ( (eq_B_C || eq_D_G) && eq_A_B ) return theEXIT;
        //if (eq_H_S && eq_F_R) return theEXIT; // Extend both sides
        //  L-shaped inner wrap (hollow corner) / high-contrast independent clear 4-pixel square / 6-pixel rectangle (note rectangle edges)
        if ( ( eq_B_C&&!eq_G_H || eq_D_G&&!eq_C_F || !eq_G_H&&!eq_C_F&&diffEB>0.5) && (eq_H_S == eq(I, SI) && eq_F_R == eq(I, RI)) ) return theEXIT;

        return mixok ? Mix382off : Xoff;
    }

    // Triangle inside wall
 	if ( En3 ) {
		if (sim_X_E) return mixok ? Mix382off : Xoff;
        if (eq_H_S && eq_F_R) return theEXIT; // Extend both sides (building edges)
       // Inner curve
        if (eq_B_C || eq_D_G) return mixok ? Mix382off : Xoff;
		// Direct return if has thickness (Z-shaped snake can break-even outer curve below)
        if (eq_A_B) return mixok ? Mix382off : Xoff;
        // Outer curve
        if (eq_B_P || eq_D_Q) return theEXIT;

        return mixok ? Mix382off : Xoff;
        // Final two rules based on experience - principle: connect inner L curve, not outer (Jimmy's eyebrows in Double Dragon)
	}

    // F - H
	// Principle: connect inner L curve, not outer
	if ( eq(F,H) ) {
    	if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;	//xxx.alpha
        // Exit for solid L-shaped inner wrap - avoid single pixel squeezed by symmetric double wrap
		if ( eq_B_C && eq_A_B && (eq_G_H||!eq_F_R) &&eq(F, I) ) return slopeBAD;
		if ( eq_D_G && eq_A_B && (eq_C_F||!eq_H_S) &&eq(F, I) ) return slopeBAD;

		//Inner curve
        if (eq_B_C && (eq_F_R||eq_G_H||eq(F, I))) return mixok ? Mix382off : Xoff;
        if (eq_D_G && (eq_C_F||eq_H_S||eq(F, I))) return mixok ? Mix382off : Xoff;
		// E-I F-H cross breaks trend
		if (eq_E_I) return slopeBAD;
        // Outer Z-shape
		if (eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
		// Outer curve - unless opposite side forms long L-shaped trend
		if (eq_B_P && (eq_C_F&&eq_H_S)) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && (eq_F_R&&eq_G_H)) return mixok ? Mix382off : Xoff;

        return slopeBAD;
	}


	// Relaxed rule for final long slope cleanup (non-clear shapes)
    // Note: This block handles different slopes than sim2 block - exit for solid corners
	if ( eq_B_C && eq_D_Q || eq_D_G && eq_B_P) {
        // Note: Clear eq_A_B first - otherwise single edge pixels get "corner cut" (eternal secret)
		if (eq_A_B) return theEXIT;
		return mixok ? Mix382off : Xoff;
	}


    //	A-side triple consecutive - NEVER use without !eq_A_B ! ! ! ! ! ! ! !
    //  if (comboA3) return X+slopeOFF;


	// One more check for B + D bidirectional extension - higher priority than inner/outer L single strip below
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;


    // Exit for clear L-shaped corner with inner single strip (sim_X_E no exemption)
    if (eq_A_B && !ThickBorder && !eq_E_I ) {

	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}

if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;	//xxx.alpha
    // Exit for hollow L-shaped corner with outer single strip (connect inner corner, not outer)
	// Empirical: Avoids font edge cutting (Captain Commando)
	if ( (B_tower || D_tower) && (eq_E_F||eq_E_H) && !eq_A_B && !eq_E_I) return theEXIT;

    // Final individual extension check for B or D

    // Maximum distance usable for slope
    if ( B_slope && !eq_A_B && eq(PC,CC) && noteq(PC,RC)) return mixok ? Mix382off : Xoff;
    if ( D_slope && !eq_A_B && eq(QG,GG) && noteq(QG,SG)) return mixok ? Mix382off : Xoff;

    // Slope can use one less judgment point if X-E are close, with internal logic restrictions
  	if ( mixok && !eq_A_B ) {
        if ( B_slope && (!eq_C_F||eq(F,RC)) ) return Mix382off;
        if ( D_slope && (!eq_G_H||eq(H,SG)) ) return Mix382off;
    }

    // Exclude single strip for Z-shaped snake below (necessary)
    if ((eq_E_F||eq_E_H) && !eq_E_I ) return theEXIT;

    // Z-shaped snake (eq_A_B has thickness, one side is tower (can be wall/slope) but other side cannot be tower/wall (naturally prohibited))
    if ( eq_B_P && !eq_B_PA && !eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
    if ( eq_D_Q && !eq_D_QA && !eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;

	return theEXIT;

}	// admixX

vec4 admixS(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint II, bool eq_B_D, bool eq_E_D, float El, float Bl, vec4 colE, vec4 colF) {

			//                                    ＡＢＣ  .
			//                                  ＱＤ🄴 🅵 🆁       Zone 4
			//					                  🅶 🅷 Ｉ
			//					                  Ｓ
    // Empirical 1: sim E B(C) conforms to original logic - E single pixel won't look abrupt when inserted by saw in practice
    // Empirical 2: Can E equal I? Yes!

    if (any_eq2(F,C,I)) return colE;
    //if (any_eq3(F,A,C,I)) return colE;

    if (eq(R, RI) && noteq(R,I)) return colE;
    if (eq(H, S) && noteq(H,I)) return colE;

    if ( eq(R, RC) || eq(G,SG) ) return colE;

	// Remove alpha channel weighting
	Bl = fract(Bl) *10.0;
	El = fract(El) *10.0;

    if ( ( eq_B_D&&eq(B,C)&&diffEB<0.381966 || eq_E_D&&sim2(colE,E,C) ) &&
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

// Skip horizontal/vertical 3x1
if ( eq_E_D && eq_E_F || eq_E_B && eq_E_H) {writeJKLM;return;}

    bool eq_B_H = eq(B,H);
    bool eq_D_F = eq(D,F);
// Skip center surrounded by mirrored blocks
if ( eq_B_H && eq_D_F ) {writeJKLM;return;}

    // Capture 5x5
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


    vec4 colB = unpackUnorm4x8(B);
    vec4 colD = unpackUnorm4x8(D);
    vec4 colF = unpackUnorm4x8(F);
    vec4 colH = unpackUnorm4x8(H);

    float Bl = luma(colB), Dl = luma(colD), El = luma(colE), Fl = luma(colF), Hl = luma(colH);


// 1:1 slope rules (P95)
    bool eq_B_D = eq(B,D);
    bool eq_B_F = eq(B,F);
    bool eq_D_H = eq(D,H);
    bool eq_F_H = eq(F,H);

    // Center surrounded by any mirrored blocks
    bool oppoPix =  eq_B_H || eq_D_F;
	// Flag for admixX entry via 1:1 slope rule
    bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;
	// Standard pixel returned normally via 1:1 slope rule
    bool slope1ok = false;  bool slope2ok = false;  bool slope3ok = false;  bool slope4ok = false;
	// slopeBAD: entered admixX but (at least one of JKLM) returns E point
    // slopOFF: returned with OFF flag - no long slope calculation
    bool slopeBAD_flag = false;

    // B - D
	if ( (!eq_E_B&&!eq_E_D&&!oppoPix) && (!eq_D_H&&!eq_B_F) && (El>=Dl&&El>=Bl || eq(E,A)) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || noteq(E,P) || noteq(E,Q) ) && ( eq_B_D&&(eq_F_H||eq(E,A)||eq(B,PC)||eq(D,QG)) || sim1(colB,colD)&&(sim2(colE,E,C)||sim2(colE,E,G)) ) ) {
		J=admixX(A,B,C,D,E,F,G,H,I,P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG, El,Bl,Dl,Fl,Hl,colE,colB,colD);
		slope1 = true;
		if (J.b > 1.0 ) {
            if (J.b > 7.0 ) J=J-8.0; 	//slopOFF
			if (J.b == 4.0 ) {
					J=colE;
					writeJKLM;
					return;
			}
			if (J.b == 2.0 ) J=colE;		//slopeBAD
		} else slope1ok = true;
	}
    // B - F
	if ( !slope1 && (!eq_E_B&&!eq_E_F&&!oppoPix) && (!eq_B_D&&!eq_F_H) && (El>=Bl&&El>=Fl || eq(E,C)) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || noteq(E,P) || noteq(E,R) ) && ( eq_B_F&&(eq_D_H||eq(E,C)||eq(B,PA)||eq(F,RI)) || sim1(colB,colF)&&(sim2(colE,E,A)||sim2(colE,E,I)) ) )  {
		K=admixX(C,F,I,B,E,H,A,D,G,R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA, El,Fl,Bl,Hl,Dl,colE,colF,colB);
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
	if ( !slope1 && (!eq_E_D&&!eq_E_H&&!oppoPix) && (!eq_F_H&&!eq_B_D) && (El>=Hl&&El>=Dl || eq(E,G))  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || noteq(E,S) || noteq(E,Q))  &&  ( eq_D_H&&(eq_B_F||eq(E,G)||eq(D,QA)||eq(H,SI)) || sim1(colD,colH) && (sim2(colE,E,A)||sim2(colE,E,I)) ) )  {
		L=admixX(G,D,A,H,E,B,I,F,C,Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II, El,Dl,Hl,Bl,Fl,colE,colD,colH);
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
	if ( !slope2 && !slope3 && (!eq_E_F&&!eq_E_H&&!oppoPix) && (!eq_B_F&&!eq_D_H) && (El>=Fl&&El>=Hl || eq(E,I))  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || noteq(E,R) || noteq(E,S))  &&  ( eq_F_H&&(eq_B_D||eq(E,I)||eq(F,RC)||eq(H,SG)) || sim1(colF,colH) && (sim2(colE,E,C)||sim2(colE,E,G)) ) )  {
		M=admixX(I,H,G,F,E,D,C,B,A,S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC, El,Hl,Fl,Dl,Bl,colE,colH,colF);
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


//  long gentle 2:1 slope  (P100)

	bool longslope = false;

    if (slope4ok && eq_F_H) { //zone4 long slope
        // Extended original rule 1. Pass adjacent pixel to admixL third parameter to prevent double blending
        // Extended original rule 2. No L-shape allowed in opposite two-pixel interval unless wall formed
        if (eq(G,H) && eq(F,R) && noteq(R, RC) && (noteq(Q,G)||eq(Q, QA))) {L=admixL(M,L,colH); longslope = true;}
        // vertical
		if (eq(C,F) && eq(H,S) && noteq(S, SG) && (noteq(P,C)||eq(P, PA))) {K=admixL(M,K,colF); longslope = true;}
    }


    if (slope3ok && eq_D_H) { //zone3 long slope
        // horizontal
        if (eq(D,Q) && eq(H,I) && noteq(Q, QA) && (noteq(R,I)||eq(R, RC))) {M=admixL(L,M,colH); longslope = true;}
        // vertical
		if (eq(A,D) && eq(H,S) && noteq(S, SI) && (noteq(A,P)||eq(P, PC))) {J=admixL(L,J,colD); longslope = true;}
    }

    if (slope2ok && eq_B_F) { //zone2 long slope
        // horizontal
        if (eq(A,B) && eq(F,R) && noteq(R, RI) && (noteq(A,Q)||eq(Q, QG))) {J=admixL(K,J,colB); longslope = true;}
        // vertical
		if (eq(F,I) && eq(B,P) && noteq(P, PA) && (noteq(I,S)||eq(S, SG))) {M=admixL(K,M,colF); longslope = true;}
    }

    if (slope1ok && eq_B_D) { //zone1 long slope
        // horizontal
        if (eq(B,C) && eq(D,Q) && noteq(Q, QG) && (noteq(C,R)||eq(R, RI))) {K=admixL(J,K,colB); longslope = true;}
        // vertical
		if (eq(D,G) && eq(B,P) && noteq(P, PC) && (noteq(G,S)||eq(S, SI))) {L=admixL(J,L,colD); longslope = true;}
    }

// Exit after longslope formation - sawslope rarely forms on diagonal
bool skiprest = longslope;

bool slopeok = slope1ok||slope2ok||slope3ok||slope4ok;


// Note: sawslope cannot exclude slopeOFF (few cases) and slopeBAD (very rare), but can exclude slopeok (strong shape)
if (!skiprest && !oppoPix && !slopeok) {


        // horizontal bottom
		if (!eq_E_H && none_eq2(H,A,C)) {

			//                                    A B Ｃ ・
			//                                  Q D 🄴 🅵 🆁       Zone 4
			//					                  🅶 🅷 I
			//					                  Ｓ
			// (!slope3 && !eq_D_H) clever combination
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H)  && !eq_F_H &&
                !eq_E_F && (eq_B_D || eq_E_D) && eq(R,H) && eq(F,G) ) {
                M = admixS(A,B,C,D,E,F,G,H,I,R,RC,RI,S,SG,SI,II,eq_B_D,eq_E_D,El,Bl,colE,colF);
                skiprest = true;}

			//                                  ・  A Ｂ C
			//                                  🆀 🅳 🄴 Ｆ R       Zone 3
			//                                     G 🅷 🅸
			//					                   Ｓ
			if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H &&
                 !eq_E_D && (eq_B_F || eq_E_F) && eq(Q,H) && eq(D,I) ) {
                L = admixS(C,B,A,F,E,D,I,H,G,Q,QA,QG,S,SI,SG,GG,eq_B_F,eq_E_F,El,Bl,colE,colD);
                skiprest = true;}
		}

        // horizontal up
		if ( !skiprest && !eq_E_B && none_eq2(B,G,I)) {

			//					                   Ｐ
			//                                    🅐 🅑 Ｃ
			//                                  ＱＤ 🄴 🅵 🆁       Zone 2
			//                                    Ｇ H  I  .
			if ( (!slope1 && !eq_B_D)  && (!slope4 && !eq_F_H) && !eq_B_F &&
				  !eq_E_F && (eq_D_H || eq_E_D) && eq(B,R) && eq(A,F) ) {
                K = admixS(G,H,I,D,E,F,A,B,C,R,RI,RC,P,PA,PC,CC,eq_D_H,eq_E_D,El,Hl,colE,colF);
                skiprest = true;}

			//					                  Ｐ
			//                                    A 🅑 🅲
			//                                 🆀 🅳 🄴 Ｆ R        Zone 1
			//                                  . G Ｈ I
			if ( !skiprest && (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_B_D &&
				 !eq_E_D && (eq_F_H || eq_E_F) && eq(B,Q) && eq(C,D) ) {
                J = admixS(I,H,G,F,E,D,C,B,A,Q,QG,QA,P,PC,PA,AA,eq_F_H,eq_E_F,El,Hl,colE,colD);
                skiprest = true;}

		}

        // vertical left
        if ( !skiprest && !eq_E_D && none_eq2(D,C,I) ) {

			//                                    🅐 B Ｃ
			//                                  Q 🅳 🄴 Ｆ R
			//                                    Ｇ 🅷 I        Zone 3
			//                                       🆂 ・
            if ( (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H &&
				  !eq_E_H && (eq_B_F || eq_E_B) && eq(D,S) && eq(A,H) ) {
                L = admixS(C,F,I,B,E,H,A,D,G,S,SI,SG,Q,QA,QG,GG,eq_B_F,eq_E_B,El,Fl,colE,colH);
                skiprest = true;}

			//                                      🅟 ・
			//                                    A 🅑 C
			//                                  Q 🅳 🄴 F R       Zone 1
			//                                    🅶 ＨＩ
			if ( !skiprest && (!slope3 && !eq_D_H) && (!slope2 && !eq_B_F) && !eq_B_D &&
				  !eq_E_B && (eq_F_H || eq_E_H) && eq(P,D) && eq(B,G) ) {
                J = admixS(I,F,C,H,E,B,G,D,A,P,PC,PA,Q,QG,QA,AA,eq_F_H,eq_E_H,El,Fl,colE,colB);
                skiprest = true;}

		}

        // vertical right
		if ( !skiprest && !eq_E_F && none_eq2(F,A,G) ) { // right

			//                                    A B 🅲
			//                                  Q D 🄴 🅵 R
			//                                    G 🅷 I        Zone 4
			//                                    . 🆂
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_F_H &&
				  !eq_E_H && (eq_B_D || eq_E_B) && eq(S,F) && eq(H,C) ) {
                M = admixS(A,D,G,B,E,H,C,F,I,S,SG,SI,R,RC,RI,II,eq_B_D,eq_E_B,El,Dl,colE,colH);
                skiprest = true;}

			//                                    ・ 🅟
			//                                    A 🅑 C
			//                                  Q D 🄴 🅵 R        Zone 2
			//                                    G H 🅸
			if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_B_F &&
				 !eq_E_B && (eq_D_H || eq_E_H) && eq(P,F) && eq(B,I) ) {
                K = admixS(G,D,A,H,E,B,I,F,C,P,PA,PC,R,RI,RC,CC,eq_D_H,eq_E_H,El,Dl,colE,colB);
                skiprest = true;}

		} // vertical right
} // sawslope

// Exit after sawslope formation - legacy approach: skiprest||slopeBAD (also uses slopeOFF (weak) and slopok (strong) but poor effect)
skiprest = skiprest||slope1||slope2||slope3||slope4||El>7.0||Bl>7.0||Dl>7.0||Fl>7.0||Hl>7.0;

/**************************************************
       Concave + Cross shape	（P100）	   
 *************************************************/
//  Use approximate pixels for cross star far end - useful for horizontal line + sawtooth and layered gradient shapes.
//  E.g. glowing text in SFIII: New Generation intro, Japanese houses in SFZ3 Mix, Fatal Fury: Wild Ambition intro

	vec4 colX;		//Define temp X

    if (!skiprest &&
        Bl<El && !eq_E_D && !eq_E_F && eq_E_H && none_eq2(E,A,C) && all_eq2(G,H,I) && sim2(colE,E,S) ) { // TOP

        if (eq_B_D||eq_B_F) { J=admixK(colB,J);    K=J;
            if (eq_D_F) { L=mix(J,L, 0.61804);   M=L; }
        } else { colX = El-Bl < abs(El-Dl) ? colB : colD;  J=admixC(colX,J);
			if (eq_D_F) { K=J;  L=mix(J,L, 0.61804);    M=L; }
			else {colX = El-Bl < abs(El-Fl) ? colB : colF; 		K=admixC(colX,K); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Hl<El && !eq_E_D && !eq_E_F && eq_E_B && none_eq2(E,G,I) && all_eq2(A,B,C) && sim2(colE,E,P) ) { // BOTTOM

        if (eq_D_H||eq_F_H) { L=admixK(colH,L);    M=L;
            if (eq_D_F) { J=mix(L,J, 0.61804);   K=J; }
        } else { colX = El-Hl < abs(El-Dl) ? colH : colD;  L=admixC(colX,L);
			if (eq_D_F) { M=L;  J=mix(L,J, 0.61804);    K=J; }
			else { colX = El-Hl < abs(El-Fl) ? colH : colF;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

   if (!skiprest &&
		Fl<El && !eq_E_B && !eq_E_H && eq_E_D && none_eq2(E,C,I) && all_eq2(A,D,G) && sim2(colE,E,Q) ) { // RIGHT

        if (eq_B_F||eq_F_H) { K=admixK(colF,K);    M=K;
            if (eq_B_H) { J=mix(K,J, 0.61804);   L=J; }
        } else { colX = El-Fl < abs(El-Bl) ? colF : colB;  K=admixC(colX,K);
			if (eq_B_H) { M=K;  J=mix(K,J, 0.61804);    L=J; }
			else { colX = El-Fl < abs(El-Hl) ? colF : colH;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Dl<El && !eq_E_B && !eq_E_H && eq_E_F && none_eq2(E,A,G) && all_eq2(C,F,I) && sim2(colE,E,R) ) { // LEFT

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
        ✕О    Scorpion Type (P99). It looks a lot like the tracking bug from The Matrix. 
        Can match some regularly interleaved pixels.
*/
// Practice Notes: 1. Using approximations for the scorpion's pincers? 
// This is prone to causing graphical glitches.
// Practice Notes: 2. Removing one grid cell from the scorpion's tail captures more patterns.
// Among the four patterns, only the scorpion type is exclusive — 
// once a previous rule has captured it (i.e., it has been matched), this pattern will not appear.

   if (!skiprest && !eq_E_F &&eq_E_D&&eq_B_F&&eq_F_H && all_eq2(E,C,I) && noteq(F,src(srcX+3, srcY)) ) {K=admixK(colF,K); M=K;J=mix(K,J, 0.61804); L=J;skiprest=true;}	// RIGHT
   if (!skiprest && !eq_E_D &&eq_E_F&&eq_B_D&&eq_D_H && all_eq2(E,A,G) && noteq(D,src(srcX-3, srcY)) ) {J=admixK(colD,J); L=J;K=mix(J,K, 0.61804); M=K;skiprest=true;}	// LEFT
   if (!skiprest && !eq_E_H &&eq_E_B&&eq_D_H&&eq_F_H && all_eq2(E,G,I) && noteq(H,src(srcX, srcY+3)) ) {L=admixK(colH,L); M=L;J=mix(L,J, 0.61804); K=J;skiprest=true;}	// BOTTOM
   if (!skiprest && !eq_E_B &&eq_E_H&&eq_B_D&&eq_B_F && all_eq2(E,A,C) && noteq(B,src(srcX, srcY-3)) ) {J=admixK(colB,J); K=J;L=mix(J,L, 0.61804); M=L;}				// TOP

	// final Exit
	writeJKLM;

}
