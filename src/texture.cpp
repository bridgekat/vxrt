#include "texture.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include "common.h"
#include "log.h"

Texture::Texture() {
  glGenTextures(1, &mHandle);
  auto prev = push();
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  pop(prev);
}

Texture::Texture(Bitmap const& image, size_t levels) {
  OpenGL::ImageFormat imageFormat;
  OpenGL::InternalFormat internalFormat;

  if (image.bytesPerPixel() == 3) {
    imageFormat = OpenGL::formatRGB;
    internalFormat = OpenGL::internalFormatRGB;
  } else if (image.bytesPerPixel() == 4) {
    imageFormat = OpenGL::formatRGBA;
    internalFormat = OpenGL::internalFormatRGBA;
  } else {
    Log::error("Trying to load image of unsupported number of channels as texture.");
    assert(false);
    return;
  }

  size_t size = image.width();
  if (size != image.height() || size != lowBit(size)) {
    Log::error("Trying to load image of non-square or non-power-of-two dimensions as texture.");
    assert(false);
    return;
  }

  auto prev = push();
  levels = std::min(levels, ceilLog2(size));
  glGenTextures(1, &mHandle);
  glBindTexture(GL_TEXTURE_2D, mHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, static_cast<GLint>(levels));
  for (size_t i = 0, scale = 1; i <= levels; i++, scale *= 2) {
    assert(scale <= size);
    Bitmap curr = image.shrink(scale);
    glTexImage2D(
      GL_TEXTURE_2D,
      static_cast<GLint>(i),
      static_cast<GLint>(internalFormat),
      static_cast<GLsizei>(curr.width()),
      static_cast<GLsizei>(curr.height()),
      0,
      imageFormat,
      GL_UNSIGNED_BYTE,
      curr.data()
    );
  }
  pop(prev);
}

Texture::~Texture() noexcept {
  if (mHandle != OpenGL::null)
    glDeleteTextures(1, &mHandle);
}

void Texture::reallocate(size_t size, OpenGL::InternalFormat internalFormat) {
  if (size != lowBit(size)) {
    Log::error("Trying to allocate image of non-power-of-two dimensions as texture.");
    assert(false);
    return;
  }
  auto prev = push();
  glTexImage2D(
    GL_TEXTURE_2D,
    0,
    static_cast<GLint>(internalFormat),
    static_cast<GLsizei>(size),
    static_cast<GLsizei>(size),
    0,
    GL_RGB,
    GL_UNSIGNED_BYTE,
    nullptr
  );
  pop(prev);
}
