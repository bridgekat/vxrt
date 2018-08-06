#include "config.h"
#include "window.h"
#include "renderer.h"
#include "vertexarray.h"
#include "camera.h"
#include "shaderbuffer.h"
#include "tree.h"

int main(){
	Config::load();
	
	Window& win = Window::getDefaultWindow("Static SVO Test", 640, 480);
	
	if (!OpenGL::coreProfile()) {
		LogError("Program must run in OpenGL core profile!");
		return 0;
	}
	
	if (!OpenGL::arbShaderStorageBufferObject()) {
		LogError("ARB_shader_storage_buffer_object not supported!");
		return 0;
	}
	
	Renderer::init();
	
	Tree tree;
	tree.generate();
	
	Camera camera;
	
	win.lockCursor();
	
	ShaderBuffer ssbo(Renderer::shader(), "TreeData");
	tree.update(ssbo);
	
	while (!win.shouldQuit()) {
		Renderer::waitForComplete();
		win.swapBuffers();
		
		Renderer::setViewport(0, 0, win.getWidth(), win.getHeight());
		Renderer::clear();
		
		Renderer::beginFinalPass();
		Renderer::setProjection(camera.getProjectionMatrix());
		Renderer::setModelview(camera.getModelViewMatrix());
		
		VertexArray va(6, VertexFormat(0, 0, 0, 2));
		va.addVertex({-1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({ 1.0f, 1.0f});
		va.addVertex({-1.0f,-1.0f});
		va.addVertex({ 1.0f,-1.0f});
		VertexBuffer(va, false).render();
		
		Renderer::endFinalPass();
		
		win.pollEvents();
		camera.setPerspective(70.0f, float(win.getWidth()) / float(win.getHeight()), 0.1f, 256.0f);
		camera.update(win);
		
		if (Window::isKeyPressed(SDL_SCANCODE_ESCAPE)) break;
	}
	
	win.unlockCursor();
	
	Config::save();
	return 0;
}

