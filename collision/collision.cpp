#include "base/logging.h"
#include "math/lin/vec3.h"
#include "ray_intersections.h"
#include "collision.h"

#include <string>

// #define COLLISION_ENABLE_SCALING

static std::string V(const Vec3 &v) {
  char temp[100];
  sprintf(temp, "(%f %f %f)", v.x, v.y, v.z);
  return std::string(temp);
}

Collidable::Collidable() {
}

Collidable::~Collidable() {
}

void Collision::Init(const Vec3 &source, const Vec3 &dest, const Vec3 &size) {
  this->size = size;
  scale.Set(1.0f / size.x, 1.0f / size.y, 1.0f / size.z);
  sourcePoint = source.scaledBy(scale);

  velocity = (dest - source).scaledBy(scale);
  velocityLength = velocity.length();
  normalizedVelocity = velocity / velocityLength;

  stuck = false;
  foundCollision = false;
  nearestDistance = velocityLength;
}

void Collision::BeginTransform(const Matrix4x4 &inverse, const Matrix4x4 &transform) {
  this->transform = &transform;
  untransformed_sourcePoint = sourcePoint;
  untransformed_velocity = velocity;

 	sourcePoint = sourcePoint * inverse;
	velocity = velocity.rotatedBy(inverse);

  normalizedVelocity = untransformed_velocity.normalized();
}

void Collision::EndTransform() {
  sourcePoint = untransformed_sourcePoint;
  velocity = untransformed_velocity;

  normalizedVelocity = untransformed_velocity.normalized();
  this->transform = 0;
}

bool Collision::registerCollision(float t, const Vec3 &polyIPoint) {
  // Special handling for multiple contact points
  if (t < 0.0f) {
    return false;
  }
  Vec3 ipoint;
  if (transform) ipoint = polyIPoint * (*transform);
  else ipoint = polyIPoint;

  if (t >= -0.0001 && t <= 0.0001 && num_contact_points < MAX_CONTACT_POINTS) {
    for (int i = 0; i < num_contact_points; i++) {
      // Check if identical - can happen on dupe edges (which we'll later remove).
      // TODO: Let's see if we can get rid of this fuzziness.
      if (ipoint.distance2To(contact_points[i]) < 0.00001)
        goto skip;
    }
    contact_points[num_contact_points++] = ipoint;
  }
skip:
  if ((t >= 0.0) && (t <= velocityLength) && (t <= this->nearestDistance)) {
    // If we are hit we have a closest hit so far. We save the information
    this->nearestDistance = t;
    this->nearestPolygonIntersectionPoint = ipoint;
    this->foundCollision = true;
    //ILOG("Collision registered, t=%f", t);
    return true;
  } else {
    // Already had a closer collision.
    return false;
  }
}

bool Collision::Triangle(Vec3 p1, Vec3 p2, Vec3 p3) {
  Vec3 pNormal = (p2 - p1) % (p3 - p1);
  pNormal.normalize();
  return Triangle(p1, p2, p3, pNormal);
}

bool Collision::Triangle(Vec3 p1, Vec3 p2, Vec3 p3, const Vec3 &pNormal) {
#ifdef COLLISION_ENABLE_SCALING
  p1.scaleBy(scale);
  p2.scaleBy(scale);
  p3.scaleBy(scale);
#endif

  // Backface
  // Early out optimization, lose a square root
  if (dot(velocity, pNormal) >= 0.0f) 
    return false;
  
  // ILOG("normal. %s", V(pNormal).c_str());
  // find the plane intersection point
  float t;
  Vec3 polyIPoint, sIPoint = sourcePoint - pNormal;
  if (pointPlaneDistance(sIPoint, p1, pNormal) > 0.0f) {
    // Plane is embedded in ellipsoid.
    // Find plane intersection point by shooting a ray from the sphere intersection
    // point along the plane normal.
    t = intersectRayPlanePerp(sIPoint, pNormal, p1);
    polyIPoint = sIPoint + pNormal * t; // calculate plane intersection point
  } else {
    // shoot ray along the velocity vector
    t = intersectRayPlane(sIPoint, normalizedVelocity, p1, pNormal);
    if (t > velocityLength) {
      // Did not hit plane. No point in doing triangle tests.
      return false;
    }
    // calculate plane intersection point
    polyIPoint = sIPoint + normalizedVelocity * t;
  }
  
  if (isPointInTriangle(polyIPoint, p1, p2, p3)) {
    // ILOG("Hit triangle! %s %s %s %s", V(polyIPoint).c_str(), V(p1).c_str(), V(p2).c_str(), V(p3).c_str());
    float depth2 = (polyIPoint - sourcePoint).length2();
    if (depth2 < 0.9999f) {
      // Here we do the error checking to see if we got ourself stuck last frame
      ELOG("Stuck in triangle! depth2 = %f", depth2);
      // ILOG("%s %s %s", V(p1).c_str(), V(p2).c_str(), V(p3).c_str());
      stuck = true;
    }
    return registerCollision(t, polyIPoint);
  } else {
    // ILOG("Missed this triangle - returning");
    return false;
  }
}

bool Collision::EdgeLine(Vec3 p1, Vec3 p2) {
  // This ellipsoid scaling stuff will result in the edge not being really cylindric.
  // Should work for many purposes though.
#ifdef COLLISION_ENABLE_SCALING
  p1.scaleBy(scale);
  p2.scaleBy(scale);
#endif
  float t = 1000000000.0;
  Vec3 polyIPoint;
  if (intersectCylinder(sourcePoint, normalizedVelocity,
                        p1, p2, &t, &polyIPoint)) {
    polyIPoint = closestPointOnLine(p1, p2, polyIPoint);
    return registerCollision(t, polyIPoint);
  } else {
    return false;
  }
}

bool Collision::Corner(Vec3 p1) {
#ifdef COLLISION_ENABLE_SCALING
  p1.scaleBy(scale);
#endif
  float t = intersectRayUnitSphere(sourcePoint, normalizedVelocity, p1);
  if (t != -1.0f) {
    return registerCollision(t, p1);
  } else {
    return false;
  }
}

bool Collision::Quad(const Vec3 &o, const Vec3 &dx, const Vec3 &dy) {
  bool hit = Triangle(o, o + dx, o + dx + dy);
  hit |= Triangle(o + dx + dy, o + dy, o);
  return hit;
}

bool Collide(Collision *collision, Collidable *scene, Vec3 *out) {
  bool hit_anything = false;
  collision->num_contact_points = 0;
  collision->lastSafePosition = collision->sourcePoint;
  // Process max 8 slides to have a limit if we get into an impossible situation.
  int i = 0;
  for (i = 0; i < 4; i++) {
    // ILOG("Try %i! %s", i, V(collision->sourcePoint).c_str());
    scene->Collide(collision);
    if (!collision->foundCollision) {
      // Nothing left to do.
      collision->sourcePoint += collision->velocity;
      collision->velocity.setZero();
      break;
    }
    hit_anything = true;
    if (collision->nearestDistance < 0.0f) {
      ILOG("NegHit! %f %f", collision->velocityLength, collision->nearestDistance);
    } else {
      // ILOG("Hit! %f %f ", collision->velocityLength, collision->nearestDistance);
    }
    
    Vec3 slidePlaneOrigin;
    Vec3 slidePlaneNormal;
    Vec3 newSourcePoint = collision->sourcePoint;

    float adjusted_hit_distance = collision->nearestDistance - 0.0001f;
    if (adjusted_hit_distance < 0) adjusted_hit_distance = 0;

    // Two cases - one where we have multiple contact points, one where we don't.
    if (collision->num_contact_points > 1) {
      ILOG("Resolve %i-ary collision, velocityLength = %f",
          collision->num_contact_points, collision->velocity.length());
      Vec3 avg_contact(0,0,0);
      for (int i = 0; i < collision->num_contact_points; i++) {
        ILOG("Contact point %s", V(collision->contact_points[i]).c_str());
        avg_contact += collision->contact_points[i];
      }
      avg_contact /= (float)collision->num_contact_points;

      // The average of the contact points should be inside the sphere.
      float dist = sqrt(avg_contact.distance2To(collision->sourcePoint));
      if (dist > 1.0) {
        ILOG("Bogus contact points, for sure: %s vs %s, dist=%f", V(avg_contact).c_str(), V(collision->sourcePoint).c_str(), dist);
      }

      Vec3 avg_contact_dir = avg_contact - collision->sourcePoint;
      avg_contact_dir.normalize();
      avg_contact = collision->sourcePoint + avg_contact_dir;

      newSourcePoint += collision->normalizedVelocity * adjusted_hit_distance;
      slidePlaneOrigin = avg_contact;
      slidePlaneNormal = (newSourcePoint - slidePlaneOrigin).normalized();

      // Push the ball out a bit along the normal to avoid re-collision.
      newSourcePoint += slidePlaneNormal * 0.01f;
      //collision->velocity += slidePlaneNormal * 0.01;

      //slidePlaneNormal = avg_contact_dir;
    } else {
      // LOG(INFO) << "Adjusting newsourcepoint to end of collision";
      newSourcePoint += collision->normalizedVelocity * adjusted_hit_distance;
      //  LOG(INFO) << "s: " << V(collision->sourcePoint);
      //  LOG(INFO) << "nsp: " << V(newSourcePoint);
      //  LOG(INFO) << "ip: " << V(collision->nearestPolygonIntersectionPoint);

      // Now we must calculate the sliding plane
      slidePlaneOrigin = collision->nearestPolygonIntersectionPoint;
      slidePlaneNormal = (newSourcePoint - slidePlaneOrigin).normalized();
    }

    // We now project the destination point onto the sliding plane
    Vec3 destinationPoint = collision->sourcePoint + collision->velocity;
    float l = intersectRayPlane(destinationPoint, slidePlaneNormal,
                                slidePlaneOrigin, -slidePlaneNormal); 

    // We can now calculate a new destination point on the sliding plane
    Vec3 newDestinationPoint = destinationPoint + slidePlaneNormal * l;
    
    // now we start over with the new position and velocity 

    collision->sourcePoint = newSourcePoint;
    // Generate the slide vector, which will become our new velocity vector
    Vec3 slide = newDestinationPoint - slidePlaneOrigin;
    slide += slidePlaneNormal * 0.0001f;
    
   // LOG(INFO) << "NDP: " << V(newDestinationPoint);
   // ILOG("Slide: %s", V(slide).c_str());
    // Recompute to get ready!
    collision->velocity = slide;
    collision->velocityLength = collision->velocity.length();
    if (collision->velocityLength <= 0.0002) {
      //ILOG("Zero-length velocity vector. Break.");
      collision->velocity.setZero();
      break;
    } else {
      // ILOG("Velocity: %s", V(collision->velocity).c_str());
    }
    collision->nearestDistance = collision->velocityLength;
    collision->normalizedVelocity = collision->velocity / collision->velocityLength;
  }
  //ILOG("%i tries", i);

  *out = collision->sourcePoint.scaledBy(collision->size);
  if (collision->stuck) {
    //ILOG("Stuck - resetting");
    // *out = collision->lastSafePosition.scaledBy(collision->size);
  }
  return hit_anything;
}

