#ifndef OPENGL_H_
#define OPENGL_H_

#include <concepts>
#include <cstdint>
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include "common.h"
#include "vec.h"

class OpenGL {
public:
  using Object = GLuint;
  using ImageFormat = GLenum;
  using InternalFormat = GLenum;
  using Primitive = GLenum;
  using ShaderStage = GLenum;
  using UniformLocation = GLint;
  using Error = GLenum;

  // See: https://stackoverflow.com/a/18193631/12785135
  static constexpr Object null = 0;

  static constexpr Primitive points = GL_POINTS;
  static constexpr Primitive lines = GL_LINES;
  static constexpr Primitive lineStrip = GL_LINE_STRIP;
  static constexpr Primitive lineLoop = GL_LINE_LOOP;
  static constexpr Primitive triangles = GL_TRIANGLES;
  static constexpr Primitive triangleStrip = GL_TRIANGLE_STRIP;
  static constexpr Primitive triangleFan = GL_TRIANGLE_FAN;

  static constexpr ShaderStage vertexShader = GL_VERTEX_SHADER;
  static constexpr ShaderStage tessControlShader = GL_TESS_CONTROL_SHADER;
  static constexpr ShaderStage tessEvaluationShader = GL_TESS_EVALUATION_SHADER;
  static constexpr ShaderStage geometryShader = GL_GEOMETRY_SHADER;
  static constexpr ShaderStage fragmentShader = GL_FRAGMENT_SHADER;
  static constexpr ShaderStage computeShader = GL_COMPUTE_SHADER;

  static constexpr ImageFormat formatR = GL_RED;
  static constexpr ImageFormat formatRG = GL_RG;
  static constexpr ImageFormat formatRGB = GL_RGB;
  static constexpr ImageFormat formatRGBA = GL_RGBA;
  static constexpr InternalFormat internalFormatD = GL_DEPTH_COMPONENT;
  static constexpr InternalFormat internalFormatDS = GL_DEPTH_STENCIL;
  static constexpr InternalFormat internalFormatR = GL_RED;
  static constexpr InternalFormat internalFormatRG = GL_RG;
  static constexpr InternalFormat internalFormatRGB = GL_RGB;
  static constexpr InternalFormat internalFormatRGBA = GL_RGBA;

  OpenGL(SDL_GLContext context);
  OpenGL(OpenGL&&) = delete;
  OpenGL& operator=(OpenGL&&) = delete;

  void commit() { glFlush(); }
  void wait() { glFinish(); }
  Error checkError();

  void setDrawArea(int x, int y, int width, int height) { glViewport(x, y, width, height); }
  void setClearColor(Vec3f const& col, float alpha = 0.0f) { glClearColor(col.x, col.y, col.z, alpha); }
  void setClearDepth(float depth) { glClearDepth(depth); }
  void clear() { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT); }

private:
  SDL_GLContext mContext;
};

static_assert(!std::move_constructible<OpenGL>);
static_assert(!std::assignable_from<OpenGL&, OpenGL&&>);

#endif // OPENGL_H_
