#include "texturearray.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include "common.h"
#include "log.h"

TextureArray::TextureArray() {
  glGenTextures(1, &mHandle);
  auto prev = push();
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  pop(prev);
}

TextureArray::~TextureArray() {
  if (mHandle != OpenGL::null) glDeleteTextures(1, &mHandle);
}

void TextureArray::reallocate(size_t size, size_t depth, OpenGL::InternalFormat internalFormat) {
  if (size != lowBit(size)) {
    Log::error("Trying to allocate image of non-power-of-two dimensions as texture.");
    assert(false);
    return;
  }
  auto prev = push();
  glTexImage3D(
    GL_TEXTURE_2D_ARRAY,
    0,
    internalFormat,
    static_cast<GLsizei>(size),
    static_cast<GLsizei>(size),
    static_cast<GLsizei>(depth),
    0,
    GL_RGB,
    GL_UNSIGNED_BYTE,
    nullptr
  );
  pop(prev);
}
