#include "config.h"
#include "window.h"
#include "renderer.h"

int main(){
	Config::load();
	
	Window& win = Window::getDefaultWindow("Voxel Ray-casting", 640, 480);
	
	if (!OpenGL::coreProfile()) {
		LogError("Program must run in OpenGL core profile!");
		return 0;
	}
	
	Renderer::init();
	Renderer::setClearColor(Vec3f(1.0f, 1.0f, 1.0f));
	
	while (!win.shouldQuit()) {
		win.swapBuffers();
		Renderer::clear();
		
		win.pollEvents();
	}
	
	Config::save();
	return 0;
}

