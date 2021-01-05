#include "Common/Math/lin/matrix4x4.h"

#include <cstdio>

#include "Common/Math/lin/vec3.h"
#include "Common/Math/fast/fast_matrix.h"

#ifdef _WIN32
#undef far
#undef near
#endif

// See http://code.google.com/p/oolongengine/source/browse/trunk/Oolong+Engine2/Math/neonmath/neon_matrix_impl.cpp?spec=svn143&r=143	when we need speed
// no wait. http://code.google.com/p/math-neon/

namespace Lin {

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

void Matrix4x4::setViewFrame(const Vec3 &pos, const Vec3 &vRight, const Vec3 &vView, const Vec3 &vUp) {
	xx = vRight.x; xy = vUp.x; xz=vView.x; xw = 0.0f;
	yx = vRight.y; yy = vUp.y; yz=vView.y; yw = 0.0f;
	zx = vRight.z; zy = vUp.z; zz=vView.z; zw = 0.0f;

	wx = -pos * vRight;
	wy = -pos * vUp;
	wz = -pos * vView;
	ww = 1.0f;
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

}
