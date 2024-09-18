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
// 8 + 4 + 12 + 12 = 36 bytes
struct SimpleVertex {
	float uv[2];
	union {
		u8 color[4];
		u32_le color_32;
	};
	Vec3Packedf nrm;
	Vec3Packedf pos;
};

class SimpleBufferManager;

namespace Spline {

void BuildIndex(u16 *indices, int &count, int num_u, int num_v, GEPatchPrimType prim_type, int total = 0);

enum SplineQuality {
	LOW_QUALITY = 0,
	MEDIUM_QUALITY = 1,
	HIGH_QUALITY = 2,
};

class Bezier3DWeight;
class Spline3DWeight;

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.

struct SurfaceInfo {
	int tess_u, tess_v;
	int num_points_u, num_points_v;
	int num_patches_u, num_patches_v;
	int type_u, type_v;
	GEPatchPrimType primType;
	bool patchFacing;

	void BaseInit() {
		// If specified as 0, uses 1.
		if (tess_u < 1) tess_u = 1;
		if (tess_v < 1) tess_v = 1;

		switch (g_Config.iSplineBezierQuality) {
		case LOW_QUALITY:
			tess_u = 2;
			tess_v = 2;
			break;
		case MEDIUM_QUALITY:
			// Don't cut below 2, though.
			if (tess_u > 2) tess_u = HALF_CEIL(tess_u);
			if (tess_v > 2) tess_v = HALF_CEIL(tess_v);
			break;
		}
	}
};

struct BezierSurface : public SurfaceInfo {
	using WeightType = Bezier3DWeight;

	int num_verts_per_patch;

	void Init(int maxVertices) {
		SurfaceInfo::BaseInit();
		// Downsample until it fits, in case crazy tessellation factors are sent.
		while ((tess_u + 1) * (tess_v + 1) * num_patches_u * num_patches_v > maxVertices) {
			tess_u--;
			tess_v--;
		}
		num_verts_per_patch = (tess_u + 1) * (tess_v + 1);
	}

	int GetTessStart(int patch) const { return 0; }

	int GetPointIndex(int patch_u, int patch_v) const { return patch_v * 3 * num_points_u + patch_u * 3; }

	int GetIndexU(int patch_u, int tile_u) const { return tile_u; }
	int GetIndexV(int patch_v, int tile_v) const { return tile_v; }

	int GetIndex(int index_u, int index_v, int patch_u, int patch_v) const {
		int patch_index = patch_v * num_patches_u + patch_u;
		return index_v * (tess_u + 1) + index_u + num_verts_per_patch * patch_index;
	}

	void BuildIndex(u16 *indices, int &count) const {
		for (int patch_u = 0; patch_u < num_patches_u; ++patch_u) {
			for (int patch_v = 0; patch_v < num_patches_v; ++patch_v) {
				int patch_index = patch_v * num_patches_u + patch_u;
				int total = patch_index * num_verts_per_patch;
				Spline::BuildIndex(indices + count, count, tess_u, tess_v, primType, total);
			}
		}
	}
};

struct SplineSurface : public SurfaceInfo {
	using WeightType = Spline3DWeight;

	int num_vertices_u;

	void Init(int maxVertices) {
		SurfaceInfo::BaseInit();
		// Downsample until it fits, in case crazy tessellation factors are sent.
		while ((num_patches_u * tess_u + 1) * (num_patches_v * tess_v + 1) > maxVertices) {
			tess_u--;
			tess_v--;
		}
		num_vertices_u = num_patches_u * tess_u + 1;
	}

	int GetTessStart(int patch) const { return (patch == 0) ? 0 : 1; }

	int GetPointIndex(int patch_u, int patch_v) const { return patch_v * num_points_u + patch_u; }

	int GetIndexU(int patch_u, int tile_u) const { return patch_u * tess_u + tile_u; }
	int GetIndexV(int patch_v, int tile_v) const { return patch_v * tess_v + tile_v; }

	int GetIndex(int index_u, int index_v, int patch_u, int patch_v) const {
		return index_v * num_vertices_u + index_u;
	}

	void BuildIndex(u16 *indices, int &count) const {
		Spline::BuildIndex(indices, count, num_patches_u * tess_u, num_patches_v * tess_v, primType);
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
			weights = T::CalcWeightsAll(key);
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

struct ControlPoints {
	Vec3f *pos = nullptr;
	Vec2f *tex = nullptr;
	Vec4f *col = nullptr;
	u32_le defcolor;

	ControlPoints() {}
	ControlPoints(const SimpleVertex *const *points, int size, SimpleBufferManager &managedBuf);
	void Convert(const SimpleVertex *const *points, int size);
	bool IsValid() const {
		return pos && tex && col;
	}
};

struct OutputBuffers {
	SimpleVertex *vertices;
	u16 *indices;
	int count;
};

template<class Surface>
void SoftwareTessellation(OutputBuffers &output, const Surface &surface, u32 origVertType, const ControlPoints &points);

} // namespace Spline

// Define function object for TemplateParameterDispatcher
#define TEMPLATE_PARAMETER_DISPATCHER_FUNCTION(NAME, FUNCNAME, FUNCTYPE) \
struct NAME { \
	template<bool ...Params> \
	static FUNCTYPE GetFunc() { \
		return &FUNCNAME<Params...>; \
	} \
};

template<typename Func, int NumParams, class Dispatcher> 
class TemplateParameterDispatcher {

	/* Store all combinations of template functions into an array */
	template<int LoopCount, int Index = 0, bool ...Params>
	struct Initializer {
		static void Init(Func funcs[]) {
			Initializer<LoopCount - 1, (Index << 1) + 1, true, Params...>::Init(funcs); // true
			Initializer<LoopCount - 1, (Index << 1) + 0, false, Params...>::Init(funcs); // false
		}
	};
 	/* Specialized for terminates the recursive loop */
	template<int Index, bool ...Params>
	struct Initializer<0, Index, Params...> {
		static void Init(Func funcs[]) {
			funcs[Index] = Dispatcher::template GetFunc<Params...>(); // Resolve the nested dependent name as template function.
		}
	};

private: 
	Func funcs[1 << NumParams]; /* Function pointers array */ 
public: 
	TemplateParameterDispatcher() { 
		Initializer<NumParams>::Init(funcs); 
	} 
 
	Func GetFunc(const bool params[]) const { 
 		/* Convert bool parameters to index of the array */ 
		int index = 0; 
		for (int i = 0; i < NumParams; ++i) 
			index |= params[i] << i; 
 
		return funcs[index]; 
	} 
};
