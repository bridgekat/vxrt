#ifndef CAMERA_H_
#define CAMERA_H_

#include "vec.h"
#include "mat.h"
#include "window.h"

class Camera {
public:
	Vec3f position() const { return mPosition; }
	Vec3f rotation() const { return mRotation; }
	void setPosition(const Vec3f& pos) { mPosition = pos; }
	void setRotation(const Vec3f& rot) { mRotation = rot; }

	void setPerspective(float fov, float aspect, float zNear, float zFar) {
		mFOV = fov, mAspect = aspect, mNear = zNear, mFar = zFar;
	}

	Mat4f getProjectionMatrix() const {
		return Mat4f::perspective(mFOV, mAspect, mNear, mFar);
	}
	Mat4f getModelViewMatrix() const {
		Mat4f res(1.0f);
		res *= Mat4f::rotation(-mRotation.z, Vec3f(0.0f, 0.0f, 1.0f));
		res *= Mat4f::rotation(-mRotation.x, Vec3f(1.0f, 0.0f, 0.0f));
		res *= Mat4f::rotation(-mRotation.y, Vec3f(0.0f, 1.0f, 0.0f));
		res *= Mat4f::translation(-mPosition);
		return res;
	}
	
	void move(const Vec3d& d) { mPosition += d; }
	void moveOriented(const Vec3d& d, const Vec3i& mul = 1) {
		Vec3f dir = mRotation.compMult(mul);
		Mat4f trans(1.0f);
		trans *= Mat4f::rotation(dir.y, Vec3f(0.0f, 1.0f, 0.0f));
		trans *= Mat4f::rotation(dir.x, Vec3f(1.0f, 0.0f, 0.0f));
		trans *= Mat4f::rotation(dir.z, Vec3f(0.0f, 0.0f, 1.0f));
		Vec3d delta = trans.transform(Vec3f(d), 1.0f).first;
		mPosition += delta;
	}
	
	void update(const Window& win) {
		MouseState mouse = win.getMouseMotion();
		mRotation += Vec3f(-mouse.y * 0.3f, -mouse.x * 0.3f, 0.0f);
		if (Window::isKeyPressed(SDL_SCANCODE_W)) moveOriented(Vec3d(0.0, 0.0, -0.5), Vec3i(0, 1, 0));
		if (Window::isKeyPressed(SDL_SCANCODE_S)) moveOriented(Vec3d(0.0, 0.0, 0.5), Vec3i(0, 1, 0));
		if (Window::isKeyPressed(SDL_SCANCODE_A)) moveOriented(Vec3d(-0.5, 0.0, 0.0), Vec3i(0, 1, 0));
		if (Window::isKeyPressed(SDL_SCANCODE_D)) moveOriented(Vec3d(0.5, 0.0, 0.0), Vec3i(0, 1, 0));
		if (Window::isKeyPressed(SDL_SCANCODE_SPACE)) moveOriented(Vec3d(0.0, 0.5, 0.0), Vec3i(0, 1, 0));
		if (Window::isKeyPressed(SDL_SCANCODE_LCTRL)) moveOriented(Vec3d(0.0, -0.5, 0.0), Vec3i(0, 1, 0));
	}

private:
	Vec3f mPosition = 0.0f, mRotation = 0.0f;
	float mFOV = 0, mAspect = 0, mNear = 0, mFar = 0;
};

#endif

