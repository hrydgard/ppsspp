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

template<typename T>
class Vec3
{
public:
	struct
	{
		T x,y,z;
	};

	T* AsArray() { return &x; }

	Vec3() {}
	Vec3(const T a[3]) : x(a[0]), y(a[1]), z(a[2]) {}
	Vec3(const T& _x, const T& _y, const T& _z) : x(_x), y(_y), z(_z) {}

	// Only implemented for T=int and T=float
	static Vec3 FromRGB(unsigned int rgb);

	static Vec3 AssignToAll(const T& f)
	{
		return Vec3<T>(f, f, f);
	}

	void Write(T a[3])
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
	Vec3 Mul(const Vec3 &other) const
	{
		return Vec3(x*other.x, y*other.y, z*other.z);
	}
	template<typename V>
	Vec3 operator * (const V& f) const
	{
		return Vec3(x*f,y*f,z*f);
	}
	template<typename V>
	void operator *= (const V& f)
	{
		x*=f; y*=f; z*=f;
	}
	template<typename V>
	Vec3 operator / (const V& f) const
	{
		return Vec3(x/f,y/f,z/f);
	}
	template<typename V>
	void operator /= (const V& f)
	{
		*this = *this / f;
	}

	T Length2() const
	{
		return x*x + y*y + z*z;
	}

	// Only implemented for T=float
	float Length() const;
	void SetLength(const float l);
	Vec3 WithLength(const float l) const;
	float Distance2To(Vec3 &other);
	Vec3 Normalized() const;
	float Normalize(); // returns the previous length, which is often useful

	T& operator [] (int i) //allow vector[2] = 3   (vector.z=3)
	{
		return *((&x) + i);
	}
	T operator [] (const int i) const
	{
		return *((&x) + i);
	}

	Vec3 Lerp(const Vec3 &other, const float t) const
	{
		return (*this)*(1-t) + other*t;
	}

	void SetZero()
	{
		x=0; y=0; z=0;
	}

	// Common aliases: UVW (texel coordinates), RGB (colors), STQ (texture coordinates)
	T& u() { return x; }
	T& v() { return y; }
	T& w() { return z; }

	T& r() { return x; }
	T& g() { return y; }
	T& b() { return z; }

	T& s() { return x; }
	T& t() { return y; }
	T& q() { return z; }

	const T& u() const { return x; }
	const T& v() const { return y; }
	const T& w() const { return z; }

	const T& r() const { return x; }
	const T& g() const { return y; }
	const T& b() const { return z; }

	const T& s() const { return x; }
	const T& t() const { return y; }
	const T& q() const { return z; }
};

template<typename T, typename V>
Vec3<T> operator * (const V& f, const Vec3<T>& vec)
{
	return Vec3<T>(f*vec.x,f*vec.y,f*vec.z);
}

typedef Vec3<float> Vec3f;

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

template<typename T>
inline T Dot(const Vec3<T>& a, const Vec3<T>& b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

template<typename T>
inline Vec3<T> Cross(const Vec3<T>& a, const Vec3<T>& b)
{
	return Vec3<T>(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
