/* =========================================================================
 * MMPX
 * by Morgan McGuire and Mara Gagiu
 * https:/／casual-effects.com/research/McGuire2021PixelArt/
 * License: MIT
 * =========================================================================
 * MMPX Enhanced
 * =========================================================================
 * An optimized refinement of the original MMPX shader that addresses key 
 * visual artifacts while fully preserving the baseline's signature performance and efficiency.
 *
 * (C) 2025-2026 by crashGG.
 * ========================================================================= */

// Performs 2x upscaling.

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


float luma(vec4 col) {

	//Use CRT-era BT.601 standard.
    return dot(col.rgb, vec3(0.299, 0.587, 0.114)) + (1.0-col.a)*256.0;
}


float mixGate(vec4 col1, vec4 col2) {

	highp vec4 diff = col1 - col2;

	float delta_range = max(diff.r, max(diff.g, diff.b)) - min(diff.r, min(diff.g, diff.b));

	highp float dot_diff = dot(diff.rgb, diff.rgb);

	highp float factor = (delta_range * delta_range) * 2.618034;

	float alpha_match_mask = step(0.5, abs(diff.a));

	return step(dot_diff , mix(0.75, 0.0, factor) - alpha_match_mask*5.0);
}

#define all_eq2(a, b1, b2) (a == b1 && a == b2)
#define all_eq4(a, b1, b2, b3, b4) (a == b1 && a == b2 && a == b3 && a == b4)
#define any_eq2(a, b1, b2) (a == b1 || a == b2)
#define none_eq2(a, b1, b2) !any_eq2(a, b1, b2)
#define none_eq4(a, b1, b2, b3, b4) (a!=b1 && a!=b2 && a!=b3 && a!=b4)

vec4 admixC(vec4 vX, vec4 vE) {

	float mixFactor = mixGate(vX, vE) * (-0.381966) + 1.0;

	return mix(vX,vE,mixFactor);
}

vec4 admixK(vec4 vX, vec4 vE) {
    vec4 diff = vX - vE;

	float mixFactor = dot(diff.rgb, diff.rgb) * 0.16666 + 0.5;

	return mix(vX,vE,mixFactor);
}

void applyScaling(uvec2 xy) {
    int srcX = int(xy.x);
    int srcY = int(xy.y);

	uint E = srcu(srcX, srcY);
	uint B = srcu(srcX, srcY-1);
	uint D = srcu(srcX-1, srcY);
	uint F = srcu(srcX+1, srcY);
	uint H = srcu(srcX, srcY+1);

	vec4 vE = unpackUnorm4x8(E);
	vec4 J = vE, K = vE, L = vE, M = vE;

	bool skiprest = (E == D && E == F) || (E == B && E == H) || (B == H && D == F);

	if (!skiprest) {

		uint A = srcu(srcX-1, srcY-1);
		uint C = srcu(srcX+1, srcY-1);
		uint G = srcu(srcX-1, srcY+1);
		uint I = srcu(srcX+1, srcY+1);

		uint P = srcu(srcX, srcY-2);
		uint Q = srcu(srcX-2, srcY);
		uint R = srcu(srcX+2, srcY);
		uint S = srcu(srcX, srcY+2);

		uint PA = srcu(srcX-1, srcY-2);
		uint PC = srcu(srcX+1, srcY-2);
		uint QA = srcu(srcX-2, srcY-1);
		uint QG = srcu(srcX-2, srcY+1);
		uint RC = srcu(srcX+2, srcY-1);
		uint RI = srcu(srcX+2, srcY+1);
		uint SG = srcu(srcX-1, srcY+2);
		uint SI = srcu(srcX+1, srcY+2);

		vec4 vB = unpackUnorm4x8(B);
		vec4 vD = unpackUnorm4x8(D);
		vec4 vF = unpackUnorm4x8(F);
		vec4 vH = unpackUnorm4x8(H);

		float Bl = luma(vB);
		float Dl = luma(vD);
		float El = luma(vE);
		float Fl = luma(vF);
		float Hl = luma(vH);

		bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;


	// B - D
		if ( E!=B && (D == B && D != H && D != F) && (El >= Dl || E == A && B !=PA && D !=QA) && any_eq2(E, C, G) && ((El < Dl) || A != D || E != P || E != Q)
			) {
			J=vB;
			slope1 = true;
		}

	// B - F
		if ( E!=B && (B == F && B != D && B != H) && (El >= Bl || E == C && B !=PC && F !=RC) && any_eq2(E, A, I) && ((El < Bl) || C != B || E != P || E != R)
		 ) {
			K=vB;
			slope2 = true;
		}

	// D - H
		if ( E!=H && (H == D && H != F && H != B) && (El >= Hl || E == G && D !=QG && H !=SG) && any_eq2(E, A, I) && ((El < Hl) || G != H || E != S || E != Q)
		 ) {
			L=vH;
			slope3 = true;
		}
	// F - H
		if ( E!=H && (F == H && F != B && F != D) && (El >= Fl || E == I && F !=RI && H !=SI) && any_eq2(E, C, G) && ((El < Fl) || I != H || E != R || E != S)
		  ) {
			M=vH;
			slope4 = true;
		}

	//  long gentle 2:1 slope

	if (slope4) { //zone4 long slope
		if (all_eq2(R,F,G) && R != RC && Q != G) L=M;
		// vertical
		if (all_eq2(S,H,C) && S != SG && P != C) K=M;
	}

	if (slope3) { //zone3 long slope
		// horizontal
		if (all_eq2(Q,D,I) && Q != QA && R != I) M=L;
		// vertical
		if (all_eq2(S,H,A) && S != SI && A != P) J=L;
	}

	if (slope2) { //zone2 long slope
		// horizontal
		if (all_eq2(R,F,A) && R != RI && A != Q) J=K;
		// vertical
		if (all_eq2(P,B,I) && P != PA && I != S) M=K;
	}

	if (slope1) { //zone1 long slope
		// horizontal
		if (all_eq2(Q,D,C) && Q != QG && C != R) K=J;
		// vertical
		if (all_eq2(P,B,G) && P != PC && G != S) L=J;
	}

	skiprest = skiprest||slope1||slope2||slope3||slope4||E==0u||B==0u||D==0u||F==0u||H==0u;

	/* Concave + Cross type */

		if (!skiprest && Bl < El && all_eq4(E, G, H, I, S) && none_eq4(E, A, D, C, F)) { J=admixC(vB,J);	K=J;    skiprest = true;}
		if (!skiprest && Hl < El && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F)) { L=admixC(vH,L);	M=L;    skiprest = true;}
		if (!skiprest && Fl < El && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H)) { K=admixC(vF,K);	M=K;    skiprest = true;}
		if (!skiprest && Dl < El && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H)) { J=admixC(vD,J);	L=J;    skiprest = true;}

	/* K type */

		if (!skiprest && (E != F && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H)) && (F != srcu(srcX + 3, srcY))) {K=admixK(vF,K); M=K;skiprest=true;}	// RIGHT
		if (!skiprest && (E != D && all_eq4(E, A, G, F, R) && all_eq2(D, B, H)) && (D != srcu(srcX - 3, srcY))) {J=admixK(vD,J); L=J;skiprest=true;}	// LEFT
		if (!skiprest && (E != H && all_eq4(E, G, I, B, P) && all_eq2(H, D, F)) && (H != srcu(srcX, srcY + 3))) {L=admixK(vH,L); M=L;skiprest=true;}	// BOTTOM
		if (!skiprest && (E != B && all_eq4(E, A, C, H, S) && all_eq2(B, D, F)) && (B != srcu(srcX, srcY - 3))) {J=admixK(vB,J); K=J;}				// TOP

	} // not constant

	//* final write

    ivec2 destXY = ivec2(xy) * 2;
    writeColorf(destXY, J);
    writeColorf(destXY + ivec2(1, 0), K);
    writeColorf(destXY + ivec2(0, 1), L);
    writeColorf(destXY + ivec2(1, 1), M);
}