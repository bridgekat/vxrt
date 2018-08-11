#ifndef RENDERER_H_
#define RENDERER_H_

#include "logger.h"
#include "opengl.h"
#include "vec.h"
#include "mat.h"
#include "vertexarray.h"
#include "shader.h"

class Renderer {
public:
	// Set up rendering. Must be called after OpenGL context is available!
	static void init();

	static void beginFinalPass() { mFinal.bind(); }
	static void endFinalPass() { mFinal.unbind(); }

	static void setRenderArea(int x, int y, int width, int height) {
		glViewport(x, y, width, height);
	}

	static void restoreProjection() {
		mProjection = Mat4f(1.0f);
		updateMatrices();
	}
	static void restoreModelview() {
		mModelview = Mat4f(1.0f);
		updateMatrices();
	}
	static void setProjection(const Mat4f& mat) {
		mProjection = mat;
		updateMatrices();
	}
	static void setModelview(const Mat4f& mat) {
		mModelview = mat;
		updateMatrices();
	}

	static void translate(const Vec3f& delta) {
		mModelview *= Mat4f::translation(delta);
		updateMatrices();
	}
	static void rotate(float degrees, const Vec3f& scale) {
		mModelview *= Mat4f::rotation(degrees, scale);
		updateMatrices();
	}
	static void applyPerspective(float fov, float aspect, float zNear, float zFar) {
		mProjection *= Mat4f::perspective(fov, aspect, zNear, zFar);
		updateMatrices();
	}
	static void applyOrtho(float left, float right, float top, float bottom, float zNear, float zFar) {
		mModelview *= Mat4f::ortho(left, right, top, bottom, zNear, zFar);
		updateMatrices();
	}

	static void enableTexture2D() {
		if (!OpenGL::coreProfile()) glEnable(GL_TEXTURE_2D);
		else glActiveTexture(GL_TEXTURE0);
	}
	static void disableTexture2D() {
		if (!OpenGL::coreProfile()) glDisable(GL_TEXTURE_2D);
	}
	static void enableDepthOverwrite() { glDepthFunc(GL_ALWAYS); }
	static void disableDepthOverwrite() { glDepthFunc(GL_LEQUAL); }
	static void enableDepthTest() { glEnable(GL_DEPTH_TEST); }
	static void disableDepthTest() { glDisable(GL_DEPTH_TEST); }
	static void enableCullFace() { glEnable(GL_CULL_FACE); }
	static void disableCullFace() { glDisable(GL_CULL_FACE); }
	static void enableBlend() { glEnable(GL_BLEND); }
	static void disableBlend() { glDisable(GL_BLEND); }

	static void setClearColor(const Vec3f& col, float alpha = 0.0f) { glClearColor(col.x, col.y, col.z, alpha); }
	static void setClearDepth(float depth) { glClearDepth(depth); }

	static void clear() { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
	static void flush() { glFlush(); }
	static void waitForComplete() { glFinish(); }
	static void checkError();
	
	static ShaderProgram& shader() { return mFinal; }

private:
	static int matrixMode;
	static Mat4f mProjection, mModelview;
	static ShaderProgram mFinal;

	static void updateMatrices() {
		if (!OpenGL::coreProfile()) {
			glMatrixMode(GL_PROJECTION);
			glLoadMatrixf(mProjection.getTranspose().data);
			glMatrixMode(GL_MODELVIEW);
			glLoadMatrixf(mModelview.getTranspose().data);
		} else {
			mFinal.bind();
			mFinal.setUniformMatrix4fv("ProjectionMatrix", mProjection.getTranspose().data);
			mFinal.setUniformMatrix4fv("ModelViewMatrix", mModelview.getTranspose().data);
			mFinal.setUniformMatrix4fv("ProjectionInverse", mProjection.getInverse().getTranspose().data);
			mFinal.setUniformMatrix4fv("ModelViewInverse", mModelview.getInverse().getTranspose().data);
		}
	}
};

#endif // !RENDERER_H_

