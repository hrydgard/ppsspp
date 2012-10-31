#include "math/lin/matrix4x4.h"

#include <stdio.h>

#include "math/lin/vec3.h"
#include "math/lin/quat.h"

#ifdef _WIN32
#undef far
#undef near
#endif

// See http://code.google.com/p/oolongengine/source/browse/trunk/Oolong+Engine2/Math/neonmath/neon_matrix_impl.cpp?spec=svn143&r=143	when we need speed
// no wait. http://code.google.com/p/math-neon/

void matrix_mul_4x4(Matrix4x4 &res, const Matrix4x4 &inA, const Matrix4x4 &inB) {
	res.xx = inA.xx*inB.xx + inA.xy*inB.yx + inA.xz*inB.zx + inA.xw*inB.wx;
	res.xy = inA.xx*inB.xy + inA.xy*inB.yy + inA.xz*inB.zy + inA.xw*inB.wy;
	res.xz = inA.xx*inB.xz + inA.xy*inB.yz + inA.xz*inB.zz + inA.xw*inB.wz;
	res.xw = inA.xx*inB.xw + inA.xy*inB.yw + inA.xz*inB.zw + inA.xw*inB.ww;

	res.yx = inA.yx*inB.xx + inA.yy*inB.yx + inA.yz*inB.zx + inA.yw*inB.wx;
	res.yy = inA.yx*inB.xy + inA.yy*inB.yy + inA.yz*inB.zy + inA.yw*inB.wy;
	res.yz = inA.yx*inB.xz + inA.yy*inB.yz + inA.yz*inB.zz + inA.yw*inB.wz;
	res.yw = inA.yx*inB.xw + inA.yy*inB.yw + inA.yz*inB.zw + inA.yw*inB.ww;

	res.zx = inA.zx*inB.xx + inA.zy*inB.yx + inA.zz*inB.zx + inA.zw*inB.wx;
	res.zy = inA.zx*inB.xy + inA.zy*inB.yy + inA.zz*inB.zy + inA.zw*inB.wy;
	res.zz = inA.zx*inB.xz + inA.zy*inB.yz + inA.zz*inB.zz + inA.zw*inB.wz;
	res.zw = inA.zx*inB.xw + inA.zy*inB.yw + inA.zz*inB.zw + inA.zw*inB.ww;

	res.wx = inA.wx*inB.xx + inA.wy*inB.yx + inA.wz*inB.zx + inA.ww*inB.wx;
	res.wy = inA.wx*inB.xy + inA.wy*inB.yy + inA.wz*inB.zy + inA.ww*inB.wy;
	res.wz = inA.wx*inB.xz + inA.wy*inB.yz + inA.wz*inB.zz + inA.ww*inB.wz;
	res.ww = inA.wx*inB.xw + inA.wy*inB.yw + inA.wz*inB.zw + inA.ww*inB.ww;
}

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
	matrix_mul_4x4(temp, *this, other);
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
	setIdentity();
	xx = 2.0f / (right - left);
	yy = 2.0f / (top - bottom);
	zz = 2.0f / (far - near);
	wx = -(right + left) / (right - left);
	wy = -(top + bottom) / (top - bottom);
	wz = -(far + near) / (far - near);
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
#if _MSC_VER
#define snprintf _snprintf
#endif
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
