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
Vec3<float> Vec3<float>::FromRGB(unsigned int rgb)
{
	return Vec3((rgb & 0xFF) * (1.0f/255.0f),
				((rgb >> 8) & 0xFF) * (1.0f/255.0f),
				((rgb >> 16) & 0xFF) * (1.0f/255.0f));
}

template<>
Vec3<int> Vec3<int>::FromRGB(unsigned int rgb)
{
	return Vec3(rgb & 0xFF, (rgb >> 8) & 0xFF, (rgb >> 16) & 0xFF);
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
float Vec3<float>::Distance2To(Vec3<float> &other)
{
	return Vec3<float>(other-(*this)).Length2();
}

template<>
Vec3<float> Vec3<float>::Normalized() const
{
	return (*this) / Length();
}

template<>
float Vec3<float>::Normalize()
{
	float len = Length();
	(*this) = (*this)/len;
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
	return Vec4(rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 24) & 0xFF);
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
