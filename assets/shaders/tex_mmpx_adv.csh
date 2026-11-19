/* =========================================================================
 * MMPX
 * by Morgan McGuire and Mara Gagiu
 * https://casual-effects.com/research/McGuire2021PixelArt/
 * License: MIT
 * =========================================================================
 * MMPX Advanced v3.5
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

    ivec2 target = ivec2(x, y);
    
    //* clamp target to safe bounds
    ivec2 safe = clamp(target, ivec2(0), ivec2(params.width - 1, params.height - 1));

    uint color = readColoru(uvec2(safe));

    //* in-bounds check
    bool is_in = all(equal(target, safe));
    
    //* return transparent color if out of range
    return is_in ? color : 0u;
}

#define src(c,d) unpackUnorm4x8(srcu(c,d))

float luma(vec4 col) {

	//* Adopt BT.601 standard used in CRT era. Clamp result to [0.0 - 0.999]
    float rgbsum =min(dot(col.rgb, vec3(0.299, 0.587, 0.114)), 0.999);

	//* Alpha weighting can be omitted when taking fractional part later
    float alphafactor = 
        (col.a > 0.854102) ? 0.0 :
        (col.a > 0.618034) ? 2.0 :
        (col.a > 0.381966) ? 4.0 :
        (col.a > 0.002) ? 6.0 : 8.0;
	//*	x2 value for pure blue
    return rgbsum * (1.0 + step(col.r+col.g, 0.01)) + alphafactor;

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


#define eq(a,b) all(lessThan(abs(a-b), vec4(0.01,0.01,0.01,0.1)))

#define ne(a,b) !eq(a,b)

#define any_eq2(a, b1, b2) (eq(a,b1)||eq(a,b2))
#define any_eq3(a, b1, b2, b3) (eq(a,b1)||eq(a,b2)||eq(a,b3))
#define any_eq4(a, b1, b2, b3, b4) (eq(a,b1)||eq(a,b2)||eq(a,b3)||eq(a,b4))
#define not_eq2(a, b1, b2)  (ne(a,b1) && ne(a,b2))
#define not_eq3(a, b1, b2, b3)  (ne(a,b1) && ne(a,b2) && ne(a,b3))
#define not_eq4(a, b1, b2, b3, b4) (ne(a,b1) && ne(a,b2) && ne(a,b3) && ne(a,b4))
#define not_eq5(a, b1, b2, b3, b4, b5) (ne(a,b1) && ne(a,b2) && ne(a,b3) && ne(a,b4) && ne(a,b5))

#define all_eq2(a, b1, b2) \
	( eq(a,b1) && eq(a,b2) )

#define all_eq3(a, b1, b2, b3) \
	( eq(a,b1) && eq(a,b2) && eq(a,b3) )

#define all_eq4(a, b1, b2, b3, b4) \
	( eq(a,b1) && eq(a,b2) && eq(a,b3) && eq(a,b4) )

#define checkblack(col) all(lessThan((col).rgb, vec3(0.1, 0.078, 0.1)))
#define checkwhite(col) all(greaterThan((col).rgb, vec3(0.92, 0.92, 0.92)))
// xxx.alpha.straight
#define checkeq(a,b) all(lessThan(abs((a).rgb - (b).rgb), vec3(0.01)))


// Pre-define
const vec4 testcolor  = vec4(1.0, 0.0, 1.0, 1.0);
const vec4 testcolor2 = vec4(0.0, 1.0, 1.0, 1.0);
const vec4 testcolor3 = vec4(1.0, 1.0, 0.0, 1.0);
const vec4 testcolor4 = vec4(1.0, 1.0, 1.0, 1.0);

const vec4 slopeOFF   = vec4(2.0);
const vec4 slopeBAD  = vec4(4.0);
const vec4 theEXIT   = vec4(8.0);

#define mixXE mix(vX,vE,mixFactor)
#define mixXEoff mixXE+slopeOFF
#define Xoff vX+slopeOFF


vec4 admixC(vec4 vX, vec4 vE) {
	// Weak blend. Use 0.618 if blendable, otherwise 1.0
	float mixFactor = mixGate(vX, vE) * (-0.381966) + 1.0;

	return mixXE;
}

vec4 admixK(vec4 vX, vec4 vE) {
    vec4 diff = vX - vE;

	float mixFactor = clamp(dot(diff.rgb, diff.rgb), 0.0, 1.0) * 0.5 + 0.5; // xxx.alpha

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
		  , float El, float Bl, float Dl, float Fl, float Hl, bool simEC, bool simEG, bool simEI
		  ) {


	bool doubleWall = eq(B,C) && eq(D,G);

	bool Bisblack = checkblack(vB);
	bool Disblack = checkblack(vD);	//xxx.alpha
	bool triBlack = Bisblack && fract(El) >0.5 && (fract(Fl)<0.078 || fract(Hl)<0.078);	//xxx.alpha.straight
	bool transBD = min(Bl,Dl)-El>1.0;	//xxx.alpha.straight
	bool border1dark = ne(B,D) && (Bisblack||Disblack) && min(vB.a,vD.a)-vE.a>0.8541;	//xxx.alpha.straight
	if (doubleWall||triBlack||transBD||border1dark) return slopeBAD;	//xxx.alpha



//* =========================================
//*                    B != D
//* ==================================== zz =
if (ne(B,D)){

	vec4 midBD = mix(vB,vD,0.5);		// xxx.alpha

	vec4 vX = vE.a<0.5 ? midBD :		// xxx.alpha.straight
				Bl>Dl ? vB : vD;

	vX.a = max(min(vB.a, vD.a), step(0.999,vE.a));		// xxx.alpha.straight

	float mixFactor = 0.145898 * mixGate(vX,vE) * step(0.002, vE.a);	//xxx.alpha

	if (eq(E,A)) {

		bool okB = El>Bl && eq(E,C) && eq(A,P) && eq(B,PC);
		bool okD = El>Bl && eq(E,G) && eq(A,Q) && eq(D,QG);

		return mixXEoff +float(!okB&&!okD)*2.0;
	}



    if (not_eq2(A,B,D)){
		bool na1 = all_eq2(A,P,Q);
		bool na2 = eq(A,P) && eq(B,PC) && ne(B,C);
		bool na3 = eq(A,Q) && eq(D,QG) && ne(D,G);

		bool na4 = eq(A,P) && eq(E,G);
		bool na5 = eq(A,Q) && eq(E,C);

		bool goodFH = min(Fl,Hl)>max(Bl,Dl)+0.145898||min(Fl,Hl)>=El;
		bool na6 = eq(E,C) && eq(B,P) && not_eq2(B,PA,PC) && goodFH;
		bool na7 = eq(E,G) && eq(D,Q) && not_eq2(D,QA,QG) && goodFH;

		bool na8 = eq(E,C) && eq(D,G) && ne(D,S) && (ne(D,Q)||ne(D,QG));
		bool na9 = eq(E,G) && eq(B,C) && ne(B,R) && (ne(B,P)||ne(B,PC));




		float Al = luma(vA);

		int BDscore = int(eq(D,Q)&&ne(B,PC)) +int(eq(E,G)) +int(eq(B,C)) -int(eq(B,P)&&ne(D,QG)) -int(eq(D,G)) -int(eq(E,C));

		bool Disbetter = BDscore>0 && (Al>Bl||vE.a<0.5);	// xxx.alpha.straight
		bool Bisbetter = BDscore<0 && (Al>Dl||vE.a<0.5);	// xxx.alpha.straight

		vX.rgb = Disbetter ? vD.rgb :				// xxx.alpha.straight
			 Bisbetter ? vB.rgb :
						vX.rgb ;

		if (na1||na2||na3||na4||na5||na6||na7||na8||na9) return mixXEoff;
	}

	vX.rgb = eq(A,D)&&ne(E,G) ?  vB.rgb :			// xxx.alpha.straight
		 eq(A,B)&&ne(E,C) ?  vD.rgb :
				vX.rgb ;

	if (eq(F,H)) {


		bool FH1 = eq(E,C) && ne(D,G) && (ne(E,F)||ne(E,P));
		bool FH2 = eq(E,G) && ne(B,C) && (ne(E,F)||ne(E,Q));

		bool FH3 = ne(E,F) && eq(B,PC) && eq(F,RC);
		bool FH4 = ne(E,F) && eq(D,QG) && eq(H,SG);
		
		if (FH1||FH2||FH3||FH4) return mixXEoff;
	}

	bool ECG1 = all_eq2(E,C,G);
	bool ECG2 = eq(E,C) && eq(B,PC) && ne(B,P);
	bool ECG3 = eq(E,G) && eq(D,QG) && ne(D,Q);

    return mixXEoff +float(!ECG1&&!ECG2&&!ECG3)*2.0;
} //* B != D


						//*********  B == D  *********

	vec4 vX = vB;
	vX.a = max(min(vB.a, vD.a), step(0.999,vE.a));		// xxx.alpha.straight

	float mixFactor = 0.145898 * mixGate(vX,vE) * step(0.002, vE.a);	// xxx.alpha

	bool B_slope;		bool B_tower;		bool B_wall;
	bool D_slope;		bool D_tower;		bool D_wall;

	bool comboE3 = all_eq2(E,C,G);
	bool comboA3 = all_eq2(A,P,Q);

//* ===================================================
//*                   E - A  x cross
//* =========================================== zz ====
if (eq(E,A)) {


	bool Eisblack = checkblack(vE);

    if ( comboE3 && not_eq2(E,F,H) && eq(E,I) ) {

		mixFactor = 0.618034 - 1.618034 * mixFactor;
		return mixXEoff +float(Eisblack)*8.0; 
	}


    if ( comboA3 && eq(A,AA) && not_eq2(A,PA,QA) )  {

		mixFactor = 0.8541 -1.618034*mixFactor -0.236068*float(ne(B,PA)&&eq(PA, QA)) ;
		return mixXEoff +float(Eisblack)*8.0; 
	}



	if ( comboE3 && comboA3 &&
		(eq(B,PC) || eq(D,QG)) && eq(D,QA) && eq(B,PA)) {
		mixFactor = 0.8541 - 1.618034 * mixFactor;
		mixFactor = max(mixFactor, float(vE.a<0.002));	// xxx.alpha
        return mixXEoff;
	}

	if ( comboE3 && eq(D,QA) && eq(B,PA)
		&& (  eq(A,P) && eq(D,QG) && eq(E,H)
			|| eq(A,Q) && eq(B,PC) && eq(E,F))
		) {
		mixFactor = 0.8541 - 1.618034 * mixFactor;
		mixFactor = max(mixFactor, float(vE.a<0.002));	// xxx.alpha
        return mixXEoff;
		}

	mixFactor *= comboA3 ? 0.145898 : 1.0;

	bool ECG1 = comboA3;

	bool ECG2 = comboE3;

	B_slope = eq(B,PC) && not_eq3(B,P,C,PA);
	D_slope = eq(D,QG) && not_eq3(D,Q,G,QA);


	bool ECG3 = B_slope && eq(E,G);
	bool ECG4 = D_slope && eq(E,C);
	bool ECG5 = El-max(Bl,Dl)>1.0 && any_eq2(E,C,G);		// xxx.alpha.straight
	if (ECG1||ECG2||ECG3||ECG4||ECG5) return mixXE+float(!ECG5)*2.0;


//* E B D Zone Checkerboard Scoring Rule


	B_wall = eq(B,C) && not_eq3(B,PC,P,PA);
	D_wall = eq(D,G) && not_eq3(D,QG,Q,QA);

	B_tower = eq(B,P) && not_eq3(B,PC,C,PA);
	D_tower = eq(D,Q) && not_eq3(D,QG,G,QA);


//*	E Zone
int scoreE = 0;

	scoreE += int(eq(E,C)) * (
				1 + int(eq(F,H)) + int(eq(B,PC)&&ne(B,PA)) + int(eq(A,Q))
				);

	scoreE += int(eq(E,G)) * (
				1 + int(eq(F,H)) + int(eq(D,QG)&&ne(D,QA)) + int(eq(A,P))
				);


	scoreE += int( (B_slope||B_wall) && eq(A,Q) || (D_slope||D_wall) && eq(A,P) );



    bool En3 = all_eq2(E,F,H);

	bool EisWhite = checkwhite(vE);

	if ( scoreE==0 && !En3 ) {

		bool leftA= eq(A,QA) && not_eq4(A,PA,AA,P,Q);
		bool upA  = eq(A,PA) && not_eq4(A,QA,AA,P,Q);

		bool leftA_leftD = leftA == D_tower;
		bool upA_upB     =   upA == B_tower;

		bool upA_leftD   =   upA == D_tower;
		bool leftA_downD = leftA == D_wall;

		bool EAdarkerBD = Bl > El+0.333 && fract(Bl) < 0.92 ||Eisblack||EisWhite;	// xxx.alpha.straight

		bool bad1a = B_wall && eq(E,F) && ne(E,I) && (leftA || leftA_leftD || EAdarkerBD&&ne(A,P));
		bool bad1b = B_wall && D_tower && leftA && (EAdarkerBD&&ne(A,P));
		
		bool bad2a = D_wall && eq(E,H) && ne(E,I) && (upA || upA_upB || EAdarkerBD&&ne(A,Q));
		bool bad2b = D_wall && B_tower && upA  && (EAdarkerBD&&ne(A,Q));

		bool bad3 = B_wall && eq(E,H) && ne(E,I) && (upA || upA_leftD);
		bool bad4 = D_wall && eq(E,F) && ne(E,I) && (leftA || leftA_downD);

		if (bad1a||bad1b||bad2a||bad2b||bad3||bad4) return theEXIT;
    }

	scoreE += int(B_slope && eq(A,P) || D_slope && eq(A,Q));


int scoreZ = 0;

	bool ext_FH = any_eq2(F,RC,SG);

	bool En4 = En3 && eq(E,I);

	if ( !En4 && eq(F,H) ) {

		bool AEIstrong= EisWhite || fract(Bl)<0.92 && fract(Fl)<0.92 && min(Bl,Fl)-El>0.145898;	// xxx.alpha.straight
		
		bool badcond0 = scoreE==0 && (Eisblack && ne(E,F) || eq(E,I) && AEIstrong && !simEC && !simEG);

		bool badcond1 = En3 && !ext_FH;
		bool badcond2 = eq(F,I) && (Eisblack || EisWhite || !(ext_FH || any_eq2(F,C,R) || any_eq2(H,G,S)));

		bool condZ1 = eq(B,C) && ne(B,P);
		bool condZ2 = eq(D,G) && ne(D,Q);

		scoreZ = int((condZ1 || condZ2) && !badcond0 && !badcond1 && !badcond2);
    }


//*	A Zone

	bool An4 = all_eq3(A,AA,PA,QA);

	scoreZ += int(eq(PA,QA) && !An4 && scoreZ==1);


	bool Bn4 = all_eq3(B,P,PC,C);
	bool Dn4 = all_eq3(D,Q,QG,G);

	int in_B = int(eq(C,P) && !Bn4);
	int in_D = int(eq(G,Q) && !Dn4);
	
//*	B Zone
 int scoreB = 0;

	scoreB -= int(eq(B,PA)) * (
		1 + int(eq(C,P)) + int(eq(A,AA))
		);

	scoreB -= in_B * int(eq(A,AA));


//*  D Zone
 int scoreD = 0; 
 
	scoreD -= int(eq(D,QA)) * (
		1 + int(eq(G,Q)) + int(eq(A,AA))
		);

	scoreD -= in_D * int(eq(A,AA));

	scoreZ = clamp(scoreZ -in_B -in_D, 0, 1);

	int scoreFinal = scoreE + scoreB + scoreD + scoreZ ;
	scoreFinal += int(
				((scoreFinal | scoreB | scoreD) == 0) && 
				!Eisblack && !EisWhite && 
				(B_wall && D_tower || B_tower && D_wall) 
				);

	mixFactor *= scoreFinal>=2 ? 0.145898 : 1.0;
	bool closeB = all_eq4(E,C,F,P,PC) && eq(A,PA) && (ne(D,G) || eq(D,Q));
	bool closeD = all_eq4(E,H,G,Q,QG) && eq(A,QA) && (ne(B,C) || eq(B,P));

	return mixXE +float(scoreFinal<=0)*4.0 +float(closeB||closeD)*8.0;

}	//* E == A

		mixFactor *= comboA3 ? 0.145898 : 1.0;

//* ===============================================
//*              	E - C - G     main rules
//* ========================================= zz ==

    if (eq(E,C) ) {

		bool badC = ne(E,G) && all_eq2(B,A,PA) && all_eq3(E,F,P,PC);

		return mixXE +float(badC)*8.0;
	}

	if (eq(E,G)) {

		bool badC = ne(E,C) && all_eq2(D,A,QA) && all_eq3(E,H,Q,QG);

		return mixXE +float(badC)*8.0;
	}

	mixFactor = vE.a<0.002 ? 1.0 : mixFactor;		// xxx.alpha // better than Exit

//* =======================================================
//*                  F - H / B+ D+ Extended Rules
//* ================================================= zz ==


	B_slope = eq(B,PC) && ne(B,P) && ne(B,C) && (ne(C,P)||eq(A,P));
	D_slope = eq(D,QG) && ne(D,Q) && ne(D,G) && (ne(G,Q)||eq(A,Q));

	bool B_slopeFoot = B_slope && (ne(B,RC) || eq(C,CC));
	bool D_slopeFoot = D_slope && (ne(D,SG) || eq(G,GG));

	bool B_slopeHead = B_slope && (ne(B,PA) || ne(A,AA));
	bool D_slopeHead = D_slope && (ne(D,QA) || ne(A,AA));


	bool B_slopeL2 = B_slopeHead && B_slopeFoot;
	bool D_slopeL2 = D_slopeHead && D_slopeFoot;
	B_tower = eq(B,P) && ne(B,PC) && ne(B,C);
	D_tower = eq(D,Q) && ne(D,QG) && ne(D,G);

	bool anyWall = eq(B,C) || eq(D,G);
	bool anySlope = eq(B,PC) || eq(D,QG);

	bool anyStickE = eq(E,F)||eq(E,H);
	

    if (ne(A,B)) {

        bool na1 = comboA3 && anySlope;

        bool na2a =  B_slopeL2 && D_slopeL2;
        bool na2b =  B_slope && D_tower;
        bool na2c =  B_tower && D_slope;

		bool na2 = na2a||na2b||na2c;

        bool na3 =  B_slopeFoot && eq(A,P);
        bool na4 =  D_slopeFoot && eq(A,Q);

        bool na5 =  B_slopeFoot && eq(A,Q) && ne(D,G);
        bool na6 =  D_slopeFoot && eq(A,P) && ne(B,C);

        bool na7a =  B_slopeHead && simEC;
        bool na7b =  D_slopeHead && simEG;
        bool na7c =  B_slopeL2 && (simEG && ne(D,G));
        bool na7d =  D_slopeL2 && (simEC && ne(B,C));


		bool na7 = na7a||na7b||na7c||na7d;


		bool na81 =  eq(A,Q) && ne(A,QA) && (simEC && ne(B,C)) && anySlope;
		bool na82 =  eq(A,P) && ne(A,PA) && (simEG && ne(D,G)) && anySlope;
		bool na8 = na81||na82;



        bool na9 =  (B_slopeL2 || D_slopeL2) && eq(F,H);
	

        bool na10 =  B_slopeL2 && eq(H,SG);
        bool na11 =  D_slopeL2 && eq(F,RC);

		mixFactor *= float(!na2);

		if (na1||na2||na3||na4||na5||na6||na7||na8||na9||na10||na11) return mixXE +float(!anyStickE)*2.0;


    }


	float E_lumDiff = mix(0.33, 0.10, clamp((fract(El) - 0.8541) * 6.8541, 0.0, 1.0));		// xxx.alpha.straight
	bool aloneE =  mixFactor<0.01 && !simEC && !simEG && !simEI && El-Fl>E_lumDiff && El-Hl>E_lumDiff;

	bool En3 = all_eq2(E,F,H);
	bool ext_FH = eq(F,H) && (eq(F,RC) || eq(H,SG));

	bool badEn3 = En3 && mixFactor<0.01 && !ext_FH && !simEC && !simEG && !simEI;

	bool En4 = En3 && eq(E,I);

	bool aloneEn4 = !ext_FH && !simEC && !simEG && eq(E,S)==eq(E,SI) && eq(E,R)==eq(E,RI);
	bool badEn4a = mixFactor<0.01 && aloneEn4;
	bool badEn4b = (eq(B,C) && eq(B,RC) && eq(E,R) || eq(D,G) && eq(D,SG) && eq(E,S)) && aloneEn4;
	bool badEn4c = eq(A,B) && anyWall;

	bool badEn4 = En4 && (badEn4a||badEn4b||badEn4c);


	if (!anyWall && !aloneE && !badEn4) {

		bool B_hook = eq(B,PA) && not_eq2(A,P,AA);
		bool D_hook = eq(D,QA) && not_eq2(A,Q,AA);

		bool nwall01 = ne(A,B) && (B_slopeL2&&D_hook||B_hook&&D_slopeL2);



		bool goodFH = min(Fl,Hl)>Bl+0.145898||min(Fl,Hl)>=El;
		bool nwall02 = ne(A,B) && B_tower  && goodFH && (ne(E,H)||eq(E,I)||simEC||simEG);
		bool nwall03 = ne(A,B) && D_tower  && goodFH && (ne(E,F)||eq(E,I)||simEC||simEG);


		bool aloneBD =  !anyWall && !anySlope && ne(B,P) && ne(D,Q);
		bool BDfirefly = eq(F,H) && eq(F,I) && El-Fl>0.06 && any_eq2(F,P,Q);
		bool shortFH = eq(F,H) && not_eq2(F,E,I) && !ext_FH;

		bool badBD1 =  aloneBD && (eq(A,B)||BDfirefly);
		bool badBD2 =  aloneBD && (eq(A,B)||ne(F,H)||BDfirefly||shortFH);

		bool nwall1 = comboA3 && eq(F,H) && !badBD1;
		bool nwall4 = comboA3 && ne(B,PA) && eq(PA,QA) && !badBD1;

		bool nwall2 = (comboA3||ne(A,B)&&eq(A,P)) && eq(B,PC) && eq(C,CC);
		bool nwall3 = (comboA3||ne(A,B)&&eq(A,Q)) && eq(D,QG) && eq(G,GG);





		bool nwall7 = eq(A,Q) && (ne(A,AA)||eq(A,B)||eq(A,PA)||eq(PA,QA)) && (ne(B,PA)||eq(B,P)) &&simEC && !badBD1;
		bool nwall8 = eq(A,P) && (ne(A,AA)||eq(A,B)||eq(A,QA)||eq(PA,QA)) && (ne(D,QA)||eq(D,Q)) &&simEG && !badBD1;


		bool nwall9 = (ne(A,B)||eq(A,P)||eq(A,Q)||eq(A,PA)||eq(A,QA)) && simEC && simEG && !badBD2;

		bool nwall10 = ne(A,B) && En4;


		if (nwall01||nwall02||nwall03||nwall1||nwall2||nwall3||nwall4||nwall7||nwall8||nwall9||nwall10)  return mixXEoff;

	}


	bool betterFH = eq(F,H)&&(ne(E,I)||not_eq2(F,RI,SI)) || En4;



	if (ne(A,B) && eq(B,C) && (ne(B,R)||eq(F,RC))) {
		bool bad1 = not_eq3(B,PC,PA,CC) && not_eq3(D,Q,QA,QG) && ne(F,H);
		bool bad2 = eq(H,I) && ne(F,H) && Bl>Hl-0.01;
		bool bad = bad1||bad2||badEn4;

		B_wall = not_eq2(B,P,PC);
		bool longH = B_wall && D_tower;
		bool snake = not_eq2(B,P,PA) && eq(C,CC) && ne(C,RC) && (eq(A,P)||eq(A,Q)||eq(PA,QA)||eq(A,QA)&&eq(D,Q)) && (ne(PC,RC)||eq(F,RC));
		bool good1 = all_eq2(F,I,RC);
		bool good2 = simEG;
		bool good3 = betterFH && (!aloneE && !badEn3 ||longH);
		bool good4 = B_wall && all_eq2(D,Q,QG);
		bool good = snake||good1||good2||good3||good4;
		return mixXE + float(bad||!good)*4.0 +float(!anyStickE)*2.0;
	}

	if (ne(A,B) && eq(D,G) && (ne(D,S)||eq(H,SG))) {

		bool bad1 = not_eq3(D,QG,QA,GG) && not_eq3(B,P,PA,PC) && ne(F,H);
		bool bad2 = eq(F,I) && ne(F,H) && Bl>Fl-0.01;
		bool bad = bad1||bad2||badEn4;
		D_wall = not_eq2(D,Q,QG);
		bool longV = D_wall && B_tower;
		bool snake = not_eq2(D,Q,QA) && eq(G,GG) && ne(G,SG) && (eq(A,P)||eq(A,Q)||eq(PA,QA)||eq(A,PA)&&eq(B,P)) && (ne(QG,SG)||eq(H,SG));
		bool good1 = all_eq2(H,I,SG);
		bool good2 = simEC;
		bool good3 = betterFH && (!aloneE && !badEn3 ||longV);
		bool good4 = D_wall && all_eq2(B,P,PC);
		bool good = snake||good1||good2||good3||good4;
		return mixXE + float(bad||!good)*4.0 +float(!anyStickE)*2.0;
	}

	bool ext1 = ne(A,B) && all_eq2(F,H,RC) && eq(B,PC) && eq(B,P)!=eq(B,C);
	bool ext2 = ne(A,B) && all_eq2(F,H,SG) && eq(D,QG) && eq(D,Q)!=eq(D,G);

	bool sideW1 = ne(A,B) && betterFH && (eq(B,PC) && ne(P,C) && ne(B,RC)) && !aloneE && !badEn3 && !badEn4;
	bool sideW2 = ne(A,B) && betterFH && (eq(D,QG) && ne(Q,G) && ne(D,SG)) && !aloneE && !badEn3 && !badEn4;

	bool lastgood = ext1||ext2||sideW1||sideW2;


	return mixXE +float(!lastgood)*4.0 +float(!anyStickE)*2.0;

}	//* admixX


vec4 admixS( vec4 A, vec4 B, vec4 C, vec4 D, vec4 E, vec4 F, vec4 G, vec4 H, vec4 I
		   , vec4 Q, vec4 R, vec4 RC, vec4 RI, vec4 S, vec4 SG, vec4 SI, vec4 CC, bool simEC
		   ) {


    bool bad1 = any_eq2(F,C,I);

    bool bad2 =  (eq(F,RI) || eq(G,S) || eq(R, RI)) && ne(R,I);

    bool bad3 = eq(H, S) && not_eq2(H,I,SG);

    bool bad4 =  eq(R, RC) || eq(G,SG);

	bool extC = any_eq2(C,RC,CC);

	#define vX vF
	float mixFactor = 0.381966 * mixGate(vX,vE) * step(0.002, vE.a);	// xxx.alpha

	mixFactor =  (bad1||bad2||bad3||bad4||vX.a<0.002) ? 1.0 : mixFactor;	// xxx.alpha

	if ( all_eq2(E,C,D) ) {
		mixFactor = ( !checkwhite(vE) || extC ) ? mixFactor : 1.0;
		return mixXE;
		}

	bool ok1 = eq(E,C) && eq(B,D) && (extC || any_eq2(D,Q,A) ||eq(A,Q));

	bool ok2 = simEC && eq(E,D) && eq(B,C);

	bool ok3 = (simEC || mixFactor>0.01) && all_eq2(B,C,D) && (extC || any_eq2(Q,A,D)) && !all_eq2(E,A,I);

	mixFactor = ok1||ok2||ok3 ? mixFactor : 1.0;
	return mixXE;
}

//**************************************************************************************************zz**

void applyScaling(uvec2 xy) {

	int srcX = int(xy.x);
	int srcY = int(xy.y);

	vec4 E = src(srcX, srcY);
	vec4 B = src(srcX, srcY-1);
	vec4 D = src(srcX-1, srcY);
	vec4 F = src(srcX+1, srcY);
	vec4 H = src(srcX, srcY+1);

	vec4 J = E, K = E, L = E, M = E;

//* Skip processing for horizontal/vertical 3-pixel identical & mirror symmetric patterns
bool skiprest = all_eq2(E,D,F) || all_eq2(E,B,H) || eq(B,H)&&eq(D,F);
if (!skiprest) {


	vec4 A = src(srcX-1, srcY-1);
	vec4 C = src(srcX+1, srcY-1);
	vec4 G = src(srcX-1, srcY+1);
	vec4 I = src(srcX+1, srcY+1);

    vec4 P = src(srcX, srcY-2);
    vec4 Q = src(srcX-2, srcY);
    vec4 R = src(srcX+2, srcY);
    vec4 S = src(srcX, srcY+2);

	float Bl = luma(vB);
	float Dl = luma(vD);
	float El = luma(vE);
	float Fl = luma(vF);
	float Hl = luma(vH);

if (ne(B,H) && ne(D,F)) {

    vec4 PA = src(srcX-1, srcY-2);
    vec4 PC = src(srcX+1, srcY-2);
    vec4 QA = src(srcX-2, srcY-1);
    vec4 QG = src(srcX-2, srcY+1); //*             AA   PA   [P]  PC   CC
    vec4 RC = src(srcX+2, srcY-1); //*               -|----|----|----|-
    vec4 RI = src(srcX+2, srcY+1); //*             QA |  A |  B | C  | RC
    vec4 SG = src(srcX-1, srcY+2); //*               -|----|----|----|-
    vec4 SI = src(srcX+1, srcY+2); //*            [Q] |  D |  E | F  | [R]
    vec4 AA = src(srcX-2, srcY-2); //*               -|----|----|----|-
    vec4 CC = src(srcX+2, srcY-2); //*             QG |  G |  H | I  | RI
    vec4 GG = src(srcX-2, srcY+2); //*               -|----|----|----|-
    vec4 II = src(srcX+2, srcY+2); //*             GG   SG   [S]  SI   II

	// MMPX vec ver. move below
	bool simEA = sim(vE,vA);
	bool simEC = sim(vE,vC);
	bool simEG = sim(vE,vG);
	bool simEI = sim(vE,vI);


//*	B - D
	bool slope1 = ( ne(D,H) && ne(B,F) &&
					(eq(E,A)&&El<min(Bl,Dl)||El>max(Bl,Dl)) && (not_eq2(A,B,D) || ne(E,P) || ne(E,Q)) &&
					( eq(B,D) &&(eq(E,A)||any_eq3(B,P,C,PC)||any_eq3(D,Q,G,QG)||simEC||simEG) || simb(vB,vD)&&(eq(F,H)||any_eq2(E,C,G)) )
				);
//*	B - F
	bool slope2 = ( ne(B,D) && ne(F,H) &&
					(eq(E,C)&&El<min(Bl,Fl)||El>max(Bl,Fl)) && (not_eq2(C,B,F) || ne(E,P) || ne(E,R)) &&
					( eq(B,F)&&(eq(E,C)||any_eq3(B,P,A,PA)||any_eq3(F,R,I,RI)||simEI||simEA) || simb(vB,vF)&&(eq(D,H)||any_eq2(E,A,I)) )
				);
//*	D - H
	bool slope3 = ( ne(F,H) && ne(B,D) &&
					(eq(E,G)&&El<min(Dl,Hl)||El>max(Dl,Hl))  &&  (not_eq2(G,D,H) || ne(E,S) || ne(E,Q)) &&
					( eq(D,H) &&(eq(E,G)||any_eq3(D,A,Q,QA)||any_eq3(H,S,I,SI)||simEA||simEI) || simb(vD,vH)&&(eq(B,F)||any_eq2(E,A,I)) )
				);
//*	F - H
	bool slope4 = ( ne(B,F) && ne(D,H) &&
					(eq(E,I)&&El<min(Fl,Hl)||El>max(Fl,Hl))  &&  (not_eq2(I,F,H) || ne(E,R) || ne(E,S)) &&
					( eq(F,H) &&(eq(E,I)||any_eq3(F,R,C,RC)||any_eq3(H,S,G,SG)||simEG||simEC) || simb(vF,vH)&&(eq(B,D)||any_eq2(E,C,G)) )
				);

	bool slope1end = false;  bool slope2end = false;  bool slope3end = false;  bool slope4end = false;

	if (slope1) {
		J=admixX(A,B,C,D,E,F,G,H,I
				,P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG
				,El,Bl,Dl,Fl,Hl, simEC,simEG,simEI
				);
		bool slope1ok = (J.b < 1.01) && max(sign(El-Dl)*(Fl-El),sign(El-Dl)*(Hl-El))>-0.1;
		bool isCopy=checkeq(J,vD);
		vec4 mixL2 = mix(J,vE,0.145898);
		bool longH = slope1ok && all_eq2(D,C,Q) && ne(D,QG) && (ne(D,R)||eq(D,RI)) && (ne(D,QA)||eq(D,A));
		bool longV = slope1ok && all_eq2(D,G,P) && ne(D,PC) && (ne(D,S)||eq(D,SI)) && (ne(D,PA)||eq(D,A));
		K= longH ? isCopy ? J : mixL2 : K;
		L= longV ? isCopy ? J : mixL2 : L;
		slope1end = (J.b < 3.99);

		J = (J.b > 3.99) ? vE :
			(J.b > 1.01) ? (J - 2.0) :
			J;
	}

	if (slope2) {
		K=admixX(C,F,I,B,E,H,A,D,G
				,R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA
				,El,Fl,Bl,Hl,Dl, simEI,simEA,simEG
				);

		bool slope2ok = K.b<1.01 && max(sign(El-Fl)*(Dl-El),sign(El-Fl)*(Hl-El))>-0.1;
		bool isCopy=checkeq(K,vF);
		vec4 mixL2 = mix(K,vE,0.145898);
		bool longH = slope2ok && all_eq2(F,A,R) && ne(F,RI) && (ne(F,RC)||eq(F,C)) && (ne(F,Q)||eq(F,QG));
		bool longV = slope2ok && all_eq2(F,I,P) && ne(F,PA) && (ne(F,PC)||eq(F,C)) && (ne(F,S)||eq(F,SG));
		J= longH ? isCopy ? K : mixL2 : J;
		M= longV ? isCopy ? K : mixL2 : M;
		slope2end = (K.b < 3.99);

		K = (K.b > 3.99) ? vE :
			(K.b > 1.01) ? (K - 2.0) :
			K;
	}

	if (slope3) {
		L=admixX(G,D,A,H,E,B,I,F,C
				,Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II
				,El,Dl,Hl,Bl,Fl, simEA,simEI,simEC
				);

		bool slope3ok = (L.b < 1.01) && max(sign(El-Dl)*(Bl-El),sign(El-Dl)*(Fl-El))>-0.1;
		bool isCopy=checkeq(L,vD);
		vec4 mixL2 = mix(L,vE,0.145898);
		bool longH = slope3ok && all_eq2(D,I,Q) && ne(D,QA) && (ne(D,QG)||eq(D,G)) && (ne(D,R)||eq(D,RC));
		bool longV = slope3ok && all_eq2(D,A,S) && ne(D,SI) && (ne(D,SG)||eq(D,G)) && (ne(D,P)||eq(D,PC));
		M= longH ? isCopy ? L : mixL2 : M;
		J= longV ? isCopy ? L : mixL2 : J;

		slope3end = (L.b < 3.99);
		L = (L.b > 3.99) ? vE :
			(L.b > 1.01) ? (L - 2.0) :
			L;
	}

	if (slope4) {
		M=admixX(I,H,G,F,E,D,C,B,A
				,S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC
				,El,Hl,Fl,Dl,Bl, simEG,simEC,simEA
				);

		bool slope4ok = (M.b < 1.01) && max(sign(El-Fl)*(Bl-El), sign(El-Fl)*(Dl-El))>-0.1;
		bool isCopy=checkeq(M,vF);
		vec4 mixL2 = mix(M,vE,0.145898);
		bool longH = slope4ok && all_eq2(F,G,R) && ne(F,RC) && (ne(F,RI)||eq(F,I)) && (ne(F,Q)||eq(F,QA));
		bool longV = slope4ok && all_eq2(F,C,S) && ne(F,SG) && (ne(F,SI)||eq(F,I)) && (ne(F,P)||eq(F,PA));
		L= longH ? isCopy ? M : mixL2 : L;
		K= longV ? isCopy ? M : mixL2 : K;

		slope4end = (M.b < 3.99);
		M = (M.b > 3.99) ? vE :
			(M.b > 1.01) ? (M - 2.0) :
			M;
	}

skiprest = slope1||slope2||slope3||slope4;


//* =========================  2:1 staggered slope  (new rule)  ===========================

    bool bottonH = not_eq5(H,E,A,C,D,F);

		if (bottonH && !slope4end && !slope2 && !slope3 && ne(B,F) &&
            ne(E,F) && eq(H,R) && eq(F,G) ) {
            M = admixS( A, B, C, D, E, F, G, H, I
                      , Q, R, RC, RI, S, SG, SI, CC, simEC
                      );
            skiprest = true;}

        if (bottonH && !slope3end && !slope1 && !slope4 && ne(B,D) &&
             ne(E,D) && eq(H,Q) && eq(D,I) ) {
            L = admixS( C, B, A, F, E, D, I, H, G
                      , R, Q, QA, QG, S, SI, SG, AA, simEA
                      );
            skiprest = true;}


    bool topH = not_eq5(B,E,G,I,D,F);

        if (topH && !slope2end && !slope1 && !slope4 && ne(F,H) &&
              ne(E,F) && eq(B,R) && eq(A,F) ) {
            K = admixS( G, H, I, D, E, F, A, B, C
                      , Q, R, RI, RC, P, PA, PC, II, simEI
                      );
            skiprest = true;}

        if (topH && !slope1end && !slope2 && !slope3 && ne(D,H) &&
             ne(E,D) && eq(B,Q) && eq(C,D) ) {
            J = admixS( I, H, G, F, E, D, C, B, A
                      , R, Q, QG, QA, P, PC, PA, GG, simEG
                      );
            skiprest = true;}


    bool leftV = not_eq5(D,E,C,I,B,H);

        if (leftV && !slope3end && !slope1 && !slope4 && ne(F,H) &&
              ne(E,H) && eq(D,S) && eq(A,H) ) {
            L = admixS( C, F, I, B, E, H, A, D, G
                      , P, S, SI, SG, Q, QA, QG, II, simEI
                      );
            skiprest = true;}

        if (leftV && !slope1end && !slope2 && !slope3 && ne(B,F) &&
              ne(E,B) && eq(D,P) && eq(B,G) ) {
            J = admixS( I, F, C, H, E, B, G, D, A
                      , S, P, PC, PA, Q, QG, QA, CC, simEC
                      );
            skiprest = true;}



    bool rightV = not_eq5(F,E,A,G,B,H);

        if (rightV && !slope4end && !slope2 && !slope3 && ne(D,H) &&
              ne(E,H) && eq(F,S) && eq(H,C) ) {
            M = admixS( A, D, G, B, E, H, C, F, I
                      , P, S, SG, SI, R, RC, RI, GG, simEG
                      );
            skiprest = true;}

        if (rightV && !slope2end && !slope1 && !slope4 && ne(B,D) &&
             ne(E,B) && eq(F,P) && eq(B,I) ) {
            K = admixS( G, D, A, H, E, B, I, F, C
                      , S, P, PA, PC, R, RI, RC, AA, simEA
                      );
            skiprest = true;}

}

skiprest = skiprest||vE.a<0.002||vB.a<0.002||vD.a<0.002||vF.a<0.002||vH.a<0.002;

if (!skiprest) {

//* =================== Concave+Cross pattern (P100) =================


 //* top [B]
    if( Bl<El && not_eq4(E,A,C,D,F) && all_eq4(E,G,H,I,S) ) {

	bool DD = eq(A, D);
	bool FF = eq(C, F);

	Bl = any_eq2(B, A, C) ? -1.0 : fract(Bl);
	Dl = fract(Dl);	Fl = fract(Fl);	// xxx.alpha.straight

	float tinS = eq(D,F) && DD!=FF ? 2.0 : 0.0;

	float brightLoss = float(ne(D,F)) + abs(Dl - El) * 1.618034;
	
	Dl = Dl - step(El,Dl)*brightLoss - float(DD) + tinS;
	Fl = Fl - step(El,Fl)*brightLoss - float(FF) + tinS;

	float maxl = max(Bl, max(Dl, Fl));

	vec4 vT =	any_eq2(B,D,F) ? vB :
			maxl <= 0.0 ? vE :
			maxl <= Dl ? vD :
			maxl <= Fl ? vF :
						vB ;
	vT = admixC(vT,vE);

	bool nolink = ne(D,F) && !DD && !FF;

	J = Dl<0.0&&nolink ? vE : vT;
    K = Fl<0.0&&nolink ? vE : vT;
}

 //* bottom (H)
if ( Hl<El && not_eq4(E,G,I,D,F) && all_eq4(E,A,B,C,P) ) {

	bool DD = eq(D, G);
	bool FF = eq(F, I);

	Hl = any_eq2(H, G, I) ? -1.0 : fract(Hl);
	Dl = fract(Dl);	Fl = fract(Fl);	// xxx.alpha.straight

	float tinS = eq(D,F) && DD!=FF ? 2.0 : 0.0;

	float brightLoss = float(ne(D,F)) + abs(Dl - El) * 1.618034;

	Dl = Dl - step(El,Dl)*brightLoss - float(DD) + tinS;
	Fl = Fl - step(El,Fl)*brightLoss - float(FF) + tinS;

	float maxl = max(Hl, max(Dl, Fl));
	
	vec4 vT = any_eq2(H,D,F) ? vH :
			maxl <= 0.0 ? vE :
			maxl <= Dl ? vD :
			maxl <= Fl ? vF :
						vH ;
	vT=admixC(vT,vE);

	bool nolink = ne(D,F) && !DD && !FF;

	L = Dl<0.0&&nolink ? vE : vT;
    M = Fl<0.0&&nolink ? vE : vT;
}

 //* right [F]
if ( Fl<El && not_eq4(E,B,C,H,I) && all_eq4(E,A,D,G,Q) ) {

	bool BB = eq(B, C);
	bool HH = eq(H, I);

	Fl = any_eq2(F, C, I) ? -1.0 : fract(Fl);
	Bl = fract(Bl);	Hl = fract(Hl);	// xxx.alpha.straight

	float tinS = eq(B,H) && BB!=HH ? 2.0 : 0.0;

	float brightLoss = float(ne(B,H)) + abs(Bl - El) * 1.618034;

	Bl = Bl - step(El,Bl)*brightLoss - float(BB) + tinS;
	Hl = Hl - step(El,Hl)*brightLoss - float(HH) + tinS;

	float maxl = max(Fl, max(Bl, Hl));

	vec4 vT =any_eq2(F,B,H) ? vF :
			maxl <= 0.0 ? vE :
			maxl <= Bl ? vB :
			maxl <= Hl ? vH :
						vF ;
	vT=admixC(vT,vE);

	bool nolink = ne(B,H) && !BB && !HH;

	K = Bl<0.0&&nolink ? vE : vT;
    M = Hl<0.0&&nolink ? vE : vT;
}

 //* left [D]
if ( Dl<El && not_eq4(E,A,B,G,H) && all_eq4(E,C,F,I,R) ) {

	bool BB = eq(B, A);
	bool HH = eq(H, G);

	Dl = any_eq2(D, A, G) ? -1.0 : fract(Dl);
	Bl = fract(Bl);	Hl = fract(Hl);	// xxx.alpha.straight

	float tinS = eq(B,H) && BB!=HH ? 2.0 : 0.0;

	float brightLoss = float(ne(B,H)) + abs(Bl - El) * 1.618034;

	Bl = Bl - step(El,Bl)*brightLoss - float(BB) + tinS;
	Hl = Hl - step(El,Hl)*brightLoss - float(HH) + tinS;

	float maxl = max(Dl, max(Bl, Hl));

	vec4 vT =any_eq2(D,B,H) ? vD :
			maxl <= 0.0 ? vE :
			maxl <= Bl ? vB :
			maxl <= Hl ? vH :
						vD ;
	vT=admixC(vT,vE);

	bool nolink = ne(B,H) && !BB && !HH;

	J = Bl<0.0&&nolink ? vE : vT;
    L = Hl<0.0&&nolink ? vE : vT;
}

//* ===================  SKorpion pattern (P99) ==================


if (ne(E,F) && all_eq2(B,F,H) && all_eq3(E,D,C,I) && (eq(E,Q)||El>Fl) && ne(F,src(srcX+3, srcY)) ) {K=admixK(vF,vE); M=K;}	//* RIGHT
if (ne(E,D) && all_eq2(B,D,H) && all_eq3(E,F,A,G) && (eq(E,R)||El>Dl) && ne(D,src(srcX-3, srcY)) ) {J=admixK(vD,vE); L=J;}	//* LEFT
if (ne(E,H) && all_eq2(D,F,H) && all_eq3(E,B,G,I) && (eq(E,P)||El>Hl) && ne(H,src(srcX, srcY+3)) ) {L=admixK(vH,vE); M=L;}	//* BOTTOM
if (ne(E,B) && all_eq2(B,D,F) && all_eq3(E,H,A,C) && (eq(E,S)||El>Bl) && ne(B,src(srcX, srcY-3)) ) {J=admixK(vB,vE); K=J;}	//* TOP

}
}
	//* final write
    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
}
