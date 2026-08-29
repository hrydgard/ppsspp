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

#include "GPU/Math3D.h"
#include "Common/Common.h"
#include "Common/Math/SIMDHeaders.h"

#if PPSSPP_ARCH(SSE2)
// For the SSE4 stuff.
#include <smmintrin.h>
#endif

namespace Math3D {

// NOTE: The float Length/Normalize functions below are deliberately scalar on every platform.
// They used to have SSE and NEON specializations, but the three summed the components in three
// different orders (and the SSE normalize used _mm_rsqrt_ps with no Newton step, roughly 12 bits),
// so the same vertex normal came out different per architecture, per build, and - since the SSE4
// variant was picked from cpu_info at runtime - per CPU within one binary. That fed environment-map
// texture coordinates and specular, i.e. actual pixels. sqrtf and division are correctly rounded by
// IEEE-754, so a single scalar version is bit-identical everywhere; the horizontal sum these were
// doing was never much faster than the three multiplies it replaced.
template<>
float Vec2<float>::Length() const
{
	return sqrtf(Length2());
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
float Vec2<float>::Distance2To(const Vec2<float> &other) const {
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
	return sqrtf(Length2());
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
float Vec3<float>::Distance2To(const Vec3<float> &other) const {
	return Vec3<float>(other-(*this)).Length2();
}

// The useSSE4 parameter is kept for source compatibility with the call sites; there is no longer a
// separate path for it to select. See the note above - a runtime-selected variant was part of the bug.
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
	return (*this) / len;
}

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
float Vec3Packed<float>::Distance2To(const Vec3Packed<float> &other) const {
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
	return sqrtf(Length2());
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
float Vec4<float>::Distance2To(const Vec4<float> &other) const {
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
