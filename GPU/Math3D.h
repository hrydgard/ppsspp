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

class Vec3;

// Like a Vec3, but acts on references
class Vec3Ref
{
public:
	union
	{
		struct
		{
			float& x;
			float& y;
			float& z;
		};
		struct
		{
			float& r;
			float& g;
			float& b;
		};
		struct
		{
			float& u;
			float& v;
			float& w;
		};
	};

	float* AsArray() { return &x; }

	Vec3Ref(float& _x, float& _y, float& _z) : x(_x), y(_y), z(_z) {}
	Vec3Ref(float a[3]) : x(a[0]), y(a[1]), z(a[2]) {}

	void Write(float a[3])
	{
		a[0] = x; a[1] = y; a[2] = z;
	}

	// operators acting on "this"
	void operator = (const Vec3Ref& other)
	{
		x = other.x;
		y = other.y;
		z = other.z;
	}
	void operator += (const Vec3Ref &other)
	{
		x+=other.x; y+=other.y; z+=other.z;
	}
	void operator -= (const Vec3Ref &other)
	{
		x-=other.x; y-=other.y; z-=other.z;
	}
	void operator *= (const float f)
	{
		x*=f; y*=f; z*=f;
	}
	void operator /= (const float f)
	{
		x/=f; y/=f; z/=f;
	}

	// operators which require creating a new Vec3 object
	Vec3 operator +(const Vec3Ref &other) const;
	Vec3 operator -(const Vec3Ref &other) const;
	Vec3 operator -() const;
	Vec3 Mul(const Vec3Ref &other) const;
	Vec3 operator * (const float f) const;
	Vec3 operator / (const float f) const;

	// methods which don't create new Vec3 objects
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
	float Normalize() //returns the previous length, is often useful
	{
		float len = Length();
		(*this) /= len;
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
	void SetZero()
	{
		x=0.f;y=0.f;z=0.f;
	}

	// methods that create a new Vec3 object
	Vec3 WithLength(const float l) const;
	Vec3 Normalized() const;
	Vec3 Lerp(const Vec3Ref &other, const float t) const;
	float Distance2To(Vec3Ref &other) const;
};

class Vec3 : public Vec3Ref
{
public:
	float x, y, z;

	Vec3() : Vec3Ref(x, y, z) {}
	Vec3(float _x, float _y, float _z) : Vec3Ref(x, y, z), x(_x), y(_y), z(_z) {}
	Vec3(const float a[3]) : Vec3Ref(x, y, z), x(a[0]), y(a[1]), z(a[2]) {}
	Vec3(const Vec3Ref& other) : Vec3Ref(x, y, z), x(other.x), y(other.y), z(other.z) {}

	static Vec3 FromRGB(unsigned int rgb)
	{
		return Vec3((rgb & 0xFF) * (1.0f/255.0f),
					((rgb >> 8) & 0xFF) * (1.0f/255.0f),
					((rgb >> 16) & 0xFF) * (1.0f/255.0f));
	}

	static Vec3 AssignToAll(float f)
	{
		return Vec3(f, f, f);
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

inline float Dot(const Vec3Ref &a, const Vec3Ref& b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

inline Vec3 Cross(const Vec3Ref &a, const Vec3Ref& b)
{
	return Vec3(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
