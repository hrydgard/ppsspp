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

#pragma once

#include <cmath>

class Vec3
{
public:
	union
	{
		float v[3];
		struct
		{
			float x,y,z;
		};
	};
	Vec3(unsigned int rgb) { x=(rgb&0xFF)/255.0f; y=((rgb>>8)&0xFF)/255.0f; z=((rgb>>16)&0xFF)/255.0f;}
	Vec3(const float a[3]) {
		v[0]=a[0];
		v[1]=a[1];
		v[2]=a[2];
	}
	Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
	Vec3() {}
	explicit Vec3(float f) : x(f), y(f), z(f) {}
	void Write(float a[3])
	{
		a[0] = x; a[1] = y; a[2] = z;
	}
	Vec3 operator +(const Vec3 &other) const
	{
		return Vec3(x+other.x, y+other.y, z+other.z);
	}
	void operator += (const Vec3 &other)
	{
		x+=other.x; y+=other.y; z+=other.z;
	}
	Vec3 operator -(const Vec3 &other) const
	{
		return Vec3(x-other.x, y-other.y, z-other.z);
	}
	void operator -= (const Vec3 &other)
	{
		x-=other.x; y-=other.y; z-=other.z;
	}
	Vec3 operator -() const
	{
		return Vec3(-x,-y,-z);
	}
	float operator *(const Vec3 &other) const
	{
		return x*other.x + y*other.y + z*other.z;
	}
	Vec3 Mul(const Vec3 &other) const
	{
		return Vec3(x*other.x, y*other.y, z*other.z);
	}
	Vec3 operator * (const float f) const
	{
		return Vec3(x*f,y*f,z*f);
	}
	void operator *= (const float f)
	{
		x*=f; y*=f; z*=f;
	}
	Vec3 operator / (const float f) const
	{
		float invf = (1.0f/f);
		return Vec3(x*invf,y*invf,z*invf);
	}
	void operator /= (const float f)
	{
		*this = *this / f;
	}
	Vec3 operator %(const Vec3 &v) const
	{
		return Vec3(y*v.z-z*v.y, z*v.x-x*v.z, x*v.y-y*v.x);
	}

	float Length2() const
	{
		return x*x + y*y + z*z;
	}
	float Length() const
	{
		return sqrtf(Length2());
	}
	void SetLength(const float l)
	{
		(*this) *= l / Length();
	}
	Vec3 WithLength(const float l) const
	{
		return (*this) * l / Length();
	}
	float Distance2To(Vec3 &other)
	{
		return Vec3(other-(*this)).Length2();
	}
	Vec3 Normalized() const {
		return (*this) / Length();
	}
	float Normalize() { //returns the previous length, is often useful
		float len = Length();
		(*this) = (*this)/len;
		return len;
	}
	float &operator [] (int i) //allow vector[2] = 3   (vector.z=3)
	{
		return *((&x) + i);
	}
	float operator [] (const int i) const
	{
		return *((&x) + i);
	}
	Vec3 Lerp(const Vec3 &other, const float t) const
	{
		return (*this)*(1-t) + other*t;
	}
	void SetZero()
	{
		x=0;y=0;z=0;
	}
};

inline void Vec3ByMatrix43(float vecOut[3], const float v[3], const float m[12])
{
	vecOut[0] = v[0] * m[0] + v[1] * m[3] + v[2] * m[6] + m[9];
	vecOut[1] = v[0] * m[1] + v[1] * m[4] + v[2] * m[7] + m[10];
	vecOut[2] = v[0] * m[2] + v[1] * m[5] + v[2] * m[8] + m[11];
}

inline void Norm3ByMatrix43(float vecOut[3], const float v[3], const float m[12])
{
	vecOut[0] = v[0] * m[0] + v[1] * m[3] + v[2] * m[6];
	vecOut[1] = v[0] * m[1] + v[1] * m[4] + v[2] * m[7];
	vecOut[2] = v[0] * m[2] + v[1] * m[5] + v[2] * m[8];
}

inline float Vec3Dot(const float v1[3], const float v2[3])
{
	return v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2];
}
