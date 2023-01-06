#ifndef TEXTUREARRAY_H_
#define TEXTUREARRAY_H_

#include <concepts>
#include <cstring>
#include <string>
#include "bitmap.h"
#include "log.h"
#include "opengl.h"

class TextureArray {
public:
  TextureArray();
  TextureArray(size_t size, size_t depth, OpenGL::InternalFormat internalFormat):
    TextureArray() {
    reallocate(size, depth, internalFormat);
  }
  TextureArray(TextureArray&& r):
    mHandle(std::exchange(r.mHandle, OpenGL::null)) {}
  TextureArray& operator=(TextureArray&& r) {
    swap(*this, r);
    return *this;
  }
  ~TextureArray();

  friend void swap(TextureArray& l, TextureArray& r) {
    using std::swap;
    swap(l.mHandle, r.mHandle);
  }

  OpenGL::Object handle() const { return mHandle; }

  GLint push() const {
    GLint prev;
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prev);
    glBindTexture(GL_TEXTURE_2D_ARRAY, mHandle);
    return prev;
  }
  static void pop(GLint prev) { glBindTexture(GL_TEXTURE_2D_ARRAY, prev); }
  static void select(size_t index) { glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index)); }

  void bindAt(size_t index) const {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index));
    glBindTexture(GL_TEXTURE_2D_ARRAY, mHandle);
  }

  // Invalidates existing and allocates new storage.
  void reallocate(size_t size, size_t depth, OpenGL::InternalFormat internalFormat);

private:
  OpenGL::Object mHandle = OpenGL::null;
};

static_assert(std::move_constructible<TextureArray>);
static_assert(std::assignable_from<TextureArray&, TextureArray&&>);

#endif // TEXTUREARRAY_H_
