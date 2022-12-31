#ifndef SHADERSTORAGEBUFFER_H_
#define SHADERSTORAGEBUFFER_H_

#include "shader.h"

class ShaderStorageBuffer {
public:
  ShaderStorageBuffer(ShaderProgram const& program, std::string const& name, GLuint binding);
  ShaderStorageBuffer(ShaderStorageBuffer&& r) noexcept: mHandle(std::exchange(r.mHandle, OpenGL::null)) {}
  ShaderStorageBuffer& operator=(ShaderStorageBuffer&& r) noexcept {
    swap(*this, r);
    return *this;
  }
  ~ShaderStorageBuffer();

  friend void swap(ShaderStorageBuffer& l, ShaderStorageBuffer& r) noexcept {
    using std::swap;
    swap(l.mHandle, r.mHandle);
  }

  void allocateImmutable(size_t size, bool staticDraw = false) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glBufferStorage(
      GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_STORAGE_BIT | (staticDraw ? 0u : GL_CLIENT_STORAGE_BIT)
    );
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

  void upload(size_t size, const void* data, bool staticDraw = false) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, staticDraw ? GL_STATIC_COPY : GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

  void uploadSubData(size_t offset, size_t size, const void* data) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

  void download(size_t size, void* data) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

  void downloadSubData(size_t offset, size_t size, void* data) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
  }

private:
  OpenGL::Object mHandle = OpenGL::null;
};

static_assert(std::move_constructible<ShaderStorageBuffer>);
static_assert(std::assignable_from<ShaderStorageBuffer&, ShaderStorageBuffer&&>);

#endif // SHADERSTORAGEBUFFER_H_
