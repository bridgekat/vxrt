#include "opengl.h"
#include <cassert>
#include "config.h"
#include "log.h"

OpenGL::OpenGL(SDL_GLContext context): mContext(context) {
  // Initialise GLEW.
  if (glewInit() != GLEW_OK) {
    Log::fatal("Failed to initialize GLEW!");
    assert(false);
  }

  // Initialise some of the GL states to our default values.
  glEnable(GL_MULTISAMPLE);

  glFrontFace(GL_CCW);
  glCullFace(GL_BACK);
  glEnable(GL_CULL_FACE);

  glDepthFunc(GL_LEQUAL);
  glEnable(GL_DEPTH_TEST);

  // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // glEnable(GL_BLEND);

  setClearColor(Vec3f(0.0f, 0.0f, 0.0f));
  setClearDepth(1.0f);
}

OpenGL::Error OpenGL::checkError() {
  OpenGL::Error error = glGetError();
  if (error == GL_NO_ERROR) return error;
  std::string str = "";
  switch (error) {
    case GL_INVALID_ENUM: str = "GL_INVALID_ENUM"; break;
    case GL_INVALID_VALUE: str = "GL_INVALID_VALUE"; break;
    case GL_INVALID_OPERATION: str = "GL_INVALID_OPERATION"; break;
    case GL_INVALID_FRAMEBUFFER_OPERATION: str = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
    case GL_OUT_OF_MEMORY: str = "OpenGL error: GL_OUT_OF_MEMORY"; break;
    case GL_STACK_UNDERFLOW: str = "OpenGL error: GL_STACK_UNDERFLOW"; break;
    case GL_STACK_OVERFLOW: str = "OpenGL error: GL_STACK_OVERFLOW"; break;
  }
  Log::error("OpenGL error: " + str);
  return error;
}
