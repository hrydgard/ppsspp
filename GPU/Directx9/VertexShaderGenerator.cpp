// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <stdio.h>
#include <locale.h>

#if defined(_WIN32) && defined(_DEBUG)
#include "Common/CommonWindows.h"
#endif

#include "base/stringutil.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "Core/Config.h"

#include "GPU/Directx9/VertexShaderGenerator.h"

#undef WRITE

#define WRITE p+=sprintf

bool CanUseHardwareTransform(int prim) {
	if (!g_Config.bHardwareTransform)
		return false;
	return !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES;
}

int TranslateNumBones(int bones) {
	if (!bones) return 0;
	if (bones < 4) return 4;
	// if (bones < 8) return 8;   I get drawing problems in FF:CC with this!
	return bones;
}

// prim so we can special case for RECTANGLES :(
void ComputeVertexShaderID(VertexShaderID *id, int prim, bool useHWTransform) {
	const u32 vertType = gstate.vertType;
	int doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doTextureProjection = gstate.getUVGenMode() == 1;

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0;
	bool hasBones = (vertType & GE_VTYPE_WEIGHT_MASK) != 0;
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();

	memset(id->d, 0, sizeof(id->d));
	id->d[0] = lmode & 1;
	id->d[0] |= ((int)gstate.isModeThrough()) << 1;
	id->d[0] |= ((int)enableFog) << 2;
	id->d[0] |= doTexture << 3;
	id->d[0] |= (hasColor & 1) << 4;
	if (doTexture) {
		id->d[0] |= (gstate_c.flipTexture & 1) << 5;
		id->d[0] |= (doTextureProjection & 1) << 6;
	}

	if (useHWTransform) {
		id->d[0] |= 1 << 8;
		id->d[0] |= (hasNormal & 1) << 9;

		// UV generation mode
		id->d[0] |= gstate.getUVGenMode() << 16;

		// The next bits are used differently depending on UVgen mode
		if (gstate.getUVGenMode() == 1) {
			id->d[0] |= gstate.getUVProjMode() << 18;
		} else if (gstate.getUVGenMode() == 2) {
			id->d[0] |= gstate.getUVLS0() << 18;
			id->d[0] |= gstate.getUVLS1() << 20;
		}

		// Bones
		if (hasBones)
			id->d[0] |= (TranslateNumBones(gstate.getNumBoneWeights()) - 1) << 22;

		// Okay, d[1] coming up. ==============

		if (gstate.isLightingEnabled() || gstate.getUVGenMode() == 2) {
			// Light bits
			for (int i = 0; i < 4; i++) {
				id->d[1] |= gstate.getLightComputation(i) << (i * 4);
				id->d[1] |= gstate.getLightType(i) << (i * 4 + 2);
			}
			id->d[1] |= (gstate.materialupdate & 7) << 16;
			for (int i = 0; i < 4; i++) {
				id->d[1] |= (gstate.isLightChanEnabled(i) & 1) << (20 + i);
			}
		}
		id->d[1] |= gstate.isLightingEnabled() << 24;
		id->d[1] |= ((vertType & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT) << 25;
	}
}

static const char * const boneWeightAttrDecl[8] = {
	"attribute mediump float a_w1;\n",
	"attribute mediump vec2 a_w1;\n",
	"attribute mediump vec3 a_w1;\n",
	"attribute mediump vec4 a_w1;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump float a_w2;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump vec2 a_w2;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump vec3 a_w2;\n",
	"attribute mediump vec4 a_w1;\nattribute mediump vec4 a_w2;\n",
};

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

#if 0 // used for debugging
void GenerateVertexShader(int prim, char *buffer, bool useHWTransform) {
	const char * vscode =
    " float4x4 u_proj : register(c0);              "
    "                                              "
    " struct VS_IN                                 "
    "                                              "
    " {                                            "
    "		float4 ObjPos   : POSITION;            "                 
	"		float3 Uv   : TEXCOORD0;               "
	"		float4 C1    : COLOR0;                 "  // Vertex color
	"		float4 C2    : COLOR1;                 "  // Vertex color
    " };                                           "
    "                                              "
    " struct VS_OUT                                "
    " {                                            "
    "		float4 ObjPos   : POSITION;            "                 
	"		float4 Uv   : TEXCOORD0;               "
	"		float4 C1    : COLOR0;                 "  // Vertex color
	"		float4 C2    : COLOR1;                 "  // Vertex color
    " };                                           "
    "                                              "
    " VS_OUT main( VS_IN In )                      "
    " {                                            "
    "		VS_OUT Out;                              "
	"       Out.ObjPos = mul( float4(In.ObjPos.xyz, 1), u_proj );  "  // Transform vertex into
	"		Out.Uv = float4(In.Uv.xy, 0, In.Uv.z);			"
	"		Out.C1 = In.C1;			"
	"		Out.C2 = In.C2;			"
    "		return Out;                              "  // Transfer color
    " }                                            ";

	strcpy(buffer, vscode);
}
#else

void GenerateVertexShader(int prim, char *buffer, bool useHWTransform) {
	char *p = buffer;
	const u32 vertType = gstate.vertType;

	int lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
	int doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0 || !useHWTransform;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0 && useHWTransform;
	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool throughmode = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
	bool flipV = gstate_c.flipTexture;
	bool doTextureProjection = gstate.getUVGenMode() == 1;

	DoLightComputation doLight[4] = {LIGHT_OFF, LIGHT_OFF, LIGHT_OFF, LIGHT_OFF};
	if (useHWTransform) {
		int shadeLight0 = gstate.getUVGenMode() == 2 ? gstate.getUVLS0() : -1;
		int shadeLight1 = gstate.getUVGenMode() == 2 ? gstate.getUVLS1() : -1;
		for (int i = 0; i < 4; i++) {
			if (i == shadeLight0 || i == shadeLight1)
				doLight[i] = LIGHT_SHADE;
			if (gstate.isLightingEnabled() && gstate.isLightChanEnabled(i))
				doLight[i] = LIGHT_FULL;
		}
	}


	if (gstate.isModeThrough())	{
		WRITE(p, "float4x4 u_proj_through;\n");
	} else {
		WRITE(p, "float4x4 u_proj;\n");
		// Add all the uniforms we'll need to transform properly.
	}
	if (useHWTransform || !hasColor)
		WRITE(p, "float4 u_matambientalpha;\n");  // matambient + matalpha
	

	WRITE(p, " struct VS_IN                                ");
    WRITE(p, "                                             ");
    WRITE(p,  " {                                          ");
    WRITE(p, "		float4 ObjPos   : POSITION;            ");
	WRITE(p, "		float3 Uv   : TEXCOORD0;               ");
	WRITE(p, "		float4 C1    : COLOR0;                 ");
	WRITE(p, "		float4 C2    : COLOR1;                 ");
    WRITE(p, " };                                          ");
    WRITE(p, "                                             ");
    WRITE(p, " struct VS_OUT                               ");
    WRITE(p, " {                                           ");
    WRITE(p, "		float4 ObjPos   : POSITION;            ");
	WRITE(p, "		float4 Uv   : TEXCOORD0;               ");
	WRITE(p, "		float4 C1    : COLOR0;                 ");
	WRITE(p, "		float4 C2    : COLOR1;                 ");
	if (enableFog) {
		WRITE(p, "float v_fogdepth:FOG;\n");
	}
    WRITE(p, " };                                          ");
    WRITE(p, "                                             ");
    WRITE(p, " VS_OUT main( VS_IN In )                     ");
    WRITE(p, " {                                           ");	
	WRITE(p, "		VS_OUT Out;							   ");  
	if (1) {
		// Simple pass-through of vertex data to fragment shader
		if (gstate.isModeThrough())	{
			WRITE(p, "Out.ObjPos = mul( float4(In.ObjPos.xyz, 1), u_proj_through );");
			//WRITE(p, "Out.ObjPos.z = ((1+Out.ObjPos.z)/2);"); // Dx z versus opengl z
		} else {
			//WRITE(p, "  Out.ObjPos = mul( u_proj, float4(In.ObjPos.xyz, 1) );");
			WRITE(p, "Out.ObjPos = mul( float4(In.ObjPos.xyz, 1), u_proj );");
			//WRITE(p, "Out.ObjPos.z = ((1+Out.ObjPos.z)/2);"); // Dx z versus opengl z
		}
	//WRITE(p, "Out.Uv = In.Uv;");
	WRITE(p, "Out.Uv = float4(In.Uv.xy, 0, In.Uv.z);");
	if (hasColor) {
		WRITE(p, "Out.C1 = In.C1;");
		WRITE(p, "Out.C2 = In.C2;");
	} else {
		WRITE(p, "  Out.C1 = u_matambientalpha;\n");
		WRITE(p, "  Out.C2 = float4(0,0,0,0);\n");
	}
	if (enableFog) {
		WRITE(p, "  Out.v_fogdepth = In.ObjPos.w;\n");
	}
    WRITE(p, "	return Out;             ");
	}
	WRITE(p, "}\n");
}



#endif
