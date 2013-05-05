#include "math/lin/aabb.h"
#include "math/lin/ray.h"
#include "math/lin/plane.h"

#define NUMDIM	3
#define RIGHT	0
#define LEFT	1
#define MIDDLE	2

static const float flt_plus_inf = -logf(0);	// let's keep C and C++ compilers happy.

AABB::AABB() : minB(0,0,0),maxB(0,0,0) {

}

void AABB::Add(const Vec3 &pt) {
	for (int i=0; i<3; i++)
	{
		if (pt[i] < minB[i])
			minB[i] = pt[i];
		if (pt[i] > maxB[i])
			maxB[i] = pt[i];
	}
}

bool AABB::Contains(const Vec3 &pt) const {
	for (int i=0; i<3; i++)
	{
		if (pt[i] < minB[i])
			return false;
		if (pt[i] > maxB[i])
			return false;
	}
	return true;
}


bool AABB::IntersectRay(const Ray &ray, float &tnear, float &tfar) const {
	float tNear=-flt_plus_inf, tFar=flt_plus_inf;
//For each pair of planes P associated with X, Y, and Z do:
//(example using X planes)
	for (int i=0; i<3; i++)
	{
		float min = minB[i];
		float max = maxB[i];

		if (ray.dir[i] == 0.0f) //parallell! ARGH! 
			if (ray.origin[i] < min || ray.origin[i] > max)
				return false;
		//Intersect with slab
		float T1 = (min - ray.origin[i]) * ray.invdir[i];
		float T2 = (max - ray.origin[i]) * ray.invdir[i];
		//Swap if necessary
		if (T1 > T2)
		{
			float temp=T1; T1=T2; T2=temp;
		}
		//Save closest/farthest hits
		if (T1 > tNear) tNear = T1;
		if (T2 < tFar)  tFar = T2;
	}

	if (tNear > tFar)
		return false; //missed the box
	else
	{
		if (tFar < 0)
			return false; //behind camera
		tnear = tNear;
		tfar = tFar;
	}
	return true;
}


// Possible orientation of the splitting plane in the interior node of the kd-tree, 
// "No axis" denotes a leaf.
enum Axes
{
	Xaxis, 
	Yaxis, 
	Zaxis, 
	Noaxis
};



int AABB::GetShortestAxis() const
{
	Vec3 delta = maxB-minB;
	if (delta.y<delta.x)
	{
		if (delta.z<delta.y)
			return 2;
		else 
			return 1;
	}
	else
	{
		if (delta.z<delta.x)
			return 2;
		else
			return 0;
	}
}


int AABB::GetLongestAxis() const
{
	Vec3 delta = maxB-minB;
	if (fabs(delta.y)>fabs(delta.x))
	{
		if (fabs(delta.z)>fabs(delta.y))
			return 2;
		else 
			return 1;
	}
	else
	{
		if (fabs(delta.z)>fabs(delta.x))
			return 2;
		else
			return 0;
	}
}



inline void FINDMINMAX(float x0, float x1, float x2, float &min, float &max )
{
	min = max = x0; 
	if(x1<min) 
		min=x1; 
	if(x1>max) 
		max=x1; 
	if(x2<min) 
		min=x2; 
	if(x2>max) 
		max=x2;
}

// X-tests
#define AXISTEST_X01( a, b, fa, fb )											\
	p0 = a * v0[1] - b * v0[2], p2 = a * v2[1] - b * v2[2]; \
    if (p0 < p2) { min = p0; max = p2;} else { min = p2; max = p0; }			\
	rad = fa * a_BoxHalfsize[1] + fb * a_BoxHalfsize[2];				\
	if (min > rad || max < -rad) return 0;

#define AXISTEST_X2( a, b, fa, fb )												\
	p0 = a * v0[1] - b * v0[2], p1 = a * v1[1] - b * v1[2];	\
    if (p0 < p1) { min = p0; max = p1; } else { min = p1; max = p0;}			\
	rad = fa * a_BoxHalfsize[1] + fb * a_BoxHalfsize[2];				\
	if(min>rad || max<-rad) return 0;
// Y-tests
#define AXISTEST_Y02( a, b, fa, fb )											\
	p0 = -a * v0[0] + b * v0[2], p2 = -a * v2[0] + b * v2[2]; \
    if(p0 < p2) { min = p0; max = p2; } else { min = p2; max = p0; }			\
	rad = fa * a_BoxHalfsize[0] + fb * a_BoxHalfsize[2];				\
	if (min > rad || max < -rad) return 0;
#define AXISTEST_Y1( a, b, fa, fb )												\
	p0 = -a * v0[0] + b * v0[2], p1 = -a * v1[0] + b * v1[2]; \
    if (p0 < p1) { min = p0; max = p1; } else { min = p1; max = p0; }			\
	rad = fa * a_BoxHalfsize[0] + fb * a_BoxHalfsize[2];				\
	if (min > rad || max < -rad) return 0;
// Z-tests
#define AXISTEST_Z12( a, b, fa, fb )											\
	p1 = a * v1[0] - b * v1[1], p2 = a * v2[0] - b * v2[1]; \
    if(p2 < p1) { min = p2; max = p1; } else { min = p1; max = p2; }			\
	rad = fa * a_BoxHalfsize[0] + fb * a_BoxHalfsize[1];				\
	if (min > rad || max < -rad) return 0;
#define AXISTEST_Z0( a, b, fa, fb )												\
	p0 = a * v0[0] - b * v0[1], p1 = a * v1[0] - b * v1[1];	\
    if(p0 < p1) { min = p0; max = p1; } else { min = p1; max = p0; }			\
	rad = fa * a_BoxHalfsize[0] + fb * a_BoxHalfsize[1];				\
	if (min > rad || max < -rad) return 0;

bool PlaneBoxOverlap(const Vec3& a_Normal, const Vec3& a_Vert, const Vec3& a_MaxBox )
{
	Vec3 vmin, vmax;
	for (int q = 0; q < 3; q++)
	{
		float v = a_Vert[q];
		if (a_Normal[q] > 0.0f)
		{
			vmin[q] = -a_MaxBox[q] - v;
			vmax[q] =  a_MaxBox[q] - v;
		}
		else
		{
			vmin[q] =  a_MaxBox[q] - v;
			vmax[q] = -a_MaxBox[q] - v;
		}
	}
	if (( a_Normal * vmin) > 0.0f) 
		return false;
	if (( a_Normal * vmax) >= 0.0f) 
		return true;

	return false;
}


bool AABB::IntersectsTriangle(const Vec3& a_V0, const Vec3& a_V1, const Vec3& a_V2 ) const
{
	Vec3 a_BoxCentre = GetMidpoint();
	Vec3 a_BoxHalfsize = GetExtents() / 2.0f;
	Vec3 v0, v1, v2, normal, e0, e1, e2;
	float min, max, p0, p1, p2, rad, fex, fey, fez;
	v0 = a_V0 - a_BoxCentre;
	v1 = a_V1 - a_BoxCentre;
	v2 = a_V2 - a_BoxCentre;
	e0 = v1 - v0, e1 = v2 - v1, e2 = v0 - v2;
	fex = fabs(e0[0]);
	fey = fabs(e0[1]);
	fez = fabs(e0[2]);
	AXISTEST_X01( e0[2], e0[1], fez, fey );
	AXISTEST_Y02( e0[2], e0[0], fez, fex );
	AXISTEST_Z12( e0[1], e0[0], fey, fex );
	fex = fabs(e1[0]);
	fey = fabs(e1[1]);
	fez = fabs(e1[2]);
	AXISTEST_X01( e1[2], e1[1], fez, fey );
	AXISTEST_Y02( e1[2], e1[0], fez, fex );
	AXISTEST_Z0 ( e1[1], e1[0], fey, fex );
	fex = fabs(e2[0]);
	fey = fabs(e2[1]);
	fez = fabs(e2[2]);
	AXISTEST_X2 ( e2[2], e2[1], fez, fey );
	AXISTEST_Y1 ( e2[2], e2[0], fez, fex );
	AXISTEST_Z12( e2[1], e2[0], fey, fex );
	FINDMINMAX( v0[0], v1[0], v2[0], min, max );
	if (min > a_BoxHalfsize[0] || max < -a_BoxHalfsize[0]) 
		return false;
	FINDMINMAX( v0[1], v1[1], v2[1], min, max );
	if (min > a_BoxHalfsize[1] || max < -a_BoxHalfsize[1]) 
		return false;
	FINDMINMAX( v0[2], v1[2], v2[2], min, max );
	if (min > a_BoxHalfsize[2] || max < -a_BoxHalfsize[2]) 
		return false;
	normal = e0 % e1;
	if (!PlaneBoxOverlap( normal, v0, a_BoxHalfsize )) 
		return false;
	return true;
}


bool AABB::BehindPlane(const Plane &plane) const {
  if (plane.Distance(minB) > 0)
    return false;
  if (plane.Distance(maxB) > 0)
    return false;

  if (plane.Distance(maxB.x, minB.y, minB.z) > 0)
    return false;
  if (plane.Distance(maxB.x, maxB.y, minB.z) > 0)
    return false;
  if (plane.Distance(maxB.x, minB.y, maxB.z) > 0)
    return false;

  if (plane.Distance(minB.x, maxB.y, minB.z) > 0)
    return false;
  if (plane.Distance(minB.x, minB.y, maxB.z) > 0)
    return false;
  if (plane.Distance(minB.x, maxB.y, maxB.z) > 0)
    return false;

  return true;
}
