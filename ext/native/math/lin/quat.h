#ifndef _MATH_LIN_QUAT_H
#define _MATH_LIN_QUAT_H

#include "math/lin/vec3.h"

class Matrix4x4;

class Quaternion
{
public:
	float x,y,z,w;

	Quaternion() { }
	Quaternion(const float _x, const float _y, const float _z, const float _w) {
		x=_x; y=_y; z=_z; w=_w;
	}
	void setIdentity()
	{
		x=y=z=0; w=1.0f;
	}
	void setXRotation(const float r) { w = cosf(r / 2); x = sinf(r / 2); y = z = 0; }
	void setYRotation(const float r) { w = cosf(r / 2); y = sinf(r / 2); x = z = 0; }
	void setZRotation(const float r) { w = cosf(r / 2); z = sinf(r / 2); x = y = 0; }
	void toMatrix(Matrix4x4 *out) const;
	static Quaternion fromMatrix(Matrix4x4 &m);

	Quaternion operator *(Quaternion &q) const
	{
		return Quaternion(
			(w * q.w) - (x * q.x) - (y * q.y) - (z * q.z),
			(w * q.x) + (x * q.w) + (y * q.z) - (z * q.y),
			(w * q.y) + (y * q.w) + (z * q.x) - (x * q.z),
			(w * q.z) + (z * q.w) + (x * q.y) - (y * q.x)
			);
	}
	Quaternion operator -()
	{
		return Quaternion(-x,-y,-z,-w);
	}
	void setRotation(Vec3 axis, float angle)
	{
		axis /= axis.length();
		angle *= .5f;
		float sine = sinf(angle);
		w = cosf(angle);
		x = sine * axis.x;
		y = sine * axis.y;
		z = sine * axis.z;
	}
	void toAxisAngle(Vec3 &v, float &angle)
	{
		normalize();
		if (w==1.0f && x==0.0f && y==0.0f && z==0.0f)
		{
			v = Vec3(0,1,0);
			angle = 0.0f;
			return;
		}
		float cos_a = w;
		angle = acosf(cos_a) * 2;
		float sin_a = sqrtf( 1.0f - cos_a * cos_a );
		if (fabsf(sin_a) < 0.00005f) sin_a = 1;
		float inv_sin_a=1.0f/sin_a;
		v.x = x * inv_sin_a;
		v.y = y * inv_sin_a;
		v.z = z * inv_sin_a;
	}
	enum {
		QUAT_SHORT,
		QUAT_LONG,
		QUAT_CW,
		QUAT_CCW
	};
	Quaternion slerp(const Quaternion &to, const float a) const;
	Quaternion multiply(const Quaternion &q) const;
	float &operator [] (int i) {
		return *((&x) + i);
	}
	float operator [] (int i) const {
		return *((&x) + i);
	}
	//not sure about this, maybe mag is supposed to sqrt
	float magnitude() const {
		return x*x + y*y + z*z + w*w;
	}
	void normalize()	{
		float f = 1.0f/sqrtf(magnitude());
		x*=f; y*=f; z*=f; w*=f;
	}
};

#endif	// _MATH_LIN_QUAT_H
