#include <stdio.h>

#include "math/lin/vec3.h"
#include "math/lin/matrix4x4.h"

Vec3 Vec3::operator *(const Matrix4x4 &m) const {
	return Vec3(x*m.xx + y*m.yx + z*m.zx + m.wx,
		x*m.xy + y*m.yy + z*m.zy + m.wy,
		x*m.xz + y*m.yz + z*m.zz + m.wz);
}
Vec4 Vec3::multiply4D(const Matrix4x4 &m) const {
	return Vec4(x*m.xx + y*m.yx + z*m.zx + m.wx,
		x*m.xy + y*m.yy + z*m.zy + m.wy,
		x*m.xz + y*m.yz + z*m.zz + m.wz,
		x*m.xw + y*m.yw + z*m.zw + m.ww);
}
Vec4 Vec4::multiply4D(Matrix4x4 &m) const {
	return Vec4(x*m.xx + y*m.yx + z*m.zx + w*m.wx,
		x*m.xy + y*m.yy + z*m.zy + w*m.wy,
		x*m.xz + y*m.yz + z*m.zz + w*m.wz,
		x*m.xw + y*m.yw + z*m.zw + w*m.ww);
}

Vec3 Vec3::rotatedBy(const Matrix4x4 &m) const {
	return Vec3(x*m.xx + y*m.yx + z*m.zx,
		x*m.xy + y*m.yy + z*m.zy,
		x*m.xz + y*m.yz + z*m.zz);
}
