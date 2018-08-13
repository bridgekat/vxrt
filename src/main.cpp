#include <stdlib.h>
#include "config.h"
#include "window.h"
#include "renderer.h"
#include "vertexarray.h"
#include "camera.h"
#include "shaderbuffer.h"
#include "tree.h"
#include "framebuffer.h"
#include "updatescheduler.h"
#include "bitmap.h"

FrameBuffer fbo[2];
ShaderProgram presenter;
int fbWidthDefault = 0, fbHeightDefault = 0;
int fbWidth, fbHeight;

void enablePathTracing(Window& win) {
	Renderer::waitForComplete();
	if (fbWidthDefault <= 0 || fbHeightDefault <= 0) {
		fbWidth = win.getWidth();
		fbHeight = win.getHeight();
	} else {
		fbWidth = fbWidthDefault;
		fbHeight = fbHeightDefault;
	}
	fbo[0].create(fbWidth, fbHeight, 1, false);
	fbo[1].create(fbWidth, fbHeight, 1, false);
	Renderer::enableTexture2D();
	win.unlockCursor();
}

void disablePathTracing(Window& win) {
	Renderer::waitForComplete();
	fbo[0].destroy();
	fbo[1].destroy();
	Renderer::disableTexture2D();
	win.lockCursor();
}

int main(){
	Config::load();

	Window& win = Window::getDefaultWindow("vxrt", 852, 480);

	if (!OpenGL::coreProfile()) {
		LogError("Program must run in OpenGL core profile!");
		return 0;
	}

	if (!OpenGL::arbShaderStorageBufferObject()) {
		LogError("ARB_shader_storage_buffer_object is not supported!");
		return 0;
	}

	Renderer::init();
	presenter.loadShadersFromFile(std::string(ShaderPath) + "Present.vsh", std::string(ShaderPath) + "Present.fsh");

	fbWidthDefault = Config::getInt("PathTracing.DefaultRenderWidth", 0);
	fbHeightDefault = Config::getInt("PathTracing.DefaultRenderHeight", 0);
	Tree tree(Config::getInt("World.Size", 256), Config::getInt("World.Height", 256));
	tree.generate();

	ShaderBuffer ssbo(Renderer::shader(), "TreeData", 0);
	tree.upload(ssbo);

	Camera camera;
	win.lockCursor();

	UpdateScheduler frameCounterScheduler(1);
	int frameCounter = 0;

	bool pathTracing = false;

	while (!win.shouldQuit()) {
		Renderer::waitForComplete();
		Renderer::checkError();
		win.swapBuffers();

		int curr = frameCounter & 1;

		if (!pathTracing) {
			Renderer::setRenderArea(0, 0, win.getWidth(), win.getHeight());
		} else {
			fbo[curr].bind();
			fbo[curr ^ 1].bindColorTextures(0);
			Renderer::setRenderArea(0, 0, fbWidth, fbHeight);
		}
		Renderer::clear();

		Renderer::beginFinalPass();
		Renderer::setProjection(camera.getProjectionMatrix());
		Renderer::setModelview(camera.getModelViewMatrix());

		Renderer::shader().setUniform1i("RootSize", tree.size());
		Vec3f pos = camera.position();
		Renderer::shader().setUniform3f("CameraPosition", pos.x, pos.y, pos.z);
		Renderer::shader().setUniform1f("RandomSeed", float(rand()) / (RAND_MAX + 1.0f));
		Renderer::shader().setUniform1i("PathTracing", int(pathTracing));
		if (pathTracing) {
			Renderer::shader().setUniform1i("PrevFrame", 0);
			Renderer::shader().setUniform1i("SampleCount", frameCounter);
			Renderer::shader().setUniform1i("FrameWidth", fbWidth);
			Renderer::shader().setUniform1i("FrameHeight", fbHeight);
			Renderer::shader().setUniform1i("FrameBufferSize", fbo[curr].size());
		}

		VertexArray va(6, VertexFormat(0, 0, 0, 2));
		va.addVertex({-1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f,-1.0f});
		VertexBuffer(va, false).render();

		Renderer::endFinalPass();

		if (pathTracing) {
			fbo[curr].unbind();
			fbo[curr].bindColorTextures(0);
			presenter.bind();
			Renderer::clear();
			Renderer::setRenderArea(0, 0, win.getWidth(), win.getHeight());
			presenter.setUniform1i("Texture", 0);
			presenter.setUniform1i("FrameWidth", fbWidth);
			presenter.setUniform1i("FrameHeight", fbHeight);
			presenter.setUniform1i("FrameBufferSize", fbo[curr].size());
			VertexBuffer(va, false).render();
			presenter.unbind();
		}

		frameCounter++;
		frameCounterScheduler.refresh();
		while (!frameCounterScheduler.inSync()) {
			std::stringstream ss;
			if (!pathTracing) {
				ss << "Voxel Raytracing Test (Static SVO, " << frameCounter << " fps)";
				frameCounter = 0;
			} else {
				ss << "Voxel Pathtracing Test (Static SVO, " << frameCounter << " samples per pixel)";
			}
			Window::getDefaultWindow().setTitle(ss.str());
			frameCounterScheduler.increase();
		}

		win.pollEvents();
		
		static bool opressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_O)) {
			if (!opressed) {
				if (pathTracing) {
					fbo[curr].bindBufferRead(0);
					Bitmap bmp(fbWidth, fbHeight, Vec3i(0, 0, 0));
					glReadPixels(0, 0, fbWidth, fbHeight, GL_RGB, GL_UNSIGNED_BYTE, bmp.data);
					fbo[curr].unbindRead();
					std::stringstream ss, sss;
					ss << ScreenshotPath << UpdateScheduler::timeFromEpoch() << "-" << frameCounter << "spp.bmp";
					bmp.save(ss.str());
					sss << "Saved screenshot " << ss.str();
					LogInfo(sss.str());
				}
			}
			opressed = true;
		} else opressed = false;

		static bool lpressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_L)) {
			if (!lpressed) {
				static bool locked = true;
				locked = !locked;
				if (locked) win.lockCursor();
				else win.unlockCursor();
			}
			lpressed = true;
		} else lpressed = false;

		static bool upressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_U)) {
			if (!upressed) {
				static bool fullscreen = false;
				fullscreen = !fullscreen;
				if (fullscreen) win.setFullscreen(true);
				else win.setFullscreen(false);
			}
			upressed = true;
		} else upressed = false;

		static bool ppressed = false;
		if (Window::isKeyPressed(SDL_SCANCODE_P)) {
			if (!ppressed) {
				pathTracing = !pathTracing;
				if (pathTracing) enablePathTracing(win);
				else disablePathTracing(win);
				frameCounter = 0;
			}
			ppressed = true;
		} else ppressed = false;
		
		if (!pathTracing) {
			camera.setPerspective(70.0f, float(win.getWidth()) / float(win.getHeight()), 0.1f, 256.0f);
			camera.update(win);
		} else {
			camera.setPerspective(70.0f, float(fbWidth) / float(fbHeight), 0.1f, 256.0f);
		}

		if (Window::isKeyPressed(SDL_SCANCODE_ESCAPE)) break;
	}

	win.unlockCursor();

	Config::save();
	return 0;
}

