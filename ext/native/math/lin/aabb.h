#pragma once

#include "math/lin/vec3.h"

struct Ray;
class Plane;

struct AABB {
	Vec3 minB;
	int pad;
	Vec3 maxB;
	int pad2;

	AABB();
	bool Contains(const Vec3 &pt) const;
	bool IntersectRay(const Ray &ray, float &tnear, float &tfar) const;

	// Doesn't currently work.
	bool IntersectRay2(const Ray &ray, float &tnear, float &tfar) const;

	bool IntersectsTriangle(const Vec3& a_V0, const Vec3& a_V1, const Vec3& a_V2) const;
	void Add(const Vec3 &pt);
	int GetShortestAxis() const;
	int GetLongestAxis() const;

	float GetSize(int axis) const {
		return maxB[axis] - minB[axis];
	}
	float GetMidpoint(int axis) const {
		return (minB[axis] + maxB[axis]) / 2;
	}
	Vec3 GetMidpoint() const {
		return (minB + maxB) / 2;
	}
	Vec3 GetExtents() const {
		return maxB - minB;
	}

	bool BehindPlane(const Plane &plane) const;
};
