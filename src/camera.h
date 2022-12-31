#ifndef CAMERA_H_
#define CAMERA_H_

#include <concepts>
#include "mat.h"
#include "vec.h"
#include "window.h"

class Camera {
public:
  Vec3f position() const { return mPosition; }
  Vec3f rotation() const { return mRotation; }
  void setPosition(Vec3f const& value) { mPosition = value; }
  void setRotation(Vec3f const& value) { mRotation = value; }

  void setPerspective(float fov, float aspect, float zNear, float zFar) {
    mFov = fov;
    mAspect = aspect;
    mNear = zNear;
    mFar = zFar;
  }

  Mat4f getProjectionMatrix() const { return Mat4f::perspective(mFov, mAspect, mNear, mFar); }
  Mat4f getModelViewMatrix() const {
    Mat4f res(1.0f);
    res *= Mat4f::rotation(-mRotation.z, Vec3f(0.0f, 0.0f, 1.0f));
    res *= Mat4f::rotation(-mRotation.x, Vec3f(1.0f, 0.0f, 0.0f));
    res *= Mat4f::rotation(-mRotation.y, Vec3f(0.0f, 1.0f, 0.0f));
    // Preserve matrix inverse accuracy by applying translation in the shaders
    //		res *= Mat4f::translation(-mPosition);
    return res;
  }

  void move(Vec3d const& d) { mPosition += d; }
  void moveOriented(Vec3d const& d, Vec3i const& mul = 1) {
    Vec3f dir = mRotation.compMul(mul);
    Mat4f trans(1.0f);
    trans *= Mat4f::rotation(dir.y, Vec3f(0.0f, 1.0f, 0.0f));
    trans *= Mat4f::rotation(dir.x, Vec3f(1.0f, 0.0f, 0.0f));
    trans *= Mat4f::rotation(dir.z, Vec3f(0.0f, 0.0f, 1.0f));
    Vec3d delta = trans.transform(Vec3f(d), 1.0f).first;
    mPosition += delta;
  }

  void update(Window const& window) {
    MouseState mouse = window.mouseMotion();
    mRotation += Vec3f(-mouse.y * 0.3f, -mouse.x * 0.3f, 0.0f);
    double speed = Window::isKeyPressed(SDL_SCANCODE_TAB) ? 100.0 : 1.0;
    if (Window::isKeyPressed(SDL_SCANCODE_W)) moveOriented(Vec3d(0.0, 0.0, -0.5) * speed, Vec3i(0, 1, 0));
    if (Window::isKeyPressed(SDL_SCANCODE_S)) moveOriented(Vec3d(0.0, 0.0, 0.5) * speed, Vec3i(0, 1, 0));
    if (Window::isKeyPressed(SDL_SCANCODE_A)) moveOriented(Vec3d(-0.5, 0.0, 0.0) * speed, Vec3i(0, 1, 0));
    if (Window::isKeyPressed(SDL_SCANCODE_D)) moveOriented(Vec3d(0.5, 0.0, 0.0) * speed, Vec3i(0, 1, 0));
    if (Window::isKeyPressed(SDL_SCANCODE_SPACE)) moveOriented(Vec3d(0.0, 0.5, 0.0) * speed, Vec3i(0, 1, 0));
    if (Window::isKeyPressed(SDL_SCANCODE_LCTRL) || Window::isKeyPressed(SDL_SCANCODE_RCTRL) || Window::isKeyPressed(SDL_SCANCODE_LALT))
      moveOriented(Vec3d(0.0, -0.5, 0.0) * speed, Vec3i(0, 1, 0));
  }

private:
  Vec3f mPosition = 0.0f, mRotation = 0.0f;
  float mFov = 0, mAspect = 0, mNear = 0, mFar = 0;
};

static_assert(std::copy_constructible<Camera>);
static_assert(std::assignable_from<Camera&, Camera&>);

#endif // CAMERA_H_
