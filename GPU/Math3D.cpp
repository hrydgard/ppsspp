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

#include "Common/Common.h"
#include "GPU/Math3D.h"

namespace Math3D {

template<>
float Vec2<float>::Length() const
{
	// Doubt this is worth it for a vec2 :/
#if defined(_M_SSE)
	float ret;
	__m128 xy = _mm_loadu_ps(&x);
	__m128 sq = _mm_mul_ps(xy, xy);
	const __m128 r2 = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(0, 0, 0, 1));
	const __m128 res = _mm_add_ss(sq, r2);
	_mm_store_ss(&ret, _mm_sqrt_ss(res));
	return ret;
#else
	return sqrtf(Length2());
#endif
}

template<>
void Vec2<float>::SetLength(const float l)
{
	(*this) *= l / Length();
}

template<>
Vec2<float> Vec2<float>::WithLength(const float l) const
{
	return (*this) * l / Length();
}

template<>
float Vec2<float>::Distance2To(Vec2<float> &other)
{
	return Vec2<float>(other-(*this)).Length2();
}

template<>
Vec2<float> Vec2<float>::Normalized() const
{
	return (*this) / Length();
}

template<>
float Vec2<float>::Normalize()
{
	float len = Length();
	(*this) = (*this)/len;
	return len;
}

template<>
float Vec3<float>::Length() const
{
#if defined(_M_SSE)
	float ret;
	__m128 xyz = _mm_loadu_ps(&x);
	__m128 sq = _mm_mul_ps(xyz, xyz);
	const __m128 r2 = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(0, 0, 0, 1));
	const __m128 r3 = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(0, 0, 0, 2));
	const __m128 res = _mm_add_ss(sq, _mm_add_ss(r2, r3));
	_mm_store_ss(&ret, _mm_sqrt_ss(res));
	return ret;
#else
	return sqrtf(Length2());
#endif
}

template<>
void Vec3<float>::SetLength(const float l)
{
	(*this) *= l / Length();
}

template<>
Vec3<float> Vec3<float>::WithLength(const float l) const
{
	return (*this) * l / Length();
}

template<>
float Vec3<float>::Distance2To(Vec3<float> &other)
{
	return Vec3<float>(other-(*this)).Length2();
}

#if defined(_M_SSE)
__m128 SSENormalizeMultiplierSSE2(__m128 v)
{
	const __m128 sq = _mm_mul_ps(v, v);
	const __m128 r2 = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(0, 0, 0, 1));
	const __m128 r3 = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(0, 0, 0, 2));
	const __m128 res = _mm_add_ss(r3, _mm_add_ss(r2, sq));

	const __m128 rt = _mm_rsqrt_ss(res);
	return _mm_shuffle_ps(rt, rt, _MM_SHUFFLE(0, 0, 0, 0));
}

#if defined(__GNUC__) || defined(__clang__) || defined(__INTEL_COMPILER)
[[gnu::target("sse4.1")]]
#endif
__m128 SSENormalizeMultiplierSSE4(__m128 v)
{
	return _mm_rsqrt_ps(_mm_dp_ps(v, v, 0xFF));
}

__m128 SSENormalizeMultiplier(bool useSSE4, __m128 v)
{
	if (useSSE4)
		return SSENormalizeMultiplierSSE4(v);
	return SSENormalizeMultiplierSSE2(v);
}

template<>
Vec3<float> Vec3<float>::Normalized(bool useSSE4) const
{
	const __m128 normalize = SSENormalizeMultiplier(useSSE4, vec);
	return _mm_mul_ps(normalize, vec);
}

template<>
Vec3<float> Vec3<float>::NormalizedOr001(bool useSSE4) const {
	const __m128 normalize = SSENormalizeMultiplier(useSSE4, vec);
	const __m128 result = _mm_mul_ps(normalize, vec);
	const __m128 mask = _mm_cmpunord_ps(result, vec);
	const __m128 replace = _mm_and_ps(_mm_set_ps(0.0f, 1.0f, 0.0f, 0.0f), mask);
	// Replace with the constant if the mask matched.
	return _mm_or_ps(_mm_andnot_ps(mask, result), replace);
}
#else
template<>
Vec3<float> Vec3<float>::Normalized(bool useSSE4) const
{
	return (*this) / Length();
}

template<>
Vec3<float> Vec3<float>::NormalizedOr001(bool useSSE4) const {
	float len = Length();
	if (len == 0.0f) {
		return Vec3<float>(0.0f, 0.0f, 1.0f);
	}
	return *this / len;
}
#endif

template<>
float Vec3<float>::Normalize()
{
	float len = Length();
	(*this) = (*this)/len;
	return len;
}

template<>
float Vec3<float>::NormalizeOr001() {
	float len = Length();
	if (len == 0.0f) {
		z = 1.0f;
	} else {
		*this /= len;
	}
	return len;
}

template<>
Vec3Packed<float> Vec3Packed<float>::FromRGB(unsigned int rgb)
{
	return Vec3Packed((rgb & 0xFF) * (1.0f/255.0f),
				((rgb >> 8) & 0xFF) * (1.0f/255.0f),
				((rgb >> 16) & 0xFF) * (1.0f/255.0f));
}

template<>
Vec3Packed<int> Vec3Packed<int>::FromRGB(unsigned int rgb)
{
	return Vec3Packed(rgb & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF);
}

template<>
unsigned int Vec3Packed<float>::ToRGB() const
{
	return ((unsigned int)(r()*255.f)) +
			((unsigned int)(g()*255.f*256.f)) +
			((unsigned int)(b()*255.f*256.f*256.f));
}

template<>
unsigned int Vec3Packed<int>::ToRGB() const
{
	return (r()&0xFF) | ((g()&0xFF)<<8) | ((b()&0xFF)<<16);
}

template<>
float Vec3Packed<float>::Length() const
{
	return sqrtf(Length2());
}

template<>
void Vec3Packed<float>::SetLength(const float l)
{
	(*this) *= l / Length();
}

template<>
Vec3Packed<float> Vec3Packed<float>::WithLength(const float l) const
{
	return (*this) * l / Length();
}

template<>
float Vec3Packed<float>::Distance2To(Vec3Packed<float> &other)
{
	return Vec3Packed<float>(other-(*this)).Length2();
}

template<>
Vec3Packed<float> Vec3Packed<float>::Normalized() const
{
	return (*this) / Length();
}

template<>
float Vec3Packed<float>::Normalize()
{
	float len = Length();
	(*this) = (*this)/len;
	return len;
}

template<>
float Vec4<float>::Length() const
{
#if defined(_M_SSE)
	float ret;
	__m128 xyzw = _mm_loadu_ps(&x);
	__m128 sq = _mm_mul_ps(xyzw, xyzw);
	const __m128 r2 = _mm_add_ps(sq, _mm_movehl_ps(sq, sq));
	const __m128 res = _mm_add_ss(r2, _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(0, 0, 0, 1)));
	_mm_store_ss(&ret, _mm_sqrt_ss(res));
	return ret;
#else
	return sqrtf(Length2());
#endif
}

template<>
void Vec4<float>::SetLength(const float l)
{
	(*this) *= l / Length();
}

template<>
Vec4<float> Vec4<float>::WithLength(const float l) const
{
	return (*this) * l / Length();
}

template<>
float Vec4<float>::Distance2To(Vec4<float> &other)
{
	return Vec4<float>(other-(*this)).Length2();
}

template<>
Vec4<float> Vec4<float>::Normalized() const
{
	return (*this) / Length();
}

template<>
float Vec4<float>::Normalize()
{
	float len = Length();
	(*this) = (*this)/len;
	return len;
}

}; // namespace Math3D
