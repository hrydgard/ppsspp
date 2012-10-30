#ifndef _PLANE_H
#define _PLANE_H

#include "math/lin/vec3.h"

class Matrix4x4;

class Plane {
public:
	float x, y, z, d;
	Plane() {}
	Plane(float x_, float y_, float z_, float d_)
			: x(x_), y(y_), z(z_), d(d_) { }
	~Plane() {}

	float Distance(const Vec3 &v) const {
		return x * v.x + y * v.y + z * v.z + d;
	}

	float Distance(float px, float py, float pz) const {
		return x * px + y * py + z * pz + d;
	}

	void Normalize() {
		float inv_length = sqrtf(x * x + y * y + z * z);
		x *= inv_length;
		y *= inv_length;
		z *= inv_length;
		d *= inv_length;
	}

	// Matrix is the inverse transpose of the wanted transform.
	// out cannot be equal to this.
	void TransformByIT(const Matrix4x4 &matrix, Plane *out);
};

#endif
