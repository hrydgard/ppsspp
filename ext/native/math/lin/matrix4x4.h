#ifndef _MATH_LIN_MATRIX4X4_H
#define _MATH_LIN_MATRIX4X4_H

#include "math/lin/vec3.h"

class Quaternion;

class Matrix4x4 {
public:
	union {
		struct {
			float xx, xy, xz, xw;
			float yx, yy, yz, yw;
			float zx, zy, zz, zw;
			float wx, wy, wz, ww;
		};
		float m[16];
	};

	const Vec3 right() const {return Vec3(xx, xy, xz);}
	const Vec3 up()		const {return Vec3(yx, yy, yz);}
	const Vec3 front() const {return Vec3(zx, zy, zz);}
	const Vec3 move()	const {return Vec3(wx, wy, wz);}

	void setRight(const Vec3 &v) {
		xx = v.x; xy = v.y; xz = v.z;
	}
	void setUp(const Vec3 &v) {
		yx = v.x; yy = v.y; yz = v.z;
	}
	void setFront(const Vec3 &v) {
		zx = v.x; zy = v.y; zz = v.z;
	}
	void setMove(const Vec3 &v) {
		wx = v.x; wy = v.y; wz = v.z;
	}

	const float &operator[](int i) const {
		return *(((const float *)this) + i);
	}
	float &operator[](int i) {
		return *(((float *)this) + i);
	}
	Matrix4x4 operator * (const Matrix4x4 &other) const ;
	void operator *= (const Matrix4x4 &other) {
		*this = *this * other;
	}
	const float *getReadPtr() const {
		return (const float *)this;
	}
	void empty() {
		memset(this, 0, 16 * sizeof(float));
	}
	void setScaling(const float f) {
		empty();
		xx=yy=zz=f; ww=1.0f;
	}

	void setIdentity() {
		setScaling(1.0f);
	}
	void setTranslation(const Vec3 &trans) {
		setIdentity();
		wx = trans.x;
		wy = trans.y;
		wz = trans.z;
	}

	Matrix4x4 inverse() const;
	Matrix4x4 simpleInverse() const;
	Matrix4x4 transpose() const;

	void setRotationX(const float a) {
		empty();
		float c = cosf(a);
		float s = sinf(a);
		xx = 1.0f;
		yy = c; yz = s;
		zy = -s; zz = c;
		ww = 1.0f;
	}
	void setRotationY(const float a)	 {
		empty();
		float c = cosf(a);
		float s = sinf(a);
		xx = c; xz = -s;
		yy = 1.0f;
		zx = s; zz = c;
		ww = 1.0f;
	}
	void setRotationZ(const float a)	 {
		empty();
		float c = cosf(a);
		float s = sinf(a);
		xx = c; xy = s;
		yx = -s; yy = c;
		zz = 1.0f; 
		ww = 1.0f;
	}
	// Exact angles to avoid any artifacts.
	void setRotationZ90() {
		empty();
		float c = 0.0f;
		float s = 1.0f;
		xx = c; xy = s;
		yx = -s; yy = c;
		zz = 1.0f;
		ww = 1.0f;
	}
	void setRotationZ180() {
		empty();
		float c = -1.0f;
		float s = 0.0f;
		xx = c; xy = s;
		yx = -s; yy = c;
		zz = 1.0f;
		ww = 1.0f;
	}
	void setRotationZ270() {
		empty();
		float c = 0.0f;
		float s = -1.0f;
		xx = c; xy = s;
		yx = -s; yy = c;
		zz = 1.0f;
		ww = 1.0f;
	}

	void setRotation(float x,float y, float z);
	void setProjection(float near_plane, float far_plane, float fov_horiz, float aspect = 0.75f);
	void setProjectionD3D(float near_plane, float far_plane, float fov_horiz, float aspect = 0.75f);
	void setOrtho(float left, float right, float bottom, float top, float near, float far);
	void setOrthoD3D(float left, float right, float bottom, float top, float near, float far);
	void setOrthoVulkan(float left, float right, float top, float bottom, float near, float far);

	void setViewLookAt(const Vec3 &from, const Vec3 &at, const Vec3 &worldup);
	void setViewLookAtD3D(const Vec3 &from, const Vec3 &at, const Vec3 &worldup);
	void setViewFrame(const Vec3 &pos, const Vec3 &right, const Vec3 &forward, const Vec3 &up);
	void toText(char *buffer, int len) const;
	void print() const;

	void translateAndScale(const Vec3 &trans, const Vec3 &scale) {
		xx = xx * scale.x + xw * trans.x;
		xy = xy * scale.y + xw * trans.y;
		xz = xz * scale.z + xw * trans.z;

		yx = yx * scale.x + yw * trans.x;
		yy = yy * scale.y + yw * trans.y;
		yz = yz * scale.z + yw * trans.z;

		zx = zx * scale.x + zw * trans.x;
		zy = zy * scale.y + zw * trans.y;
		zz = zz * scale.z + zw * trans.z;

		wx = wx * scale.x + ww * trans.x;
		wy = wy * scale.y + ww * trans.y;
		wz = wz * scale.z + ww * trans.z;
	}
};

#endif	// _MATH_LIN_MATRIX4X4_H

