/* =========================================================================
 * MMPX
 * by Morgan McGuire and Mara Gagiu
 * https://casual-effects.com/research/McGuire2021PixelArt/
 * License: MIT
 * =========================================================================
 * MMPX Advanced v3.2
 * =========================================================================
 * An optimized and heavily expanded derivative of the MMPX algorithm.
 * 
 * The baseline MMPX implementation relies on a minimalist rule-set, which 
 * inherently suffers from topological conflicts at complex intersections, 
 * manifesting as jarring structural artifacts, "bubbles," and "spurs." 
 * 
 * MMPX Advanced introduces comprehensive morphological analysis, utilizing a 
 * vast array of high-precision conditional predicates to expand the 
 * architectural logic by an order of magnitude. Through exhaustive conflict-
 * scenario analysis and granular edge-case resolution, this refinement 
 * completely eliminates morphological glitches. 
 * 
 * Furthermore, the integration of approximate pixel-matching logic effectively 
 * addresses unmapped topological configurations overlooked by the baseline 
 * specification, thereby delivering flawless detail reconstruction while 
 * preserving the authentic pixel-art aesthetic.
 * 
 * Copyright (c) 2025-2026 by crashGG.
 * ========================================================================= */
// Performs 2x upscaling.
// If we took an image as input, we could use a sampler to do the clamping. But we decode
// low-bpp texture data directly, so...

precision mediump float;

uint srcu(int x, int y) {

	//* out-of-bounds check, return transparent color if coordinates are out of range
    return (x >= 0 && x < params.width && y >= 0 && y < params.height) 
           ? readColoru(uvec2(x, y)) 
           : 0u;
}

#define src(c,d) unpackUnorm4x8(srcu(c,d))

//* RGB luminance weight + alpha segmentation
float luma(vec4 col) {

	//* Adopt BT.601 standard used in CRT era. Clamp result to [0.0 - 0.999]
    float rgbsum =min(dot(col.rgb, vec3(0.299, 0.587, 0.114)), 0.999);

	//* Alpha weighting can be omitted when taking fractional part later
    float alphafactor = 
        (col.a > 0.854102) ? 0.0 :
        (col.a > 0.618034) ? 2.0 :
        (col.a > 0.381966) ? 4.0 :
        (col.a > 0.145898) ? 6.0 :
        (col.a > 0.002) ? 8.0 : 10.0;

    return rgbsum + alphafactor;

}

/* Constant Descriptions:
 * 0.145898    : (~0.382) ^2
 * 0.8541      : 1.0 -(~0.382) ^2
 * 0.0638587   : (RGB Euclidean distance scaled by ~0.382 twice) ^2
 * 0.75        : (RGB Euclidean distance scaled by  0.500) ^2
 */

bool simb(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	//* RGB color difference range (max_diff - min_diff)
	float delta_range = max(diff.r, max(diff.g, diff.b)) - min(diff.r, min(diff.g, diff.b));

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	//* Equivalent to (delta_range ÷ 0.145898 )²
	highp float factor = (delta_range * delta_range) * 46.9787;

	//* Return false if alpha difference between two pixels exceeds threshold
	float alpha_match_mask = step(0.381966, abs(diff.a));

	return dot_diff < mix(0.0638587, 0.0, factor) - alpha_match_mask*5.0;
}

bool simc(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	//* RGB color difference range (max_diff - min_diff)
	float delta_range = max(diff.r, max(diff.g, diff.b)) - min(diff.r, min(diff.g, diff.b));

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	//* Equivalent to (delta_range ÷ 0.145898 )²
	highp float factor = (delta_range * delta_range) * 46.9787;

	//* Return true unconditionally if both pixels are nearly transparent
	float both_near_trans = step(max(col1.a, col2.a), 0.381966);
	//* Return false if alpha difference between two pixels exceeds threshold
	float alpha_match_mask = step(0.381966, abs(diff.a));

	return dot_diff < mix(0.0638587, 0.0, factor) - alpha_match_mask*5.0 + both_near_trans*10.0;
}

bool sim(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	float delta_range = max(diff.r, max(diff.g, diff.b)) - min(diff.r, min(diff.g, diff.b));

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	//* Equivalent to (delta_range ÷ 0.382 )²
	highp float factor = (delta_range * delta_range) * 6.8541;

	//* Return true unconditionally if both pixels are nearly transparent
	float both_near_trans = step(max(col1.a, col2.a), 0.381966);
	//* Return false if alpha difference between two pixels exceeds threshold
	float alpha_match_mask = step(0.381966, abs(diff.a));

	return dot_diff < mix(0.0638587, 0.0, factor) - alpha_match_mask*5.0 + both_near_trans*10.0;
}

float mixGate(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	// RGB color difference range (max_diff - min_diff)
	float delta_range = max(diff.r, max(diff.g, diff.b)) - min(diff.r, min(diff.g, diff.b));

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	//* Equivalent to (delta_range ÷ 0.618 )²
	highp float factor = (delta_range * delta_range) * 2.618034;

	//* Disable blending directly if alpha difference exceeds half
	float alpha_match_mask = step(0.5, abs(diff.a));

	return step(dot_diff , mix(0.75, 0.0, factor) - alpha_match_mask*5.0);
}

//* Allows per-channel int2 tolerance for RGB
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
#define none_eq2(a, b1, b2) !any_eq2(a, b1, b2)


// Pre-define
const vec4 slopOFF   = vec4(2.0);
const vec4 slopeBAD  = vec4(4.0);
const vec4 theEXIT   = vec4(8.0);

#define mixXE mix(vX,vE,mixFactor)
#define mixXEoff mixXE+slopOFF
#define Xoff vX+slopOFF
#define checkblack(col) all(lessThan((col).rgb, vec3(0.1, 0.078, 0.1)))
#define checkwhite(col) all(greaterThan((col).rgb, vec3(0.92, 0.92, 0.92)))



vec4 admixC(vec4 vX, vec4 vE) {
	// Weak blend. Use 0.618 if blendable, otherwise 1.0
	float mixFactor = mixGate(vX, vE) * (-0.381966) + 1.0;

	return mixXE;
}


vec4 admixK(vec4 vX, vec4 vE) {
    vec4 diff = vX - vE;
	float mixFactor = dot(diff.rgb, diff.rgb) * 0.16666 + 0.5;
	return mixXE;
}


vec4 admixL(vec4 vE, vec4 vX) {

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

//**************************************************************************************************************************************
//* 												main slope + X cross-processing mechanism						                *
//******************************************************************************************************************************** zz  *
vec4 admixX( vec4 A, vec4 B, vec4 C, vec4 D, vec4 E, vec4 F, vec4 G, vec4 H, vec4 I
		  , vec4 P, vec4 PA, vec4 PC, vec4 Q, vec4 QA, vec4 QG, vec4 R, vec4 RC, vec4 RI, vec4 S, vec4 SG, vec4 SI, vec4 AA, vec4 CC, vec4 GG
		  , float El, float Bl, float Dl, float Fl, float Hl
		  ) {


	bool eq_B_C = eq(B,C);
	bool eq_D_G = eq(D,G);


    if (eq_B_C && eq_D_G) return slopeBAD;


	// Pre-declare
	bool eq_B_P;		bool eq_B_PA;		bool eq_B_PC;
	bool eq_D_Q;		bool eq_D_QA;		bool eq_D_QG;
	bool eq_E_F;		bool eq_E_H;		bool eq_A_AA;

	vec4 vX;
	float mixFactor;

	bool eq_E_C = eq(E,C);
	bool eq_E_G = eq(E,G);
    bool eq_A_P = eq(A,P);
    bool eq_A_Q = eq(A,Q);
    bool comboE3 = eq_E_C && eq_E_G;
    bool comboA3 = eq_A_P && eq_A_Q;

//* =========================================
//*                    B != D
//* ==================================== zz =
if (neq(B,D)){

	// Exit if E and A are identical, violates preset logic
	if (eq(E,A)) return slopeBAD;

	float diffBD = abs(Bl-Dl);
	if (diffBD > El-Bl || diffBD > El-Dl) return slopeBAD;


	vX = mix(vB, vD, 0.5);
	vX.a = min(vB.a, vD.a);

	mixFactor = 0.381966 * mixGate(vX,vE) * step(0.002, vE.a);

	eq_B_PC = eq(B,PC);
	eq_D_QG = eq(D,QG);
 
    if (none_eq2(A,B,D)){
		if (comboA3) return mixXEoff;
		if ( eq_A_P && eq_B_PC && !eq_B_C ) return mixXEoff;
		if ( eq_A_Q && eq_D_QG && !eq_D_G ) return mixXEoff;

		if ( eq_A_P && eq_E_G ) return mixXEoff;
		if ( eq_A_Q && eq_E_C ) return mixXEoff;

		if ( eq_E_C && eq_D_G ) return mixXEoff;
		if ( eq_E_G && eq_B_C ) return mixXEoff;

}
    if ( comboE3 ) return mixXEoff;

    if ( eq_E_C && eq_B_PC && neq(B,P)) return mixXEoff;
	if ( eq_E_G && eq_D_QG && neq(D,Q)) return mixXEoff;

	eq_E_F = eq(E,F);

	if (eq(F,H)) {

		if ( eq_E_C && !eq_D_G && (!eq_E_F||neq(E,P)) ) return mixXEoff;
		if ( eq_E_G && !eq_B_C && (!eq_E_F||neq(E,Q)) ) return mixXEoff;

		if ( !eq_E_F && eq_B_PC && eq(F,RC) ) return mixXEoff;
		if ( !eq_E_F && eq_D_QG && eq(H,SG) ) return mixXEoff;
	}

    return slopeBAD;
} //* B != D


						//*********  B == D  *********

	Bl = fract(Bl);
	Dl = fract(Dl);
	El = fract(El);
	Fl = fract(Fl);
	Hl = fract(Hl);
  

	bool Xisblack = checkblack(vB);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

	vX = vB;
	vX.a = min(vB.a, vD.a);

	mixFactor = 0.381966 * mixGate(vX,vE) * step(0.002, vE.a);

	bool B_slope;	bool B_tower;	bool B_wall;
    bool D_slope;	bool D_tower;	bool D_wall;
	bool En3;
    #define En4square En3&&eq(E,I)

//* ===================================================
//*                   E - A  x cross
//* =========================================== zz ====
if (eq(E,A)) {

	eq_E_F = eq(E,F);
	eq_E_H = eq(E,H);

	bool Eisblack = checkblack(vE);


    if ( comboE3 && !eq_E_F && !eq_E_H && eq(E,I) ) {

		if (Eisblack) return theEXIT;
		mixFactor = 0.618034 * (1.0 - mixFactor);
		return mixXEoff;
	}

	eq_A_AA = eq(A,AA);


    if ( comboA3 && eq_A_AA && none_eq2(A,PA,QA) )  {	
		if (Eisblack) return theEXIT;
		mixFactor = 0.618034 * (1.0 - mixFactor);
        if ( neq(B,PA) && eq(PA,QA) ) return mixXEoff;
		mixFactor += 0.236068;
		return mixXEoff;
	}

	// Use default connection if center is fully transparent and X is not black
	if (vE.a<0.002 && !Xisblack) return vX;

    eq_B_PC = eq(B,PC);
    eq_B_PA = eq(B,PA);
    eq_D_QG = eq(D,QG);
    eq_D_QA = eq(D,QA);

	if ( comboE3 && comboA3 &&
		(eq_B_PC || eq_D_QG) && eq_D_QA && eq_B_PA) {
		mixFactor = mixFactor * (-0.618034) + 0.8541;
        return mixXEoff;
	}

    // 4. Quarter-dot pattern, prevents ugly small artifacts
	if ( comboE3 && eq_A_P
		 && eq_B_PA && eq_D_QA && eq_D_QG
		 && eq_E_H
		) {
		mixFactor = mixFactor * (-0.618034) + 0.8541;
        return mixXEoff;
		}

	if ( comboE3 && eq_A_Q
		 && eq_B_PA && eq_D_QA && eq_B_PC
		 && eq_E_F
		) {
		mixFactor = mixFactor * (-0.618034) + 0.8541;
        return mixXEoff;
		}


    if (comboA3) return Xoff;

    if (comboE3) return mixXEoff;

	eq_B_P = eq(B, P);
	eq_D_Q = eq(D, Q);

	B_slope = eq_B_PC && !eq_B_P && !eq_B_C && !eq_B_PA;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G && !eq_D_QA;

	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;
	D_wall = eq_D_G && !eq_D_QG && !eq_D_Q;
	
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;


	if ( B_slope && eq_E_G ) return mixXEoff;
	if ( D_slope && eq_E_C ) return mixXEoff;


//* E B D region checkerboard scoring rule

    float scoreE = 0.0; float scoreB = 0.0; float scoreD = 0.0; float scoreZ = 0.0;

//*	E Zone
    if (eq_E_C) {
		scoreE += 1.0 +float(eq(F,H)) +float(B_slope);
		scoreE -= float(all_eq2(E,P,PC)&&!D_wall);
	}

    if (eq_E_G) {
        scoreE += 1.0 +float(eq(F,H)) +float(D_slope);
		scoreE -= float(all_eq2(E,Q,QG)&&!B_wall);
    }

	scoreE += float(B_slope && eq_A_Q || D_slope && eq_A_P);

    En3 = eq_E_F && eq_E_H;

	// Early exit for clean 4/6 rectangles, skip long slope evaluation
	if ( scoreE<0.1 && mixFactor<0.1 && En4square && eq(E,S)==eq(E,SI) && eq(E,R)==eq(E,RI) ) return theEXIT;

	if ( scoreE<0.1 && !En3 && neq(E,I) ) {
		if ( B_wall && eq_E_F ) return theEXIT;
		if ( D_wall && eq_E_H ) return theEXIT;
    }

	scoreE += float(B_slope && eq_A_P || D_slope && eq_A_Q);

    if ( !En3 && eq(F,H) ) {
		if (Eisblack) return slopeBAD;
		bool condZ1 = B_wall && (eq(F,R) || eq(F,RC) || eq(G,H) || eq(F,I));
		bool condZ2 = D_wall && (eq(C,F) || eq(H,SG) || eq(H,S) || eq(F,I));
		scoreZ = float(condZ1 || condZ2);
    }


//*	B Zone

    if (eq_B_PA) {
		scoreB -= 1.0 +float(eq(P,C)) +float(eq_A_AA);
	}

	if (eq(P,C)){
		scoreB -= float(eq_A_AA);
		scoreZ *= float(scoreE < 0.1);
	}

//*  D Zone

    if (eq_D_QA) {
		scoreD -= 1.0 +float(eq(G,Q)) +float(eq_A_AA);
	}

	if (eq(G,Q)){
		scoreD -= float(eq_A_AA);
		scoreZ *= float(scoreE < 0.1);
	}

    float scoreFinal = scoreE + scoreB + scoreD + scoreZ ;

	// Disable blending for long slope patterns with no penalties on B/D zone
	scoreFinal += float(min(scoreB,scoreD) > -0.1 && (B_wall && D_tower || B_tower && D_wall)) *2.0;

	mixFactor *= (1.0 - step(1.9, scoreFinal));
	return mixXE + slopeBAD*(1.0 - step(0.9, scoreFinal));

}	//* E == A

	
//* ===============================================
//*              	E - C - G     main rules
//* ========================================= zz ==

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


//* =======================================================
//*                  F - H / B+ D+  extended rules
//* ================================================= zz ==

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

	// Exit for high contrast isolated center pixel
	float E_lumDiff = mix(0.381966, 0.145898, max((El - 0.8541),0.0) * 6.8541);

    if ( mixFactor<0.1 && !sim_EC && !sim_EG && E.a>0.381966 && neq(E,I) && abs(El-Fl)>E_lumDiff && abs(El-Hl)>E_lumDiff ) return slopeBAD;


	eq_E_F = eq(E,F);
	eq_E_H = eq(E,H);

    // long slope special pattern
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

	// Enclosed 4-cell rectangle
	if ( En4square ) {
        if ( ( eq_B_C || eq_D_G) && eq_A_B) return theEXIT;
        if ( ( eq_B_C || eq_D_G || mixFactor<0.1) && (eq(E,S) == eq(E, SI) && eq(E,R) == eq(E, RI)) ) return theEXIT;
        return mixXEoff;
    }

	if (vE.a<0.381966) return mixXEoff;

	// B-D solid non-wall region
	if (!eq_B_C && !eq_D_G ) {
		 if ( comboA3 && eq_F_H ) return Xoff;

		if ( comboA3&&eq_B_PC&&eq(C,CC) ) return Xoff;
		if ( comboA3&&eq_D_QG&&eq(G,GG) ) return Xoff;

		if ( !eq_B_P && !eq_B_PC && !eq_D_Q && !eq_D_QG && !En3 ) return slopeBAD;

		if (eq_A_Q&&sim_EC) return mixXEoff;
		if (eq_A_P&&sim_EG) return mixXEoff;
		if (sim_EC&&sim_EG ) return mixXEoff;
	}
	
 	// Wall-enclosed triangle
 	if ( En3 && eq_A_B) return theEXIT;

	if (eq_F_H) {

		if ( eq_B_PC&&eq(F,RC) || eq_D_QG&&eq(H,SG) ) return mixXEoff;

		if (eq_A_B) return slopeBAD;

		if ( eq_B_C || eq_D_G) return mixXEoff;
		if ( eq_B_PC || eq_D_QG) return mixXEoff;

	}

	return slopeBAD;

}	//* admixX


vec4 admixS( vec4 A, vec4 B, vec4 C, vec4 D, vec4 E, vec4 F, vec4 G, vec4 H, vec4 I
		   , vec4 R, vec4 RC, vec4 RI, vec4 S, vec4 SG, vec4 SI, vec4 II, vec4 CC
		   ) {

    if (any_eq2(F,C,I)) return vE;

	if ( (eq(F,RI) || eq(G,S) || eq(R, RI)) && neq(R,I) ) return vE;

    if (eq(H, S) && none_eq2(H,I,SG)) return vE;

    if ( eq(R, RC) || eq(G,SG) ) return vE;

	// Extend pattern for white center with D==E==C
	if ( checkwhite(vE) && all_eq2(E,C,D) && none_eq2(E,RC,CC)) return vE;


	#define vX vF
	float mixFactor = 0.381966 * mixGate(vX,vE) * step(0.002, vE.a);

	if ( eq(E,C) && (eq(E,D)||eq(B,D)) ) return mixXE;

	bool sim_E_C = sim(vE,vC);

	if ( sim_E_C && eq(E,D) && eq(B,C) ) return mixXE;

	if ( (sim_E_C || mixFactor>0.1) && all_eq2(B,C,D) ) return mixXE;

    return vE;
}
//*///////////////////////////////////////////////////////////////////////////////////////////////////// zz //

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

//* skip 3x1
bool skiprest = (eq_E_D && eq_E_F) || (eq_E_B && eq_E_H) || (eq_B_H && eq_D_F);
if (!skiprest) {


    //*	 5x5
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
    vec4 QG = src(srcX-2, srcY+1);
    vec4 RC = src(srcX+2, srcY-1);
    vec4 RI = src(srcX+2, srcY+1);
    vec4 SG = src(srcX-1, srcY+2);
    vec4 SI = src(srcX+1, srcY+2);
    vec4 AA = src(srcX-2, srcY-2);
    vec4 CC = src(srcX+2, srcY-2);
    vec4 GG = src(srcX-2, srcY+2);
    vec4 II = src(srcX+2, srcY+2);


	//*	pre-cal luma
    float Bl = luma(vB);
    float Dl = luma(vD);
    float El = luma(vE);
    float Fl = luma(vF);
    float Hl = luma(vH);

	//* pre-cal
	bool eq_B_D = eq(B,D);
    bool eq_B_F = eq(B,F);
    bool eq_D_H = eq(D,H);
    bool eq_F_H = eq(F,H);

	//*  1:1 slope
	bool oppoPix =  eq_B_H || eq_D_F;
    bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;
    bool slope1ok = false;  bool slope2ok = false;  bool slope3ok = false;  bool slope4ok = false;
    bool slope1end = false;  bool slope2end = false;  bool slope3end = false;  bool slope4end = false;

	//*  B - D
	if ( (vB.a>0.002 && vD.a>0.002) && // xxx.alpha
		(!eq_E_B && !eq_E_D && !oppoPix) && (!eq_D_H && !eq_B_F)
	 && (eq(E,A) || El>=Dl&&El>=Bl) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || neq(E,P) || neq(E,Q) )
	 && ( eq_B_D &&(eq(E,A)||eq(B,PC)||eq(D,QG)||sim(vE,vC)||sim(vE,vG)) || simb(vB,vD)&&(eq_F_H||eq(E,C)||eq(E,G)) )
		) {
		J=admixX(A,B,C,D,E,F,G,H,I
				,P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG
				,El, Bl, Dl, Fl, Hl
				);
		slope1 = true;
		slope1ok = (J.b < 1.1);
		slope1end = (J.b < 3.1);
		skiprest = (J.b > 7.1);
		J = (J.b > 3.1) ? vE :
			(J.b > 1.1) ? (J - 2.0) :
			J;
	}

	//*  B - F
	if ( !slope1 && (vB.a>0.002 && vF.a>0.002)
	 && (!eq_E_B && !eq_E_F && !oppoPix) && (!eq_B_D && !eq_F_H)
	 && (eq(E,C) || El>=Bl&&El>=Fl) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || neq(E,P) || neq(E,R) )
	 && ( eq_B_F &&(eq(E,C)||eq(B,PA)||eq(F,RI)||sim(vE,vA)||sim(vE,vI)) || simb(vB,vF)&&(eq_D_H||eq(E,A)||eq(E,I)) )
	 ) {
		K=admixX(C,F,I,B,E,H,A,D,G
				,R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA
				,El,Fl,Bl,Hl,Dl
				);
		slope2 = true;
		slope2ok = (K.b < 1.1);
		slope2end = (K.b < 3.1);
		skiprest = (K.b > 7.1);
		K = (K.b > 3.1) ? vE :
			(K.b > 1.1) ? (K - 2.0) :
			K;
	}

	//*  D - H
	if ( !slope1 && !skiprest && (vD.a>0.002 && vH.a>0.002)
	 && (!eq_E_D && !eq_E_H && !oppoPix) && (!eq_F_H && !eq_B_D)
	 && (eq(E,G) || El>=Hl&&El>=Dl)  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || neq(E,S) || neq(E,Q))
	 &&	( eq_D_H &&(eq(E,G)||eq(D,QA)||eq(H,SI)||sim(vE,vA)||sim(vE,vI)) || simb(vD,vH)&&(eq_B_F||eq(E,A)||eq(E,I)) )
	 ) {
		L=admixX(G,D,A,H,E,B,I,F,C
				,Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II
				,El,Dl,Hl,Bl,Fl
				);
		slope3 = true;
		slope3ok = (L.b < 1.1);
		slope3end = (L.b < 3.1);
		skiprest = (L.b > 7.1);
		L = (L.b > 3.1) ? vE :
			(L.b > 1.1) ? (L - 2.0) :
			L;
	}

	//* F - H
	if ( !slope2 && !slope3 && !skiprest && (vF.a>0.002 && vH.a>0.002)
	 && (!eq_E_F && !eq_E_H && !oppoPix) && (!eq_B_F && !eq_D_H)
	 && (eq(E,I) || El>=Fl&&El>=Hl)  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || neq(E,R) || neq(E,S))
	 && ( eq_F_H &&(eq(E,I)||eq(F,RC)||eq(H,SG)||sim(vE,vC)||sim(vE,vG)) || simb(vF,vH)&&(eq_B_D||eq(E,C)||eq(E,G)) )
	  ) {
		M=admixX(I,H,G,F,E,D,C,B,A
				,S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC
				,El,Hl,Fl,Dl,Bl
				);
		slope4 = true;
		slope4ok = (M.b < 1.1);
		slope4end = (M.b < 3.1);
		skiprest = (M.b > 7.1);
		M = (M.b > 3.1) ? vE :
			(M.b > 1.1) ? (M - 2.0) :
			M;
	}


	//*	2:1 long stepped slope  (P100)
	if (slope4ok) {
		if (all_eq2(R,F,G) && neq(R, RC) && (neq(Q,G)||eq(Q, QA))) {L=admixL(vE,vH); skiprest = true;}
		if (all_eq2(S,H,C) && neq(S, SG) && (neq(P,C)||eq(P, PA))) {K=admixL(vE,vF); skiprest = true;}
	}

	if (slope3ok) {
		if (all_eq2(Q,D,I) && neq(Q, QA) && (neq(R,I)||eq(R, RC))) {M=admixL(vE,vH); skiprest = true;}
		if (all_eq2(S,H,A) && neq(S, SI) && (neq(A,P)||eq(P, PC))) {J=admixL(vE,vD); skiprest = true;}
	}

	if (slope2ok) {
		if (all_eq2(R,F,A) && neq(R, RI) && (neq(A,Q)||eq(Q, QG))) {J=admixL(vE,vB); skiprest = true;}
		if (all_eq2(P,B,I) && neq(P, PA) && (neq(I,S)||eq(S, SG))) {M=admixL(vE,vF); skiprest = true;}
	}

	if (slope1ok) {
		if (all_eq2(Q,D,C) && neq(Q, QG) && (neq(C,R)||eq(R, RI))) {K=admixL(vE,vB); skiprest = true;}
		if (all_eq2(P,B,G) && neq(P, PC) && (neq(G,S)||eq(S, SI))) {L=admixL(vE,vD); skiprest = true;}
	}

	//*   2:1 staggered slope  (new rule) 
if (!skiprest && !oppoPix) {


    if (!eq_E_H && none_eq2(H,A,C)) {

        if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && (!slope4end && !eq_F_H) && vF.a>0.002 &&
            !eq_E_F && eq(R,H) && eq(F,G) ) {
            M = admixS( A, B, C, D, E, F, G, H, I
                      , R, RC, RI, S, SG, SI, II, CC
                      );
            skiprest = true;}

        if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && (!slope3end && !eq_D_H) && vD.a>0.002 &&
             !eq_E_D && eq(Q,H) && eq(D,I) ) {
            L = admixS( C, B, A, F, E, D, I, H, G
                      , Q, QA, QG, S, SI, SG, GG, AA
                      );
            skiprest = true;}
    }

    if ( !skiprest && !eq_E_B && none_eq2(B,G,I)) {

        if ( (!slope1 && !eq_B_D)  && (!slope4 && !eq_F_H) && (!slope2end && !eq_B_F) && vF.a>0.002 &&
              !eq_E_F && eq(B,R) && eq(A,F) ) {
            K = admixS( G, H, I, D, E, F, A, B, C
                      , R, RI, RC, P, PA, PC, CC, II
                      );
            skiprest = true;}

        if ( !skiprest && (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && (!slope1end && !eq_B_D) && vD.a>0.002 &&
             !eq_E_D && eq(B,Q) && eq(C,D) ) {
            J = admixS( I, H, G, F, E, D, C, B, A
                      , Q, QG, QA, P, PC, PA, AA, GG
                      );
            skiprest = true;}

    }


    if ( !skiprest && !eq_E_D && none_eq2(D,C,I) ) {

        if ( (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && (!slope3end && !eq_D_H) && vH.a>0.002 &&
              !eq_E_H && eq(D,S) && eq(A,H) ) {
            L = admixS( C, F, I, B, E, H, A, D, G
                      , S, SI, SG, Q, QA, QG, GG, II
                      );
            skiprest = true;}

        if ( !skiprest && (!slope3 && !eq_D_H) && (!slope2 && !eq_B_F) && (!slope1end && !eq_B_D) && vB.a>0.002 &&
             !eq_E_B && eq(P,D) && eq(B,G) ) {
            J = admixS( I, F, C, H, E, B, G, D, A
                      , P, PC, PA, Q, QG, QA, AA, CC
                      );
            skiprest = true;}

    }

    if ( !skiprest && !eq_E_F && none_eq2(F,A,G) ) {

        if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && (!slope4end && !eq_F_H) && vH.a>0.002 &&
              !eq_E_H && eq(S,F) && eq(H,C) ) {
            M = admixS( A, D, G, B, E, H, C, F, I
                      , S, SG, SI, R, RC, RI, II, GG
                      );
            skiprest = true;}

        if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && (!slope2end && !eq_B_F) && vB.a>0.002 &&
             !eq_E_B && eq(P,F) && eq(B,I) ) {
            K = admixS( G, D, A, H, E, B, I, F, C
                      , P, PA, PC, R, RI, RC, CC, AA
                      );
            skiprest = true;}

    } // vertical right
} // staggered slope

skiprest = skiprest||slope1||slope2||slope3||slope4||vE.a<0.002||vB.a<0.002||vD.a<0.002||vF.a<0.002||vH.a<0.002;

//* =======================================================
//*            О✕✕
//*          ООО✕
//*            О✕✕		Concave + Cross （P100）
//* ================================================= zz ==
vec4 vT;		float maxl;

if (!skiprest &&
    Bl<El && !eq_E_D && !eq_E_F && eq_E_H && none_eq2(E,A,C) && all_eq2(E,G,I) && simc(vE,S) ) {

	float B1 = float(eq(B, A));
	float B2 = float(eq(B, C));
	float DD = float(eq(A, D));
	float FF = float(eq(C, F));

	float sideFactor = eq_B_D && DD!=FF ? 1.0 : -0.145898;

	Bl = fract(Bl) - (B1 +B2 +B1*B2) *0.381966;
	Dl = fract(Dl) - step(El, Dl) + DD*sideFactor;
	Fl = fract(Fl) - step(El, Fl) + FF*sideFactor;

	maxl = max(Bl, max(Dl, Fl));

	vT = eq_B_D || eq_B_F ? vB :
		 maxl <= Dl      ? vD :
		 maxl <= Fl      ? vF :
						   vB ;
	vT = admixC(vT,vE);

	bool nolink = !eq_D_F && (DD==0.0) && (FF==0.0);
	J = Dl<0.0&&nolink ? vE : vT;
    K = Fl<0.0&&nolink ? vE : vT;
	L = mix(vE, J, 0.381966*float(eq_D_F));
	M = L;

   skiprest = true;
}
if (!skiprest &&
    Hl<El && !eq_E_D && !eq_E_F && eq_E_B && none_eq2(E,G,I) && all_eq2(E,A,C) && simc(vE,P) ) {

	float H1 = float(eq(H, G));
	float H2 = float(eq(H, I));
	float DD = float(eq(D, G));
	float FF = float(eq(F, I));

	float sideFactor = eq_B_D && DD!=FF ? 1.0 : -0.145898;

	Hl = fract(Hl) - (H1+H2+H1*H2) *0.381966;
	Dl = fract(Dl) - step(El, Dl) + DD*sideFactor;
	Fl = fract(Fl) - step(El, Fl) + FF*sideFactor;

	maxl = max(Hl, max(Dl, Fl));

	vT = eq_D_H || eq_F_H ? vH :
		 maxl <= Dl      ? vD :
		 maxl <= Fl      ? vF :
						   vH ;
	vT=admixC(vT,vE);

	bool nolink = !eq_D_F && (DD==0.0) && (FF==0.0);

	L = Dl<0.0&&nolink ? vE : vT;
    M = Fl<0.0&&nolink ? vE : vT;
	J = mix(vE, L, 0.381966*float(eq_D_F));
	K = J;

   skiprest = true;
}
if (!skiprest &&
    Fl<El && !eq_E_B && !eq_E_H && eq_E_D && none_eq2(E,C,I) && all_eq2(E,A,G) && simc(vE,Q) ) {

	float F1 = float(eq(F, C));
	float F2 = float(eq(F, I));
	float BB = float(eq(B, C));
	float HH = float(eq(H, I));

	float sideFactor = eq_B_H && BB!=HH ? 1.0 : -0.145898;

	Fl = fract(Fl) - (F1+F2+F1*F2) *0.381966;
	Bl = fract(Bl) - step(El, Bl) + BB*sideFactor;
	Hl = fract(Hl) - step(El, Hl) + HH*sideFactor;

	maxl = max(Fl, max(Bl, Hl));

	vT = eq_B_F || eq_F_H ? vF :
		 maxl <= Bl      ? vB :
		 maxl <= Hl      ? vH :
						   vF ;
	vT=admixC(vT,vE);

	bool nolink =  !eq_B_H && (BB==0.0) && (HH==0.0);

	K = Bl<0.0&&nolink ? vE : vT;
    M = Hl<0.0&&nolink ? vE : vT;
	J = mix(vE, K, 0.381966*float(eq_B_H));
	L = J;

   skiprest = true;
}
if (!skiprest &&
    Dl<El && !eq_E_B && !eq_E_H && eq_E_F && none_eq2(E,A,G) && all_eq2(E,C,I) && simc(vE,R) ) {

	float D1 = float(eq(D, A));
	float D2 = float(eq(D, G));
	float BB = float(eq(B, A));
	float HH = float(eq(H, G));

	float sideFactor = eq_B_H && BB!=HH ? 1.0 : -0.145898;

	Dl = fract(Dl) - (D1+D2+D1*D2) *0.381966;
	Bl = fract(Bl) - step(El, Bl) + BB*sideFactor;
	Hl = fract(Hl) - step(El, Hl) + HH*sideFactor;

	maxl = max(Dl, max(Bl, Hl));

	vT = eq_B_D || eq_D_H ? vD :
		 maxl <= Bl      ? vB :
		 maxl <= Hl      ? vH :
						   vD ;
	vT=admixC(vT,vE);

	bool nolink =  !eq_B_H && (BB==0.0) && (HH==0.0);

	J = Bl<0.0&&nolink ? vE : vT;
    L = Hl<0.0&&nolink ? vE : vT;
	K = mix(vE, J, 0.381966*float(eq_B_H));
	M = K;

   skiprest = true;
}

//* =======================================================
//*            ✕О
//*        ООО✕
//*            ✕О    K - type
//* ================================================= zz ==
if (!skiprest && !eq_E_F&&eq_E_D&&eq_B_F&&eq_F_H && all_eq2(E,C,I) && (eq(E,Q)||El>Fl) && neq(F,src(srcX+3, srcY)) ) {K=admixK(vF,vE); M=K;skiprest=true;}	//* RIGHT
if (!skiprest && !eq_E_D&&eq_E_F&&eq_B_D&&eq_D_H && all_eq2(E,A,G) && (eq(E,R)||El>Dl) && neq(D,src(srcX-3, srcY)) ) {J=admixK(vD,vE); L=J;skiprest=true;}		//* LEFT
if (!skiprest && !eq_E_H&&eq_E_B&&eq_D_H&&eq_F_H && all_eq2(E,G,I) && (eq(E,P)||El>Hl) && neq(H,src(srcX, srcY+3)) ) {L=admixK(vH,vE); M=L;skiprest=true;}	//* BOTTOM
if (!skiprest && !eq_E_B&&eq_E_H&&eq_B_D&&eq_B_F && all_eq2(E,A,C) && (eq(E,S)||El>Bl) && neq(B,src(srcX, srcY-3)) ) {J=admixK(vB,vE); K=J;}					//* TOP
}
	//* final write
    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
}
