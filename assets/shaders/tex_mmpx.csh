/* MMPX.glc
   Copyright 2020 Morgan McGuire & Mara Gagiu.
   Provided under the Open Source MIT license https://opensource.org/licenses/MIT

   by Morgan McGuire and Mara Gagiu.
   2025 Enhanced by CrashGG.
*/

// Performs 2x upscaling.

/* If we took an image as input, we could use a sampler to do the clamping. But we decode
   low-bpp texture data directly, so...

   readColoru is a built-in function (implementation depends on rendering engine) that reads unsigned integer format color data from texture/framebuffer.
   Normalization loss: Using readColor (without 'u') reads as floats (vec4), mapping integer range (0-255) to [0.0, 1.0], causing precision loss (255→1.0, 1→0.0039215686...).
   The unpackUnorm4x8 function in MMPX converts uint to vec4 (normalized floats) - this step is lossy.
*/
uint src(int x, int y) {
    return readColoru(uvec2(clamp(x, 0, params.width - 1), clamp(y, 0, params.height - 1)));
}

// RGB visual weight + alpha segmentation
int luma(uint C) {

	if (C==0) return 7340032;

	int alpha = int(C >> 24);
	// CRT era BT.601 standard (255 × (299 + 587 + 114), output range: 0~255000)
    int rgbsum = int(((C >> 16) & 0xFF) *299 + ((C >> 8) & 0xFF) *587 + (C & 0xFF) *114);
	// R G B average
    //int rgbsum = ((C >> 16) & 0xFF) + ((C >> 8) & 0xFF) + (C & 0xFF);

	// 2^20 * n to facilitate subsequent removal of alpha weighting using bit shifts
    int alphafactor = 
        (alpha == 0) ? 7340032 :		// Fully transparent
        (alpha > 217) ? 1048576 :		// Two golden sections		0.854102 ×255≈217.8
        (alpha > 157) ? 2097152 :		// 1 golden section		0.618034 ×255≈157.6
        (alpha > 97) ? 3145728 :		// 1 golden section (short)	0.381966 ×255≈97.4
        (alpha > 37) ? 4194304 : 5242880;	// Two golden sections (short)	0.145898 ×255≈37.2

    return rgbsum + alphafactor;

}

bool checkblack(vec4 col) {
  if (col.r > 0.1 || col.g > 0.078 || col.b > 0.1) return false;

    return true;
}

//bool checkwhite(vec4 col) {
// if (col.r < 0.9 || col.g < 0.92 || col.b < 0.9) return false;
//    return true;
//}

bool sim(vec4 rgbaC1, vec4 rgbaC2, int Lv) {

    vec4 diff = abs(rgbaC1 - rgbaC2);

    // 1. Fast component difference check
    float chDiff = Lv==1 ? 0.09652388 : Lv==2 ? 0.145898 : 0.2527;
    if ( diff.r > chDiff || diff.g > chDiff || diff.b > chDiff || diff.a > 0.145898 ) return false;

    vec3 rgbC1 = rgbaC1.rgb;
    vec3 rgbC2 = rgbaC2.rgb;

    // 2. Quickly filter out identical pixels and very dark/bright pixels after clamping
    vec3 clampCol1 = clamp(rgbC1, vec3(0.078),vec3(0.92));
    vec3 clampCol2 = clamp(rgbC2, vec3(0.078),vec3(0.92));
    vec3 clampdiff = clampCol1 - clampCol2;
    vec3 absclampdiff = abs(clampdiff);
    if ( absclampdiff.r < 0.05572809 && absclampdiff.g < 0.021286 && absclampdiff.b < 0.05572809 ) return true;

	// Cube of Euclidean distance between two points, 2+1, 2 short golden sections
    float dotdist = Lv==1 ? 0.00931686 : Lv==2 ? 0.024391856 : 0.0638587;

    // 3. Fast squared distance check
	float dot_diff = dot(diff.rgb, diff.rgb);
    if (dot_diff > dotdist) return false;

	// 4. Re-check squared distance after adding opposite channels
	float teamA = 0.0;
	float teamB = 0.0;

	if (clampdiff.r > 0.0) teamA += absclampdiff.r; else teamB += absclampdiff.r;
	if (clampdiff.g > 0.0) teamA += absclampdiff.g; else teamB += absclampdiff.g;
	if (clampdiff.b > 0.0) teamA += absclampdiff.b; else teamB += absclampdiff.b;
	float team = min(teamA,teamB);
	// Testing shows at least 3 times the opposite channel value needs to be added. (+1x draws, +2x creates an increasing trend)
	if (dot_diff + team*team*3.0 > dotdist) return false;

	// 5. Check for gray pixels
    float sum1 = rgbC1.r + rgbC1.g + rgbC1.b;
	float avg1 = sum1 * 0.3333333;
    float threshold1 = avg1 * 0.08;

    float sum2 = rgbC2.r + rgbC2.g + rgbC2.b;
	float avg2 = sum2 * 0.3333333;
    float threshold2 = avg2 * 0.08;

    bool Col1isGray = all(lessThan(abs(rgbC1 - vec3(avg1)), vec3(threshold1)));
	bool Col2isGray = all(lessThan(abs(rgbC2 - vec3(avg2)), vec3(threshold2)));

	// Return true if both are gray or both are not gray
	return Col1isGray == Col2isGray;
}

bool sim1(uint C1, uint C2) {
	if (C1 == C2) return true;
    vec4 rgbaC1 = unpackUnorm4x8(C1);
    vec4 rgbaC2 = unpackUnorm4x8(C2);
    return sim(rgbaC1, rgbaC2, 1);
}

bool v4i_sim1(vec4 rgbaE, uint C) {
    vec4 rgbaC = unpackUnorm4x8(C);
    return sim(rgbaE, rgbaC, 1);
}

bool v4i_sim2(vec4 rgbaE, uint C) {
    vec4 rgbaC = unpackUnorm4x8(C);
    return sim(rgbaE, rgbaC, 2);
}

bool v4_sim3(vec4 C1, vec4 C2) {
    return sim(C1, C2, 3);
}

bool mixcheck(vec4 col1, vec4 col2) {
	
	// Cannot mix if one is transparent and the other is not
	bool col1alpha0 = col1.a < 0.003;
	bool col2alpha0 = col2.a < 0.003;
	if (col1alpha0!=col2alpha0) return false;

    vec4 diff = col1 - col2;
    vec4 absdiff = abs(diff);

    // 1. Fast component difference check
    if ( absdiff.r > 0.618034 || absdiff.g > 0.618034 || absdiff.b > 0.618034 || absdiff.a > 0.5 ) return false;
    // Quickly filter out similar pixels (0.4377 divided by 6, square root max can be 0.27)
    if ( absdiff.r < 0.27 && absdiff.g < 0.27 && absdiff.b < 0.27 ) return true;

    // 3. Fast squared distance check
	float dot_diff = dot(diff.rgb, diff.rgb);
    if (dot_diff > 0.4377) return false;	// One short golden section of Euclidean distance

	// 4. Re-check squared distance after adding opposite channels
	float teamA = 0.0;
	float teamB = 0.0;

	if (diff.r > 0.0) teamA += absdiff.r; else teamB += absdiff.r;
	if (diff.g > 0.0) teamA += absdiff.g; else teamB += absdiff.g;
	if (diff.b > 0.0) teamA += absdiff.b; else teamB += absdiff.b;
	float team = min(teamA,teamB);
	// Testing shows at least 3 times the opposite channel value needs to be added. (+1x draws, +2x creates a positive trend)
	if (dot_diff + team*team*3.0 > 0.4377) return false;

    return true;
}

bool eq(uint C1, uint C2){
    if (C1 == C2) return true;

	int rgbC1 = int(C1 & 0x00FFFFFF);
	int rgbC2 = int(C2 & 0x00FFFFFF);

	if (rgbC1 != rgbC2) return false;

	int alphaC1 = int(C1 >> 24);
	int alphaC2 = int(C2 >> 24);

	return abs(alphaC1-alphaC2)<38;
}

bool noteq(uint B, uint A0){
    return !eq(B,A0);
}
/*
bool fullnoteq(uint B, uint A0){
    return B != A0;
}

bool rgb_eq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);
    if (diff.r > 0.004 || diff.g > 0.004 || diff.b > 0.004) return false;
    return true;
}
*/
bool v4_noteq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);

    if (diff.r > 0.004 || diff.g > 0.004 || diff.b > 0.004 || diff.a > 0.021286) return true;

    return false;
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

///////////////////////     Test Colors     ///////////////////////
//const  vec4 testcolor = vec4(1.0, 0.0, 1.0, 1.0);  // Magenta (Opaque)
//const  vec4 testcolor2 = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan (Opaque)
//const  vec4 testcolor3 = vec4(1.0, 1.0, 0.0, 1.0);  // Yellow (Opaque)
//const  vec4 testcolor4 = vec4(1.0, 1.0, 1.0, 1.0);  // White (Opaque)

//   "Concave + Cross" type Weak mixing (Weak Mix / None)
vec4 admixC(uint X, vec4 rgbaE) {

    vec4 rgbaX = unpackUnorm4x8(X);

	// Transparent pixels already filtered in main logic

	bool mixok = mixcheck(rgbaX, rgbaE);

	return mixok ? mix(rgbaX, rgbaE, 0.618034) : rgbaE;
}

// K type Forced weak mixing (Weaker Mix / Even Weaker)
vec4 admixK(uint X, vec4 rgbaE) {

    vec4 rgbaX = unpackUnorm4x8(X);

	// Transparent pixels already filtered in main logic

	bool mixok = mixcheck(rgbaX, rgbaE);

	return mixok ? mix(rgbaX, rgbaE, 0.618034) : mix(rgbaX, rgbaE, 0.8541);
}

vec4 admixL(vec4 rgbaX, vec4 rgbaE, uint S) {

    // If E is fully transparent, copy target X
    //if (rgbaE.a < 0.01) return rgbaX;
 
    // If X is fully transparent, return original value E
    //if (rgbaX.a < 0.01) return rgbaE;


    vec4 rgbaS = unpackUnorm4x8(S);
	// If target X and reference S(sample) are different, it means it has been mixed once already, then just copy the target.
	if (v4_noteq(rgbaX, rgbaS)) return rgbaX;

	bool mixok = mixcheck(rgbaX, rgbaE);

    return mixok ? mix(rgbaX, rgbaE, 0.381966) : rgbaX;
}

/********************************************************************************************************************************************
 *              												main slope + X cross-processing mechanism					                *
 *******************************************************************************************************************************************/
vec4 admixX(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint P, uint PA, uint PC, uint Q, uint QA, uint QG, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint AA, uint CC, uint GG, int El, int Bl, int Dl, int Fl, int Hl, vec4 rgbaE) {

    // Pre-define 3 types of special exits
    const vec4 slopeBAD = vec4(2.0);
    const vec4 theEXIT = vec4(8.0);
    const vec4 slopEND = vec4(33.0);

	//pre-cal
	bool eq_B_C = eq(B, C);
	bool eq_D_G = eq(D, G);

    // Exit if sandwiched by straight walls on both sides
    if (eq_B_C && eq_D_G) return slopeBAD;

    vec4 rgbaB = unpackUnorm4x8(B);
	vec4 rgbaD;
	vec4 rgbaX;
	// Remove alpha channel weighting
	int rgbBl = Bl & 0xFFFFF;
	int rgbDl = Dl & 0xFFFFF;
	int rgbEl = El & 0xFFFFF;
	int rgbFl = Fl & 0xFFFFF;
	int rgbHl = Hl & 0xFFFFF;

	bool eq_B_D = eq(B,D);
	if (eq_B_D) {

        rgbaX = rgbaB;
		if ( B != D ) {
			float alphaD = float(D >> 24) * 0.0039215686;		// 1/255
			rgbaX.a = min(rgbaB.a , alphaD);
			}
    } else {
        // Exit if E-A equality does not meet preset logic
        if (eq(E,A)) return slopeBAD;

        // If D and B are not equal, and the difference between them is greater than the difference between either one and the center point E, exit.
        int diffBD = abs(rgbBl-rgbDl);
        int diffEB = abs(rgbEl-rgbBl);
        int diffED = abs(rgbEl-rgbDl);
        if (diffBD > diffEB || diffBD > diffED) return slopeBAD;

		// Generate X using the intermediate value of B-D
		rgbaD = unpackUnorm4x8(D);
		rgbaX = vec4( mix(rgbaB.rgb,rgbaD.rgb,0.5), min(rgbaB.a,rgbaD.a) );
    }


	// Avoid single-pixel font edges being squeezed by black background on both sides (font and background brightness difference usually exceeds 0.5)
	// Note: If BD are not equal, cannot use average for judgment, both must satisfy the black condition.
	bool Xisblack = eq_B_D ? checkblack(rgbaB) : checkblack(rgbaB)&&checkblack(rgbaD);
	if ( Xisblack && rgbEl > 127500 ) {	// 127500/255000 = 0.5/1
        // Use Fl Hl to save cost
		if ( rgbFl<19890 || rgbHl<19890 ) return theEXIT;		// 19890/255000 = 0.078/1
	}

	//Pre-declare
	bool eq_A_B;	bool eq_A_D;	bool eq_A_P;	bool eq_A_Q;
	bool eq_B_P;    bool eq_B_PA;   bool eq_B_PC;
	bool eq_D_Q;    bool eq_D_QA;   bool eq_D_QG;
    bool eq_E_F;    bool eq_E_H;    bool eq_E_I;    bool En3;	bool En4square;
	bool B_slope;	bool B_tower;	bool B_wall;
    bool D_slope;	bool D_tower;	bool D_wall;
    bool comboA3;	bool mixok;

// B != D
if (!eq_B_D){

	eq_A_B = eq(A,B);
	if ( !Xisblack && eq_A_B && eq_D_G && eq(B,P) ) return slopeBAD;

	eq_A_D = eq(A,D);
	if ( !Xisblack && eq_A_D && eq_B_C && eq(D,Q) ) return slopeBAD;


    // B D not connected to anything? Not applicable here (Can eliminate some artifacts, but also loses some shapes, especially in non-native pixel art, e.g., character portraits in Double Dragon)

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	comboA3 = eq_A_P && eq_A_Q;
	mixok = mixcheck(rgbaX,rgbaE);

	// A-side three in a row, high priority
    if (comboA3) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;

    // Official original rule
    if ( eq(E,C) || eq(E,G) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;
    // Enhanced original rule Practice: Beneficial for non-native pixel art, but harmful for native pixel art (JoJo's wall and clock)
    if ( !eq_D_G&&eq(E,QG)&&v4i_sim2(rgbaE,G) || !eq_B_C&&eq(E,PC)&&v4i_sim2(rgbaE,C) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;

    eq_E_F = eq(E, F);
    eq_E_H = eq(E, H);
    En3 = eq_E_F && eq_E_H;


    if (!Xisblack){
        if ( eq_A_B&&eq_B_C || eq_A_D&&eq_D_G ) return slopeBAD;
    }
    if ( eq(F,H) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;
    // Exclude "two-cell single-side wall" situations, merged with next rule
    //if (eq_A_B||eq_A_D) return slopeBAD;
    // The rest are single pixels without logic but similar within sim2 range, so use weak mixing??? Just give up!
    return slopeBAD;
} // B != D

	//pre-cal B == D
    bool eq_E_A = eq(E,A);
    bool eq_E_C = eq(E,C);
    bool eq_E_G = eq(E,G);
	bool sim_EC = eq_E_C || v4i_sim2(rgbaE,C);
	bool sim_EG = eq_E_G || v4i_sim2(rgbaE,G);

	bool ThickBorder;

// Enhanced original main rule using sim
if ( (sim_EC || sim_EG) && !eq_E_A ){

/* Approach:
    1. Handle continuous boundary shapes without mixing
    2. Special handling for long slopes
    3. Original rules
    4. Handle E-zone inlines like En4 En3 F-H, leaving single sticks and single pixels
    5. Handle L-shaped inward and outward single sticks
    5. Normal fallback
*/

	eq_A_B = eq(A,B);
	eq_B_P = eq(B,P);
    eq_B_PC = eq(B,PC);
	eq_D_Q = eq(D,Q);
    eq_D_QG = eq(D,QG);
    eq_B_PA = eq(B,PA);
    eq_D_QA = eq(D,QA);
	B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;

    // step1:
    // B + D + their respective extension shapes
    // Note: Only used for judging X without mixing, different from the "rule capture return" logic in the final section.

    if ( (B_slope||B_tower) && (D_slope||D_tower) && !eq_A_B) return rgbaX +slopEND;


	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);
	comboA3 = eq_A_P && eq_A_Q;
	mixok = mixcheck(rgbaX,rgbaE);
    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));
	if (ThickBorder && !Xisblack) mixok=false;

    // A-side three in a row
    if (comboA3) {
        if (!eq_A_B) return rgbaX + slopEND;
        else mixok=false;
    }

    // XE_messL B-D-E L-shape highly similar with sim2   WIP
    // bool XE_messL = (eq_B_C && !sim_EG || eq_D_G && !sim_EC) ;

    eq_E_F = eq(E, F);
	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Long slope clear long slope (Not thick solid edge. Strong trend!)
    // Long slope case is slightly special, judged separately
	if ( B_wall && D_tower ) {
        if (eq_E_G || sim_EG&&eq(E,QG) ) {   // Original rule + Original enhancement
            if (eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966): rgbaX;    // Has thickness
            return rgbaX;                                           // Hollow
        }
        // Clear Z-shaped snake, exclude XE_messL ???
        //if (eq_A_B  && !XE_messL) return slopeBAD;                    // WIP
        if (eq_A_B) return slopeBAD;
        //  2-cell with subsequent long slope
        if (eq_E_F ) return rgbaX;
        //  1-cell without subsequent long slope
        return rgbaX +slopEND;
    }

    eq_E_H = eq(E, H);
	D_wall = eq_D_G && !eq_D_QG&& !eq_D_Q;

	if ( B_tower && D_wall ) {
        if (eq_E_C || sim_EC&&eq(E,PC) ) {   // Original rule + Original enhancement
            if (eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966): rgbaX;    // Has thickness
            return rgbaX;                                           // Hollow
        }
        if (eq_A_B) return slopeBAD;
        //  2-cell with subsequent long slope
        if (eq_E_H ) return rgbaX;
        //  1-cell without subsequent long slope
        return rgbaX +slopEND;
    }


    // Official original rule (placed after the special shapes above, special shapes specify no mixing!)
    if (eq_E_C || eq_E_G) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;

    // Original rule enhancement 1
    if (sim_EG&&!eq_D_G&&eq(E,QG) || sim_EC&&!eq_B_C&&eq(E,PC)) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

    // Original rule enhancement 2
    if (sim_EC && sim_EG) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;


    // F-H inline trend (Skip En4 En3)
    if ( eq(F,H) )  return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

	// Relaxed rule finally cleans up two long slopes (non-clear shapes)
    // Practice: This section handles long slopes and is different from F-H. Default is draw, unless it's a cube.
	if ( eq_B_C && eq_D_Q) {
        // Double cube exit
		if (eq_B_P && eq_B_PC && eq_A_B && eq_D_QA && !eq_D_QG && eq_E_F && eq(H,I) ) return theEXIT;

		return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
	}

	if ( eq_D_G && eq_B_P) {
        // Double cube exit
		if (eq_D_Q && eq_D_QG && eq_A_B && eq_B_PA && !eq_B_PC && eq_E_H && eq(F,I) ) return theEXIT;

		return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
	}

    eq_E_I = eq(E, I);

    // L-shaped corner (clear) inward single stick (parallel), exit
    if (eq_A_B && !ThickBorder && !eq_E_I ) {
	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}

    // Skip next step and return early if colors are similar
    if (mixok) return mix(rgbaX,rgbaE,0.381966)+slopEND;

    // L-shaped corner (hollow) outward single stick (any direction), exit (solves font edge issues)
    if ( !eq_A_B && (eq_E_F||eq_E_H) && !eq_E_I) {
        if (B_tower && !eq_D_Q && !eq_D_QG) return theEXIT;
        if (D_tower && !eq_B_P && !eq_B_PC) return theEXIT;
    }

    // Fallback processing
    return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

} // sim2 base


/*===================================================
                    E - A Cross
  ===============================================zz */

if (eq_E_A) {

	// When judging crosses ✕, have a concept of "area" and "trend". Conditions need tightening for different areas.

    // B D not connected to anything exit? Not needed here!!!

    // X fully transparent exit (slope transparent B D cutting the crossing E A doesn't make logical sense)
	bool X_alpha0 = rgbaX.a < 0.003;
	if (X_alpha0) return slopeBAD;

    eq_E_F = eq(E, F);
    eq_E_H = eq(E, H);
    eq_E_I = eq(E, I);
    En3 = eq_E_F&&eq_E_H;
    En4square = En3 && eq_E_I;

	// Special shape: Square
	if ( En4square ) {
        if( noteq(G,H) && noteq(C,F)                      //  Independent clear 4-cell square / 6-cell rectangle (both sides satisfy simultaneously)
		&& (eq(H,S) == eq(I,SI) && eq(F,R) == eq(I,RI)) ) return theEXIT;
        //else return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;    Note: Cannot return directly. Need to enter the chessboard decision rule because adjacent B, D areas might also form bubbles with square structures.
    }

    //  Special shape: Dithering pattern
	//	Practice 1: !eq_E_F, !eq_E_H Not using !sim, because it loses some shapes.
    //  Practice 2: Force mixing, otherwise shapes are lost. (Gouketsuji horse, WWF Super Wrestlemania 2)

	bool Eisblack = checkblack(rgbaE);
	mixok = mixcheck(rgbaX,rgbaE);

	//  1. Dithering center
    if ( eq_E_C && eq_E_G && eq_E_I && !eq_E_F && !eq_E_H ) {

		// Exit if center E is black (KOF96 power gauge, Punisher's belt) Avoid mixing with too high contrast.
		if (Eisblack) return theEXIT;
		// Example of dithering created by changing transparency against same-color background: Black Rock Shooter
		//if ( rgb_eq(rgbaX,rgbaE) ) return slopeBAD;

		return mixok ? mix(rgbaX, rgbaE, 0.381966)+slopEND : mix(rgbaX, rgbaE, 0.618034)+slopEND;
	}

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
    eq_B_PA = eq(B,PA);

	//  2. Dithering edge
    if ( eq_A_P && eq_A_Q && eq(A,AA) && noteq(A,PA) && noteq(A,QA) )  {
		if (Eisblack) return theEXIT;

        // Layered progressive edge, use strong mixing
		if ( !eq_B_PA && eq(PA,QA) ) return mixok ? mix(rgbaX, rgbaE, 0.381966)+slopEND : mix(rgbaX, rgbaE, 0.618034)+slopEND;
        // Remaining 1. Perfect cross, must be dithering edge, use weak mixing.
        // Remaining 2. Fallback weak mixing
		return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;
		// Note: No need to specify health bar border separately.
	}

    eq_D_QA = eq(D,QA);
    eq_D_QG = eq(D,QG);
    eq_B_PC = eq(B,PC);

  //   3. Half dithering Usually shadow expression on contour edges, use weak mixing
	if ( eq_E_C && eq_E_G && eq_A_P && eq_A_Q &&
		(eq_B_PC || eq_D_QG) &&
		 eq_D_QA && eq_B_PA) {

        return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;
		}

    //   4. Quarter dithering, prone to ugly little finger effect

	if ( eq_E_C && eq_E_G && eq_A_P
		 && eq_B_PA &&eq_D_QA && eq_D_QG
		 && eq_E_H
		) return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;

	if ( eq_E_C && eq_E_G && eq_A_Q
		 && eq_B_PA &&eq_D_QA && eq_B_PC
		 && eq_E_F
		) return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;


    // E-side three in a row (Must be after dithering)
    if ( eq_E_C && eq_E_G ) return rgbaX+slopEND;

    comboA3 = eq_A_P && eq_A_Q;

    // A-side three in a row (Must be after dithering) Because E-A are the same, prefer mixing over direct copy.
	if (comboA3) return mixok ? mix(rgbaX, rgbaE, 0.381966)+slopEND : rgbaX+slopEND;


    // B-D  part of long slope
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
        // 1 stick
        if (eq_E_F ||eq_E_H) return theEXIT;
    }

    if ( eq(F,H) ) {
		scoreE += 1;
        if ( scoreZ==0 && B_wall && (eq(F,R) || eq(G,H) || eq(F,I)) ) scoreZ = 1;
        if ( scoreZ==0 && D_wall && (eq(C,F) || eq(H,S) || eq(F,I)) ) scoreZ = 1;
    }

	bool Bn3 = eq_B_P&&eq_B_C;
	bool Dn3 = eq_D_G&&eq_D_Q;

//	B Zone
	scoreB -= int(Bn3);
	scoreB -= int(eq(C,P));
    if (scoreB < 0) scoreZ = 0;

    if (eq_B_PA) {
		scoreB -= 1;
		scoreB -= int(eq(P,PA));
	}

//        D Zone
	scoreD -= int(Dn3);
	scoreD -= int(eq(G,Q));
    if (scoreD < 0) scoreZ = 0;

    if (eq_D_QA) {
		scoreD -= 1;
		scoreD -= int(eq(Q,QA));
	}

    int scoreFinal = scoreE + scoreB + scoreD + scoreZ ;

    if (scoreE >= 1 && scoreB >= 0 && scoreD >=0) scoreFinal += 1;

    if (scoreFinal >= 2) return rgbaX;

    if (scoreFinal == 1) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;

    // Final supplement: Total score zero, and B, D zones have no deductions, forming a long slope shape.
    if (scoreB >= 0 && scoreD >=0) {
        if (B_wall&&D_tower) return rgbaX;
        if (B_tower&&D_wall) return rgbaX;
    }

    return slopeBAD;

}	// eq_E_A



/*=========================================================
                    F - H / -B - D- Extension New Rules
  ==================================================== zz */

// This section is different from the sim section. The center point and related En4square and BD logic naturally have a wall separating them.
// Judgment rules are different from the sim side.

	eq_B_P = eq(B, P);
    eq_B_PC = eq(B, PC);
	eq_D_Q = eq(D, Q);
    eq_D_QG = eq(D, QG);

    // B D not connected to anything exit (Practice: This branch section needs it)
    if ( !eq_B_C && !eq_B_P && !eq_B_PC && !eq_D_G && !eq_D_Q && !eq_D_QG ) return slopeBAD;

	mixok = mixcheck(rgbaX,rgbaE);
    eq_E_I = eq(E, I);

	// Exit if center point E is a high-contrast pixel, visually very different from surrounding pixels.
	int E_lumDiff = rgbEl>234600 ? 37204 : 97401;	// 234600/255000=0.92/1 37204=0.145898/1 97401=0.382/1
	bool E_ally = mixok || abs(rgbEl-rgbFl)<E_lumDiff || abs(rgbEl-rgbHl)<E_lumDiff || eq_E_I;
    if (!E_ally) return slopeBAD;

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
        // B + D + their respective extension shapes
        // Practice 1: One side is a definite clear shape, the other side can be looser.
        // Practice 2: "厂" shaped edge flattens the outside but not the inside, (can be a tower but not a wall)
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return rgbaX +slopEND;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return rgbaX +slopEND;

        // A-side three in a row, high priority
        if (comboA3) return rgbaX + slopEND;

        // combo 2x2 as supplement to the previous one
        if ( B_slope && eq_A_P ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;
        if ( D_slope && eq_A_Q ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;
    }

    eq_E_F = eq(E, F);
	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Long slope clear long slope (Not solid edge. Strong trend!)
    // Long slope case is slightly special, judged separately
	if ( B_wall && D_tower ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_F ) return rgbaX;  //wip: Test direct rgbaX no mixing
        return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
    }

    eq_E_H = eq(E, H);
	D_wall = eq_D_G && !eq_D_QG&& !eq_D_Q;

	if ( B_tower && D_wall ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_H ) return rgbaX;
        return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
    }


    En3 = eq_E_F&&eq_E_H;
    En4square = En3 && eq_E_I;
    bool sim_X_E = v4_sim3(rgbaX,rgbaE);
    bool eq_G_H = eq(G, H);
    bool eq_C_F = eq(C, F);
    bool eq_H_S = eq(H, S);
    bool eq_F_R = eq(F, R);

    // Wall enclosed 4-cell rectangle
	if ( En4square ) {  // This square detection needs to be placed after the previous rule
		if (sim_X_E) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        // Solid L enclosed exit (Fix some font edge corners, and building corners that shouldn't be rounded (Mega Man 7))
        if ( (eq_B_C || eq_D_G) && eq_A_B ) return theEXIT;
        //if (eq_H_S && eq_F_R) return theEXIT; // Both sides extend simultaneously
        //  L enclosed (hollow corner) / High contrast (greater than 0.5) Independent clear 4-cell square / 6-cell rectangle (Note judging both edges of the rectangle)
        if ( ( eq_B_C&&!eq_G_H || eq_D_G&&!eq_C_F || !eq_G_H&&!eq_C_F&&abs(rgbEl-rgbBl)>127500) && (eq_H_S == eq(I, SI) && eq_F_R == eq(I, RI)) ) return theEXIT;

        return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
    }

    // Wall enclosed triangle
 	if ( En3 ) {
		if (sim_X_E) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        if (eq_H_S && eq_F_R) return theEXIT; // Both sides extend simultaneously (building edges)
       // Inner bend
        if (eq_B_C || eq_D_G) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		// Return directly if has thickness (Z-shaped snake can flatten the outer bend below)
        if (eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        // Outer bend
        if (eq_B_P || eq_D_Q) return theEXIT;

        return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        // The last two rules are based on experience. Principle: Connect L inner bend, do not connect L outer bend (Double Dragon Jimmy's eyebrow)
	}

    // F - H
	// Principle: Connect L inner bend, do not connect L outer bend
	if ( eq(F,H) ) {
    	if (sim_X_E) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        // Solid L enclosed, avoid bilateral symmetric full enclosure squeezing single pixel
		if ( eq_B_C && eq_A_B && (eq_G_H||!eq_F_R) &&eq(F, I) ) return slopeBAD;
		if ( eq_D_G && eq_A_B && (eq_C_F||!eq_H_S) &&eq(F, I) ) return slopeBAD;

		//Inner bend
        if (eq_B_C && (eq_F_R||eq_G_H||eq(F, I))) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        if (eq_D_G && (eq_C_F||eq_H_S||eq(F, I))) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		// E-I F-H cross破坏 trend
		if (eq_E_I) return slopeBAD;
        // Z-shaped outer
		if (eq_B_P && eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        if (eq_D_Q && eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		// Outer bend unless the opposite side forms a long L-shaped trend
		if (eq_B_P && (eq_C_F&&eq_H_S)) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        if (eq_D_Q && (eq_F_R&&eq_G_H)) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

        return slopeBAD;
	}


	// Relaxed rule finally cleans up two long slopes (non-clear shapes)
    // Note: This section handles long slopes differently from the sim2 section. Solid corners exit.
	if ( eq_B_C && eq_D_Q || eq_D_G && eq_B_P) {
        // Note: Must clear eq_A_B first, otherwise edge single pixels will be chipped away (Everlasting Secret)
		if (eq_A_B) return theEXIT;
		return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
	}


    //	A-side three in a row, MUST NOT be used separately without !eq_A_B !!!!!!!!!!!!
    //  if (comboA3) return rgbaX+slopEND;


	// Use B + D bidirectional extension to capture once more, priority higher than L inner/outer single stick below.
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return rgbaX +slopEND;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return rgbaX +slopEND;


    // L-shaped corner (clear) inward single stick, exit
    if (eq_A_B && !ThickBorder && !eq_E_I ) {

	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}


    // L-shaped corner (hollow) outward single stick, exit (Connect inside corner, not outside corner)
	// Practice: Can prevent font edges from being shaved (Captain Commando)
	if ( (B_tower || D_tower) && (eq_E_F||eq_E_H) && !eq_A_B && !eq_E_I) return theEXIT;

    // Final B or D individual extension judgment

    // Farthest distance a slope can utilize
    if ( B_slope && eq(PC,CC) && noteq(PC,RC) && !eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
    if ( D_slope && eq(QG,GG) && noteq(QG,SG) && !eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

    // With X E being close, the slope can have one less judgment point, and some restrictions internally.
  	if ( mixok && !eq_A_B ) {
        if ( B_slope && (!eq_C_F||eq(F,RC)) ) return mix(rgbaX,rgbaE,0.381966)+slopEND;
        if ( D_slope && (!eq_G_H||eq(H,SG)) ) return mix(rgbaX,rgbaE,0.381966)+slopEND;
    }

    // Tower type relax eq_A_B to form Z-shaped snake, and one side can be wall, slope. But the other side cannot be tower and wall (naturally forbidden)
    if ( eq_B_P && !eq_B_PA && !eq_D_Q && eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
    if ( eq_D_Q && !eq_D_QA && !eq_B_P && eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

	return theEXIT;

}	// admixX

vec4 admixS(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint II, bool eq_B_D, bool eq_E_D, int El, int Bl, vec4 rgbaE) {

			//                                    A B C .
			//                                  Q D 🄴 🅵 🆁       Zone 4
			//					                  🅶 🅷 I
			//					                    S
	int rgbBl = Bl & 0xFFFFF;
	int rgbEl = El & 0xFFFFF;

    if (any_eq2(F,C,I)) return rgbaE;

    if (eq(R, RI) && noteq(R,I)) return rgbaE;
    if (eq(H, S) && noteq(H,I)) return rgbaE;

    if ( eq(R, RC) || eq(G,SG) ) return rgbaE;
	// 97401/255000 = 0.382/1
    if ( ( eq_B_D&&eq(B,C)&&abs(rgbEl-rgbBl)<97401 || eq_E_D&&v4i_sim2(rgbaE,C) ) &&
    (any_eq3(I,H,S,RI) || eq(SI,RI)&&noteq(I,II)) ) return unpackUnorm4x8(F);

    return rgbaE;
}

void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

    uint D = src(srcX - 1, srcY + 0), E = src(srcX, srcY + 0), F = src(srcX + 1, srcY + 0);

    // Predefine default output for easy conditional return later
    ivec2 destXY = ivec2(xy) * 2;
    vec4 rgbaE = unpackUnorm4x8(E);
    vec4 J = rgbaE, K = rgbaE, L = rgbaE, M = rgbaE;

	bool eq_E_D = eq(E,D);
	bool eq_E_F = eq(E,F);

// Skip same-color horizontal 3x1 block
if ( eq_E_D && eq_E_F ) {
	writeColorf(destXY, J);
	writeColorf(destXY + ivec2(1, 0), K);
	writeColorf(destXY + ivec2(0, 1), L);
	writeColorf(destXY + ivec2(1, 1), M);
	return;
}

    uint B = src(srcX, srcY - 1), H = src(srcX, srcY + 1);
	bool eq_E_B = eq(E,B);
	bool eq_E_H = eq(E,H);
 
// Skip same-color vertical 3x1 block
if ( eq_E_B && eq_E_H ) {
	writeColorf(destXY, J);
	writeColorf(destXY + ivec2(1, 0), K);
	writeColorf(destXY + ivec2(0, 1), L);
	writeColorf(destXY + ivec2(1, 1), M);
	return;
}

   bool eq_B_H = eq(B,H);
   bool eq_D_F = eq(D,F);

// Skip mirrored block surrounding center point
if ( eq_B_H && eq_D_F ) {
	writeColorf(destXY, J);
	writeColorf(destXY + ivec2(1, 0), K);
	writeColorf(destXY + ivec2(0, 1), L);
	writeColorf(destXY + ivec2(1, 1), M);
	return;
}

    uint A = src(srcX - 1, srcY - 1), C = src(srcX + 1, srcY - 1);
    uint G = src(srcX - 1, srcY + 1), I = src(srcX + 1, srcY + 1);

	uint P = src(srcX, srcY - 2), S = src(srcX, srcY + 2);
	uint Q = src(srcX - 2, srcY), R = src(srcX + 2, srcY);

	int Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);

// Continue expanding to 5x5
   uint PA = src(srcX-1, srcY-2);
   uint PC = src(srcX+1, srcY-2);
   uint QA = src(srcX-2, srcY-1);
   uint QG = src(srcX-2, srcY+1); //             AA  PA  [P]  PC  CC
   uint RC = src(srcX+2, srcY-1); //                ┌───┬───┬───┐
   uint RI = src(srcX+2, srcY+1); //             QA │ A │ B │ C │ RC
   uint SG = src(srcX-1, srcY+2); //                ├───┼───┼───┤
   uint SI = src(srcX+1, srcY+2); //            [Q] │ D │ E │ F │ [R]
   uint AA = src(srcX-2, srcY-2); //                ├───┼───┼───┤
   uint CC = src(srcX+2, srcY-2); //             QG │ G │ H │ I │ RI
   uint GG = src(srcX-2, srcY+2); //                └───┴───┴───┘
   uint II = src(srcX+2, srcY+2); //             GG   SG [S]  SI  II


// 1:1 slope rules (P95)

   // main slops
   bool eq_B_D = eq(B,D);
   bool eq_B_F = eq(B,F);
   bool eq_D_H = eq(D,H);
   bool eq_F_H = eq(F,H);

    // Any mirrored block surrounding center point
    bool oppoPix =  eq_B_H || eq_D_F;
	// Flag indicating entry into adminxX function if caught by 1:1 slope rule
    bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;
	// Standard pixel that successfully passed the 1:1 slope rule and returned normally
    bool slope1ok = false;  bool slope2ok = false;  bool slope3ok = false;  bool slope4ok = false;
	// slopeBAD entered adminxX, but (at least one of JKLM) returned E point
    bool slopeBAD = false;  bool slopEND = false;

    // B - D
	if ( (!eq_E_B&&!eq_E_D&&!oppoPix) && (!eq_D_H&&!eq_B_F) && (El>=Dl&&El>=Bl || eq(E,A)) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || noteq(E,P) || noteq(E,Q) ) && ( eq_B_D&&(eq_F_H||eq(E,A)||eq(B,PC)||eq(D,QG)) || sim1(B,D)&&(v4i_sim2(rgbaE,C)||v4i_sim2(rgbaE,G)) ) ) {
		J=admixX(A,B,C,D,E,F,G,H,I,P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG, El,Bl,Dl,Fl,Hl,rgbaE);
		slope1 = true;
		if (J.b > 1.0 ) {
            if (J.b > 30.0 ) {J=J-33.0; slopEND=true;}
			if (J.b == 8.0 ) {
					J=rgbaE;
					writeColorf(destXY, J);
					writeColorf(destXY + ivec2(1, 0), K);
					writeColorf(destXY + ivec2(0, 1), L);
					writeColorf(destXY + ivec2(1, 1), M);
					return;
			}
			if (J.b == 2.0 ) {slopeBAD=true; J=rgbaE;}
		} else slope1ok = true;
	}
    // B - F
	if ( !slope1 && (!eq_E_B&&!eq_E_F&&!oppoPix) && (!eq_B_D&&!eq_F_H) && (El>=Bl&&El>=Fl || eq(E,C)) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || noteq(E,P) || noteq(E,R) ) && ( eq_B_F&&(eq_D_H||eq(E,C)||eq(B,PA)||eq(F,RI)) || sim1(B,F)&&(v4i_sim2(rgbaE,A)||v4i_sim2(rgbaE,I)) ) )  {
		K=admixX(C,F,I,B,E,H,A,D,G,R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA, El,Fl,Bl,Hl,Dl,rgbaE);
		slope2 = true;
		if (K.b > 1.0 ) {
            if (K.b > 30.0 ) {K=K-33.0; slopEND=true;}
			if (K.b == 8.0 ) {
					K=rgbaE;
					writeColorf(destXY, J);
					writeColorf(destXY + ivec2(1, 0), K);
					writeColorf(destXY + ivec2(0, 1), L);
					writeColorf(destXY + ivec2(1, 1), M);
					return;
			}
			if (K.b == 2.0 ) {slopeBAD=true; K=rgbaE;}
		} else {slope2ok = true;}
	}
    // D - H
	if ( !slope1 && (!eq_E_D&&!eq_E_H&&!oppoPix) && (!eq_F_H&&!eq_B_D) && (El>=Hl&&El>=Dl || eq(E,G))  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || noteq(E,S) || noteq(E,Q))  &&  ( eq_D_H&&(eq_B_F||eq(E,G)||eq(D,QA)||eq(H,SI)) || sim1(D,H) && (v4i_sim2(rgbaE,A)||v4i_sim2(rgbaE,I)) ) )  {
		L=admixX(G,D,A,H,E,B,I,F,C,Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II, El,Dl,Hl,Bl,Fl,rgbaE);
		slope3 = true;
		if (L.b > 1.0 ) {
            if (L.b > 30.0 ) {L=L-33.0; slopEND=true;}
			if (L.b == 8.0 ) {
					L=rgbaE;
					writeColorf(destXY, J);
					writeColorf(destXY + ivec2(1, 0), K);
					writeColorf(destXY + ivec2(0, 1), L);
					writeColorf(destXY + ivec2(1, 1), M);
					return;
			}
			if (L.b == 2.0 ) {slopeBAD=true; L=rgbaE;}
		} else {slope3ok = true;}
	}
    // F - H
	if ( !slope2 && !slope3 && (!eq_E_F&&!eq_E_H&&!oppoPix) && (!eq_B_F&&!eq_D_H) && (El>=Fl&&El>=Hl || eq(E,I))  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || noteq(E,R) || noteq(E,S))  &&  ( eq_F_H&&(eq_B_D||eq(F,RC)||eq(H,SG)||eq(E,I)) || sim1(F,H) && (v4i_sim2(rgbaE,C)||v4i_sim2(rgbaE,G)) ) )  {
		M=admixX(I,H,G,F,E,D,C,B,A,S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC, El,Hl,Fl,Dl,Bl,rgbaE);
		slope4 = true;
		if (M.b > 1.0 ) {
            if (M.b > 30.0 ) {M=M-33.0; slopEND=true;}
			if (M.b == 8.0 ) {
					M=rgbaE;
					writeColorf(destXY, J);
					writeColorf(destXY + ivec2(1, 0), K);
					writeColorf(destXY + ivec2(0, 1), L);
					writeColorf(destXY + ivec2(1, 1), M);
					return;
			}
			if (M.b == 2.0 ) {slopeBAD=true; M=rgbaE;}
		} else {slope4ok = true;}
	}


if (slopEND) {
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
	return;
}


//  long gentle 2:1 slope  (P100)

	bool longslope = false;

    if (slope4ok && eq_F_H) { //zone4 long slope
        // Original rule extension 1. adminxL third parameter passes adjacent pixel for comparison, ensuring no double mixing.
        // Original rule extension 2. Cannot have L shape again within the two-pixel gap on the opposite side, unless forming a wall.
        if (eq(G,H) && eq(F,R) && noteq(R, RC) && (noteq(Q,G)||eq(Q, QA))) {L=admixL(M,L,H); longslope = true;}
        // virtical
		if (eq(C,F) && eq(H,S) && noteq(S, SG) && (noteq(P,C)||eq(P, PA))) {K=admixL(M,K,F); longslope = true;}
    }


    if (slope3ok && eq_D_H) { //zone3 long slope
        // horizontal
        if (eq(D,Q) && eq(H,I) && noteq(Q, QA) && (noteq(R,I)||eq(R, RC))) {M=admixL(L,M,H); longslope = true;}
        // virtical
		if (eq(A,D) && eq(H,S) && noteq(S, SI) && (noteq(A,P)||eq(P, PC))) {J=admixL(L,J,D); longslope = true;}
    }

    if (slope2ok && eq_B_F) { //zone2 long slope
        // horizontal
        if (eq(A,B) && eq(F,R) && noteq(R, RI) && (noteq(A,Q)||eq(Q, QG))) {J=admixL(K,J,B); longslope = true;}
        // virtical
		if (eq(F,I) && eq(B,P) && noteq(P, PA) && (noteq(I,S)||eq(S, SG))) {M=admixL(K,M,F); longslope = true;}
    }

    if (slope1ok && eq_B_D) { //zone1 long slope
        // horizontal
        if (eq(B,C) && eq(D,Q) && noteq(Q, QG) && (noteq(C,R)||eq(R, RI))) {K=admixL(J,K,B); longslope = true;}
        // virtical
		if (eq(D,G) && eq(B,P) && noteq(P, PC) && (noteq(G,S)||eq(S, SI))) {L=admixL(J,L,D); longslope = true;}
    }


// longslope formed can exit, basically won't form sawslope on the diagonal
if (longslope) {
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
	return;
}

bool sawslope = false;

bool slopeok = slope1ok||slope2ok||slope3ok||slope4ok;

// Note: sawslope cannot exclude slopEND (few) and slopeBAD (very few), but can exclude slopeok (strong shape)
if (!oppoPix && !slopeok) {


        // horizontal bottom
		if (!eq_E_H && none_eq2(H,A,C)) {

			//                                    A B C .
			//                                  Q D 🄴 🅵 🆁       Zone 4
			//					                  🅶 🅷 I
			//					                    S
			// (!slope3 && !eq_D_H) Such consecutive use is quite well
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H)  && !eq_F_H &&
                !eq_E_F && (eq_B_D || eq_E_D) && eq(R,H) && eq(F,G) ) {
                M = admixS(A,B,C,D,E,F,G,H,I,R,RC,RI,S,SG,SI,II,eq_B_D,eq_E_D,El,Bl,rgbaE);
                sawslope = true;}

			//                                  . A B C
			//                                  🆀 🅳 🄴 F R       Zone 3
			//                                    G 🅷 🅸
			//					                    S
			if ( !sawslope && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H &&
                 !eq_E_D && (eq_B_F || eq_E_F) && eq(Q,H) && eq(D,I) ) {
                L = admixS(C,B,A,F,E,D,I,H,G,Q,QA,QG,S,SI,SG,GG,eq_B_F,eq_E_F,El,Bl,rgbaE);
                sawslope = true;}
		}

        // horizontal up
		if ( !sawslope && !eq_E_B && none_eq2(B,G,I)) {

			//					                    P
			//                                    🅐 🅑 C
			//                                  Q D 🄴 🅵 🆁       Zone 2
			//                                    G H I .
			if ( (!slope1 && !eq_B_D)  && (!slope4 && !eq_F_H) && !eq_B_F &&
				  !eq_E_F && (eq_D_H || eq_E_D) && eq(B,R) && eq(A,F) ) {
                K = admixS(G,H,I,D,E,F,A,B,C,R,RI,RC,P,PA,PC,CC,eq_D_H,eq_E_D,El,Hl,rgbaE);
                sawslope = true;}

			//					                    P
			//                                    A 🅑 🅲
			//                                  🆀 🅳 🄴 F R        Zone 1
			//                                  . G H I
			if ( !sawslope && (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_B_D &&
				 !eq_E_D && (eq_F_H || eq_E_F) && eq(B,Q) && eq(C,D) ) {
                J = admixS(I,H,G,F,E,D,C,B,A,Q,QG,QA,P,PC,PA,AA,eq_F_H,eq_E_F,El,Hl,rgbaE);
                sawslope = true;}

		}

        // vertical left
        if ( !sawslope && !eq_E_D && none_eq2(D,C,I) ) {

			//                                    🅐 B C
			//                                  Q 🅳 🄴 F R
			//                                    G 🅷 I        Zone 3
			//                                      🆂 .
            if ( (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H &&
				  !eq_E_H && (eq_B_F || eq_E_B) && eq(D,S) && eq(A,H) ) {
                L = admixS(C,F,I,B,E,H,A,D,G,S,SI,SG,Q,QA,QG,GG,eq_B_F,eq_E_B,El,Fl,rgbaE);
                sawslope = true;}

			//                                      🅟 .
			//                                    A 🅑 C
			//                                  Q 🅳 🄴 F R       Zone 1
			//                                    🅶 H I
			if ( !sawslope && (!slope3 && !eq_D_H) && (!slope2 && !eq_B_F) && !eq_B_D &&
				  !eq_E_B && (eq_F_H || eq_E_H) && eq(P,D) && eq(B,G) ) {
                J = admixS(I,F,C,H,E,B,G,D,A,P,PC,PA,Q,QG,QA,AA,eq_F_H,eq_E_H,El,Fl,rgbaE);
                sawslope = true;}

		}

        // vertical right
		if ( !sawslope && !eq_E_F && none_eq2(F,A,G) ) { // right

			//                                    A B 🅲
			//                                  Q D 🄴 🅵 R
			//                                    G 🅷 I        Zone 4
			//                                    . 🆂
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_F_H &&
				  !eq_E_H && (eq_B_D || eq_E_B) && eq(S,F) && eq(H,C) ) {
                M = admixS(A,D,G,B,E,H,C,F,I,S,SG,SI,R,RC,RI,II,eq_B_D,eq_E_B,El,Dl,rgbaE);
                sawslope = true;}

			//                                    . 🅟
			//                                    A 🅑 C
			//                                  Q D 🄴 🅵 R        Zone 2
			//                                    G H 🅸
			if ( !sawslope && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_B_F &&
				 !eq_E_B && (eq_D_H || eq_E_H) && eq(P,F) && eq(B,I) ) {
                K = admixS(G,D,A,H,E,B,I,F,C,P,PA,PC,R,RI,RC,CC,eq_D_H,eq_E_H,El,Dl,rgbaE);
                sawslope = true;}

		} // vertical right
} // sawslope

// sawslope formed can exit, slopeBAD is not suitable for the CnC below
// Exit early if all 5 points (up, down, left, right, center) are fully transparent
if (sawslope||slopeBAD||El>=7340032||Bl>=7340032||Dl>=7340032||Fl>=7340032||Hl>=7340032) {
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
	return;
}


/**************************************************
 *     "Concave + Cross" type (P100)	          *
 *************************************************/
// The far end of the cross star uses similar pixels, useful for some horizontal lines + sawtooth and layered gradient shapes. E.g., glowing text in SFIII 3rd Strike intro, Japanese houses in SFZ3Mix, Garou Mark of the Wolves intro.
// Practice: CnC cannot exclude slopEND (weak shape) and slopeok (strong shape) but can exclude slopeBAD !!!

bool cnc = false;	uint X;
    if (!slope3 && !slope4 &&
        Bl<El && !eq_E_D && !eq_E_F && eq_E_H && none_eq2(E,A,C) && all_eq2(G,H,I) && v4i_sim1(rgbaE,S) ) { // TOP
	    if (eq_D_F){
			if (eq_B_D) J=admixK(B,J);
			else { X = abs(El-Bl) < abs(El-Dl) ? B : D;		J=admixC(X,J); }
			K=J;
            L=mix(J,L, 0.61804);
			M=L;
        } else {
				if (!slope1) { X = abs(El-Bl) < abs(El-Dl) ? B : D;		J=admixC(X,J); }
				if (!slope2) { X = abs(El-Bl) < abs(El-Fl) ? B : F; 		K=admixC(X,K); }
		}

	   cnc = true;
	}

    if (!slope1 && !slope2 && !cnc &&
		Hl<El && !eq_E_D && !eq_E_F && eq_E_B && none_eq2(E,G,I) && all_eq2(A,B,C) && v4i_sim1(rgbaE,P) ) { // BOTTOM
	    if (eq_D_F){
			if (eq_D_H) L=admixK(H,L);
			else { X = abs(El-Dl) < abs(El-Hl) ? D : H;		L=admixC(X,L); }
			M=L;
            J=mix(L,J, 0.61804);
			K=J;
        } else {
				if (!slope3) { X = abs(El-Dl) < abs(El-Hl) ? D : H;		L=admixC(X,L); }
				if (!slope4) { X = abs(El-Fl) < abs(El-Hl) ? F : H; 		M=admixC(X,M); }
		}

	   cnc = true;
	}

    if (!slope1 && !slope3 && !cnc &&
		Fl<El && !eq_E_B && !eq_E_H && eq_E_D && none_eq2(E,C,I) && all_eq2(A,D,G) && v4i_sim1(rgbaE,Q) ) { // RIGHT
        if (eq_B_H) {
			if (eq_B_F) K=admixK(F,K);
			else { X = abs(El-Bl) < abs(El-Fl) ? B : F;		K=admixC(X,K); }
			M=K;
            J=mix(K,J, 0.61804);
			L=J;
        } else {
				if (!slope2) { X = abs(El-Bl) < abs(El-Fl) ? B : F;		K=admixC(X,K); }
				if (!slope4) { X = abs(El-Fl) < abs(El-Hl) ? F : H; 		M=admixC(X,M); }
		}

	}
    if (!slope2 && !slope4 && !cnc &&
		Dl<El && !eq_E_B && !eq_E_H && eq_E_F && none_eq2(E,A,G) && all_eq2(C,F,I) && v4i_sim1(rgbaE,R) ) { // LEFT
        if (eq_B_H) {
			if (eq_B_D) J=admixK(B,J);
			else { X = abs(El-Bl) < abs(El-Dl) ? B : D;		J=admixC(X,J); }
			L=J;
            K=mix(J,K, 0.61804);
			M=K;
        } else {
				if (!slope1) { X = abs(El-Bl) < abs(El-Dl) ? B : D;		J=admixC(X,J); }
				if (!slope3) { X = abs(El-Dl) < abs(El-Hl) ? D : H; 		L=admixC(X,L); }
		}

	   cnc = true;
	}

	bool slope = slope1 || slope2 || slope3 || slope4;

if (slope||cnc) {
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
	return;
}

/*
       ㇿО
     ОООㇿ
	   ㇿО    Scorpion type (P99). Looks like the tracking bug in The Matrix. Can flatten some regularly interlaced pixels.
*/
// Practice: 1. Scorpion pincers use approximation, otherwise easily become a small C shape and cause graphical artifacts.
// Practice: 2. Remove one segment from the scorpion tail to capture more shapes.
// Among the four shapes, only the scorpion is exclusive. Once caught by previous rules (entered), this shape won't appear. It's also the least noticeable shape. So placed last.

bool scorpion = false;
                if (!eq_E_F &&eq_E_D&&eq_B_F&&eq_F_H && all_eq2(E,C,I) && noteq(F,src(+3, 0)) ) {K=admixK(F,K); M=K;J=mix(K,J, 0.61804); L=J;scorpion=true;}	// RIGHT
   if (!scorpion && !eq_E_D &&eq_E_F&&eq_B_D&&eq_D_H && all_eq2(E,A,G) && noteq(D,src(-3, 0)) ) {J=admixK(D,J); L=J;K=mix(J,K, 0.61804); M=K;scorpion=true;}	// LEFT
   if (!scorpion && !eq_E_H &&eq_E_B&&eq_D_H&&eq_F_H && all_eq2(E,G,I) && noteq(H,src(0, +3)) ) {L=admixK(H,L); M=L;J=mix(L,J, 0.61804); K=J;scorpion=true;}	// BOTTOM
   if (!scorpion && !eq_E_B &&eq_E_H&&eq_B_D&&eq_B_F && all_eq2(E,A,C) && noteq(B,src(0, -3)) ) {J=admixK(B,J); K=J;L=mix(J,L, 0.61804); M=L;}					// TOP


    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
}
