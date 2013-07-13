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


Vec3 Vec3Ref::operator +(const Vec3Ref &other) const
{
	return Vec3(x+other.x, y+other.y, z+other.z);
}

Vec3 Vec3Ref::operator -(const Vec3Ref &other) const
{
	return Vec3(x-other.x, y-other.y, z-other.z);
}

Vec3 Vec3Ref::operator -() const
{
	return Vec3(-x,-y,-z);
}

Vec3 Vec3Ref::Mul(const Vec3Ref &other) const
{
	return Vec3(x*other.x, y*other.y, z*other.z);
}

Vec3 Vec3Ref::operator * (const float f) const
{
	return Vec3(x*f,y*f,z*f);
}

Vec3 Vec3Ref::operator / (const float f) const
{
	float invf = (1.0f/f);
	return Vec3(x*invf,y*invf,z*invf);
}

Vec3 Vec3Ref::WithLength(const float l) const
{
	return (*this) * l / Length();
}

Vec3 Vec3Ref::Normalized() const
{
	return (*this) / Length();
}

Vec3 Vec3Ref::Lerp(const Vec3Ref &other, const float t) const
{
	return (*this)*(1-t) + other*t;
}

float Vec3Ref::Distance2To(Vec3Ref &other) const
{
	return (other-(*this)).Length2();
}
