#include "math/lin/matrix4x4.h"

#include <stdio.h>

#include "base/compat.h"
#include "math/lin/vec3.h"
#include "math/lin/quat.h"
#include "math/fast/fast_matrix.h"

#ifdef _WIN32
#undef far
#undef near
#endif

// See http://code.google.com/p/oolongengine/source/browse/trunk/Oolong+Engine2/Math/neonmath/neon_matrix_impl.cpp?spec=svn143&r=143	when we need speed
// no wait. http://code.google.com/p/math-neon/

Matrix4x4 Matrix4x4::simpleInverse() const {
	Matrix4x4 out;
	out.xx = xx;
	out.xy = yx;
	out.xz = zx;

	out.yx = xy;
	out.yy = yy;
	out.yz = zy;

	out.zx = xz;
	out.zy = yz;
	out.zz = zz;

	out.wx = -(xx * wx + xy * wy + xz * wz);
	out.wy = -(yx * wx + yy * wy + yz * wz);
	out.wz = -(zx * wx + zy * wy + zz * wz);

	out.xw = 0.0f;
	out.yw = 0.0f;
	out.zw = 0.0f;
	out.ww = 1.0f;

	return out;
}

Matrix4x4 Matrix4x4::transpose() const
{
	Matrix4x4 out;
	out.xx = xx;out.xy = yx;out.xz = zx;out.xw = wx;
	out.yx = xy;out.yy = yy;out.yz = zy;out.yw = wy;
	out.zx = xz;out.zy = yz;out.zz = zz;out.zw = wz;
	out.wx = xw;out.wy = yw;out.wz = zw;out.ww = ww;
	return out;
}

Matrix4x4 Matrix4x4::operator * (const Matrix4x4 &other) const 
{
	Matrix4x4 temp;
	fast_matrix_mul_4x4(temp.m, other.m, this->m);
	return temp;
}

Matrix4x4 Matrix4x4::inverse() const {
	Matrix4x4 temp;
	float dW = 1.0f / (xx*(yy*zz - yz*zy) - xy*(yx*zz - yz*zx) - xz*(yy*zx - yx*zy));

	temp.xx = (yy*zz - yz*zy) * dW;
	temp.xy = (xz*zy - xy*zz) * dW;
	temp.xz = (xy*yz - xz*yy) * dW;
	temp.xw = xw;

	temp.yx = (yz*zx - yx*zz) * dW;
	temp.yy = (xx*zz - xz*zx) * dW;
	temp.yz = (xz*yx - xx*zx) * dW;
	temp.yw = yw;

	temp.zx = (yx*zy - yy*zx) * dW;
	temp.zy = (xy*zx - xx*zy) * dW;
	temp.zz = (xx*yy - xy*yx) * dW;
	temp.zw = zw;

	temp.wx = (yy*(zx*wz - zz*wx) + yz*(zy*wx - zx*wy) - yx*(zy*wz - zz*wy)) * dW;
	temp.wy = (xx*(zy*wz - zz*wy) + xy*(zz*wx - zx*wz) + xz*(zx*wy - zy*wx)) * dW;
	temp.wz = (xy*(yx*wz - yz*wx) + xz*(yy*wx - yx*wy) - xx*(yy*wz - yz*wy)) * dW;
	temp.ww = ww;

	return temp;
}

void Matrix4x4::setViewLookAt(const Vec3 &vFrom, const Vec3 &vAt, const Vec3 &vWorldUp) {
	Vec3 vView = vFrom - vAt;	// OpenGL, sigh...
	vView.normalize();
	float DotProduct = vWorldUp * vView;
	Vec3 vUp = vWorldUp - vView * DotProduct;
	float Length = vUp.length();

	if (1e-6f > Length) {
		// EMERGENCY
		vUp = Vec3(0.0f, 1.0f, 0.0f) - vView * vView.y;
		// If we still have near-zero length, resort to a different axis.
		Length = vUp.length();
		if (1e-6f > Length)
		{
			vUp		 = Vec3(0.0f, 0.0f, 1.0f) - vView * vView.z;
			Length	= vUp.length();
			if (1e-6f > Length)
				return;
		}
	}
	vUp.normalize(); 
	Vec3 vRight = vUp % vView;
	empty();

	xx = vRight.x; xy = vUp.x; xz=vView.x;
	yx = vRight.y; yy = vUp.y; yz=vView.y;
	zx = vRight.z; zy = vUp.z; zz=vView.z;

	wx = -vFrom * vRight;
	wy = -vFrom * vUp;
	wz = -vFrom * vView;
	ww = 1.0f;
}

void Matrix4x4::setViewLookAtD3D(const Vec3 &vFrom, const Vec3 &vAt, const Vec3 &vWorldUp) {
	Vec3 vView = vAt - vFrom;
	vView.normalize();
	float DotProduct = vWorldUp * vView;
	Vec3 vUp = vWorldUp - vView * DotProduct;
	float Length = vUp.length();

	if (1e-6f > Length) {
		vUp = Vec3(0.0f, 1.0f, 0.0f) - vView * vView.y;
		// If we still have near-zero length, resort to a different axis.
		Length = vUp.length();
		if (1e-6f > Length)
		{
			vUp		 = Vec3(0.0f, 0.0f, 1.0f) - vView * vView.z;
			Length	= vUp.length();
			if (1e-6f > Length)
				return;
		}
	}
	vUp.normalize(); 
	Vec3 vRight = vUp % vView;
	empty();

	xx = vRight.x; xy = vUp.x; xz=vView.x;
	yx = vRight.y; yy = vUp.y; yz=vView.y;
	zx = vRight.z; zy = vUp.z; zz=vView.z;

	wx = -vFrom * vRight;
	wy = -vFrom * vUp;
	wz = -vFrom * vView;
	ww = 1.0f;
}


void Matrix4x4::setViewFrame(const Vec3 &pos, const Vec3 &vRight, const Vec3 &vView, const Vec3 &vUp) {
	xx = vRight.x; xy = vUp.x; xz=vView.x; xw = 0.0f;
	yx = vRight.y; yy = vUp.y; yz=vView.y; yw = 0.0f;
	zx = vRight.z; zy = vUp.z; zz=vView.z; zw = 0.0f;

	wx = -pos * vRight;
	wy = -pos * vUp;
	wz = -pos * vView;
	ww = 1.0f;
}

//YXZ euler angles
void Matrix4x4::setRotation(float x,float y, float z) 
{
	setRotationY(y);
	Matrix4x4 temp;
	temp.setRotationX(x);
	*this *= temp;
	temp.setRotationZ(z);
	*this *= temp;
}

void Matrix4x4::setProjection(float near, float far, float fov_horiz, float aspect) {
	// Now OpenGL style.
	empty();

	float xFac = tanf(fov_horiz * 3.14f/360);
	float yFac = xFac * aspect;	
	xx = 1.0f / xFac;
	yy = 1.0f / yFac;
	zz = -(far+near)/(far-near);
	zw = -1.0f;
	wz = -(2*far*near)/(far-near);
}

void Matrix4x4::setProjectionD3D(float near_plane, float far_plane, float fov_horiz, float aspect) {
	empty();
	float Q, f;

	f = fov_horiz*0.5f;
	Q = far_plane / (far_plane - near_plane);

	xx = (float)(1.0f / tanf(f));;
	yy = (float)(1.0f / tanf(f*aspect)); 
	zz = Q; 
	wz = -Q * near_plane;
	zw = 1.0f;
}

void Matrix4x4::setOrtho(float left, float right, float bottom, float top, float near, float far) {
	empty();
	xx = 2.0f / (right - left);
	yy = 2.0f / (top - bottom);
	zz = 2.0f / (far - near);
	wx = -(right + left) / (right - left);
	wy = -(top + bottom) / (top - bottom);
	wz = -(far + near) / (far - near);
	ww = 1.0f;
}

void Matrix4x4::setOrthoD3D(float left, float right, float bottom, float top, float near, float far) {
	empty();
	xx = 2.0f / (right - left);
	yy = 2.0f / (top - bottom);
	zz = 1.0f / (far - near);
	wx = -(right + left) / (right - left);
	wy = -(top + bottom) / (top - bottom);
	wz = -near / (far - near);
	ww = 1.0f;
}

void Matrix4x4::setOrthoVulkan(float left, float right, float top, float bottom, float near, float far) {
	empty();
	xx = 2.0f / (right - left);
	yy = 2.0f / (bottom - top);
	zz = 1.0f / (far - near);
	wx = -(right + left) / (right - left);
	wy = -(top + bottom) / (bottom - top);
	wz = -near / (far - near);
	ww = 1.0f;
}

void Matrix4x4::setProjectionInf(const float near_plane, const float fov_horiz, const float aspect) {
	empty();
	float f = fov_horiz*0.5f;
	xx = 1.0f / tanf(f);
	yy = 1.0f / tanf(f*aspect);
	zz = 1;
	wz = -near_plane;
	zw = 1.0f;
}

void Matrix4x4::setRotationAxisAngle(const Vec3 &axis, float angle) {
	Quaternion quat;
	quat.setRotation(axis, angle);
	quat.toMatrix(this);
}

// from a (Position, Rotation, Scale) vec3 quat vec3 tuple
Matrix4x4 Matrix4x4::fromPRS(const Vec3 &positionv, const Quaternion &rotv, const Vec3 &scalev) {
	Matrix4x4 newM;
	newM.setIdentity();
	Matrix4x4 rot, scale;
	rotv.toMatrix(&rot);
	scale.setScaling(scalev);
	newM = rot * scale;
	newM.wx = positionv.x;
	newM.wy = positionv.y;
	newM.wz = positionv.z;
	return newM;
}

void Matrix4x4::toText(char *buffer, int len) const {
	snprintf(buffer, len, "%f %f %f %f\n%f %f %f %f\n%f %f %f %f\n%f %f %f %f\n",
		xx,xy,xz,xw,
		yx,yy,yz,yw,
		zx,zy,zz,zw,
		wx,wy,wz,ww);
	buffer[len - 1] = '\0';
}

void Matrix4x4::print() const {
	char buffer[256];
	toText(buffer, 256);
	puts(buffer);
}
