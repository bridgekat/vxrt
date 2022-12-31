#ifndef FRAMEBUFFER_H_
#define FRAMEBUFFER_H_

#include <concepts>
#include <optional>
#include <utility>
#include <vector>
#include "opengl.h"

class FrameBuffer {
public:
  FrameBuffer(size_t width, size_t height, size_t colorAttachCount, bool depthAttach);
  FrameBuffer(FrameBuffer&& r):
    mWidth(r.mWidth),
    mHeight(r.mHeight),
    mSize(r.mSize),
    mHandle(std::exchange(r.mHandle, OpenGL::null)),
    mColorTextures(std::move(r.mColorTextures)),
    mDepthTexture(std::move(r.mDepthTexture)),
    mDepthRenderBuffer(std::move(r.mDepthRenderBuffer)) {}
  ~FrameBuffer();

  FrameBuffer& operator=(FrameBuffer&& r) {
    swap(*this, r);
    return *this;
  }

  friend void swap(FrameBuffer& l, FrameBuffer& r) {
    using std::swap;
    swap(l.mWidth, r.mWidth);
    swap(l.mHeight, r.mHeight);
    swap(l.mSize, r.mSize);
    swap(l.mHandle, r.mHandle);
    swap(l.mColorTextures, r.mColorTextures);
    swap(l.mDepthTexture, r.mDepthTexture);
    swap(l.mDepthRenderBuffer, r.mDepthRenderBuffer);
  }

  size_t width() { return mWidth; }
  size_t height() { return mHeight; }
  size_t size() { return mSize; }

  void bindBuffer(size_t index);
  void bindBufferRead(size_t index);
  void bindBuffers();

  static void unbind() {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, OpenGL::null);
    glDrawBuffer(GL_BACK);
  }

  static void unbindRead() {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, OpenGL::null);
    glReadBuffer(GL_BACK);
  }

  void bindColorTexturesAt(size_t startNumber) {
    for (size_t i = 0; i < mColorTextures.size(); i++) {
      glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + startNumber + i));
      glBindTexture(GL_TEXTURE_2D, mColorTextures[i]);
    }
  }

  void bindDepthTextureAt(size_t number) {
    if (mDepthTexture.has_value()) {
      glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + number));
      glBindTexture(GL_TEXTURE_2D, mDepthTexture.value());
    }
  }

  std::vector<OpenGL::Object> const& colorTextures() const { return mColorTextures; }
  std::optional<OpenGL::Object> const& depthTexture() const { return mDepthTexture; }

private:
  size_t mWidth;
  size_t mHeight;
  size_t mSize;

  OpenGL::Object mHandle = OpenGL::null;
  std::vector<OpenGL::Object> mColorTextures;
  std::optional<OpenGL::Object> mDepthTexture;
  std::optional<OpenGL::Object> mDepthRenderBuffer;
};

static_assert(std::move_constructible<FrameBuffer>);
static_assert(std::assignable_from<FrameBuffer&, FrameBuffer&&>);

#endif // FRAMEBUFFER_H_
