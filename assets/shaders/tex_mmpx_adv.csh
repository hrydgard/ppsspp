/* MMPX.glc
   Copyright 2020 Morgan McGuire & Mara Gagiu.
   Provided under the Open Source MIT license https://opensource.org/licenses/MIT

   by Morgan McGuire and Mara Gagiu.
   2025-2026 Enhanced by CrashGG.
*/

// Performs 2x upscaling.

/* If we took an image as input, we could use a sampler to do the clamping. But we decode
   low-bpp texture data directly, so...

   use readColoru to read uint format color data from texture/framebuffer.
   in ppsspp ,readColorf(p) eq unpackUnorm4x8(readColoru(p)),It is not actually read directly.
*/

precision mediump float;

uint srcu(int x, int y) {

	// First perform out-of-bounds check to determine if coordinates are within valid range. Return transparent color if out of bounds.
    return (x >= 0 && x < params.width && y >= 0 && y < params.height) 
           ? readColoru(uvec2(x, y)) 
           : 0u;
}

#define src(c,d) unpackUnorm4x8(srcu(c,d))

// RGB visual weight + alpha segmentation
float luma(vec4 col) {

	// Use the CRT-era BT.601 standard. Clamp the result to [0.0 - 0.999]
    float rgbsum =min(dot(col.rgb, vec3(0.299, 0.587, 0.114)), 0.999);

	// Alpha weighting can be removed for subsequent fractional bit extraction
    float alphafactor = 
        (col.a > 0.854102) ? 0.0 :		// Large part of two short golden ratio splits
        (col.a > 0.618034) ? 2.0 :		// One golden ratio split
        (col.a > 0.381966) ? 4.0 :		// One short golden ratio split
        (col.a > 0.145898) ? 6.0 :		// Two short golden ratio splits
        (col.a > 0.002) ? 8.0 : 10.0;	// Fully transparent

    return rgbsum + alphafactor;

}

/* Explanation of several constants:
0.145898	:			Two short golden ratio splits of 1.0
0.0638587	:		Squared value after two short golden ratio splits of RGB Euclidean distance
0.4377		:		Squared value after one short golden ratio split of RGB Euclidean distance
0.75			:		Squared half of RGB Euclidean distance
*/

bool simb(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	float maxdiff = max(diff.r, max(diff.g, diff.b));
	float mindiff = min(diff.r, min(diff.g, diff.b));

	// Luminance base weight: both colors must satisfy > 0.078 (0.234÷3)
	float weight = step(0.234, min(col1.r+col1.g+col1.b, col2.r+col2.g+col2.b));
	// Transparency base weight: both colors must be fully opaque
	float weight2 = step(0.998, min(col1.a, col2.a));

	// Find the most opposite channel: if one positive and one negative, take the one with the smallest absolute value; 0 if same sign
	// Filter same-sign cases using max(0.0, ...)
	// Skip team_rebel if either pixel has luminance < 0.078 or is not fully opaque
	float team_rebel = min(max(0.0, maxdiff), max(0.0, -mindiff)) * weight * weight2;
	float finaldist = (maxdiff - mindiff) + team_rebel;

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	// Equivalent to (finaldist ÷ 0.145898 )²
	highp float factor = (finaldist * finaldist) * 46.9787;

	// Mask = 0.0 when two alphas are close, dot_diff[0-3] is valid at this point
	float alpha_match_mask = step(0.145898, abs(diff.a));

	return dot_diff < mix(0.0638587, 0.0, factor) - alpha_match_mask*5.0;
}

bool sim(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	// RGB color difference variation range (max_diff - min_diff)
	float delta_range = max(diff.r, max(diff.g, diff.b)) - min(diff.r, min(diff.g, diff.b));

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	// Equivalent to (delta_range ÷ 0.382 )²
	highp float factor = (delta_range * delta_range) * 6.8541;

	// Both near transparent: both_near_trans=1.0, return true unconditionally
	float both_near_trans = step(max(col1.a, col2.a), 0.381966);
	// Mask = 0.0 when two alphas are close, dot_diff[0-3]< mix() takes effect at this point
	float alpha_match_mask = step(0.381966, abs(diff.a));

	return dot_diff < mix(0.0638587, 0.0, factor) - alpha_match_mask*5.0 + both_near_trans*10.0;
}

float mixGate(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	// RGB color difference variation range (max_diff - min_diff)
	float delta_range = max(diff.r, max(diff.g, diff.b)) - min(diff.r, min(diff.g, diff.b));

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	// Equivalent to (delta_range ÷ 0.618 )²
	highp float factor = (delta_range * delta_range) * 2.618034;

	// Mask = 0.0 when alpha difference between two colors is within 0.5, dot_diff[0-3]< mix() takes effect at this point
	float alpha_match_mask = step(0.5, abs(diff.a));

	return step(dot_diff , mix(0.75, 0.0, factor) - alpha_match_mask*5.0);
}


#define eq(a,b) all(lessThan(abs(a-b), vec4(0.01, 0.01, 0.01, 0.145898)))

#define neq(a,b) !eq(a,b)

#define all_eq2(a, b1, b2) \
	( eq(a,b1) && eq(a,b2))

#define all_eq3(a, b1, b2, b3) \
	( eq(a,b1) && eq(a,b2) && eq(a,b3))

#define all_eq4(a, b1, b2, b3, b4) \
	( eq(a,b1) && eq(a,b2) && eq(a,b3) && eq(a,b4))

#define any_eq2(a, b1, b2) (eq(a,b1)||eq(a,b2))
#define any_eq3(a, b1, b2, b3) (eq(a,b1)||eq(a,b2)||eq(a,b3))
// Better than a!=b1 && a!=b2
#define none_eq2(a, b1, b2) !any_eq2(a, b1, b2)


// Three channels allow total int2 difference
//#define vec_neq(a,b) (dot(abs(a-b), vec4(1.0)) > 0.01)
// Three channels allow int2 difference per channel
//#define vec_eq(a,b) all(lessThan(abs(a-b), vec4(0.01)))
#define vec_neq(a, b) !eq(a,b)


// Pre-define
//const vec4 testcolor  = vec4(1.0, 0.0, 1.0, 1.0);  // Magenta
//const vec4 testcolor2 = vec4(0.0, 1.0, 1.0, 1.0);  // Cyan
//const vec4 testcolor3 = vec4(1.0, 1.0, 0.0, 1.0);  // Yellow
//const vec4 testcolor4 = vec4(1.0, 1.0, 1.0, 1.0);  // White
const vec4 slopOFF   = vec4(2.0);
const vec4 slopeBAD  = vec4(4.0);
const vec4 theEXIT   = vec4(8.0);

#define mixXE mix(vX,vE,mixFactor)
#define mixXEoff mixXE+slopOFF
#define Xoff vX+slopOFF
//#define checkblack(col) ((col).g < 0.078 && (col).r < 0.1 && (col).b < 0.1)
#define checkblack(col) all(lessThan((col).rgb, vec3(0.1, 0.078, 0.1)))
#define checkwhite(col) all(greaterThan((col).rgb, vec3(0.92, 0.92, 0.92)))

//pin zz
// Concave + Cross shape: Weak blending (weak blend/none)
vec4 admixC(vec4 vX, vec4 vE) {
	// Weak blending. Blendable? 0.618 else 1.0
	float mixFactor = mixGate(vX, vE) * (-0.381966) + 1.0;

	return mixXE;
}

// K-shape: Forced weak blending
vec4 admixK(vec4 vX, vec4 vE) {
    vec4 diff = vX - vE;
	// mixFactor slides from 0.5-1.0 based on point set distance, quadratic curve, steeper closer to 1.0
	float mixFactor = dot(diff.rgb, diff.rgb) * 0.16666 + 0.5;
	// mixFactor slides linearly from 0.5-1.0 based on Euclidean distance
	//float mixFactor = distance(vX, vE) * 0.28867 + 0.5;
	return mixXE;
}

// L-shape: 2:1 slope, extension of main corner
// Practical note: This rule requires all 4 pixels on the strict slope to be identical. Otherwise, various glitches will occur!
vec4 admixL(vec4 vX, vec4 vE, vec4 vS) {

    // Judging eqX,E would catch many duplicate pixels, but the main thread has passed slopeok filtering.

	// If target X differs from reference S(sample), it means blending has been done once; directly copy, no need to blend again
	if (vec_neq(vX, vS)) return vX;

	float mixFactor = 0.381966 * mixGate(vX,vE);

    return mixXE;
}

#define vE E
#define vB B
#define vD D
#define vF F
#define vH H
#define vA A
#define vC C
#define vG G
#define vI I

/**************************************************************************************************************************************
 * 												main slope + X cross-processing mechanism						                *
 ******************************************************************************************************************************** zz  */
vec4 admixX( vec4 A, vec4 B, vec4 C, vec4 D, vec4 E, vec4 F, vec4 G, vec4 H, vec4 I
		  , vec4 P, vec4 PA, vec4 PC, vec4 Q, vec4 QA, vec4 QG, vec4 R, vec4 RC, vec4 RI, vec4 S, vec4 SG, vec4 SI, vec4 AA, vec4 CC, vec4 GG
		  , float El, float Bl, float Dl, float Fl, float Hl
		  //, vec4 vE, vec4 vB, vec4 vD, vec4 vC, vec4 vG
		  ) {


	bool eq_B_C = eq(B,C);
	bool eq_D_G = eq(D,G);

    // Exit if sandwiched by bilateral straight walls
    if (eq_B_C && eq_D_G) return slopeBAD;


	//Pre-declare
	bool eq_B_P;		bool eq_B_PA;	bool eq_B_PC;
	bool eq_D_Q;		bool eq_D_QA;	bool eq_D_QG;
	bool eq_E_F;		bool eq_E_H;		bool eq_A_AA;

	vec4 vX;
	float mixFactor;


	bool eq_E_C = eq(E,C);
	bool eq_E_G = eq(E,G);
    bool eq_A_P = eq(A,P);
    bool eq_A_Q = eq(A,Q);
    bool comboE3 = eq_E_C && eq_E_G;
    bool comboA3 = eq_A_P && eq_A_Q;

/*=========================================
                    B != D
  ==================================== zz */
if (neq(B,D)){

	// E == A violates preset logic, exit
	if (eq(E,A)) return slopeBAD;

	// B-D unconnected? Removed

	// B and D are different, and the difference is larger than either side to center E; exit
	float diffBD = abs(Bl-Dl);
	if (diffBD > El-Bl || diffBD > El-Dl) return slopeBAD;


	// X is the blend of B and D
	vX = mix(vB, vD, 0.5);
	vX.a = min(vB.a, vD.a);

	mixFactor = 0.381966 * mixGate(vX,vE) * step(0.002, vE.a);

	eq_B_PC = eq(B,PC);
	eq_D_QG = eq(D,QG);
 
	// Strong trend collection
    if (none_eq2(A,B,D)){
		if (comboA3) return mixXEoff;
		if ( eq_A_P && eq_B_PC && !eq_B_C ) return mixXEoff;
		if ( eq_A_Q && eq_D_QG && !eq_D_G ) return mixXEoff;

		// Double slope sandwiching BD, note the matching direction
		if ( eq_A_P && eq_E_G ) return mixXEoff;
		if ( eq_A_Q && eq_E_C ) return mixXEoff;

		// Hollow L inner curve
		if ( eq_E_C && eq_D_G ) return mixXEoff;
		if ( eq_E_G && eq_B_C ) return mixXEoff;

}
    // E-side three in a row
    if ( comboE3 ) return mixXEoff;

	// Original rule, added a slope condition
    if ( eq_E_C && eq_B_PC && neq(B,P)) return mixXEoff;
	if ( eq_E_G && eq_D_QG && neq(D,Q)) return mixXEoff;

	eq_E_F = eq(E,F);

	// F - H
	if (eq(F,H)) {

		// Double slope (exclude single pixel surrounded by C, BD different colors, loose connection)
		if ( eq_E_C && !eq_D_G && (!eq_E_F||neq(E,P)) ) return mixXEoff;
		if ( eq_E_G && !eq_B_C && (!eq_E_F||neq(E,Q)) ) return mixXEoff;

		// F+ H+ extension
		if ( !eq_E_F && eq_B_PC && eq(F,RC) ) return mixXEoff;
		if ( !eq_E_F && eq_D_QG && eq(H,SG) ) return mixXEoff;
	}

    return slopeBAD;
} // B != D


						/*********  B == D  *********/

	// Remove alpha channel weighting
	Bl = fract(Bl);
	Dl = fract(Dl);
	El = fract(El);
	Fl = fract(Fl);
	Hl = fract(Hl);
  
	// Avoid font edges being squeezed on three sides by black background
	bool Xisblack = checkblack(vB);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

	vX = vB;
	vX.a = min(vB.a, vD.a);

	mixFactor = 0.381966 * mixGate(vX,vE) * step(0.002, vE.a);

	bool B_slope;	bool B_tower;	bool B_wall;
    bool D_slope;	bool D_tower;	bool D_wall;
	bool En3;
    #define En4square En3&&eq(E,I)

/*===================================================
                    E - A Intersection
  ============================================== zz */
if (eq(E,A)) {

    // Special pattern: Dithering pattern
    // Goal: Forced blending

	eq_E_F = eq(E,F);
	eq_E_H = eq(E,H);

	bool Eisblack = checkblack(vE);

	// 1. Dithering center
    if ( comboE3 && !eq_E_F && !eq_E_H && eq(E,I) ) {

		// Exit if center E is black (The King of Fighters '96 power gauge, The Punisher's belt) to avoid excessive contrast blending
		if (Eisblack) return theEXIT;
		// Practical note 1: Cannot catch black B point, that's normal logic entry
		// Blendable? 0.381966 else 0.618034
		mixFactor = 0.618034 * (1.0 - mixFactor);
		return mixXEoff;
	}


	eq_A_AA = eq(A,AA);

	// 2. Dithering edge
    if ( comboA3 && eq_A_AA && none_eq2(A,PA,QA) )  {	
		if (Eisblack) return theEXIT;
		// Blendable? 0.381966 else 0.618034
		mixFactor = 0.618034 * (1.0 - mixFactor);
        // Layered gradient edges use strong blending
		if ( neq(B,PA) && eq(PA,QA) ) return mixXEoff;
        // Remaining perfect cross, must be dithering edge, use weak blending.
		// Practical note: No need to specify health bar border cases separately.
        // Guaranteed weak blending. Blendable? 0.618 else 0.854
		// Note mixFactor has been modified once in the upper layer
		mixFactor += 0.236068;
		return mixXEoff;
	}

	// Return X early if center pixel is fully transparent
	if (vE.a<0.002) return vX;

    eq_B_PC = eq(B,PC);
    eq_B_PA = eq(B,PA);
    eq_D_QG = eq(D,QG);
    eq_D_QA = eq(D,QA);

	// No need to judge Eisblack for subsequent two cases
	// 3. Semi-dithering, usually silhouette edge shadow expression, use weak blending

	if ( comboE3 && comboA3 &&
		(eq_B_PC || eq_D_QG) && eq_D_QA && eq_B_PA) {
        // Guaranteed weak blending. Blendable? 0.618 else 0.854
		mixFactor = mixFactor * (-0.618034) + 0.8541;
        return mixXEoff;
	}

    // 4. Quarter dithering, prone to ugly small pinky effect (Guile's plane in Street Fighter II, character select screen in Cadillacs and Dinosaurs)

	if ( comboE3 && eq_A_P
		 && eq_B_PA && eq_D_QA && eq_D_QG
		 && eq_E_H
		) {// Guaranteed weak blending. Blendable? 0.618 else 0.854
		mixFactor = mixFactor * (-0.618034) + 0.8541;
        return mixXEoff;
		}

	if ( comboE3 && eq_A_Q
		 && eq_B_PA && eq_D_QA && eq_B_PC
		 && eq_E_F
		) {// Guaranteed weak blending. Blendable? 0.618 else 0.854
		mixFactor = mixFactor * (-0.618034) + 0.8541;
        return mixXEoff;
		}


    // A-side three in a row, strong pattern, must come after dithering rules.
	if (comboA3) return Xoff;

    // E-side three in a row (must come after comboA3)
    if (comboE3) return mixXEoff;

	eq_B_P = eq(B, P);
	eq_D_Q = eq(D, Q);

	B_slope = eq_B_PC && !eq_B_P && !eq_B_C && !eq_B_PA;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G && !eq_D_QA;

	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;	// Remove one misalignment check
	D_wall = eq_D_G && !eq_D_QG && !eq_D_Q;	// Remove one misalignment check
	
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;


	if ( B_slope && eq_E_G ) return mixXEoff;
	if ( D_slope && eq_E_C ) return mixXEoff;


// E B D region checkerboard scoring rules

    float scoreE = 0.0; float scoreB = 0.0; float scoreD = 0.0; float scoreZ = 0.0;

//	E Zone
    if (eq_E_C) {
		scoreE += 1.0 +float(eq(F,H)) +float(B_slope);
		scoreE -= float(all_eq2(E,P,PC)&&!D_wall);
	}

    if (eq_E_G) {
        scoreE += 1.0 +float(eq(F,H)) +float(D_slope);
		scoreE -= float(all_eq2(E,Q,QG)&&!B_wall);
    }

	// Higher priority than rectangle
	scoreE += float(B_slope && eq_A_Q || D_slope && eq_A_P);

    En3 = eq_E_F && eq_E_H;

	// Clear 4/6 rectangle exit early, do not participate in final Z long slope judgment
	if ( scoreE<0.1 && mixFactor<0.1 && En4square && eq(E,S)==eq(E,SI) && eq(E,R)==eq(E,RI) ) return theEXIT;

	// No score for En3
	//if ( scoreE==0 && En3 ) scoreE += 1;

	// Single bar
	if ( scoreE<0.1 && !En3 && neq(E,I) ) {
		if ( B_wall && eq_E_F ) return theEXIT;
		if ( D_wall && eq_E_H ) return theEXIT;
    }

	// Lower priority than single bar
	scoreE += float(B_slope && eq_A_P || D_slope && eq_A_Q);

    if ( !En3 && eq(F,H) ) {
		if (Eisblack) return slopeBAD;		// Black single pixel
		// slope+ eq_F_H combination cannot be used, will generate bubbles with inner L in BD area
		//scoreE += float(B_slope&&neq(C,F))+float(D_slope&&neq(G,H));

		bool condZ1 = B_wall && (eq(F,R) || eq(F,RC) || eq(G,H) || eq(F,I));
		bool condZ2 = D_wall && (eq(C,F) || eq(H,SG) || eq(H,S) || eq(F,I));
		scoreZ = float(condZ1 || condZ2);
    }


//	B Zone

    if (eq_B_PA) {
		scoreB -= 1.0 +float(eq(P,C)) +float(eq_A_AA);
	}

	if (eq(P,C)){
		scoreB -= float(eq_A_AA); 
		// Critical. Prevent Z scoring only from F==H in clone patterns on this side!
		// Equivalent: if (scoreE==0) scoreZ = 0;
		scoreZ *= float(scoreE < 0.1);
	}

//  D Zone

    if (eq_D_QA) {
		scoreD -= 1.0 +float(eq(G,Q)) +float(eq_A_AA);
	}

	if (eq(G,Q)){
		scoreD -= float(eq_A_AA);
		// Same logic as B zone
		scoreZ *= float(scoreE < 0.1);
	}

    float scoreFinal = scoreE + scoreB + scoreD + scoreZ ;

	// Long slope pattern: return vX without blending when forming a long slope with no deductions in B/D zones
	scoreFinal += float(min(scoreB,scoreD) > -0.1 && (B_wall && D_tower || B_tower && D_wall)) *2.0;

	// Set mixFactor to 0 and return vX without blending if scoreFinal >= 2
	mixFactor *= (1.0 - step(1.9, scoreFinal));
	// Return mixXE if scoreFinal >= 1, else slopeBAD
	return mixXE + slopeBAD*(1.0 - step(0.9, scoreFinal));

}	// E == A

	
/*===============================================
                 Main Rule	E - C - G 
  ========================================== zz */

    if (eq_E_C ) {
		if (comboA3) return vX;
		if (comboE3) return mixXE;
		if (all_eq2(B,A,PA) && all_eq3(E,F,P,PC)) return theEXIT;
		return mixXE;
	}

	if (eq_E_G) {
		if (comboA3) return vX;
		if (comboE3) return mixXE;
		if (all_eq2(D,A,QA) && all_eq3(E,H,Q,QG)) return theEXIT;
		return mixXE;
	}


/*=========================================================
                   F - H / B+ D+ Extension New Rules
  ==================================================== zz */

	// This section is the remainder after the previous filtering, En4square in the center area is naturally wall-isolated from BD logic
    // B-D unconnected? No longer needed after new "double slope rule" processing.
	// Experience 1: Hollow L(1+2) flattens inner side but not outer side
	// Experience 2: “厂”shape edge flattens outer side but not inner side

	// No need to participate in subsequent calculations if E is hollow with no diagonal connections
	if (vE.a<0.002) return theEXIT;

    bool eq_A_B = eq(A,B);
    bool eq_F_H = eq(F,H);

	eq_B_P  = eq(B,P);
	eq_B_PC = eq(B,PC);
	eq_B_PA = eq(B,PA);
	eq_D_Q  = eq(D,Q);
	eq_D_QG = eq(D,QG);
	eq_D_QA = eq(D,QA);

	B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;
	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;
	D_wall = eq_D_G && !eq_D_QG && !eq_D_Q;


//	1. B-D hollow slope
    if (!eq_A_B) {

        // A-side three in a row, high priority
		// Note: comboA3 cannot be used without A!=B in this section
        if (comboA3) return Xoff;

        if ( (B_slope||B_tower) && (D_slope||D_tower) ) return Xoff;

        if ( B_slope && eq_A_P ) return mixXEoff;
        if ( D_slope && eq_A_Q ) return mixXEoff;

        if ( (B_slope || D_slope) && eq_F_H ) return mixXEoff;

        if ( B_slope && eq(H,SG) ) return mixXEoff;
        if ( D_slope && eq(F,RC) ) return mixXEoff;

        if ( B_slope && eq_A_Q && eq(Q,QG) ) return mixXEoff;
        if ( D_slope && eq_A_P && eq(P,PC) ) return mixXEoff;

    }



	bool sim_EC = sim(vE, vC);
	bool sim_EG = sim(vE, vG);

	// Exit if center E is a single high-contrast pixel
	// Tighten threshold if E is bright
	float E_lumDiff = mix(0.381966, 0.145898, max((El - 0.8541),0.0) * 6.8541);

	// Large difference from surroundings (lower priority than slope detection)
    if ( mixFactor<0.1 && !sim_EC && !sim_EG && E.a>0.381966 && neq(E,I) && abs(El-Fl)>E_lumDiff && abs(El-Hl)>E_lumDiff ) return slopeBAD;


	eq_E_F = eq(E,F);
	eq_E_H = eq(E,H);

    // Long slope special trend
    // Note: let squares pass, handled by En4square judgment later
	if ( eq_B_C && eq_D_Q ) {
		if ( eq(P,PC) && eq(A,QA) && !eq_D_QG && eq_E_F && !eq_E_H && eq(H,I)) return theEXIT;
		if ( eq_A_B ) return slopeBAD;
		if ( B_wall && D_tower && eq_E_F) return vX;
		return mixXEoff;
	}

	if ( eq(D,G) && eq(B,P)) {
		if ( eq(Q,QG) && eq(A,PA) && !eq_B_PC && eq_E_H && !eq_E_F && eq(F,I)) return theEXIT;
		if ( eq_A_B ) return slopeBAD;
		if ( B_tower && D_wall && eq_E_H) return vX;
		return mixXEoff;
	}


    En3 = eq_E_F && eq_E_H;

    // Wall-enclosed 4-pixel square (En3 && eq(E,I))
	if ( En4square ) {  // This square detection must come after the previous rule
        // Exit for solid L inner enclosure (some font edges, building corners)
        // L inner enclosure (corner hollow) / high-contrast independent clear 4-pixel square / 6-pixel rectangle (no need to judge eq(G,H), eq(C,F) for rectangle edges, impossible)
        if ( ( eq_B_C || eq_D_G) && eq_A_B) return theEXIT;
        if ( ( eq_B_C || eq_D_G || mixFactor<0.1) && (eq(E,S) == eq(E, SI) && eq(E,R) == eq(E, RI)) ) return theEXIT;
        return mixXEoff;
    }

	// xxx.alpha
	if (vE.a<0.381966) return mixXEoff;

	// BD-side solid non-wall pattern
	if (!eq_B_C && !eq_D_G ) {
		 // BD-side semi-solid 1 (requires F-H)
		if ( comboA3 && eq_F_H ) return Xoff;

		// BD-side semi-solid 2 (add a "definitely round" trend judgment)
		if ( comboA3&&eq_B_PC&&eq(C,CC) ) return Xoff;
		if ( comboA3&&eq_D_QG&&eq(G,GG) ) return Xoff;

		// BD unconnected and non-En3 exit (practical: required for this branch section)
		if ( !eq_B_P && !eq_B_PC && !eq_D_Q && !eq_D_QG && !En3 ) return slopeBAD;

		// 3 diagonal gradients after excluding the above
		if (eq_A_Q&&sim_EC) return mixXEoff;
		if (eq_A_P&&sim_EG) return mixXEoff;
		if (sim_EC&&sim_EG ) return mixXEoff;
	}
	
    // Wall-enclosed triangle (remove one solid corner, leave the rest to the next rule)
 	if ( En3 && eq_A_B) return theEXIT;

    // F - H
	// Principle: Connect L inner curve, not L outer curve
	if (eq_F_H) {

		// F-H three-star pattern, huge boost! Prioritize A==B
		if ( eq_B_PC&&eq(F,RC) || eq_D_QG&&eq(H,SG) ) return mixXEoff;

		if (eq_A_B) return slopeBAD;

		if ( eq_B_C || eq_D_G) return mixXEoff;
		if ( eq_B_PC || eq_D_QG) return mixXEoff;

	}

	return slopeBAD;

}	// admixX


vec4 admixS( vec4 A, vec4 B, vec4 C, vec4 D, vec4 E, vec4 F, vec4 G, vec4 H, vec4 I
		   , vec4 R, vec4 RC, vec4 RI, vec4 S, vec4 SG, vec4 SI, vec4 II, vec4 CC
		   //, vec4 vE, vec4 vF, vec4 vC
		   ) {

			//                                   Ａ B Ｃ .
			//                                 ＱＤ 🄴 🅵 🆁       Zone 4
			//                                   🅶 🅷 Ｉ
			//                                      S


    if (any_eq2(F,C,I)) return vE;


	if ( (eq(F,RI) || eq(G,S) || eq(R, RI)) && neq(R,I) ) return vE;

    if (eq(H, S) && none_eq2(H,I,SG)) return vE;

    if ( eq(R, RC) || eq(G,SG) ) return vE;


	if ( checkwhite(vE) && all_eq2(E,C,D) && none_eq2(E,RC,CC)) return vE;


	#define vX vF
	float mixFactor = 0.381966 * mixGate(vX,vE) * step(0.002, vE.a);	// xxx.alpha

	if ( eq(E,C) && (eq(E,D)||eq(B,D)) ) return mixXE;

	bool sim_E_C = sim(vE,vC);

	if ( sim_E_C && eq(E,D) && eq(B,C) ) return mixXE;

	if ( (sim_E_C || mixFactor>0.1) && all_eq2(B,C,D) ) return mixXE;

    return vE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////// zz

void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

    vec4 E = src(srcX, srcY);
	vec4 B = src(srcX, srcY-1);
	vec4 D = src(srcX-1, srcY);
    vec4 F = src(srcX+1, srcY);
    vec4 H = src(srcX, srcY+1);

    vec4 J = E, K = E, L = E, M = E;

    bool eq_E_D = eq(E,D);
    bool eq_E_F = eq(E,F);
    bool eq_E_B = eq(E,B);
    bool eq_E_H = eq(E,H);
    bool eq_B_H = eq(B,H);
    bool eq_D_F = eq(D,F);

// Skip horizontal/vertical 3x1 lines
bool skiprest = (eq_E_D && eq_E_F) || (eq_E_B && eq_E_H) || (eq_B_H && eq_D_F);
if (!skiprest) {


    // Fetch 5x5 pixel grid
    vec4 A = src(srcX-1, srcY-1);
    vec4 C = src(srcX+1, srcY-1);
    vec4 G = src(srcX-1, srcY+1);
    vec4 I = src(srcX+1, srcY+1);
    vec4 P = src(srcX, srcY-2);
    vec4 Q = src(srcX-2, srcY);
    vec4 R = src(srcX+2, srcY);
    vec4 S = src(srcX, srcY+2);


    vec4 PA = src(srcX-1, srcY-2);
    vec4 PC = src(srcX+1, srcY-2);
    vec4 QA = src(srcX-2, srcY-1);
    vec4 QG = src(srcX-2, srcY+1); //             AA    PA    [P]   PC    CC
    vec4 RC = src(srcX+2, srcY-1); //                ┌──┬──┬──┐
    vec4 RI = src(srcX+2, srcY+1); //             QA │  A │  B │ C  │ RC
    vec4 SG = src(srcX-1, srcY+2); //                ├──┼──┼──┤
    vec4 SI = src(srcX+1, srcY+2); //            [Q] │  D │  E │ F  │ [R]
    vec4 AA = src(srcX-2, srcY-2); //                ├──┼──┼──┤
    vec4 CC = src(srcX+2, srcY-2); //             QG │  G │  H │ I  │ RI
    vec4 GG = src(srcX-2, srcY+2); //                └──┴──┴──┘
    vec4 II = src(srcX+2, srcY+2); //             GG    SG    [S]   SI    II


// Precompute luminance
    float Bl = luma(vB);
    float Dl = luma(vD);
    float El = luma(vE);
    float Fl = luma(vF);
    float Hl = luma(vH);


// 	Pre-calculate
    bool eq_B_D = eq(B,D);
    bool eq_B_F = eq(B,F);
    bool eq_D_H = eq(D,H);
    bool eq_F_H = eq(F,H);

    // Any mirrored block surrounds the center pixel
    bool oppoPix =  eq_B_H || eq_D_F;
	// Flag for entering admixX function if caught by 1:1 slope rule
    bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;
	// Standard pixel returned normally after passing 1:1 slope rule
    bool slope1ok = false;  bool slope2ok = false;  bool slope3ok = false;  bool slope4ok = false;
	// slopeBAD: entered admixX but returned E point (at least one of JKLM)
    // slopOFF: returned with OFF mark, not participating in long slope calculation later


// B - D
	if ( (vB.a>0.002 && vD.a>0.002) &&
		(!eq_E_B && !eq_E_D && !oppoPix) && (!eq_D_H && !eq_B_F)
	 && (eq(E,A) || El>=Dl&&El>=Bl) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || neq(E,P) || neq(E,Q) )
	 && ( eq_B_D &&(eq(E,A)||eq(B,PC)||eq(D,QG)||sim(vE,vC)||sim(vE,vG)) || simb(vB,vD)&&(eq_F_H||eq(E,C)||eq(E,G)) )
		) {
		J=admixX(A,B,C,D,E,F,G,H,I
				,P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG
				,El, Bl, Dl, Fl, Hl
				//,vE, vB, vD, vC, vG
				);
		slope1 = true;			// Mark on entry
		slope1ok = (J.b < 1.1);	// Normal pixel
		skiprest = (J.b > 7.1);	// theEXIT
		J = (J.b > 3.1) ? vE :		// Restore vE for slopeBAD/theEXIT
			(J.b > 1.1) ? (J - 2.0) :// slopeoff
			J;					// Normal pixel [0-1.0]
	}
// B - F
	if ( !slope1 && (vB.a>0.002 && vF.a>0.002)
	 && (!eq_E_B && !eq_E_F && !oppoPix) && (!eq_B_D && !eq_F_H)
	 && (eq(E,C) || El>=Bl&&El>=Fl) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || neq(E,P) || neq(E,R) )
	 && ( eq_B_F &&(eq(E,C)||eq(B,PA)||eq(F,RI)||sim(vE,vA)||sim(vE,vI)) || simb(vB,vF)&&(eq_D_H||eq(E,A)||eq(E,I)) ) 
	 ) {
		K=admixX(C,F,I,B,E,H,A,D,G
				,R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA
				,El,Fl,Bl,Hl,Dl
				//,vE,vF,vB,vI,vA
				);
		slope2 = true;
		slope2ok = (K.b < 1.1);
		skiprest = (K.b > 7.1);
		K = (K.b > 3.1) ? vE :	
			(K.b > 1.1) ? (K - 2.0) :
			K;
	}
// D - H
	if ( !slope1 && !skiprest && (vD.a>0.002 && vH.a>0.002)
	 && (!eq_E_D && !eq_E_H && !oppoPix) && (!eq_F_H && !eq_B_D)
	 && (eq(E,G) || El>=Hl&&El>=Dl)  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || neq(E,S) || neq(E,Q))
	 &&	( eq_D_H &&(eq(E,G)||eq(D,QA)||eq(H,SI)||sim(vE,vA)||sim(vE,vI)) || simb(vD,vH)&&(eq_B_F||eq(E,A)||eq(E,I)) )
	 ) {
		L=admixX(G,D,A,H,E,B,I,F,C
				,Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II
				,El,Dl,Hl,Bl,Fl
				//,vE,vD,vH,vA,vI
				);
		slope3 = true;
		slope3ok = (L.b < 1.1);
		skiprest = (L.b > 7.1);
		L = (L.b > 3.1) ? vE :	
			(L.b > 1.1) ? (L - 2.0) :
			L;
	}
// F - H
	if ( !slope2 && !slope3 && !skiprest && (vF.a>0.002 && vH.a>0.002)
	 && (!eq_E_F && !eq_E_H && !oppoPix) && (!eq_B_F && !eq_D_H)
	 && (eq(E,I) || El>=Fl&&El>=Hl)  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || neq(E,R) || neq(E,S))
	 && ( eq_F_H &&(eq(E,I)||eq(F,RC)||eq(H,SG)||sim(vE,vC)||sim(vE,vG)) || simb(vF,vH)&&(eq_B_D||eq(E,C)||eq(E,G)) )
	  ) {
		M=admixX(I,H,G,F,E,D,C,B,A
				,S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC
				,El,Hl,Fl,Dl,Bl
				//,vE,vH,vF,vG,vC
				);
		slope4 = true;
		slope4ok = (M.b < 1.1);
		skiprest = (M.b > 7.1);
		M = (M.b > 3.1) ? vE :	
			(M.b > 1.1) ? (M - 2.0) :
			M;
	}


//  long gentle 2:1 slope  (P100)

	if (slope4ok) { //zone4 long slope
		// Original rule extension 1. Pass adjacent pixel comparison as 3rd parameter in admixL to ensure no double blending
		// Original rule extension 2. No L pattern can reappear within the interval of opposite two pixels unless forming a wall
		if (all_eq2(R,F,G) && neq(R, RC) && (neq(Q,G)||eq(Q, QA))) {L=admixL(M,L,vH); skiprest = true;}
		// vertical
		if (all_eq2(S,H,C) && neq(S, SG) && (neq(P,C)||eq(P, PA))) {K=admixL(M,K,vF); skiprest = true;}
	}

	if (slope3ok) { //zone3 long slope
		// horizontal
		if (all_eq2(Q,D,I) && neq(Q, QA) && (neq(R,I)||eq(R, RC))) {M=admixL(L,M,vH); skiprest = true;}
		// vertical
		if (all_eq2(S,H,A) && neq(S, SI) && (neq(A,P)||eq(P, PC))) {J=admixL(L,J,vD); skiprest = true;}
	}

	if (slope2ok) { //zone2 long slope
		// horizontal
		if (all_eq2(R,F,A) && neq(R, RI) && (neq(A,Q)||eq(Q, QG))) {J=admixL(K,J,vB); skiprest = true;}
		// vertical
		if (all_eq2(P,B,I) && neq(P, PA) && (neq(I,S)||eq(S, SG))) {M=admixL(K,M,vF); skiprest = true;}
	}

	if (slope1ok) { //zone1 long slope
		// horizontal
		if (all_eq2(Q,D,C) && neq(Q, QG) && (neq(C,R)||eq(R, RI))) {K=admixL(J,K,vB); skiprest = true;}
		// vertical
		if (all_eq2(P,B,G) && neq(P, PC) && (neq(G,S)||eq(S, SI))) {L=admixL(J,L,vD); skiprest = true;}
	}

// Can exit after longslope formation, basically no sawslope formed on diagonals
// Note: sawslope cannot exclude slopeOFF (few) and slopeBAD (very few), but can exclude slopeok (strong pattern)
if (!skiprest && !oppoPix && !slope1ok && !slope2ok && !slope3ok && !slope4ok) {


        // horizontal bottom
    if (!eq_E_H && none_eq2(H,A,C)) {

        //                                    A B Ｃ ・
        //                                  Q D 🄴 🅵 🆁       Zone 4
        //					                🅶🅷 I
        //					                  Ｓ
        // (!slope3 && neq(D,H)) such consecutive comparisons are useful
        if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_F_H && vF.a>0.002 &&
            !eq_E_F && eq(R,H) && eq(F,G) ) {
            M = admixS( A, B, C, D, E, F, G, H, I
                      , R, RC, RI, S, SG, SI, II, CC
                      //, vE, vF, vC
                      );
            skiprest = true;}

        //                                  ・  A Ｂ C
        //                                  🆀 🅳 🄴 Ｆ R       Zone 3
        //                                     G 🅷 🅸
        //					                   Ｓ
        if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H && vD.a>0.002 &&
             !eq_E_D && eq(Q,H) && eq(D,I) ) {
            L = admixS( C, B, A, F, E, D, I, H, G
                      , Q, QA, QG, S, SI, SG, GG, AA
                      //, vE, vD, vA
                      );
            skiprest = true;}
    }

    // horizontal up
    if ( !skiprest && !eq_E_B && none_eq2(B,G,I)) {

        //					                   Ｐ
        //                                    🅐 🅑 Ｃ
        //                                  ＱＤ 🄴 🅵 🆁       Zone 2
        //                                    Ｇ H  I  .
        if ( (!slope1 && !eq_B_D)  && (!slope4 && !eq_F_H) && !eq_B_F && vF.a>0.002 &&
              !eq_E_F && eq(B,R) && eq(A,F) ) {
            K = admixS( G, H, I, D, E, F, A, B, C
                      , R, RI, RC, P, PA, PC, CC, II
                      //, vE, vF, vI
                      );
            skiprest = true;}

        //					                  Ｐ
        //                                    A 🅑 🅲
        //                                 🆀 🅳 🄴 Ｆ R        Zone 1
        //                                  . G Ｈ I
        if ( !skiprest && (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_B_D && vD.a>0.002 &&
             !eq_E_D && eq(B,Q) && eq(C,D) ) {
            J = admixS( I, H, G, F, E, D, C, B, A
                      , Q, QG, QA, P, PC, PA, AA, GG
                      //, vE, vD, vG
                      );
            skiprest = true;}

    }

    // vertical left
    if ( !skiprest && !eq_E_D && none_eq2(D,C,I) ) {

        //                                    🅐 B Ｃ
        //                                  Q 🅳 🄴 Ｆ R
        //                                    Ｇ 🅷 I        Zone 3
        //                                       🆂 ・
        if ( (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H && vH.a>0.002 &&
              !eq_E_H && eq(D,S) && eq(A,H) ) {
            L = admixS( C, F, I, B, E, H, A, D, G
                      , S, SI, SG, Q, QA, QG, GG, II
                      //, vE, vH, vI
                      );
            skiprest = true;}

        //                                      🅟 ・
        //                                    A 🅑 C
        //                                  Q 🅳 🄴 F R       Zone 1
        //                                    🅶 ＨＩ
        if ( !skiprest && (!slope3 && !eq_D_H) && (!slope2 && !eq_B_F) && !eq_B_D && vB.a>0.002 &&
              !eq_E_B && eq(P,D) && eq(B,G) ) {
            J = admixS( I, F, C, H, E, B, G, D, A
                      , P, PC, PA, Q, QG, QA, AA, CC
                      //, vE, vB, vC
                      );
            skiprest = true;}

    }

    // vertical right
    if ( !skiprest && !eq_E_F && none_eq2(F,A,G) ) { // right

        //                                    A B 🅲
        //                                  Q D 🄴 🅵 R
        //                                    G 🅷 I        Zone 4
        //                                    . 🆂
        if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_F_H && vH.a>0.002 &&
              !eq_E_H && eq(S,F) && eq(H,C) ) {
            M = admixS( A, D, G, B, E, H, C, F
                      , I, S, SG, SI, R, RC, RI, II, GG
                      //, vE, vH, vG
                      );
            skiprest = true;}

        //                                    ・ 🅟
        //                                    A 🅑 C
        //                                  Q D 🄴 🅵 R        Zone 2
        //                                    G H 🅸
        if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_B_F && vB.a>0.002 &&
             !eq_E_B && eq(P,F) && eq(B,I) ) {
            K = admixS( G, D, A, H, E, B, I, F, C
                      , P, PA, PC, R, RI, RC, CC, AA
                      //, vE, vB, vA
                      );
            skiprest = true;}

    } // vertical right
} // sawslope

// Can exit after sawslope formation, old scheme: skiprest||slopeBAD (will still use slopeOFF (weak pattern) and slopok (strong pattern) but效果很一般)
skiprest = skiprest||slope1||slope2||slope3||slope4||vE.a<0.002||vB.a<0.002||vD.a<0.002||vF.a<0.002||vH.a<0.002;

/**************************************************
       "Concave + Cross" Shape	（P100）	   
 *************************************************/
// Use approximate pixels for cross star distal ends, useful for some horizontal line + jagged and layered gradient patterns. e.g., glowing text in Street Fighter III: New Generation opening, Japanese-style houses in Street Fighter Zero 3 Mix, opening of Fatal Fury: Mark of the Wolves

vec4 vT;		// Temporary T

if (!skiprest &&
    Bl<El && !eq_E_D && !eq_E_F && eq_E_H && none_eq2(E,A,C) && all_eq2(G,H,I) && sim(E,S) ) { // TOP

    if (eq_B_D||eq_B_F) { J=admixC(vB,J);    K=J;
        if (eq_D_F) { L=mix(J,L, 0.61804);   M=L; }
    } else { vT = El-Bl < abs(El-Dl) ? vB : vD;  J=admixC(vT,J);
            if (eq_D_F) { K=J;  L=mix(J,L, 0.61804);    M=L; }
            else {vT = El-Bl < abs(El-Fl) ? vB : vF; 		K=admixC(vT,K); }
           }

   skiprest = true;
}

if (!skiprest &&
    Hl<El && !eq_E_D && !eq_E_F && eq_E_B && none_eq2(E,G,I) && all_eq2(A,B,C) && sim(E,P) ) { // BOTTOM

    if (eq_D_H||eq_F_H) { L=admixC(vH,L);    M=L;
        if (eq_D_F) { J=mix(L,J, 0.61804);   K=J; }
    } else { vT = El-Hl < abs(El-Dl) ? vH : vD;  L=admixC(vT,L);
            if (eq_D_F) { M=L;  J=mix(L,J, 0.61804);    K=J; }
            else { vT = El-Hl < abs(El-Fl) ? vH : vF;    M=admixC(vT,M); }
           }

   skiprest = true;
}

if (!skiprest &&
    Fl<El && !eq_E_B && !eq_E_H && eq_E_D && none_eq2(E,C,I) && all_eq2(A,D,G) && sim(E,Q) ) { // RIGHT

    if (eq_B_F||eq_F_H) { K=admixC(vF,K);    M=K;
        if (eq_B_H) { J=mix(K,J, 0.61804);   L=J; }
    } else { vT = El-Fl < abs(El-Bl) ? vF : vB;  K=admixC(vT,K);
            if (eq_B_H) { M=K;  J=mix(K,J, 0.61804);    L=J; }
            else { vT = El-Fl < abs(El-Hl) ? vF : vH;    M=admixC(vT,M); }
           }

   skiprest = true;
}

if (!skiprest &&
    Dl<El && !eq_E_B && !eq_E_H && eq_E_F && none_eq2(E,A,G) && all_eq2(C,F,I) && sim(E,R) ) { // LEFT

    if (eq_B_D||eq_D_H) { J=admixC(vD,J);    L=J;
        if (eq_B_H) { K=mix(J,K, 0.61804);   M=K; }
    } else { vT = El-Dl < abs(El-Bl) ? vD : vB;  J=admixC(vT,J);
            if (eq_B_H) { L=J;   K=mix(J,K, 0.61804);    M=K; }
            else { vT = El-Dl < abs(El-Hl) ? vD : vH;    L=admixC(vT,L); }
           }

   skiprest = true;
}

/*
     ✕О
 ООО✕
     ✕О    Scorpion Shape (P99). Looks exactly like the sentinels in The Matrix. Smooths some regularly staggered pixels.
*/
// Practical note 1: Do not use approximate pixels, easy to cause graphical glitches
// Practical note 2: Center pixel has high luminance, remove one grid from scorpion tail to catch more patterns
// Four patterns are exclusive: if caught by previous rules (entered), this pattern will not trigger

if (!skiprest && !eq_E_F&&eq_E_D&&eq_B_F&&eq_F_H && all_eq2(E,C,I) && (eq(E,Q)||El>Fl) && neq(F,src(srcX+3, srcY)) ) {K=admixK(vF,K); M=K;skiprest=true;}	// RIGHT
if (!skiprest && !eq_E_D&&eq_E_F&&eq_B_D&&eq_D_H && all_eq2(E,A,G) && (eq(E,R)||El>Dl) && neq(D,src(srcX-3, srcY)) ) {J=admixK(vD,J); L=J;skiprest=true;}	// LEFT
if (!skiprest && !eq_E_H&&eq_E_B&&eq_D_H&&eq_F_H && all_eq2(E,G,I) && (eq(E,P)||El>Hl) && neq(H,src(srcX, srcY+3)) ) {L=admixK(vH,L); M=L;skiprest=true;}	// BOTTOM
if (!skiprest && !eq_E_B&&eq_E_H&&eq_B_D&&eq_B_F && all_eq2(E,A,C) && (eq(E,S)||El>Bl) && neq(B,src(srcX, srcY-3)) ) {J=admixK(vB,J); K=J;}				// TOP

}
	//final write
    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);

}