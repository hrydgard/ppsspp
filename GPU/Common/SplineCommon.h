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
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"
#include "GPU/Math3D.h"
#include "GPU/ge_constants.h"
#include "Core/Config.h"

#define HALF_CEIL(x) (x + 1) / 2 // Integer ceil = (int)ceil((float)x / 2.0f)

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

void BuildIndex(u16 *indices, int &count, int num_u, int num_v, GEPatchPrimType prim_type, int total = 0);

enum SplineQuality {
	LOW_QUALITY = 0,
	MEDIUM_QUALITY = 1,
	HIGH_QUALITY = 2,
};

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.
struct BezierPatch {
	int tess_u;
	int tess_v;
	int count_u;
	int count_v;
	int type_u;
	int type_v;
	int num_patches_u;
	int num_patches_v;
	GEPatchPrimType primType;
	bool patchFacing;

	void Init(int maxVertices) {
		switch (g_Config.iSplineBezierQuality) {
		case LOW_QUALITY:
			tess_u = 2;
			tess_v = 2;
			break;
		case MEDIUM_QUALITY:
			// Don't cut below 2, though.
			if (tess_u > 2) tess_u = HALF_CEIL(tess_u);
			if (tess_v > 2) tess_v = HALF_CEIL(tess_v);
			// Pass through
		case HIGH_QUALITY:
			// Downsample until it fits, in case crazy tessellation factors are sent.
			while ((tess_u + 1) * (tess_v + 1) * num_patches_u * num_patches_v > maxVertices) {
				tess_u--;
				tess_v--;
			}
			break;
		}
	}

	int GetTessU(int patch_u) const { return tess_u + 1; }
	int GetTessV(int patch_v) const { return tess_v + 1; }

	int GetPointIndex(int patch_u, int patch_v) const { return patch_v * 3 * count_u + patch_u * 3;}

	int GetIndexU(int patch_u, int tile_u) const { return tile_u; }
	int GetIndexV(int patch_v, int tile_v) const { return tile_v; }

	int GetPatchIndex(int patch_u, int patch_v) const { return patch_v * num_patches_u + patch_u;}
	int GetIndex(int index_u, int index_v, int patch_u, int patch_v) const {
		return index_v * (tess_u + 1) + index_u + (tess_u + 1) * (tess_v + 1) * GetPatchIndex(patch_u, patch_v);
	}

	void BuildIndex(u16 *indices, int &count) const {
		for (int patch_u = 0; patch_u < num_patches_u; ++patch_u) {
			for (int patch_v = 0; patch_v < num_patches_v; ++patch_v) {
				int patch_index = patch_v * num_patches_u + patch_u;
				int total = patch_index * (tess_u + 1) * (tess_v + 1);
				::BuildIndex(indices + count, count, tess_u, tess_v, primType, total);
			}
		}
	}
};

struct SplinePatchLocal {
	int tess_u;
	int tess_v;
	int count_u;
	int count_v;
	int type_u;
	int type_v;
	int num_patches_u;
	int num_patches_v;
	int num_divisions_u;
	int num_divisions_v;
	bool patchFacing;
	GEPatchPrimType primType;

	void Init(int maxVertices) {
		switch (g_Config.iSplineBezierQuality) {
		case LOW_QUALITY:
			tess_u = 2;
			tess_v = 2;
			break;
		case MEDIUM_QUALITY:
			// Don't cut below 2, though.
			if (tess_u > 2) tess_u = HALF_CEIL(tess_u);
			if (tess_v > 2) tess_v = HALF_CEIL(tess_v);
			// Pass through
		case HIGH_QUALITY:
			// Downsample until it fits, in case crazy tessellation factors are sent.
			while ((num_patches_u * tess_u + 1) * (num_patches_v * tess_v + 1) > maxVertices) {
				tess_u--;
				tess_v--;
			}
			break;
		}

		num_divisions_u = num_patches_u * tess_u;
		num_divisions_v = num_patches_v * tess_v;
	}

	int GetTessU(int patch_u) const { return (patch_u == num_patches_u - 1) ? tess_u + 1 : tess_u; }
	int GetTessV(int patch_v) const { return (patch_v == num_patches_v - 1) ? tess_v + 1 : tess_v; }

	int GetPointIndex(int patch_u, int patch_v) const { return patch_v * count_u + patch_u;}

	int GetIndexU(int patch_u, int tile_u) const { return patch_u * tess_u + tile_u; }
	int GetIndexV(int patch_v, int tile_v) const { return patch_v * tess_v + tile_v; }

	int GetIndex(int index_u, int index_v, int patch_u, int patch_v) const {
		return index_v * (num_divisions_u + 1) + index_u;
	}

	void BuildIndex(u16 *indices, int &count) const {
		::BuildIndex(indices, count, num_divisions_u, num_divisions_v, primType);
	}
};

struct Weight {
	float basis[4], deriv[4];
};

template<class T>
class WeightCache : public T {
private:
	std::unordered_map<u32, Weight*> weightsCache;
public:
	Weight* operator [] (u32 key) {
		Weight *&weights = weightsCache[key];
		if (!weights)
			weights = CalcWeightsAll(key);
		return weights;
	}

	void Clear() {
		for (auto it : weightsCache)
			delete[] it.second;
		weightsCache.clear();
	}
};

struct Weight2D {
	const Weight *u, *v;
	int size_u, size_v;

	template<class T>
	Weight2D(WeightCache<T> &cache, u32 key_u, u32 key_v) {
		u = cache[key_u];
		v = (key_u != key_v) ? cache[key_v] : u; // Use same weights if u == v
	}
};

bool CanUseHardwareTessellation(GEPatchPrimType prim);
void TessellateSplinePatch(u8 *&dest, u16 *indices, int &count, SplinePatchLocal &spatch, u32 origVertType, int maxVertices);
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
