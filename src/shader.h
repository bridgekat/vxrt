#ifndef SHADER_H_
#define SHADER_H_

#include <concepts>
#include <initializer_list>
#include <string>
#include <utility>
#include "opengl.h"

class ShaderStage {
public:
  ShaderStage(OpenGL::ShaderStage stage, std::string const& filename);
  ShaderStage(ShaderStage&& r) noexcept: mStage(r.mStage), mHandle(std::exchange(r.mHandle, OpenGL::null)) {}
  ShaderStage& operator=(ShaderStage&& r) noexcept {
    swap(*this, r);
    return *this;
  }
  ~ShaderStage();

  friend void swap(ShaderStage& l, ShaderStage& r) noexcept {
    using std::swap;
    swap(l.mStage, r.mStage);
    swap(l.mHandle, r.mHandle);
  }

  OpenGL::ShaderStage stage() const noexcept { return mStage; }
  OpenGL::Object handle() const noexcept { return mHandle; }

private:
  OpenGL::ShaderStage mStage;
  OpenGL::Object mHandle = OpenGL::null;
};

static_assert(std::move_constructible<ShaderStage>);
static_assert(std::assignable_from<ShaderStage&, ShaderStage&&>);

class ShaderProgram {
public:
  ShaderProgram(std::initializer_list<ShaderStage> stages);
  ShaderProgram(ShaderProgram&& r) noexcept: mHandle(std::exchange(r.mHandle, OpenGL::null)) {}
  ShaderProgram& operator=(ShaderProgram&& r) noexcept {
    swap(*this, r);
    return *this;
  }
  ~ShaderProgram();

  friend void swap(ShaderProgram& l, ShaderProgram& r) noexcept {
    using std::swap;
    swap(l.mHandle, r.mHandle);
  }

  OpenGL::Object handle() const noexcept { return mHandle; }
  OpenGL::UniformLocation uniformLocation(std::string const& name);

  // `-1`s in uniform locations are silently ignored.
  // See: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml
#define L uniformLocation
  void uniformInt(std::string const& name, int value) { glUniform1i(L(name), value); }
  void uniformUInt(std::string const& name, unsigned int value) { glUniform1ui(L(name), value); }
  void uniformBool(std::string const& name, bool value) { glUniform1i(L(name), value ? 1 : 0); }
  void uniformSampler(std::string const& name, size_t index) { glUniform1i(L(name), index); }
  void uniformImage(std::string const& name, size_t index) { glUniform1i(L(name), index); }
  void uniformFloat(std::string const& name, float x) { glUniform1f(L(name), x); }
  void uniformVec2(std::string const& name, float x, float y) { glUniform2f(L(name), x, y); }
  void uniformVec3(std::string const& name, float x, float y, float z) { glUniform3f(L(name), x, y, z); }
  void uniformVec4(std::string const& name, float x, float y, float z, float w) { glUniform4f(L(name), x, y, z, w); }
  void uniformMat4(std::string const& name, float* p, bool transpose = true) {
    glUniformMatrix4fv(L(name), 1, transpose ? GL_TRUE : GL_FALSE, p);
  }
#undef L

  void use() const { glUseProgram(mHandle); }

private:
  OpenGL::Object mHandle = OpenGL::null;
};

static_assert(std::move_constructible<ShaderProgram>);
static_assert(std::assignable_from<ShaderProgram&, ShaderProgram&&>);

#endif // SHADER_H_
