#include "shaderstorage.h"
#include <limits>

// GLuint index = glGetProgramResourceIndex(program.handle(), GL_SHADER_STORAGE_BLOCK, name.c_str());
// glShaderStorageBlockBinding(program.handle(), index, binding);

ShaderStorage::ShaderStorage(size_t size, bool persistent) {
  glGenBuffers(1, &mHandle);

  // Allocate persistently-mapped buffer or mutable buffer.
  if (persistent) {
    GLenum flags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER, size, nullptr, flags);
    mPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, size, flags | GL_MAP_FLUSH_EXPLICIT_BIT);
    mSize = size;
  } else if (size != 0) {
    reallocate(size);
  }
}

ShaderStorage::~ShaderStorage() noexcept {
  if (mHandle != OpenGL::null) glDeleteBuffers(1, &mHandle);
}

void ShaderStorage::reallocate(size_t size) {
  // Persistent buffers are immutable, so we must check.
  assert(!persistent());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
  glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_STATIC_DRAW);
  mSize = size;
}

void ShaderStorage::upload(size_t offset, size_t size, void const* data) {
  assert(offset + size <= mSize);
  if (persistent()) {
    std::copy(
      reinterpret_cast<char const*>(data),
      reinterpret_cast<char const*>(data) + size,
      reinterpret_cast<char*>(mPtr) + offset
    );
  } else {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
  }
}

void ShaderStorage::download(size_t offset, size_t size, void* data) const {
  assert(offset + size <= mSize);
  if (persistent()) {
    std::copy(
      reinterpret_cast<char*>(mPtr) + offset,
      reinterpret_cast<char*>(mPtr) + offset + size,
      reinterpret_cast<char*>(data)
    );
  } else {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
  }
}

void ShaderStorage::flush(size_t offset, size_t size) {
  assert(persistent());
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
  glFlushMappedBufferRange(GL_SHADER_STORAGE_BUFFER, offset, size);
}

void ShaderStorage::wait() {
  glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
  auto sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, std::numeric_limits<GLuint64>::max());
}
