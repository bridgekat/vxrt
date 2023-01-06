#ifndef TEXTURE_H_
#define TEXTURE_H_

#include <concepts>
#include <cstring>
#include <string>
#include "bitmap.h"
#include "log.h"
#include "opengl.h"

class Texture {
public:
  Texture();
  Texture(size_t size, OpenGL::InternalFormat internalFormat): Texture() { reallocate(size, internalFormat); }
  Texture(Bitmap const& image, size_t levels = static_cast<size_t>(-1));
  Texture(Texture&& r): mHandle(std::exchange(r.mHandle, OpenGL::null)) {}
  Texture& operator=(Texture&& r) {
    swap(*this, r);
    return *this;
  }
  ~Texture();

  friend void swap(Texture& l, Texture& r) {
    using std::swap;
    swap(l.mHandle, r.mHandle);
  }

  OpenGL::Object handle() const { return mHandle; }

  GLint push() const {
    GLint prev;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glBindTexture(GL_TEXTURE_2D, mHandle);
    return prev;
  }
  static void pop(GLint prev) { glBindTexture(GL_TEXTURE_2D, prev); }
  static void select(size_t index) { glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index)); }

  void bindAt(size_t index) const {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index));
    glBindTexture(GL_TEXTURE_2D, mHandle);
  }

  // Invalidates existing and allocates new storage.
  void reallocate(size_t size, OpenGL::InternalFormat internalFormat);

  static size_t maxSize() {
    GLint res;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &res);
    return static_cast<size_t>(res);
  }

private:
  OpenGL::Object mHandle = OpenGL::null;
};

static_assert(std::move_constructible<Texture>);
static_assert(std::assignable_from<Texture&, Texture&&>);

#endif // TEXTURE_H_
