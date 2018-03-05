#ifndef CAMERA_H_
#define CAMERA_H_

#include "vec.h"
#include "mat.h"

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

private:
	Vec3f mPosition = 0.0f, mRotation = 0.0f;
	float mFOV = 0, mAspect = 0, mNear = 0, mFar = 0;
};

#endif

