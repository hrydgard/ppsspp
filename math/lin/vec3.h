#ifndef _MATH_LIN_VEC3
#define _MATH_LIN_VEC3

#include <math.h>
#include <string.h>	// memset

class Matrix4x4;

// Hm, doesn't belong in this file.
class Vec4 {
public:
	float x,y,z,w;
	Vec4(){}
	Vec4(float a, float b, float c, float d) {x=a;y=b;z=c;w=d;}
	Vec4 multiply4D(Matrix4x4 &m) const;
};

class Vec3 {
public:
	float x,y,z;

	Vec3() { }
	explicit Vec3(float f) {x=y=z=f;}

	float operator [] (int i) const { return (&x)[i]; }
	float &operator [] (int i) { return (&x)[i]; }

	Vec3(const float _x, const float _y, const float _z) {
		x=_x; y=_y; z=_z;
	}
	void Set(float _x, float _y, float _z) {
		x=_x; y=_y; z=_z;
	}
	Vec3 operator + (const Vec3 &other) const {
		return Vec3(x+other.x, y+other.y, z+other.z);
	}
	void operator += (const Vec3 &other) {
		x+=other.x; y+=other.y; z+=other.z;
	}
	Vec3 operator -(const Vec3 &v) const {
		return Vec3(x-v.x,y-v.y,z-v.z);
	}
	void operator -= (const Vec3 &other)
	{
		x-=other.x; y-=other.y; z-=other.z;
	}
	Vec3 operator -() const {
		return Vec3(-x,-y,-z);
	}

	Vec3 operator * (const float f) const {
		return Vec3(x*f,y*f,z*f);
	}
	Vec3 operator / (const float f) const {
		float invf = (1.0f/f);
		return Vec3(x*invf,y*invf,z*invf);
	}
	void operator /= (const float f)
	{
		*this = *this / f;
	}
	float operator * (const Vec3 &other) const {
		return x*other.x + y*other.y + z*other.z;
	}
	void operator *= (const float f) {
		*this = *this * f;
	}
	void scaleBy(const Vec3 &other) {
		x *= other.x; y *= other.y; z *= other.z;
	}
	Vec3 scaledBy(const Vec3 &other) const {
		return Vec3(x*other.x, y*other.y, z*other.z);
	}
	Vec3 scaledByInv(const Vec3 &other) const {
		return Vec3(x/other.x, y/other.y, z/other.z);
	}
	Vec3 operator *(const Matrix4x4 &m) const;
	void operator *=(const Matrix4x4 &m) {
		*this = *this * m;
	}
	Vec4 multiply4D(const Matrix4x4 &m) const;
	Vec3 rotatedBy(const Matrix4x4 &m) const;
	Vec3 operator %(const Vec3 &v) const {
		return Vec3(y*v.z-z*v.y, z*v.x-x*v.z, x*v.y-y*v.x);
	}	
	float length2() const {
		return x*x + y*y + z*z;
	}
	float length() const {
		return sqrtf(length2());
	}
	void setLength(const float l) {
		(*this) *= l/length();
	}
	Vec3 withLength(const float l) const {
		return (*this) * l / length();
	}
	float distance2To(const Vec3 &other) const {
		return Vec3(other-(*this)).length2();
	}
	Vec3 normalized() const {
		return (*this) / length();
	}
	float normalize() { //returns the previous length, is often useful
		float len = length();
		(*this) = (*this)/len;
		return len;
	}
	bool operator == (const Vec3 &other) const {
		if (x==other.x && y==other.y && z==other.z)
			return true;
		else
			return false;
	}
	Vec3 lerp(const Vec3 &other, const float t) const {
		return (*this)*(1-t) + other*t;
	}
	void setZero() {
		memset((void *)this,0,sizeof(float)*3);
	}
};

inline Vec3 operator * (const float f, const Vec3 &v) {return v * f;}

// In new code, prefer these to the operators.

inline float dot(const Vec3 &a, const Vec3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
	return a % b;
}

inline float sqr(const Vec3 &v) {
	return dot(v, v);
}

class AABBox {
public:
	Vec3 min;
	Vec3 max;
};

#endif	// _MATH_LIN_VEC3
