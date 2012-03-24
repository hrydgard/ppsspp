#ifndef _MATH_LIN_RAY_H
#define _MATH_LIN_RAY_H

#include "vec3.h"

namespace lin {

/* __declspec(align(16)) */ struct Ray
{
	Vec3 origin;
	int pad;
	Vec3 dir;
	int pad2;
	Vec3 invdir;
	int pad3;
};

}  // namespace lin

#endif  // _MATH_LIN_RAY_H
