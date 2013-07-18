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

/**
 * Vec2 - two dimensional vector with arbitrary base type
 */
template<typename X, typename Y=X>
class Vec2;

// Like a Vec2, but acts on references
template<typename X, typename Y=X>
class Vec2Ref
{
public:
	union
	{
		struct
		{
			X& x;
			Y& y;
		};
		struct
		{
			X& u;
			Y& v;
		};
		struct
		{
			X& s;
			Y& t;
		};
	};

	X* AsArray() { return &x; }

	Vec2Ref(X& _x, Y& _y) : x(_x), y(_y) {}

	Vec2Ref(X a[2]) : x(a[0]), y(a[1]) {}

	template<typename X2, typename Y2>
	Vec2<X2,Y2> Cast() const
	{
		return Vec2<X2,Y2>((X2)x, (Y2)y);
	}

	void Write(X a[2])
	{
		a[0] = x; a[1] = y;
	}

	// operators acting on "this"
	void operator = (const Vec2Ref& other)
	{
		x = other.x;
		y = other.y;
	}
	void operator += (const Vec2Ref &other)
	{
		x+=other.x; y+=other.y;
	}
	void operator -= (const Vec2Ref &other)
	{
		x-=other.x; y-=other.y;
	}

	void operator *= (const X& f)
	{
		x*=f; y*=f;
	}
	void operator /= (const X& f)
	{
		x/=f; y/=f;
	}

	// operators which require creating a new Vec2 object
	Vec2<X,Y> operator +(const Vec2Ref &other) const;
	Vec2<X,Y> operator -(const Vec2Ref &other) const;
	Vec2<X,Y> operator -() const;
	Vec2<X,Y> operator *(const Vec2Ref &other) const;
	Vec2<X,Y> operator * (const X& f) const;
	Vec2<X,Y> operator / (const X& f) const;

	// methods which don't create new Vec2 objects
	float Length2() const
	{
		return x*x + y*y;
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
		return (i==0) ? x : y;
	}
	float operator [] (const int i) const
	{
		return (i==0) ? x : y;
	}
	void SetZero()
	{
		x=(X)0; y=(Y)0;
	}

	// methods that create a new Vec2 object
	Vec2<X,Y> WithLength(const X& l) const;
	Vec2<X,Y> Normalized() const;
	Vec2<X,Y> Lerp(const Vec2Ref &other, const float t) const;
	float Distance2To(const Vec2Ref &other) const;

	// swizzlers - create a subvector of references to specific components
	Vec2Ref<Y,X> yx() { return Vec2Ref(y, x); }
	Vec2Ref<Y,X> vu() { return Vec2Ref(v, u); }
	const Vec2Ref<Y,X> yx() const { return Vec2Ref(y, x); }
	const Vec2Ref<Y,X> vu() const { return Vec2Ref(v, u); }
};

template<typename X, typename Y>
class Vec2 : public Vec2Ref<X,Y>
{
public:
	X x;
	Y y;

	Vec2() : Vec2Ref<X,Y>(x, y) {}
	Vec2(const X& _x, const Y& _y) : Vec2Ref<X,Y>(x, y), x(_x), y(_y) {}
	Vec2(const X a[2]) : Vec2Ref<X,Y>(x, y), x(a[0]), y(a[1]) {}
	Vec2(const Vec2Ref<X,Y>& other) : Vec2Ref<X,Y>(x, y), x(other.x), y(other.y) {}

	static Vec2 AssignToAll(X f)
	{
		return Vec2(f, f, f);
	}
};

typedef Vec2<float> Vec2f;

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::operator +(const Vec2Ref<X,Y> &other) const
{
	return Vec2<X,Y>(x+other.x, y+other.y);
}

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::operator -(const Vec2Ref<X,Y> &other) const
{
	return Vec2<X,Y>(x-other.x, y-other.y);
}

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::operator -() const
{
	return Vec2<X,Y>(-x,-y);
}

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::operator *(const Vec2Ref<X,Y> &other) const
{
	return Vec2<X,Y>(x*other.x, y*other.y);
}

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::operator * (const X& f) const
{
	return Vec2<X,Y>(x*f,y*f);
}

template<typename X, typename Y=X>
Vec2<X,Y> operator * (const X& f, const Vec2Ref<X,Y>& vec)
{
	return Vec2<X,Y>(f*vec.x, f*vec.y);
}

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::operator / (const X& f) const
{
	float invf = (1.0f/f);
	return Vec2<X,Y>(x*invf,y*invf);
}

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::WithLength(const X& l) const
{
	return (*this) * l / Length();
}

template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::Normalized() const
{
	return (*this) / Length();
}

// TODO: Shouldn't be using a float parameter for all base types
template<typename X, typename Y>
Vec2<X,Y> Vec2Ref<X,Y>::Lerp(const Vec2Ref<X,Y> &other, const float t) const
{
	return (*this)*(1-t) + other*t;
}

template<typename X, typename Y>
float Vec2Ref<X,Y>::Distance2To(const Vec2Ref<X,Y>& other) const
{
	return (other-(*this)).Length2();
}


/**
 * Vec3 - three dimensional vector with arbitrary base type
 */
template<typename X, typename Y=X, typename Z=X>
class Vec3;

// Like a Vec3, but acts on references
template<typename X, typename Y=X, typename Z=X>
class Vec3Ref
{
public:
	union
	{
		struct
		{
			X& x;
			Y& y;
			Z& z;
		};
		struct
		{
			X& r;
			Y& g;
			Z& b;
		};
		struct
		{
			X& u;
			Y& v;
			Z& w;
		};
	};

	X* AsArray() { return &x; }

	Vec3Ref(X& _x, Y& _y, Z& _z) : x(_x), y(_y), z(_z) {}

	Vec3Ref(X a[3]) : x(a[0]), y(a[1]), z(a[2]) {}

	template<typename X2, typename Y2, typename Z2>
	Vec3<X2,Y2,Z2> Cast() const
	{
		return Vec3<X2,Y2,Z2>((X2)x, (Y2)y, (Z2)z);
	}

	void Write(X a[3])
	{
		a[0] = x; a[1] = y; a[2] = z;
	}

	// Only defined for X=float and X=int; alpha bits set to zero
	unsigned int ToRGB() const;

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

	void operator *= (const X& f)
	{
		x*=f; y*=f; z*=f;
	}
	void operator /= (const X& f)
	{
		x/=f; y/=f; z/=f;
	}

	// operators which require creating a new Vec3 object
	Vec3<X,Y,Z> operator +(const Vec3Ref &other) const;
	Vec3<X,Y,Z> operator -(const Vec3Ref &other) const;
	Vec3<X,Y,Z> operator -() const;
	Vec3<X,Y,Z> operator *(const Vec3Ref &other) const;
	Vec3<X,Y,Z> operator * (const X& f) const;
	Vec3<X,Y,Z> operator / (const X& f) const;

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
		return (i==0) ? x : (i==1) ? y : z;
	}
	float operator [] (const int i) const
	{
		return (i==0) ? x : (i==1) ? y : z;
	}
	void SetZero()
	{
		x=(X)0; y=(Y)0; z=(Z)0;
	}

	// methods that create a new Vec3 object
	Vec3<X,Y,Z> WithLength(const X& l) const;
	Vec3<X,Y,Z> Normalized() const;
	Vec3<X,Y,Z> Lerp(const Vec3Ref &other, const float t) const;
	float Distance2To(const Vec3Ref &other) const;

	// swizzlers - create a subvector of references to specific components
	// e.g. Vec2Ref<X,Y> xy() { return Vec2Ref<X,Y>(x,y); }
	// _DEFINE_SWIZZLER2 defines a single such function (and its "const" version)
	// DEFINE_SWIZZLER2 defines all of them for all component names (x<->r) and permutations (xy<->yx)

#define _DEFINE_SWIZZLER2(a, b, A, B) Vec2Ref<A,B> a##b() { return Vec2Ref<A,B>(a,b); }; const Vec2Ref<A,B> a##b() const { return Vec2Ref<A,B>(a,b); }
#define DEFINE_SWIZZLER2(a, b, a2, b2, a3, b3, A, B) _DEFINE_SWIZZLER2(a, b, A, B); _DEFINE_SWIZZLER2(a2, b2, A, B); _DEFINE_SWIZZLER2(a3, b3, A, B); _DEFINE_SWIZZLER2(b, a, B, A); _DEFINE_SWIZZLER2(b2, a2, B, A); _DEFINE_SWIZZLER2(b3, a3, B, A);
	DEFINE_SWIZZLER2(x, y, r, g, u, v, X, Y);
	DEFINE_SWIZZLER2(x, z, r, b, u, w, X, Z);
	DEFINE_SWIZZLER2(y, z, g, b, v, w, Y, Z);
#undef DEFINE_SWIZZLER2
#undef _DEFINE_SWIZZLER2
};

template<typename X, typename Y, typename Z>
class Vec3 : public Vec3Ref<X,Y,Z>
{
public:
	X x;
	Y y;
	Z z;

	Vec3() : Vec3Ref<X,Y,Z>(x, y, z) {}
	Vec3(const X& _x, const Y& _y, const Z& _z) : Vec3Ref<X,Y,Z>(x, y, z), x(_x), y(_y), z(_z) {}
	Vec3(const X a[3]) : Vec3Ref<X,Y,Z>(x, y, z), x(a[0]), y(a[1]), z(a[2]) {}
	Vec3(const Vec3Ref<X,Y,Z>& other) : Vec3Ref<X,Y,Z>(x, y, z), x(other.x), y(other.y), z(other.z) {}

	// Only defined for X=float and X=int
	static Vec3 FromRGB(unsigned int rgb);

	static Vec3 AssignToAll(X f)
	{
		return Vec3(f, f, f);
	}
};

typedef Vec3<float> Vec3f;

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::operator +(const Vec3Ref<X,Y,Z> &other) const
{
	return Vec3<X,Y,Z>(x+other.x, y+other.y, z+other.z);
}

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::operator -(const Vec3Ref<X,Y,Z> &other) const
{
	return Vec3<X,Y,Z>(x-other.x, y-other.y, z-other.z);
}

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::operator -() const
{
	return Vec3<X,Y,Z>(-x,-y,-z);
}

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::operator *(const Vec3Ref<X,Y,Z> &other) const
{
	return Vec3<X,Y,Z>(x*other.x, y*other.y, z*other.z);
}

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::operator * (const X& f) const
{
	return Vec3<X,Y,Z>(x*f,y*f,z*f);
}

template<typename X, typename Y=X, typename Z=X>
Vec3<X,Y,Z> operator * (const X& f, const Vec3Ref<X,Y,Z>& vec)
{
	return Vec3<X,Y,Z>(f*vec.x, f*vec.y, f*vec.z);
}

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::operator / (const X& f) const
{
	float invf = (1.0f/f);
	return Vec3<X,Y,Z>(x*invf,y*invf,z*invf);
}

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::WithLength(const X& l) const
{
	return (*this) * l / Length();
}

template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::Normalized() const
{
	return (*this) / Length();
}

// TODO: Shouldn't be using a float parameter for all base types
template<typename X, typename Y, typename Z>
Vec3<X,Y,Z> Vec3Ref<X,Y,Z>::Lerp(const Vec3Ref<X,Y,Z> &other, const float t) const
{
	return (*this)*(1-t) + other*t;
}

template<typename X, typename Y, typename Z>
float Vec3Ref<X,Y,Z>::Distance2To(const Vec3Ref<X,Y,Z>& other) const
{
	return (other-(*this)).Length2();
}


/**
 * Vec4 - four dimensional vector with arbitrary base type
 */
template<typename X, typename Y=X, typename Z=X, typename W=X>
class Vec4;

// Like a Vec3, but acts on references
template<typename X, typename Y=X, typename Z=X, typename W=X>
class Vec4Ref
{
public:
	union
	{
		struct
		{
			X& x;
			Y& y;
			Z& z;
			W& w;
		};
		struct
		{
			X& r;
			Y& g;
			Z& b;
			W& a;
		};
	};

	X* AsArray() { return &x; }

	Vec4Ref(X& _x, Y& _y, Z& _z, W& _w) : x(_x), y(_y), z(_z), w(_w) {}

	Vec4Ref(X a[4]) : x(a[0]), y(a[1]), z(a[2]), w(a[3]) {}

	template<typename X2, typename Y2, typename Z2, typename W2>
	Vec4<X2,Y2,Z2,W2> Cast() const
	{
		return Vec4<X2,Y2,Z2,W2>((X2)x, (Y2)y, (Z2)z, (W2)w);
	}

	void Write(X a[4])
	{
		a[0] = x; a[1] = y; a[2] = z; a[3] = w;
	}

	// Only defined for X=float and X=int
	unsigned int ToRGBA() const;

	// operators acting on "this"
	void operator = (const Vec4Ref& other)
	{
		x = other.x;
		y = other.y;
		z = other.z;
		w = other.w;
	}
	void operator += (const Vec4Ref &other)
	{
		x+=other.x; y+=other.y; z+=other.z; w+=other.w;
	}
	void operator -= (const Vec4Ref &other)
	{
		x-=other.x; y-=other.y; z-=other.z; w-=other.w;
	}

	void operator *= (const X& f)
	{
		x*=f; y*=f; z*=f; w*=f;
	}
	void operator /= (const X& f)
	{
		x/=f; y/=f; z/=f; w/=f;
	}

	// operators which require creating a new Vec4 object
	Vec4<X,Y,Z,W> operator +(const Vec4Ref &other) const;
	Vec4<X,Y,Z,W> operator -(const Vec4Ref &other) const;
	Vec4<X,Y,Z,W> operator -() const;
	Vec4<X,Y,Z,W> operator *(const Vec4Ref &other) const;
	Vec4<X,Y,Z,W> operator * (const X& f) const;
	Vec4<X,Y,Z,W> operator / (const X& f) const;

	// methods which don't create new Vec4 objects
	float Length2() const
	{
		return x*x + y*y + z*z + w*w;
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
		return (i==0) ? x : (i==1) ? y : (i==2) ? z : w;
	}
	float operator [] (const int i) const
	{
		return (i==0) ? x : (i==1) ? y : (i==2) ? z : w;
	}
	void SetZero()
	{
		x=(X)0; y=(Y)0; z=(Z)0; w=(W)0;
	}

	// methods that create a new Vec4 object
	Vec4<X,Y,Z,W> WithLength(const X& l) const;
	Vec4<X,Y,Z,W> Normalized() const;
	Vec4<X,Y,Z,W> Lerp(const Vec4Ref &other, const float t) const;
	float Distance2To(const Vec4Ref &other) const;

	// swizzlers - create a subvector of references to specific components
	// e.g. Vec2Ref<X,Y> xy() { return Vec2Ref<X,Y>(x,y); }
	// _DEFINE_SWIZZLER2 defines a single such function (and its "const" version)
	// DEFINE_SWIZZLER2 defines all of them for all component names (x<->r) and permutations (xy<->yx)

#define _DEFINE_SWIZZLER2(a, b, A, B) Vec2Ref<A,B> a##b() { return Vec2Ref<A,B>(a,b); }; const Vec2Ref<A,B> a##b() const { return Vec2Ref<A,B>(a,b); }
#define DEFINE_SWIZZLER2(a, b, a2, b2, A, B) _DEFINE_SWIZZLER2(a, b, A, B); _DEFINE_SWIZZLER2(a2, b2, A, B); _DEFINE_SWIZZLER2(b, a, B, A); _DEFINE_SWIZZLER2(b2, a2, B, A);
	DEFINE_SWIZZLER2(x, y, r, g, X, Y);
	DEFINE_SWIZZLER2(x, z, r, b, X, Z);
	DEFINE_SWIZZLER2(x, w, r, a, X, W);
	DEFINE_SWIZZLER2(y, z, g, b, Y, Z);
	DEFINE_SWIZZLER2(y, w, g, a, Y, W);
	DEFINE_SWIZZLER2(z, w, b, a, Z, W);
#undef DEFINE_SWIZZLER2
#undef _DEFINE_SWIZZLER2

#define _DEFINE_SWIZZLER3(a, b, c, A, B, C) Vec3Ref<A,B,C> a##b##c() { return Vec3Ref<A,B,C>(a,b,c); }; const Vec3Ref<A,B,C> a##b##c() const { return Vec3Ref<A,B,C>(a,b,c); }
#define DEFINE_SWIZZLER3(a, b, c, a2, b2, c2, A, B, C) \
	_DEFINE_SWIZZLER3(a, b, c, A, B, C); \
	_DEFINE_SWIZZLER3(a, c, b, A, C, B); \
	_DEFINE_SWIZZLER3(b, a, c, B, A, C); \
	_DEFINE_SWIZZLER3(b, c, a, B, C, A); \
	_DEFINE_SWIZZLER3(c, a, b, C, A, B); \
	_DEFINE_SWIZZLER3(c, b, a, C, B, A); \
	_DEFINE_SWIZZLER3(a2, b2, c2, A, B, C); \
	_DEFINE_SWIZZLER3(a2, c2, b2, A, C, B); \
	_DEFINE_SWIZZLER3(b2, a2, c2, B, A, C); \
	_DEFINE_SWIZZLER3(b2, c2, a2, B, C, A); \
	_DEFINE_SWIZZLER3(c2, a2, b2, C, A, B); \
	_DEFINE_SWIZZLER3(c2, b2, a2, C, B, A);

	DEFINE_SWIZZLER3(x, y, z, r, g, b, X, Y, Z);
	DEFINE_SWIZZLER3(x, y, w, r, g, a, X, Y, W);
	DEFINE_SWIZZLER3(x, z, w, r, b, a, X, Z, W);
	DEFINE_SWIZZLER3(y, z, w, g, b, a, Y, Z, W);
#undef DEFINE_SWIZZLER3
#undef _DEFINE_SWIZZLER3
};

template<typename X, typename Y, typename Z, typename W>
class Vec4 : public Vec4Ref<X,Y,Z,W>
{
public:
	X x;
	Y y;
	Z z;
	W w;

	Vec4() : Vec4Ref<X,Y,Z,W>(x, y, z, w) {}
	Vec4(const X& _x, const Y& _y, const Z& _z, const W& _w) : Vec4Ref<X,Y,Z,W>(x, y, z, w), x(_x), y(_y), z(_z), w(_w) {}
	Vec4(const X a[4]) : Vec4Ref<X,Y,Z,W>(x, y, z, w), x(a[0]), y(a[1]), z(a[2]), w(a[3]) {}
	Vec4(const Vec4Ref<X,Y,Z,W>& other) : Vec4Ref<X,Y,Z,W>(x, y, z, w), x(other.x), y(other.y), z(other.z), w(other.w) {}

	// Only defined for X=float and X=int
	static Vec4 FromRGBA(unsigned int rgba);

	static Vec4 AssignToAll(X f)
	{
		return Vec4(f, f, f, f);
	}
};

typedef Vec4<float> Vec4f;

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::operator +(const Vec4Ref<X,Y,Z,W> &other) const
{
	return Vec4<X,Y,Z,W>(x+other.x, y+other.y, z+other.z, w+other.w);
}

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::operator -(const Vec4Ref<X,Y,Z,W> &other) const
{
	return Vec4<X,Y,Z,W>(x-other.x, y-other.y, z-other.z, w-other.w);
}

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::operator -() const
{
	return Vec4<X,Y,Z,W>(-x,-y,-z,-w);
}

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::operator *(const Vec4Ref<X,Y,Z,W> &other) const
{
	return Vec4<X,Y,Z,W>(x*other.x, y*other.y, z*other.z, w*other.w);
}

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::operator * (const X& f) const
{
	return Vec4<X,Y,Z,W>(x*f,y*f,z*f,w*f);
}

template<typename X, typename Y=X, typename Z=X, typename W=X>
Vec4<X,Y,Z,W> operator * (const X& f, const Vec4Ref<X,Y,Z,W>& vec)
{
	return Vec4<X,Y,Z,W>(f*vec.x, f*vec.y, f*vec.z, f*vec.w);
}

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::operator / (const X& f) const
{
	float invf = (1.0f/f);
	return Vec4<X,Y,Z,W>(x*invf,y*invf,z*invf,w*invf);
}

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::WithLength(const X& l) const
{
	return (*this) * l / Length();
}

template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::Normalized() const
{
	return (*this) / Length();
}

// TODO: Shouldn't be using a float parameter for all base types
template<typename X, typename Y, typename Z, typename W>
Vec4<X,Y,Z,W> Vec4Ref<X,Y,Z,W>::Lerp(const Vec4Ref<X,Y,Z,W> &other, const float t) const
{
	return (*this)*(1-t) + other*t;
}

template<typename X, typename Y, typename Z, typename W>
float Vec4Ref<X,Y,Z,W>::Distance2To(const Vec4Ref<X,Y,Z,W>& other) const
{
	return (other-(*this)).Length2();
}


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

// Dot product only really makes sense for same types
template<typename X>
inline X Dot(const Vec2Ref<X> &a, const Vec2Ref<X>& b)
{
	return a.x*b.x + a.y*b.y;
}

template<typename X>
inline X Dot(const Vec3Ref<X> &a, const Vec3Ref<X>& b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

template<typename X>
inline X Dot(const Vec4Ref<X> &a, const Vec4Ref<X>& b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
}

template<typename X, typename Y=X, typename Z=X>
inline Vec3<X,Y,Z> Cross(const Vec3Ref<X,Y,Z> &a, const Vec3Ref<X,Y,Z>& b)
{
	return Vec3<X,Y,Z>(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
