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

//RGB visual weight + alpha segmentation
uint luma(uint C) {

	if (C==0) return 7000000;

	uint alpha = C >> 24;
	//BT.601 standard from the CRT era
    uint rgbsum = ((C >> 16) & 0xFF) *299 + ((C >> 8) & 0xFF) *587 + (C & 0xFF) *114;
	// R G B average
    //uint rgbsum = ((C >> 16) & 0xFF) + ((C >> 8) & 0xFF) + (C & 0xFF);

    uint alphafactor =
        (alpha == 0) ? 7000000 :		// Fully transparent
        (alpha > 217) ? 1000000 :		// Two golden sections		0.854102 ×255≈217.8
        (alpha > 157) ? 2000000 :		// 1 golden section		0.618034 ×255≈157.6
        (alpha > 97) ? 3000000 :		// 1 golden section (short)	0.381966 ×255≈97.4
        (alpha > 37) ? 4000000 : 5000000;	// Two golden sections (short)	0.145898 ×255≈37.2

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

    // 1. Quick component difference check
    if ( diff.r > 0.145898 || diff.g > 0.145898 || diff.b > 0.145898 || diff.a > 0.145898 ) return false;

    vec3 rgbC1 = rgbaC1.rgb;
    vec3 rgbC2 = rgbaC2.rgb;
 
	if (rgbC1 == rgbC2) return true;
 
    // 2.Quickly filter out identical pixels and very dark/bright pixels after clamping
    vec3 clampCol1 = clamp(rgbC1, vec3(0.078),vec3(0.92));
    vec3 clampCol2 = clamp(rgbC2, vec3(0.078),vec3(0.92));
    vec3 clampdiff = clampCol1 - clampCol2;
    vec3 absclampdiff = abs(clampdiff);
    if ( absclampdiff.r < 0.05572809 && absclampdiff.g < 0.021286 && absclampdiff.b < 0.05572809 ) return true;

    float dotdist = 0.00931686;
	if (Lv==2) dotdist = 0.024391856;
	if (Lv==3) dotdist = 0.0638587;

    // 3. Quick squared distance check
	float dot_diff = dot(diff.rgb, diff.rgb);
    if (dot_diff > dotdist) return false;

	// 4. Add opposite channels to squared distance and re-evaluate
	float teamA = 0.0;
	float teamB = 0.0;

	if (clampdiff.r > 0.0) teamA += absclampdiff.r; else teamB += absclampdiff.r;
	if (clampdiff.g > 0.0) teamA += absclampdiff.g; else teamB += absclampdiff.g;
	if (clampdiff.b > 0.0) teamA += absclampdiff.b; else teamB += absclampdiff.b;
	float team = min(teamA,teamB);
	// Testing shows at least 3 times the opposite channel value is needed. (+1x ties, +2x creates an increasing trend)
	if (dot_diff + team*team*3.0 > dotdist) return false;

	if (Lv==3) return true;

	// 5. Check for gray pixels
    float sum1 = rgbC1.r + rgbC1.g + rgbC1.b;
	float avg1 = sum1 * 0.3333333;
    float threshold1 = avg1 * 0.08;

    float sum2 = rgbC2.r + rgbC2.g + rgbC2.b;
	float avg2 = sum2 * 0.3333333;
    float threshold2 = avg2 * 0.08;

    bool Col1isGray = all(lessThan(abs(rgbC1 - vec3(avg1)), vec3(threshold1)));
	bool Col2isGray = all(lessThan(abs(rgbC2 - vec3(avg2)), vec3(threshold2)));

	// Return true if both or neither are gray
	return Col1isGray == Col2isGray;
}
bool sim1(uint C1, uint C2) {
	if (C1 == C2) return true;
    vec4 rgbaC1 = unpackUnorm4x8(C1);
    vec4 rgbaC2 = unpackUnorm4x8(C2);
    return sim(rgbaC1, rgbaC2, 1);
}
bool sim2(uint C1, uint C2) {
	if (C1 == C2) return true;
    vec4 rgbaC1 = unpackUnorm4x8(C1);
    vec4 rgbaC2 = unpackUnorm4x8(C2);
    return sim(rgbaC1, rgbaC2, 2);
}
bool vec4_sim2(vec4 C1, vec4 C2) {
    return sim(C1, C2, 2);
}
bool vec4_sim3(vec4 C1, vec4 C2) {
    return sim(C1, C2, 3);
}

bool mixcheck(vec4 col1, vec4 col2) {
	
	// Cannot mix if one is transparent
	bool col1alpha0 = col1.a < 0.003;
	bool col2alpha0 = col2.a < 0.003;
	if (col1alpha0!=col2alpha0) return false;

    vec4 diff = col1 - col2;
    vec4 absdiff = abs(diff);

    // 1. Quick component difference check
    if ( absdiff.r > 0.618034 || absdiff.g > 0.618034 || absdiff.b > 0.618034 || absdiff.a > 0.5 ) return false;
    // Quickly filter out similar pixels (0.4377 divided by 6, sqrt max can be 0.27)
    if ( absdiff.r < 0.27 && absdiff.g < 0.27 && absdiff.b < 0.27 ) return true;

    // 3. Quick squared distance check
	float dot_diff = dot(diff.rgb, diff.rgb);
    if (dot_diff > 0.4377) return false;

	// 4. Add opposite channels to squared distance and re-evaluate
	float teamA = 0.0;
	float teamB = 0.0;

	if (diff.r > 0.0) teamA += absdiff.r; else teamB += absdiff.r;
	if (diff.g > 0.0) teamA += absdiff.g; else teamB += absdiff.g;
	if (diff.b > 0.0) teamA += absdiff.b; else teamB += absdiff.b;
	float team = min(teamA,teamB);
	// Testing shows at least 3 times the opposite channel value is needed. (+1x ties, +2x creates a positive trend)
	if (dot_diff + team*team*3.0 > 0.4377) return false;

    return true;
}

bool eq(uint B, uint A0){
    return B == A0;
}

bool noteq(uint B, uint A0){
    return B != A0;
}

bool vec_eq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);

    if (diff.r > 0.004 || diff.g > 0.004 || diff.b > 0.004 || diff.a > 0.021286) return false;

    return true;
}

bool rgb_eq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);

    if (diff.r > 0.004 || diff.g > 0.004 || diff.b > 0.004) return false;

    return true;
}

bool vec_noteq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);

    if (diff.r > 0.004 || diff.g > 0.004 || diff.b > 0.004 || diff.a > 0.021286) return true;

    return false;
}

bool all_eq2(uint B, uint A0, uint A1) {
    return (eq(B,A0) && eq(B,A1));
}

bool any_eq3(uint B, uint A0, uint A1, uint A2) {
   return (eq(B,A0) || eq(B,A1) || eq(B,A2));
}

bool none_eq2(uint B, uint A0, uint A1) {
   return (noteq(B,A0) && noteq(B,A1));
}

///////////////////////     Test Colors     ///////////////////////
 //vec4 testcolor = vec4(1.0, 0.0, 1.0, 1.0);  // Magenta (Opaque)
 //vec4 testcolor2 = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan (Opaque)
 //vec4 testcolor3 = vec4(1.0, 1.0, 0.0, 1.0);  // Yellow (Opaque)
 //vec4 testcolor4 = vec4(1.0, 1.0, 1.0, 1.0);  // White (Opaque)

//   "Concave + Cross" type Weak blending (Weak mix/None)
vec4 admixC(uint X1, uint X2, vec4 rgbaE) {

    // Return if center point is transparent
    // if (rgbaE.a < 0.003) return rgbaE;

    // Unpack uint to RGBA float vector (range 0.0-1.0)
    vec4 rgbaX1 = unpackUnorm4x8(X1);
    vec4 rgbaX2 = unpackUnorm4x8(X2);

    // Return if black on both sides? Not necessary, E is also dark when mixing is possible.
    //if (checkblack(rgbaX1) && checkblack(rgbaX2)) return rgbaE;

	bool mix1ok = mixcheck(rgbaX1, rgbaE);
	bool mix2ok = mixcheck(rgbaX2, rgbaE);

	if (mix1ok&&!mix2ok) return mix(rgbaX1, rgbaE, 0.618034);
	if (!mix1ok&&mix2ok) return mix(rgbaX2, rgbaE, 0.618034);
	if (mix1ok&&mix2ok) {
		float rgbDist1 = dot(rgbaX1 - rgbaE, rgbaX1 - rgbaE);
		float rgbDist2 = dot(rgbaX2 - rgbaE, rgbaX2 - rgbaE);

		// Choose the closer reference color
		vec4 rgbaX = rgbDist1 < rgbDist2 ? rgbaX1 : rgbaX2;
		return mix(rgbaX, rgbaE, 0.618034);
	}

	return rgbaE;

}

// K type Forced weak blending (Weak mix/Weaker)
vec4 admixK(uint X, vec4 rgbaE) {
//return testcolor;
    vec4 rgbaX = unpackUnorm4x8(X);

	// When E is transparent, return X's rgb value and the weakest mixed alpha value (avoid mixing rgb with black)
	// Checked before entry that it won't be transparent.
    // if (rgbaX.a < 0.003 || rgbaE.a < 0.003) return rgbaE;

	bool mixok = mixcheck(rgbaX, rgbaE);

	return mixok ? mix(rgbaX, rgbaE, 0.618034) : mix(rgbaX, rgbaE, 0.8541);
}

vec4 admixL(vec4 rgbaX, vec4 rgbaE, uint S) {

    // If E is fully transparent, copy target X
    //if (rgbaE.a < 0.01) return rgbaX;
 
    // If X is fully transparent, return original value E
    //if (rgbaX.a < 0.01) return rgbaE;


    vec4 rgbaS = unpackUnorm4x8(S);
	// If target X and reference S(sample) are different, it means already mixed once, then just copy the target.
	if (vec_noteq(rgbaX, rgbaS)) return rgbaX;

	bool mixok = mixcheck(rgbaX, rgbaE);

    return mixok ? mix(rgbaX, rgbaE, 0.381966) : rgbaX;
}

/********************************************************************************************************************************************
 *              												main slope + X cross-processing mechanism					                *
 *******************************************************************************************************************************************/
vec4 admixX(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint P, uint PA, uint PC, uint Q, uint QA, uint QG, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint AA) {

    // Pre-define 3 types of special exits
    vec4 slopeBAD = vec4(2.0);
    vec4 theEXIT = vec4(8.0);
    vec4 slopEND = vec4(33.0);

	//pre-cal
	bool eq_B_D = eq(B, D);
	bool eq_B_C = eq(B, C);
	bool eq_D_G = eq(D, G);

	// Exit if bilateral straight line clamping
	if (eq_B_D && eq_B_C && eq_D_G) return slopeBAD;

    vec4 rgbaE = unpackUnorm4x8(E); 
    vec4 rgbaB = unpackUnorm4x8(B);
    vec4 rgbaD = unpackUnorm4x8(D);

	// Although rules limit B D pixel channel differences to be small. Need to avoid mixing non-transparent black and transparent (black), generating a darker non-transparent color, causing artifacts.
	// So when one is transparent, copy the rgb value of the other.
	// But with the next step, this becomes redundant. After mixing, the alpha from the lesser side will be used, becoming fully transparent.
    //if (rgbaB.a < 0.003) rgbaB.rgb = rgbaD.rgb;
    //if (rgbaD.a < 0.003) rgbaD.rgb = rgbaB.rgb;

	// Merge D B into X
    vec4 rgbaX = eq_B_D ? rgbaB : mix(rgbaB,rgbaD,0.5);

	// After mixing, use the alpha from the lesser side to reduce artifacts.
    if (rgbaB.a < rgbaX.a ) rgbaX.a = rgbaB.a;
    if (rgbaD.a < rgbaX.a ) rgbaX.a = rgbaD.a;
    // Practice: Cases exist where both B D sides are fully transparent, and the center point is fully transparent. In such cases, follow the rules to copy. Otherwise, shape will be lost.

	// Avoid opposite side squeezing when E is at black edge (for fonts with contrast exceeding half)
	bool Bisblack = checkblack(rgbaB);
	// Cannot use average for X black when BD are not equal
	bool Xisblack = eq_B_D ? Bisblack : Bisblack&&checkblack(rgbaD);
	//bool Aisblack = checkblack(A);
	if (Xisblack && abs((rgbaE.r + rgbaE.g + rgbaE.b) - (rgbaX.r + rgbaX.g + rgbaX.b)) >1.5 ) {

		if ( checkblack(unpackUnorm4x8(F)) || checkblack(unpackUnorm4x8(H)) ) return theEXIT;
	}

	//Pre-declare
	bool eq_A_B;	bool eq_A_D;	bool eq_A_P;	bool eq_A_Q;
	bool eq_B_P;    bool eq_B_PA;   bool eq_B_PC;
	bool eq_D_Q;    bool eq_D_QA;   bool eq_D_QG;
    bool eq_E_F;    bool eq_E_H;    bool eq_E_I;    bool En3;	bool En4square;
	bool linkB1;	bool linkB2;	bool linkD1;	bool linkD2;
	bool BDlong;	bool DBlong;
    bool comboA3;	bool mixok;

	//pre-cal
    bool eq_E_A = eq(E,A);

// B != D
if (!eq_B_D){

    // Exit if it doesn't meet preset logic
	if (eq_E_A) return slopeBAD;

	eq_A_B = eq(A,B);
	eq_A_D = eq(A,D);

	// B D are not equal (non-black), one side is straight line, the other must be single pixel.
	// Practice: Avoids mixing at corners of parallel lines where BD are each continuous.
    if (!Xisblack){
        if ( eq_A_B && (eq_D_G||eq(D,Q)) ) return slopeBAD;
        if ( eq_A_D && (eq_B_C||eq(B,P)) ) return slopeBAD;
    }

    // When D B are not equal, and their rgb difference is large, greater than either side's difference with the center, exit.
    vec3 rgbE = rgbaE.rgb; 
    vec3 rgbB = rgbaB.rgb;
    vec3 rgbD = rgbaD.rgb;
    vec3 diffBD = rgbB - rgbD;
    vec3 diffEB = rgbE - rgbB;
    vec3 diffED = rgbE - rgbD;
	float distBD = dot(diffBD,diffBD);
	if (distBD > dot(diffEB,diffEB) || distBD > dot(diffED,diffED)) return slopeBAD;

    // B D not connected? Not applicable here (Can eliminate some faults, but also lose some shapes, especially in non-native pixel art, e.g., Double Dragon character portraits)

	// Treat black-like the same as DB?
    // Practice: Special treatment for black not applicable,Easily causes burrs in dark areas (Samurai Shodown 2 Charlotte)
	//if (Xisblack) return X +slopEND;

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	comboA3 = eq_A_P && eq_A_Q;
	mixok = mixcheck(rgbaX, rgbaE);

	// A-side three-star alignment, high priority
    if (comboA3) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;

    // Official original rule
    if ( eq(E,C) || eq(E,G) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;
    // Enhanced original rule Practice: Beneficial for non-native pixel art, but harmful for native pixel art (JoJo's wall and clock)
    // if (sim_EC && sim_EG) return mixok ? mix(X,E,0.381966) +slopEND : X +slopEND;

    eq_E_F = eq(E, F);
    eq_E_H = eq(E, H);
    En3 = eq_E_F && eq_E_H;

    if (En3) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;
    // Exclude "single stick" situation, including eq_E_I bent stick situation
    if ( eq_E_F || eq_E_H ) return slopeBAD;
    // Exclude "three-grid single-side wall" situation
    if ( eq_A_B&&eq_B_C || eq_A_D&&eq_D_G ) return slopeBAD;
    // F-H inline trend, placed after the three-grid single-side wall rule as it might be blocked by it.
    if ( eq(F,H) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;
    // Exclude "two-grid single-side wall" situation, merged with next rule
    //if (eq_A_B||eq_A_D) return slopeBAD;
    // The rest are single pixels within sim2 range that are similar but have no logic, so use weak mixing??? Just give up!
    return slopeBAD;
} // B != D

	//pre-cal B == D
    bool eq_E_C = eq(E,C);
    bool eq_E_G = eq(E,G);
	bool sim_EC = eq_E_C || vec4_sim2(rgbaE, unpackUnorm4x8(C));
	bool sim_EG = eq_E_G || vec4_sim2(rgbaE, unpackUnorm4x8(G));

	bool ThickBorder;
	uint X = B;
// Original main rule enhanced with sim
if ( (sim_EC || sim_EG) && !eq_E_A ){

	eq_A_B = eq(A,B);
	eq_B_P = eq(B,P);
    eq_B_PC = eq(B,PC);
	eq_D_Q = eq(D,Q);
    eq_D_QG = eq(D,QG);

	linkB1 = eq_B_PC;
	linkB2 = eq_B_P;
	linkD1 = eq_D_QG;
	linkD2 = eq_D_Q;

	// B + D + their respective extension forms, highest priority. Initial screening.
	// Practice: Need to add the !eq_A_B case (E side is continuous step edge, opposite side is black background)
	if ( (linkB1||linkB2)&&!eq_B_C && (linkD1||linkD2)&&!eq_D_G && !eq_A_B ) return rgbaX +slopEND;

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	comboA3 = eq_A_P && eq_A_Q;
	mixok = mixcheck(rgbaX,rgbaE);

	// A-side three-star alignment, high priority
    if (comboA3) {
		// Copy directly if not border
		if (!eq_A_B) return rgbaX + slopEND;
		// Disable mixing at border
		mixok=false;
	}

    // When A-side is border, and A has a support on the diagonal (has thickness), copy without mixing.
    // Without thickness detection, if D B A are the same and copy, it will increase thickness at the edge,It disrupts the original design aesthetic.
    // Exclude black, overly thick black borders affect appearance. Keep some transition near black edges,Add scan lines later for better layering.
    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq(B,PA)||eq(D,QA)));
	if (ThickBorder && !Xisblack) mixok=false;

    // Official original rule
    if (eq_E_C || eq_E_G) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
    // Enhanced original rule
    if (sim_EC && sim_EG) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;

    // En3 skip

    // F-H inline trend
    if ( eq(F,H) ) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;

    // When one side is a wall, besides the official rule and inline trend, the opposite side needs sim, otherwise exit (exclude cases where the wall itself is similar to center E)
    if (eq_B_C && !sim_EG) return slopeBAD;
    if (eq_D_G && !sim_EC) return slopeBAD;

	BDlong = eq_B_C && eq_D_Q;
	DBlong = eq_D_G && eq_B_P;
    eq_E_F = eq(E,F);
    eq_E_H = eq(E,H);
    eq_E_I = eq(E,I);

	if ( BDlong ) {
		if (eq_D_QG) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		if (eq_E_F && !eq_E_I) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
		return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		}

	if ( DBlong ) {
		if (eq_B_PC) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX+slopEND;
		if (eq_E_H && !eq_E_I) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
		return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX+slopEND;
		}

    //	Use A-side three-star alignment again to return faster.
    if (comboA3) return rgbaX +slopEND;

	// Use B + D + their respective extensions again, priority higher than L + single stick below
  	if ( (linkB1 || linkB2) && (linkD1 || linkD2) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;

    // X E Lv3 similarity can ignore the next two exit rules
    if ( vec4_sim3(rgbaX,rgbaE) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;

    // L-shaped corner (hollow) with an internal stick, exit
    if (eq_A_B && !ThickBorder && !eq_E_I) {
	    if (eq_B_C && eq_E_F) return theEXIT;
	    if (eq_D_G && eq_E_H) return theEXIT;
	}

    // L-shaped corner (hollow) with an external stick, exit (connect inside corner, not outside)
	// Practice: Can avoid font edges being shaved (Captain Commando)
	if ( (linkB2 || linkD2) && (eq_E_F||eq_E_H) && !ThickBorder && !eq_E_I) return theEXIT;

    return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

} // sim2 base


/*===================================================
                    E - A Cross
  ===============================================zz */

if (eq_E_A) {

	// When judging cross ✕, need concepts of "region" and "trend". Conditions need tightening for different regions.

    // B D not connected exit? Not needed here!!!
    // Sometimes connecting two separate pixels looks abrupt. But in many cases, shapes are lost!
    //if ( !eq_B_C && !eq_B_P && !eq_B_PC && !eq_D_G && !eq_D_Q && !eq_D_QG ) return slopeBAD;

    // X fully transparent exit (Using transparent B D to cut the crossing E A doesn't make logical sense for slope)
	bool X_alpha0 = rgbaX.a < 0.003;
	if (X_alpha0) return slopeBAD;
	//if (E_alpha0) return testcolor;

    eq_E_F = eq(E, F);
    eq_E_H = eq(E, H);
    eq_E_I = eq(E, I);
    En3 = eq_E_F&&eq_E_H;
    En4square = En3 && eq_E_I;

	// Special form: Square
	if ( En4square ) {
        if( noteq(G,H) && noteq(C,F)                      // Independent clear 4-grid square / 6-grid rectangle (both sides satisfied)
		&& (eq(H,S) == eq(I,SI) && eq(F,R) == eq(I,RI)) ) return theEXIT;
        //else return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;    Note: Cannot return directly. Need to enter the board decision rule, because the opposite shore might also be a square forming a bubble.
    }

    // Special form: Halftone dot
	// Practice 1: !eq_E_F, !eq_E_H not using !sim, because it loses some shapes.
    // Practice 2: Force mixing, otherwise shapes are lost. (Gouketsuji Ichizoku Matrimelee, WWF Super WrestleMania)

	bool Eisblack = checkblack(rgbaE);
	mixok = mixcheck(rgbaX,rgbaE);

	// 1. Halftone dot center
    if ( eq_E_C && eq_E_G && eq_E_I && !eq_E_F && !eq_E_H ) {

		// Exit if center E is black (KOF96 power gauge, Punisher's belt) Avoid mixing with too high contrast.
		if (Eisblack) return theEXIT;
		if ( rgb_eq(rgbaX,rgbaE) ) return slopeBAD;
		// Practice 1: Cannot catch black B point, B points entering here all satisfy the form.
		//if (Xisblack) return testcolor2;
		// Option 1 Layered五花肉 (eq_F_H occurrence 95%) + progressive layering inside health bar, remaining 0.5 fallback mixing.
		// if (eq_F_H) return mixok ? mix(rgbaX, rgbaE, 0.381966)+slopEND : mix(rgbaX, rgbaE, 0.618034)+slopEND;
        // return mix(rgbaX, rgbaE, 0.5)+slopEND;
		// Final decision: unify it.
		return mixok ? mix(rgbaX, rgbaE, 0.381966)+slopEND : mix(rgbaX, rgbaE, 0.618034)+slopEND;
	}
//return testcolor;
	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
    eq_B_PA = eq(B,PA);

	// 2. Halftone dot edge
    if ( eq_A_P && eq_A_Q && eq(A,AA) && noteq(A,PA) && noteq(A,QA) )  {
		if (Eisblack) return theEXIT;

        // Layered progressive edge, use strong mixing.
		if ( !eq_B_PA && eq(PA,QA) ) return mixok ? mix(rgbaX, rgbaE, 0.381966)+slopEND : mix(rgbaX, rgbaE, 0.618034)+slopEND;
        // Remaining 1. Perfect cross, must be halftone dot edge, use weak mixing.
        // Remaining 2. Fallback weak mixing.
		return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;
		// Note: No need to specify health bar border separately.
	}

    eq_D_QA = eq(D,QA);
    eq_D_QG = eq(D,QG);
    eq_B_PC = eq(B,PC);

  // 3. Half halftone Usually shadow expression on contour edges, use weak mixing.
	if ( eq_E_C && eq_E_G && eq_A_P && eq_A_Q &&
		(eq_B_PC || eq_D_QG) &&
		 eq_D_QA && eq_B_PA) {
		//if (Eisblack) return testcolor;

        return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;
		}

    // 4. Quarter halftone (ugly pinky finger effect)

	if ( eq_E_C && eq_E_G && eq_A_P
		 && eq_B_PA &&eq_D_QA && eq_D_QG
		 && eq_E_H
		) return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;

	if ( eq_E_C && eq_E_G && eq_A_Q
		 && eq_B_PA &&eq_D_QA && eq_B_PC
		 && eq_E_F
		) return mixok ? mix(rgbaX, rgbaE, 0.618034)+slopEND : mix(rgbaX, rgbaE, 0.8541)+slopEND;


    // E-side three-star alignment (Must be after halftone)
    if ( eq_E_C && eq_E_G ) return rgbaX+slopEND;

    comboA3 = eq_A_P && eq_A_Q;

    // A-side three-star alignment (Must be after halftone) Since E-A are same, prefer mixing over direct copying.
	if (comboA3) return mixok ? mix(rgbaX, rgbaE, 0.381966)+slopEND : rgbaX+slopEND;


    // B-D part of long slope
	eq_B_P = eq(B,P);
	eq_D_Q = eq(D,Q);
	bool B_hori = eq_B_C && !eq_B_P;
	bool B_vert = eq_B_P && !eq_B_C;
	bool D_hori = eq_D_Q && !eq_D_G;
	bool D_vert = eq_D_G && !eq_D_Q;

    int scoreE = 0; int scoreB = 0; int scoreD = 0; int scoreZ = 0;


// E B D region board scoring rule

// E Zone
    if (En3) {
        scoreE += 1;
        if (B_hori || B_vert || D_hori || D_vert) scoreZ = 1;
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
        if ( scoreZ==0 && B_hori && (eq(F,R) || eq(G,H) || eq(F,I)) ) scoreZ = 1;
        if ( scoreZ==0 && D_vert && (eq(C,F) || eq(H,S) || eq(F,I)) ) scoreZ = 1;
    }

	bool Bn3 = eq_B_P&&eq_B_C;
	bool Dn3 = eq_D_G&&eq_D_Q;

// B Zone
	scoreB -= int(Bn3);
	scoreB -= int(eq(C,P));
    if (scoreB < 0) scoreZ = 0;

    if (eq_B_PA) {
		scoreB -= 1;
		scoreB -= int(eq(P,PA));
	}

// D Zone
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

    // Final addition: If total score is zero, and B, D zones have no deductions, forming a long slope shape.
    if (scoreB >= 0 && scoreD >=0) {
        if (B_hori&&D_hori) return rgbaX;
        if (B_vert&&D_vert) return rgbaX;
    }

    return slopeBAD;

}	// eq_E_A



/*=========================================================
                    F - H / -B - D- Extension New Rules
  ==================================================== zz*/

// This side is different from the sim side. Since the inner pixels mostly have high contrast with the surroundings and are not sim.
// Sometimes it's more like the opposite side of sim, with B-D as the boundary, so the judgment rules are different from the sim side.

	eq_B_P = eq(B, P);
    eq_B_PC = eq(B, PC);
	eq_D_Q = eq(D, Q);
    eq_D_QG = eq(D, QG);

    // B D not connected exit (Practice: Indeed needed)
    if ( !eq_B_C && !eq_B_P && !eq_B_PC && !eq_D_G && !eq_D_Q && !eq_D_QG ) return slopeBAD;

    eq_A_B = eq(A, B);
	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);

	mixok = mixcheck(rgbaX,rgbaE);
    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq(B,PA)||eq(D,QA)));

	if (ThickBorder && !Xisblack) mixok=false;

	linkB1 = eq_B_PC;
	linkB2 = eq_B_P;
	linkD1 = eq_D_QG;
	linkD2 = eq_D_Q;


	// B + D + their respective extension forms, highest priority. Initial screening.
	// Practice: Need to add the !eq_A_B case (E side is continuous step edge, opposite side is black background)
	if ( (linkB1||linkB2)&&!eq_B_C && (linkD1||linkD2)&&!eq_D_G && !eq_A_B ) return rgbaX +slopEND;

	comboA3 = eq_A_P && eq_A_Q;

	// A-side three-star alignment, high priority
    if (comboA3) {
		// Copy directly if not border
		if (!eq_A_B) return rgbaX + slopEND;
		// Disable mixing at border
		mixok=false;
	}

    eq_E_F = eq(E, F);
    eq_E_H = eq(E, H);
    eq_E_I = eq(E, I);
    En3 = eq_E_F&&eq_E_H;
    En4square = En3 && eq_E_I;
    bool sim_X_E = vec4_sim3(rgbaX,rgbaE);
    bool eq_G_H = eq(G, H);
    bool eq_C_F = eq(C, F);
    bool eq_H_S = eq(H, S);
    bool eq_F_R = eq(F, R);

    // Special form: Independent clear 4-grid/6-grid rectangle
    // Practice: Detecting rectangles from this branch is necessary (edges of buildings, fonts)
	if ( En4square ) {  // This square detection needs to be after the previous rule.
		if (sim_X_E) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
        if( !eq_G_H && !eq_C_F                      // Independent clear 4-grid square / 6-grid rectangle (both sides satisfied)
		&& (eq_H_S == eq(I, SI) && eq_F_R == eq(I, RI)) ) return theEXIT;
        else return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
    }

    // Triangle
 	if ( En3 ) {
		if (sim_X_E) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;

        if (eq_H_S && eq_F_R) return theEXIT; // Both sides extended simultaneously (building edge)

        if (eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
        // Inner bend
        if (eq_B_C || eq_D_G) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
        // Outer bend
        if (eq_B_P || eq_D_Q) return theEXIT;

        return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;
        // Last two rules based on experience, principle: Connect L inner bend, not L outer bend (Double Dragon Jimmy's eyebrows)
	}

    // F - H
	// Principle: Connect L inner bend, not L outer bend.
	// Practice: E single pixel should not use subsequent long slopes.
	if ( eq(F,H) ) {
    	if (sim_X_E) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        // Avoid bilateral symmetric full clamping squeezing single pixel.
		if ( eq_B_C && eq_A_B && eq_G_H &&eq(F, I) ) return theEXIT;
		if ( eq_D_G && eq_A_B && eq_C_F &&eq(F, I) ) return theEXIT;
		//Inner bend
        if (eq_B_C && (eq_F_R||eq_G_H||eq(F, I))) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        if (eq_D_G && (eq_C_F||eq_H_S||eq(F, I))) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		// E-I F-H cross破坏趋势
		if (eq_E_I) return slopeBAD;
        // Z-shape outer
		if (eq_B_P && eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        if (eq_D_Q && eq_A_B) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		// Outer bend unless the opposite side forms a long L-shape trend.
		if (eq_B_P && (eq_C_F&&eq_H_S)) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
        if (eq_D_Q && (eq_F_R&&eq_G_H)) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

        return slopeBAD;
	}

	BDlong = eq_B_C && eq_D_Q;
	DBlong = eq_D_G && eq_B_P;

// Clear out two long slopes (Long slopes not caught by simE2 rule, cannot use here!)
	if ( BDlong ) {
		if (eq_D_QG) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		return theEXIT;
	}
	if ( DBlong ) {
		if (eq_B_PC) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;
		return theEXIT;
	}
    // Use A-side three-star alignment again to return faster.
    if (comboA3) return rgbaX+slopEND;

    // Can X E similarity override all rules below and return directly???
    if (sim_X_E) return mixok ? mix(rgbaX,rgbaE,0.381966) : rgbaX;

	// Use B + D + their respective extensions again, priority higher than L + single stick below.
  	if ( (linkB1 || linkB2) && (linkD1 || linkD2) ) return mixok ? mix(rgbaX,rgbaE,0.381966) +slopEND : rgbaX +slopEND;

    // L-shaped corner (hollow) with an internal stick, exit.
    if (eq_A_B && !eq_E_I) {

	    if (eq_B_C && eq_E_F) return theEXIT;
	    if (eq_D_G && eq_E_H) return theEXIT;
	}

    // L-shaped corner (hollow) with an external stick, exit (connect inside corner, not outside)
	// Practice: Can avoid font edges being shaved (Captain Commando)
	if ( (linkB2 || linkD2) && (eq_E_F||eq_E_H) && !eq_E_I) return theEXIT;

    // Finally use B or D extension rule to filter, tighten the rule.
	linkB1 =  eq_B_PC && !eq_B_C  && (!eq_C_F||eq(F,RC));
	linkD1 =  eq_D_QG && !eq_D_G  && (!eq_G_H||eq(H,SG));

  	if ( (linkB1 || linkD1 )&& !eq_A_B ) return mixok ? mix(rgbaX,rgbaE,0.381966)+slopEND : rgbaX+slopEND;

	return slopeBAD;

}	// admixX

void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

    uint A = src(srcX - 1, srcY - 1), B = src(srcX, srcY - 1), C = src(srcX + 1, srcY - 1);
    uint D = src(srcX - 1, srcY + 0), E = src(srcX, srcY + 0), F = src(srcX + 1, srcY + 0);
    uint G = src(srcX - 1, srcY + 1), H = src(srcX, srcY + 1), I = src(srcX + 1, srcY + 1);


    // Predefine default output for easy conditional returns.
    ivec2 destXY = ivec2(xy) * 2;
    vec4 rgbaE = unpackUnorm4x8(E);
    vec4 J = rgbaE, K = rgbaE, L = rgbaE, M = rgbaE;

   bool eq_E_B = eq(E,B);
   bool eq_E_D = eq(E,D);
   bool eq_E_F = eq(E,F);
   bool eq_E_H = eq(E,H);
   
// New skip acceleration, skip if three consecutive horizontal or vertical pixels are equal.
if ( !(eq_E_D && eq_E_F) && !(eq_E_B && eq_E_H) ) {

        uint P = src(srcX, srcY - 2), S = src(srcX, srcY + 2);
        uint Q = src(srcX - 2, srcY), R = src(srcX + 2, srcY);
        uint Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);

// Extend to 5x5
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


   // main slops
   bool eq_B_D = eq(B,D);
   bool eq_B_F = eq(B,F);
   bool eq_D_H = eq(D,H);
   bool eq_F_H = eq(F,H);

	// other side
   bool eq_B_H = eq(B,H);
   bool eq_D_F = eq(D,F);

// 1:1 slope rules (P95)

    bool slopeBAD = false;  bool slopEND = false;
//        .------------------- 1st ---------------------.      .---- New ----.      .-------- 3rd ----------.        .~~~~~New Pocket Rule------ 4th ------------.         .------------------------ 5th ----------------------------.
    // B - D
    bool slope1 = false;
	if ( (!eq_E_B&&!eq_E_D) && (!eq_D_H&&!eq_D_F && !eq_B_H&&!eq_B_F) && (El>=Dl&&El>=Bl || eq(E,A)) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || noteq(E,P) || noteq(E,Q) ) && ( eq_B_D&&(eq_F_H||eq(E,A)||eq(B,PC)||eq(D,QG)) || sim1(B,D)&&(sim2(E,C)||sim2(E,G)) ) ) {
		J=admixX(A,B,C,D,E,F,G,H,I,P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA);
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
		} else slope1 = true;
	}
    // B - F
    bool slope2 = false;
	if ( (!eq_E_B&&!eq_E_F) && (!eq_B_D&&!eq_B_H && !eq_D_F&&!eq_F_H) && (El>=Bl&&El>=Fl || eq(E,C)) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || noteq(E,P) || noteq(E,R) ) && ( eq_B_F&&(eq_D_H||eq(E,C)||eq(B,PA)||eq(F,RI)) || sim1(B,F)&&(sim2(E,A)||sim2(E,I)) ) )  {
		K=admixX(C,F,I,B,E,H,A,D,G,R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC);
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
		} else {slope2 = true;slopeBAD = false;}
	}
    // D - H
    bool slope3 = slope1;
	if ( !slope1 && (!eq_E_D&&!eq_E_H)  &&  (!eq_F_H&&!eq_B_H && !eq_D_F&&!eq_B_D) && (El>=Hl&&El>=Dl || eq(E,G))  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || noteq(E,S) || noteq(E,Q))  &&  ( eq_D_H&&(eq_B_F||eq(E,G)||eq(D,QA)||eq(H,SI)) || sim1(D,H) && (sim2(E,A)||sim2(E,I)) ) )  {
		L=admixX(G,D,A,H,E,B,I,F,C,Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG);
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
		} else {slope3 = true;slopeBAD = false;}
	}
    // F - H
    bool slope4 = slope2;
	if ( !slope2 && (!eq_E_F&&!eq_E_H)  &&  (!eq_B_F&&!eq_D_F && !eq_B_H&&!eq_D_H) && (El>=Fl&&El>=Hl || eq(E,I))  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || noteq(E,R) || noteq(E,S))  &&  ( eq_F_H&&(eq_B_D||eq(F,RC)||eq(H,SG)||eq(E,I)) || sim1(F,H) && (sim2(E,C)||sim2(E,G)) ) )  {
		M=admixX(I,H,G,F,E,D,C,B,A,S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II);
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
		} else {slope4 = true;slopeBAD = false;}
	}

if (slopEND) {
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
	return;
}

	bool slope = slope1 || slope2 || slope3 || slope4;

// long gentle 2:1 slope (P100) and saw-tooth slope

	bool longslope1 = false; bool longslope2 = false; bool longslope3 = false; bool longslope4 = false; bool sawslope = false;

	if (!eq_B_H) { // horizontal

		if (!eq_E_H && none_eq2(H,A,C)) {

			//                                    A B C .
			//                                  Q D 🄴 🅵 🆁       Zone 4
			//					                  🅶 🅷 I
			//					                    S
			// (!slope3 && !eq_D_H) This combination is better than directly !sim_D_H, can catch more shapes that are "close but different, didn't pass the main slope rule for some reason".
			if ( (!slope3 && !eq_D_H) && noteq(R, RC)) {	// H ≠ RC replaced because H = R
                if (slope4) {	// Divert based on current main slope formation marker.
                    // Original rule extension 1. Third parameter compares adjacent pixels to ensure no double mixing.
                    // Original rule extension 2. L-shape cannot appear again within the two-pixel gap on the opposite side.
                    if (eq_F_H && eq(G,H) && eq(F,R) && (noteq(Q,G)||eq(Q, QA))) {L=admixL(M,L,H); longslope4 = true;}
                    // Practice: Finally decided to replace !sim_F_H with !eq_F_H (because added any_eq3(I,H,S,SG) condition).
                    // Practice 2: E cannot equal I, otherwise it will form bubbles with the F branch!
                } else if (!eq_F_H && !eq_B_F && !eq_E_F && noteq(E,I) && none_eq2(F,C,I) && (eq_B_D&&eq(B,C)&&sim2(E,B) || eq_E_D&&(eq(E,C)||eq(H,I)||eq(I,S)&&eq(I,RI))) && (noteq(R, RI)||eq(R,I)) && eq(R,H) && eq(F,G) && noteq(G,SG)) {M=unpackUnorm4x8(F); sawslope = true;}
			}
			//                                  . A B C
			//                                  🆀 🅳 🄴 F R       Zone 3
			//                                    G 🅷 🅸
			//					                    S
			if ( (!longslope4 && !sawslope) && (!slope4 && !eq_F_H) && noteq(Q, QA) ) {	// H ≠ QA replaced because H = Q next
                if (slope3) {
                    if (eq_D_H && eq(D,Q) && eq(H,I) && (noteq(R,I)||eq(R, RC))) {M=admixL(L,M,H); longslope3 = true;}
                } else if (!eq_D_H && !eq_B_D && !eq_E_D && noteq(E,G) && none_eq2(D,A,G) && (eq_B_F&&eq(B,A)&&sim2(E,B) || eq_E_F&&(eq(E,A)||eq(H,G)||eq(G,S)&&eq(G,QG))) && (noteq(Q, QG)||eq(Q,G)) && eq(Q,H) && eq(D,I) && noteq(I,SI)) {L=unpackUnorm4x8(D); sawslope = true;}
			}
		}

		if ( (!sawslope) && !eq_E_B && none_eq2(B,G,I)) {

			//					                    P
			//                                    🅐 🅑 C
			//                                  Q D 🄴 🅵 🆁       Zone 2
			//                                    G H I .
			if ( (!longslope4) && (!slope1 && !eq_B_D) && noteq(R, RI)) {	// B ≠ RI replaced because B = R next
				if (slope2) {
					if (eq_B_F && eq(A,B) && eq(F,R) && (noteq(A,Q)||eq(Q, QG))) {J=admixL(K,J,B); longslope2 = true;}
				} else if (!eq_B_F && !eq_F_H && !eq_E_F && noteq(E,C) && none_eq2(F,C,I) && (eq_D_H&&eq(H,I)&&sim2(E,H) || eq_E_D&&(eq(E,I)||eq(B,C)||eq(I,S)&&eq(I,RI)||eq(C,P)&&eq(C,RC))) && (noteq(R, RC)||eq(R,C)) && eq(B,R) && eq(A,F) && noteq(A,PA)) {K=unpackUnorm4x8(F); sawslope = true;}
			}
			//					                    P
			//                                    A 🅑 🅲
			//                                  🆀 🅳 🄴 F R        Zone 1
			//                                  . G H I
			if ( (!longslope2 && !longslope3 && !sawslope) && (!slope2 && !eq_B_F) && noteq(Q, QG)) {	// B ≠ QG replaced
				if (slope1) {
					if (eq_B_D && eq(B,C) && eq(D,Q) && (noteq(C,R)||eq(R, RI))) {K=admixL(J,K,B); longslope1 = true;}
				} else if (!eq_B_D && !eq_D_H && !eq_E_D && noteq(E,A) && none_eq2(D,A,G) && (eq_F_H&&eq(H,G)&&sim2(E,H) || eq_E_F&&(eq(E,G)||eq(B,A)||eq(A,P)&&eq(A,QA))) && (noteq(Q, QA)||eq(Q,A)) && eq(B,Q) && eq(C,D) && noteq(C,PC)) {J=unpackUnorm4x8(D); sawslope = true;}
			}
		}

    }

	bool longslope = longslope1||longslope2||longslope3||longslope4;
	longslope1 = false; longslope2 = false; longslope3 = false; longslope4 = false;	// reset

	if ( (!longslope && !sawslope) && !eq_D_F ) { // vertical

                       // E point on the right
        if ( !eq_E_D && none_eq2(D,C,I) ) { // left

			//                                    🅐 B C
			//                                  Q 🅳 🄴 F R
			//                                    G 🅷 I        Zone 3
			//                                      🆂 .
            if ( (!slope1 && !eq_B_D) && noteq(S, SI) ) {	// D ≠ SI replaced
				if (slope3) {
					if(eq_D_H && eq(A,D) && eq(H,S) && (noteq(A,P)||eq(P, PC))) {J=admixL(L,J,D); longslope3 = true;}
				} else if (!eq_D_H && !eq_F_H && !eq_E_H && noteq(E,G) && none_eq2(H,G,I) && (eq_B_F&&eq(F,I)&&sim2(E,F) || eq_E_B&&(eq(E,I)||eq(D,G)||eq(G,Q)&&eq(G,SG))) && (noteq(S, SG)||eq(S,G)) && eq(D,S) && eq(A,H) && noteq(A,QA)) {L=unpackUnorm4x8(H); sawslope = true;}
            }
			//                                      🅟 .
			//                                    A 🅑 C
			//                                  Q 🅳 🄴 F R       Zone 1
			//                                    🅶 H I
			if ( (!longslope3 && !sawslope) && (!slope3 && !eq_D_H) && noteq(P, PC) ) {	// D ≠ PC replaced
				if (slope1) {
					if (eq_B_D && eq(D,G) && eq(B,P) && (noteq(G,S)||eq(S, SI))) {L=admixL(J,L,D); longslope1 = true;}
				} else if (!eq_B_D && !eq_B_F && !eq_E_B && noteq(E,A) && none_eq2(B,A,C) && (eq_F_H&&eq(F,C)&&sim2(E,H) || eq_E_H&&(eq(E,C)||eq(D,A)||eq(A,Q)&&eq(A,PA))) && (noteq(P, PA)||eq(P,A)) && eq(P,D) && eq(B,G) && noteq(G,QG)) {J=unpackUnorm4x8(B); sawslope = true;}
			}

		}

		if ( (!sawslope) && !eq_E_F && none_eq2(F,A,G) ) { // right

			//                                    A B 🅲
			//                                  Q D 🄴 🅵 R
			//                                    G 🅷 I        Zone 4
			//                                    . 🆂
			if ( (!longslope3) && (!slope2 && !eq_B_F) && noteq(S, SG)) {	// F ≠ SG replaced
				if (slope4) {
					if (eq_F_H && eq(C,F) && eq(H,S) && (noteq(P,C)||eq(P, PA))) {K=admixL(M,K,F); longslope4 = true;}
				} else if (!eq_F_H && !eq_D_H && !eq_E_H && noteq(E,I) && none_eq2(H,G,I) && (eq_B_D&&eq(D,G)&&sim2(E,D) || eq_E_B&&(eq(E,G)||eq(F,I)||eq(I,R)&&eq(I,SI))) && (noteq(S, SI)||eq(S,I)) && eq(S,F) && eq(H,C) && noteq(C,RC)) {M=unpackUnorm4x8(H); sawslope = true;}
			}
			//                                    . 🅟
			//                                    A 🅑 C
			//                                  Q D 🄴 🅵 R        Zone 2
			//                                    G H 🅸
			if ( (!longslope1 && !longslope4 && !sawslope) && (!slope4 && !eq_F_H) && noteq(P, PA)) {	// F ≠ PA replaced
				if (slope2) {
					if (eq_B_F && eq(F,I) && eq(B,P) && (noteq(I,S)||eq(S, SG))) {M=admixL(K,M,F); longslope2 = true;}
			 		 // B -> K
				} else if (!eq_B_F && !eq_B_D && !eq_E_B && noteq(E,C) && none_eq2(B,A,C) && (eq_D_H&&eq(D,A)&&sim2(E,D) || eq_E_H&&(eq(E,A)||eq(F,C)||eq(C,R)&&eq(C,PC))) && (noteq(P, PC)||eq(P,C)) && eq(P,F) && eq(B,I) && noteq(I,RI)) {K=unpackUnorm4x8(B); sawslope = true;}
			}
		}
	} // F ≠ D

longslope = longslope||longslope1||longslope2||longslope3||longslope4;

if (slopeBAD||longslope||sawslope||El>=7000000||Bl>=7000000||Dl>=7000000||Fl>=7000000||Hl>=7000000) {
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
	return;
}

/**************************************************
 *     "Concave + Cross" type (P100)	  *
 *************************************************/
// The far end of the cross star uses similar pixels, useful for some horizontal line + sawtooth and layered gradient shapes. e.g., SFIII 2nd Impact opening glowing text, SFZ3Mix Japanese houses, Garou Mark of the Wolves opening.
bool cnc = false;
    if (!slope3 && !slope4 &&
        Bl<El && !eq_E_D && !eq_E_F && eq_E_H && none_eq2(E,A,C) && all_eq2(G,H,I) && sim1(E,S) ) { // TOP
	    if (eq_B_D && eq_B_F) {
            J=admixK(B,J); K=J;
            L=mix(J,L, 0.61804); M=L;
        } else {
            if (!slope1) J=admixC(D,B,J);
            if (!slope2) K=admixC(B,F,K);
            }
	   cnc = true;
	}
    if (!slope1 && !slope2 && !cnc &&
		Hl<El && !eq_E_D && !eq_E_F && eq_E_B && none_eq2(E,G,I) && all_eq2(A,B,C) && sim1(E,P) ) { // BOTTOM
	    if (eq_D_H && eq_F_H) {
            L=admixK(H,L); M=L;
            J=mix(L,J, 0.61804); K=J;
        } else {
            if (!slope3) L=admixC(D,H,L);
            if (!slope4) M=admixC(F,H,M);
            }
	   cnc = true;
	}
    if (!slope1 && !slope3 && !cnc &&
		Fl<El && !eq_E_B && !eq_E_H && eq_E_D && none_eq2(E,C,I) && all_eq2(A,D,G) && sim1(E,Q) ) { // RIGHT
        if (eq_B_F && eq_F_H) {
            K=admixK(F,K); M=K;
            J=mix(K,J, 0.61804); L=J;
        } else {
            if (!slope2) K=admixC(B,F,K);
            if (!slope4) M=admixC(F,H,M);
            }
	   cnc = true;
	}
    if (!slope2 && !slope4 && !cnc &&
		Dl<El && !eq_E_B && !eq_E_H && eq_E_F && none_eq2(E,A,G) && all_eq2(C,F,I) && sim1(E,R) ) { // LEFT
        if (eq_B_D && eq_D_H) {
            J=admixK(D,J); L=J;
            K=mix(J,K, 0.61804); M=K;
        } else {
            if (!slope1) J=admixC(D,B,J);
            if (!slope3) L=admixC(D,H,L);
            }
	   cnc = true;
	}

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
	   ㇿО    Scorpion type (P99). Looks like the tracking bug in The Matrix. Can connect or flatten some regular alternating/single pixels.
*/
// At sawtooth intersections, using 50% similar pixels can blur some jagged edges, looks slightly better (JoJo's wall).
// Practice: 1. Scorpion tail and claw cannot be similar at the same time. 2. Scorpion tail Q, body D cannot use similarity.
// Note: The two pixels diagonally adjacent need to be fullsame.
// Among the four forms, only the scorpion is exclusive; once caught, can return early. Conversely, if other forms match (have been entered), also k.

bool scorpion = false;
                if (!eq_E_F &&eq_E_D&&eq_B_F&&eq_F_H && all_eq2(E,C,I) && noteq(F,src(+3, 0)) ) {K=admixK(F,K); M=K;J=mix(K,J, 0.61804); L=J;scorpion=true;}	// RIGHT
   if (!scorpion && !eq_E_D &&eq_E_F&&eq_B_D&&eq_D_H && all_eq2(E,A,G) && noteq(D,src(-3, 0)) ) {J=admixK(D,J); L=J;K=mix(J,K, 0.61804); M=K;scorpion=true;}	// LEFT
   if (!scorpion && !eq_E_H &&eq_E_B&&eq_D_H&&eq_F_H && all_eq2(E,G,I) && noteq(H,src(0, +3)) ) {L=admixK(H,L); M=L;J=mix(L,J, 0.61804); K=J;scorpion=true;}	// BOTTOM
   if (!scorpion && !eq_E_B &&eq_E_H&&eq_B_D&&eq_B_F && all_eq2(E,A,C) && noteq(B,src(0, -3)) ) {J=admixK(B,J); K=J;L=mix(J,L, 0.61804); M=L;}					// TOP

 } // end of all

    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
}
