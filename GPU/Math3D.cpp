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

#include "Math3D.h"

template<>
float Vec2Ref<float>::Length() const
{
	return sqrtf(Length2());
}

template<>
void Vec2Ref<float>::SetLength(const float l)
{
	(*this) *= l / Length();
}

template<>
float Vec2Ref<float>::Normalize() //returns the previous length, is often useful
{
	float len = Length();
	(*this) /= len;
	return len;
}

template<>
Vec3<float> Vec3<float>::FromRGB(unsigned int rgb)
{
	return Vec3((rgb & 0xFF) * (1.0f/255.0f),
				((rgb >> 8) & 0xFF) * (1.0f/255.0f),
				((rgb >> 16) & 0xFF) * (1.0f/255.0f));
}

template<>
Vec3<int> Vec3<int>::FromRGB(unsigned int rgb)
{
	return Vec3(rgb & 0xFF,
				(rgb >> 8) & 0xFF,
				(rgb >> 16) & 0xFF);
}

template<>
unsigned int Vec3Ref<float>::ToRGB() const
{
	return ((unsigned int)(r*255.f)) +
			((unsigned int)(g*255.f*256.f)) +
			((unsigned int)(b*255.f*256.f*256.f));
}

template<>
unsigned int Vec3Ref<int>::ToRGB() const
{
	return (r&0xFF) | ((g&0xFF)<<8) | ((b&0xFF)<<16);
}

template<>
float Vec3Ref<float>::Length() const
{
	return sqrtf(Length2());
}

template<>
void Vec3Ref<float>::SetLength(const float l)
{
	(*this) *= l / Length();
}

template<>
float Vec3Ref<float>::Normalize() //returns the previous length, is often useful
{
	float len = Length();
	(*this) /= len;
	return len;
}

template<>
Vec4<float> Vec4<float>::FromRGBA(unsigned int rgba)
{
	return Vec4((rgba & 0xFF) * (1.0f/255.0f),
				((rgba >> 8) & 0xFF) * (1.0f/255.0f),
				((rgba >> 16) & 0xFF) * (1.0f/255.0f),
				((rgba >> 24) & 0xFF) * (1.0f/255.0f));
}

template<>
Vec4<int> Vec4<int>::FromRGBA(unsigned int rgba)
{
	return Vec4(rgba & 0xFF,
				(rgba >> 8) & 0xFF,
				(rgba >> 16) & 0xFF,
				(rgba >> 24) & 0xFF);
}

template<>
unsigned int Vec4Ref<float>::ToRGBA() const
{
	return ((unsigned int)(r*255.f)) +
			((unsigned int)(g*255.f*256.f)) +
			((unsigned int)(b*255.f*256.f*256.f)) +
			((unsigned int)(a*255.f*256.f*256.f*256.f));
}

template<>
unsigned int Vec4Ref<int>::ToRGBA() const
{
	return (r&0xFF) | ((g&0xFF)<<8) | ((b&0xFF)<<16) | ((a&0xFF)<<24);
}

template<>
float Vec4Ref<float>::Length() const
{
	return sqrtf(Length2());
}

template<>
void Vec4Ref<float>::SetLength(const float l)
{
	(*this) *= l / Length();
}

template<>
float Vec4Ref<float>::Normalize() //returns the previous length, is often useful
{
	float len = Length();
	(*this) /= len;
	return len;
}
