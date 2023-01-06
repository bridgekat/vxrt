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
  ~ShaderStage() noexcept;

  ShaderStage(ShaderStage&& r) noexcept:
    mStage(r.mStage),
    mHandle(std::exchange(r.mHandle, OpenGL::null)) {}

  ShaderStage& operator=(ShaderStage&& r) noexcept {
    swap(*this, r);
    return *this;
  }

  friend void swap(ShaderStage& l, ShaderStage& r) noexcept {
    using std::swap;
    swap(l.mStage, r.mStage);
    swap(l.mHandle, r.mHandle);
  }

  OpenGL::ShaderStage stage() const { return mStage; }
  OpenGL::Object handle() const { return mHandle; }

private:
  OpenGL::ShaderStage mStage;
  OpenGL::Object mHandle = OpenGL::null;
};

static_assert(std::move_constructible<ShaderStage>);
static_assert(std::assignable_from<ShaderStage&, ShaderStage&&>);

class ShaderProgram {
public:
  ShaderProgram(std::initializer_list<ShaderStage> stages);
  ~ShaderProgram() noexcept;

  ShaderProgram(ShaderProgram&& r) noexcept:
    mHandle(std::exchange(r.mHandle, OpenGL::null)) {}

  ShaderProgram& operator=(ShaderProgram&& r) noexcept {
    swap(*this, r);
    return *this;
  }

  friend void swap(ShaderProgram& l, ShaderProgram& r) noexcept {
    using std::swap;
    swap(l.mHandle, r.mHandle);
  }

  OpenGL::Object handle() const { return mHandle; }
  OpenGL::UniformLocation uniformLocation(std::string const& name) const;

  // `-1`s in uniform locations are silently ignored.
  // See: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glUniform.xhtml
#define L uniformLocation
  // clang-format off
  void uniformInt(std::string const& name, GLint value) const  { glUniform1i(L(name), value); }
  void uniformUInt(std::string const& name, GLuint value) const  { glUniform1ui(L(name), value); }
  void uniformBool(std::string const& name, bool value) const  { glUniform1i(L(name), value ? 1 : 0); }
  void uniformSampler(std::string const& name, GLint index) const  { glUniform1i(L(name), index); }
  void uniformSamplers(std::string const& name, size_t count, GLint const* indices) const  { glUniform1iv(L(name), count, indices); }
  void uniformImage(std::string const& name, GLint index) const  { glUniform1i(L(name), index); }
  void uniformImages(std::string const& name, size_t count, GLint const* indices) const  { glUniform1iv(L(name), count, indices); }
  void uniformFloat(std::string const& name, GLfloat x) const  { glUniform1f(L(name), x); }
  void uniformVec2(std::string const& name, GLfloat x, GLfloat y) const  { glUniform2f(L(name), x, y); }
  void uniformVec3(std::string const& name, GLfloat x, GLfloat y, GLfloat z) const  { glUniform3f(L(name), x, y, z); }
  void uniformVec4(std::string const& name, GLfloat x, GLfloat y, GLfloat z, GLfloat w) const  { glUniform4f(L(name), x, y, z, w); }
  void uniformMat4(std::string const& name, GLfloat const* p, bool transpose = true) const  { glUniformMatrix4fv(L(name), 1, transpose ? GL_TRUE : GL_FALSE, p); }
  // clang-format on
#undef L

  void use() const { glUseProgram(mHandle); }

private:
  OpenGL::Object mHandle = OpenGL::null;
};

static_assert(std::move_constructible<ShaderProgram>);
static_assert(std::assignable_from<ShaderProgram&, ShaderProgram&&>);

#endif // SHADER_H_
