#include "math/lin/matrix4x4.h"
#include "math/lin/plane.h"


void Plane::TransformByIT(const Matrix4x4 &m, Plane *out) {
	out->x = x * m.xx + y * m.yx + z * m.zx + d * m.wx;
	out->y = x * m.xy + y * m.yy + z * m.zy + d * m.wy;
	out->z = x * m.xz + y * m.yz + z * m.zz + d * m.wz;
	out->d = x * m.xw + y * m.yw + z * m.zw + d * m.ww;
}
