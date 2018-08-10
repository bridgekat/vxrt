#include "config.h"
#include "window.h"
#include "renderer.h"
#include "vertexarray.h"
#include "camera.h"
#include "shaderbuffer.h"
#include "tree.h"
#include "updatescheduler.h"

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
	
//	Tree tree(4096, 134217728);
	Tree tree(256, 524288);
	tree.generate();
	
	ShaderBuffer ssbo(Renderer::shader(), "TreeData");
	tree.upload(ssbo);
	
	Camera camera;
	win.lockCursor();
	
	UpdateScheduler fpsCounterScheduler(1);
	int fpsCounter = 0;
	
	while (!win.shouldQuit()) {
		Renderer::waitForComplete();
		win.swapBuffers();
		
		Renderer::setViewport(0, 0, win.getWidth(), win.getHeight());
		Renderer::clear();
		
		Renderer::beginFinalPass();
		Renderer::setProjection(camera.getProjectionMatrix());
		Renderer::setModelview(camera.getModelViewMatrix());
		
		Renderer::shader().setUniform1i("RootSize", tree.size());
		Vec3f pos = camera.position();
		Renderer::shader().setUniform3f("CameraPosition", pos.x, pos.y, pos.z);
		
		VertexArray va(6, VertexFormat(0, 0, 0, 2));
		va.addVertex({-1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f,-1.0f});
		VertexBuffer(va, false).render();
		
		Renderer::endFinalPass();
		
		fpsCounter++;
		fpsCounterScheduler.refresh();
		while (!fpsCounterScheduler.inSync()) {
			std::stringstream ss;
			ss << "Voxel Raytracing Test (Static SVO, " << fpsCounter << " fps)";
			Window::getDefaultWindow().setTitle(ss.str());
			fpsCounter = 0;
			fpsCounterScheduler.increase();
		}
		
		win.pollEvents();
		camera.setPerspective(70.0f, float(win.getWidth()) / float(win.getHeight()), 0.1f, 256.0f);
		camera.update(win);
		
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
		if (Window::isKeyPressed(SDL_SCANCODE_ESCAPE)) break;
	}
	
	win.unlockCursor();
	
	Config::save();
	return 0;
}

