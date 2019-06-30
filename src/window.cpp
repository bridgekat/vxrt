#include "window.h"
#include <sstream>
#include "opengl.h"
#include "logger.h"
#include "debug.h"
#include "common.h"
#include "config.h"

#ifdef VXRT_TARGET_WINDOWS
#	define GLCALLBACK __stdcall
#else
#   define GLCALLBACK
#endif

// OpenGL debug callback
void GLCALLBACK glDebugCallback(GLenum /*source*/, GLenum /*type*/, GLuint /*id*/, GLenum severity, GLsizei /*length*/, const GLchar* msg, const void* /*data*/) {
	if (severity != GL_DEBUG_SEVERITY_NOTIFICATION) {
		std::stringstream ss("OpenGL debug: ");
		ss << std::string(msg);
		LogVerbose(ss.str());
	}
}

Window::Window(const std::string& title, int width, int height):
	mTitle(title), mWidth(width), mHeight(height) {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	static bool forceVersion = Config::getInt("OpenGL.ForceMinimumVersion", 0) != 0;
	if (forceVersion) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	LogInfo("OpenGL profile: Core/ES");

	static bool openglDebug = Config::getInt("OpenGL.Debugging", 0) != 0;
	if (openglDebug) SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

	mWindow = SDL_CreateWindow(mTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, mWidth, mHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (mWindow == nullptr) {
		LogFatal("Failed to create SDL window!");
		Assert(false);
	}

	mContext = SDL_GL_CreateContext(mWindow);
	if (mContext == nullptr) {
		LogFatal("Failed to create OpenGL context!");
		Assert(false);
	}

	makeCurrent();
	OpenGL::init(true);
	SDL_GL_SetSwapInterval(0);

	if (openglDebug) {
		if (GLEW_ARB_debug_output) {
			glDebugMessageCallbackARB(&glDebugCallback, nullptr);
			LogInfo("GL_ARB_debug_output enabled.");
		} else LogWarning("GL_ARB_debug_output not supported, disabling OpenGL debugging.");
	}
}

Window::~Window() {
	SDL_DestroyWindow(mWindow);
	SDL_GL_DeleteContext(mContext);
	SDL_Quit();
}

void Window::pollEvents() {
	// Update mouse state
	// Relative mode: motion = mMouse.xy, position = [not available]
	// Absolute mode: motion = (mMouse.xy - mPrevMouse.xy), position = mMouse.xy
	if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
		Uint32 buttons = SDL_GetRelativeMouseState(&mMouse.x, &mMouse.y);
		mMouse.left = buttons & SDL_BUTTON_LEFT;
		mMouse.right = buttons & SDL_BUTTON_RIGHT;
		mMouse.mid = buttons & SDL_BUTTON_MIDDLE;
		mMouse.relative = true;
	} else {
		mPrevMouse = mMouse;
		Uint32 buttons = SDL_GetMouseState(&mMouse.x, &mMouse.y);
		mMouse.left = buttons & SDL_BUTTON_LEFT;
		mMouse.right = buttons & SDL_BUTTON_RIGHT;
		mMouse.mid = buttons & SDL_BUTTON_MIDDLE;
		if (mMouse.relative) mPrevMouse = mMouse;
		mMouse.relative = false;
	}

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			mShouldQuit = true;
			break;
		case SDL_WINDOWEVENT:
			switch (e.window.event) {
			case SDL_WINDOWEVENT_RESIZED:
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				mWidth = e.window.data1;
				mHeight = e.window.data2;
				break;
			}
			break;
		}
	}
}

