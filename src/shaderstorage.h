#ifndef SHADERSTORAGE_H_
#define SHADERSTORAGE_H_

#include <cassert>
#include <concepts>
#include "shader.h"

// A shader storage buffer that can optionally be persistently mapped.
class ShaderStorage {
public:
  ShaderStorage(size_t size = 0, bool persistent = false);

  ShaderStorage(ShaderStorage&& r) noexcept:
    mHandle(std::exchange(r.mHandle, OpenGL::null)),
    mPtr(std::exchange(r.mPtr, nullptr)),
    mSize(std::exchange(r.mSize, 0)) {}

  ShaderStorage& operator=(ShaderStorage&& r) noexcept {
    swap(*this, r);
    return *this;
  }

  ~ShaderStorage();

  friend void swap(ShaderStorage& l, ShaderStorage& r) noexcept {
    using std::swap;
    swap(l.mHandle, r.mHandle);
    swap(l.mPtr, r.mPtr);
    swap(l.mSize, r.mSize);
  }

  OpenGL::Object handle() const noexcept { return mHandle; }

  bool persistent() const noexcept { return mPtr != nullptr; }

  void* get() noexcept { return mPtr; }

  size_t size() const noexcept { return mSize; }

  void bindAt(size_t index) const noexcept {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(index), mHandle);
  }

  // Invalidates existing and allocates new storage. `this` must not be persistently mapped.
  void reallocate(size_t size) noexcept;

  // Uploads data, or writes data to persistently-mapped memory (does not flush).
  void upload(size_t offset, size_t size, void const* data) noexcept;

  // Downloads data, or copies data from persistently-mapped memory (does not wait).
  void download(size_t offset, size_t size, void* data) const noexcept;

  // Flushes changes to GPU. `this` must be persistently mapped.
  void flush(size_t offset, size_t size) noexcept;

  // Waits for all GPU operations to complete.
  static void wait() noexcept;

private:
  OpenGL::Object mHandle = OpenGL::null;
  void* mPtr = nullptr;
  size_t mSize = 0;
};

static_assert(std::move_constructible<ShaderStorage>);
static_assert(std::assignable_from<ShaderStorage&, ShaderStorage&&>);

#endif // SHADERSTORAGE_H_
