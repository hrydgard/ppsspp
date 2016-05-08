#pragma once

#include "vec3.h"

/* __declspec(align(16)) */ struct Ray
{
	Vec3 origin;
	int pad;
	Vec3 dir;
	int pad2;
	Vec3 invdir;
	int pad3;
};

