#include "window.h"
#include <sstream>
#include "common.h"
#include "config.h"
#include "log.h"
#include "opengl.h"

#ifdef VXRT_TARGET_WINDOWS
#  define GLCALLBACK __stdcall
#else
#  define GLCALLBACK
#endif

// OpenGL debug callback.
void GLCALLBACK glDebugCallback(GLenum, GLenum, GLuint, GLenum severity, GLsizei, GLchar const* msg, void const*) {
  std::string s = "OpenGL debug message: " + std::string(msg);
  switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH: Log::error(s); break;
    case GL_DEBUG_SEVERITY_MEDIUM: Log::warning(s); break;
    case GL_DEBUG_SEVERITY_LOW: Log::info(s); break;
    case GL_DEBUG_SEVERITY_NOTIFICATION: Log::verbose(s); break;
  }
}

Window::Window(
  std::string const& title,
  size_t width,
  size_t height,
  size_t multisample,
  bool forceMinimumVersion,
  bool debugContext
):
  mTitle(title),
  mWidth(width),
  mHeight(height) {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

  Log::info("Creating SDL window with OpenGL core context...");
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  if (multisample > 1) {
    Log::info("- with multisampling buffers");
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, static_cast<int>(multisample));
  }
  if (forceMinimumVersion) {
    Log::info("- with minimum required GL version (4.3)");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  }
  if (debugContext) {
    Log::info("- with debug mode context");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
  }

  mWindow = SDL_CreateWindow(
    mTitle.c_str(),
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    static_cast<int>(mWidth),
    static_cast<int>(mHeight),
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
  );
  if (!mWindow) {
    Log::fatal("Failed to create SDL window.");
    assert(false);
    return;
  }

  mContext = SDL_GL_CreateContext(mWindow);
  if (!mContext) {
    Log::fatal("Failed to create OpenGL context.");
    assert(false);
    return;
  }

  makeCurrent();
  SDL_GL_SetSwapInterval(0);
  mGL.emplace(mContext);

  if (debugContext) {
    if (GLEW_ARB_debug_output) {
      glDebugMessageCallbackARB(&glDebugCallback, nullptr);
      Log::info("GL_ARB_debug_output enabled.");
    } else {
      Log::warning("GL_ARB_debug_output not supported, OpenGL debug mode disabled.");
    }
  }
}

Window::~Window() {
  SDL_DestroyWindow(mWindow);
  SDL_GL_DeleteContext(mContext);
  SDL_Quit();
}

void Window::pollEvents() {
  // Update mouse state.
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
      case SDL_QUIT: mShouldQuit = true; break;
      case SDL_WINDOWEVENT:
        switch (e.window.event) {
          case SDL_WINDOWEVENT_RESIZED:
          case SDL_WINDOWEVENT_SIZE_CHANGED:
            mWidth = static_cast<size_t>(e.window.data1);
            mHeight = static_cast<size_t>(e.window.data2);
            break;
        }
        break;
    }
  }
}
