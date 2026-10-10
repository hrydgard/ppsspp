// Copyright (c) 2021- PPSSPP Project.

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

#include "ppsspp_config.h"

#include "Common/Common.h"
#include "Common/Math/SIMDHeaders.h"

#if PPSSPP_ARCH(ARM64_NEON)
#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

#include "GPU/Math3D.h"

namespace Rasterizer {

// How the pixel and sampler functions take vectors: in registers where the platform can.
#if PPSSPP_ARCH(ARM64_NEON)
typedef int32x4_t Vec4IntArg;
typedef int32x4_t Vec4IntResult;
typedef float32x4_t Vec4FloatArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return vld1q_s32(a.AsArray()); }
static inline Vec4IntArg ToVec4IntArg(const Vec4IntResult &a) { return a; }
static inline Vec4IntResult ToVec4IntResult(const Math3D::Vec4<int> &a) { return vld1q_s32(a.AsArray()); }
static inline Vec4FloatArg ToVec4FloatArg(const Math3D::Vec4<float> &a) { return vld1q_f32(a.AsArray()); }
#elif PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
typedef __m128i Vec4IntArg;
typedef __m128i Vec4IntResult;
typedef __m128 Vec4FloatArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return a.ivec; }
static inline Vec4IntArg ToVec4IntArg(const Vec4IntResult &a) { return a; }
static inline Vec4IntResult ToVec4IntResult(const Math3D::Vec4<int> &a) { return a.ivec; }
static inline Vec4FloatArg ToVec4FloatArg(const Math3D::Vec4<float> &a) { return a.vec; }
#else
typedef const Math3D::Vec4<int> &Vec4IntArg;
typedef Math3D::Vec4<int> Vec4IntResult;
typedef const Math3D::Vec4<float> &Vec4FloatArg;
static inline Vec4IntArg ToVec4IntArg(const Math3D::Vec4<int> &a) { return a; }
static inline Vec4IntResult ToVec4IntResult(const Math3D::Vec4<int> &a) { return a; }
static inline Vec4FloatArg ToVec4FloatArg(const Math3D::Vec4<float> &a) { return a; }
#endif

#if PPSSPP_ARCH(AMD64) && PPSSPP_PLATFORM(WINDOWS) && (defined(_MSC_VER) || defined(__clang__) || defined(__INTEL_COMPILER))
#define SOFTRAST_CALL __vectorcall
#else
#define SOFTRAST_CALL
#endif

};  // namespace Rasterizer
