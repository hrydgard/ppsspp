// Collision detection engine. You pass in a class representing a scene,
// and out comes a nice FPS-like "sliding physics" response.

// By hrydgard@gmail.com, 


#ifndef _COLLISION_H
#define _COLLISION_H

#include "math/lin/vec3.h"
#include "math/lin/matrix4x4.h"

class Collision;

class Collidable {
 public:
  Collidable();
  virtual ~Collidable();
  // Apply the collision functions to collision as appropriate.
  virtual void Collide(Collision *collision) = 0;
};

class Collision {
 public:
  void Init(const Vec3 &source, const Vec3 &dest, const Vec3 &size);

  // Collision functions
  // These should be called from Collidable::Collide and nowhere else
  // except the unit test.

  // To correctly collide with meshes, just apply these in sequence. You MUST
  // collide against all external (convex) edges, otherwise you can get
  // quite stuck. There's PLENTY of optimization to do here.

  bool Triangle(Vec3 p1, Vec3 p2, Vec3 p3);
  bool Triangle(Vec3 p1, Vec3 p2, Vec3 p3, const Vec3 &normal);
  bool EdgeLine(Vec3 p1, Vec3 p2);  // Cylinder
  bool Quad(const Vec3 &origin, const Vec3 &dX, const Vec3 &dY);
  bool UnitCube(const Vec3 &origin);
  bool Corner(Vec3 p1);   // Sphere
  bool Corners(Vec3 *p, int count);

  // There's an opportunity to provide hyper optimized versions of
  // Edge for axis aligned edges. The cylinder intersection becomes trivial.

  // These cannot be nested!
  void BeginTransform(const Matrix4x4 &inverse, const Matrix4x4 &transform);
  void EndTransform();

 private:
  bool registerCollision(float t, const Vec3 &polyIPoint);

 public:
	Vec3 sourcePoint;

  float velocityLength;

  Vec3 size;
  Vec3 scale;
	Vec3 velocity;  // data about player movement
  Vec3 normalizedVelocity;

	Vec3 lastSafePosition;  // for error handling  
	bool stuck;
	// data for collision response
	bool foundCollision;
	float nearestDistance;  // nearest distance to hit
	Vec3 nearestPolygonIntersectionPoint;  // on polygon

  const Matrix4x4 *transform;

  // Saved state during transformed operation
  Vec3 untransformed_sourcePoint;
  Vec3 untransformed_velocity;

  enum {
    MAX_CONTACT_POINTS = 10
  };
  Vec3 contact_points[MAX_CONTACT_POINTS];
  int num_contact_points;
};

// Sliding physics.
bool Collide(Collision *collision, Collidable *scene, Vec3 *out);

// Bouncy physics
// void CollideBouncy( ... )

#endif  // _COLLISION_H
