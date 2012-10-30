#ifndef _MATH_LIN_MATRIX4X4_H
#define _MATH_LIN_MATRIX4X4_H

#include "math/lin/vec3.h"

class Quaternion;

class Matrix4x4 {
public:
	float xx, xy, xz, xw;
	float yx, yy, yz, yw;
	float zx, zy, zz, zw;
	float wx, wy, wz, ww;

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
	void setScaling(const Vec3 f) {
		empty();
		xx=f.x;
		yy=f.y;
		zz=f.z;
		ww=1.0f;
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
		float c=cosf(a);
		float s=sinf(a);
		xx = 1.0f;
		yy =	c;			yz = s;
		zy = -s;			zz = c;
		ww = 1.0f;
	}
	void setRotationY(const float a)	 {
		empty();
		float c=cosf(a);
		float s=sinf(a);
		xx = c;									 xz = -s;
		yy =	1.0f;
		zx = s;									 zz = c	;
		ww = 1.0f;
	}
	void setRotationZ(const float a)	 {
		empty();
		float c=cosf(a);
		float s=sinf(a);
		xx = c;		xy = s;
		yx = -s;	 yy = c;
		zz = 1.0f; 
		ww = 1.0f;
	}
	void setRotationAxisAngle(const Vec3 &axis, float angle);


	void setRotation(float x,float y, float z);
	void setProjection(float near_plane, float far_plane, float fov_horiz, float aspect = 0.75f);
	void setProjectionD3D(float near_plane, float far_plane, float fov_horiz, float aspect = 0.75f);
	void setProjectionInf(float near_plane, float fov_horiz, float aspect = 0.75f);
	void setOrtho(float left, float right, float bottom, float top, float near, float far);
	void setShadow(float Lx, float Ly, float Lz, float Lw) {
		float Pa=0;
		float Pb=1;
		float Pc=0;
		float Pd=0;
		//P = normalize(Plane);
		float d = (Pa*Lx + Pb*Ly + Pc*Lz + Pd*Lw);

		xx=Pa * Lx + d;	xy=Pa * Ly;		 xz=Pa * Lz;		 xw=Pa * Lw;
		yx=Pb * Lx;			yy=Pb * Ly + d; yz=Pb * Lz;		 yw=Pb * Lw;
		zx=Pc * Lx;			zy=Pc * Ly;		 zz=Pc * Lz + d; zw=Pc * Lw;
		wx=Pd * Lx;			wy=Pd * Ly;		 wz=Pd * Lz;		 ww=Pd * Lw + d;
	}

	void setViewLookAt(const Vec3 &from, const Vec3 &at, const Vec3 &worldup);
	void setViewLookAtD3D(const Vec3 &from, const Vec3 &at, const Vec3 &worldup);
	void setViewFrame(const Vec3 &pos, const Vec3 &right, const Vec3 &forward, const Vec3 &up);
	void stabilizeOrtho() {
		/*
		front().normalize();
		right().normalize();
		up() = front() % right();
		right() = up() % front();
		*/
	}
	void toText(char *buffer, int len) const;
	void print() const;
	static Matrix4x4 fromPRS(const Vec3 &position, const Quaternion &normal, const Vec3 &scale);
};

#endif	// _MATH_LIN_MATRIX4X4_H

