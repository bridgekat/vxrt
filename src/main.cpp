#include "config.h"
#include "window.h"
#include "renderer.h"
#include "vertexarray.h"

int main(){
	Config::load();
	
	Window& win = Window::getDefaultWindow("Voxel Ray-casting", 640, 480);
	
	if (!OpenGL::coreProfile()) {
		LogError("Program must run in OpenGL core profile!");
		return 0;
	}
	
	Renderer::init();
	
	while (!win.shouldQuit()) {
		win.swapBuffers();
		
		Renderer::setViewport(0, 0, win.getWidth(), win.getHeight());
		Renderer::clear();
		
		Renderer::beginFinalPass();
		Renderer::setProjection(Mat4f::perspective(70.0f, float(win.getWidth()) / float(win.getHeight()), 0.1f, 256.0f));
		Renderer::restoreModelview();
		
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
	}
	
	Config::save();
	return 0;
}

