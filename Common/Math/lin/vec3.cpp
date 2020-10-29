#include <stdio.h>

#include "Common/Math/lin/vec3.h"
#include "Common/Math/lin/matrix4x4.h"

namespace Lin {

Vec3 Vec3::operator *(const Matrix4x4 &m) const {
	return Vec3(x*m.xx + y*m.yx + z*m.zx + m.wx,
		x*m.xy + y*m.yy + z*m.zy + m.wy,
		x*m.xz + y*m.yz + z*m.zz + m.wz);
}

Vec3 Vec3::rotatedBy(const Matrix4x4 &m) const {
	return Vec3(x*m.xx + y*m.yx + z*m.zx,
		x*m.xy + y*m.yy + z*m.zy,
		x*m.xz + y*m.yz + z*m.zz);
}

}  // namespace Lin
