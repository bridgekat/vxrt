#ifndef CAMERA_H_
#define CAMERA_H_

#include <concepts>
#include "mat.h"
#include "vec.h"
#include "window.h"

class Camera {
public:
  Vec3f position = 0, rotation = 0;
  float fov = 0.0f, aspect = 0.0f, near = 0.0f, far = 0.0f;

  Mat4f projection() const { return Mat4f::perspective(fov, aspect, near, far); }
  Mat4f modelView() const {
    Mat4f res(1.0f);
    res *= Mat4f::rotation(-rotation.z, Vec3f(0.0f, 0.0f, 1.0f));
    res *= Mat4f::rotation(-rotation.x, Vec3f(1.0f, 0.0f, 0.0f));
    res *= Mat4f::rotation(-rotation.y, Vec3f(0.0f, 1.0f, 0.0f));
    // Preserve matrix inverse accuracy by applying translation in the shaders
    // res *= Mat4f::translation(-position);
    return res;
  }

  Vec3f transformedVelocity(Vec3f const& v, Vec3i const& mul = 1) {
    Vec3f dir = rotation.compMul(mul);
    Mat4f t = 1;
    t *= Mat4f::rotation(dir.y, Vec3f(0.0f, 1.0f, 0.0f));
    t *= Mat4f::rotation(dir.x, Vec3f(1.0f, 0.0f, 0.0f));
    t *= Mat4f::rotation(dir.z, Vec3f(0.0f, 0.0f, 1.0f));
    return t.transform(Vec3f(v), 1.0f).first;
  }

  static Camera lerp(Camera const& l, Camera const& r, float k) {
    return Camera{
      l.position * (1.0f - k) + r.position * k,
      l.rotation * (1.0f - k) + r.rotation * k,
      l.fov,
      l.aspect,
      l.near,
      l.far,
    };
  }
};

static_assert(std::copy_constructible<Camera>);
static_assert(std::assignable_from<Camera&, Camera&>);

#endif // CAMERA_H_
