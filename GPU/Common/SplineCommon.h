// Copyright (c) 2013- PPSSPP Project.

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

#pragma once

#include "Common/CommonTypes.h"
#include "Common/Swap.h"
#include "GPU/Math3D.h"
#include "GPU/ge_constants.h"

// PSP compatible format so we can use the end of the pipeline in beziers etc
struct SimpleVertex {
	float uv[2];
	union {
		u8 color[4];
		u32_le color_32;
	};
	Vec3Packedf nrm;
	Vec3Packedf pos;
};

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.
struct BezierPatch {
	Vec3f *pos;
	Vec4f *col;
	Vec2f *tex;
	u32_le defcolor;
	int count_u;
	int count_v;
	GEPatchPrimType primType;
	bool patchFacing;
};

struct SplinePatchLocal {
	Vec3f *pos;
	Vec4f *col;
	Vec2f *tex;
	u32_le defcolor;
	int tess_u;
	int tess_v;
	int count_u;
	int count_v;
	int type_u;
	int type_v;
	bool patchFacing;
	GEPatchPrimType primType;
};

enum SplineQuality {
	LOW_QUALITY = 0,
	MEDIUM_QUALITY = 1,
	HIGH_QUALITY = 2,
};

bool CanUseHardwareTessellation(GEPatchPrimType prim);
void TessellateSplinePatch(u8 *&dest, u16 *indices, int &count, const SplinePatchLocal &spatch, u32 origVertType, int maxVertices);
void TessellateBezierPatch(u8 *&dest, u16 *&indices, int &count, int tess_u, int tess_v, const BezierPatch &patch, u32 origVertType, int maxVertices);

#define TEMPLATE_PARAMETER_DISPATCHER(NAME, FUNCNAME) \
template<typename Func, int NumParams> \
class TemplateParameterDispatcher##NAME { \
 	/* Store all combinations of template functions into an array */ \
	template<int LoopCount, int Index = 0, bool ...Params> \
	struct Initializer { \
		Initializer(Func funcs[]) { \
			Initializer<LoopCount - 1, (Index << 1) + 1, true, Params...> _true(funcs); \
			Initializer<LoopCount - 1, (Index << 1) + 0, false, Params...> _false(funcs); \
		} \
	}; \
 	/* Specialized for terminates the recursive loop */ \
	template<int Index, bool ...Params> \
	struct Initializer<0, Index, Params...> { \
		Initializer(Func funcs[]) { \
			funcs[Index] = &FUNCNAME<Params...>; \
		} \
	}; \
 \
private: \
	Func funcs[1 << NumParams]; /* Function pointers array */ \
public: \
	TemplateParameterDispatcher##NAME() { \
		Initializer<NumParams>::Initializer(funcs); \
	} \
 \
	Func GetFunc(const bool params[]) const { \
 		/* Convert bool parameters to index of the array */ \
		int param = 0; \
		for (int i = 0; i < NumParams; ++i) \
			param |= params[i] << i; \
 \
		return funcs[param]; \
	} \
};
