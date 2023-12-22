#ifndef FRAMEBUFFER_H_
#define FRAMEBUFFER_H_

#include <cassert>
#include <concepts>
#include <optional>
#include <utility>
#include <vector>
#include "opengl.h"

class FrameBuffer {
public:
  FrameBuffer(size_t width, size_t height, size_t colorAttachCount, bool depthAttach);
  ~FrameBuffer() noexcept;

  FrameBuffer(FrameBuffer&& r) noexcept:
      mWidth(r.mWidth),
      mHeight(r.mHeight),
      mSize(r.mSize),
      mHandle(std::exchange(r.mHandle, OpenGL::null)),
      mColorTextures(std::move(r.mColorTextures)),
      mDepthTexture(std::exchange(r.mDepthTexture, OpenGL::null)),
      mDepthRenderBuffer(std::exchange(r.mDepthRenderBuffer, OpenGL::null)) {}

  FrameBuffer& operator=(FrameBuffer&& r) noexcept {
    swap(*this, r);
    return *this;
  }

  friend void swap(FrameBuffer& l, FrameBuffer& r) noexcept {
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

  void bindBuffers();
  static void unbindBuffers() { glBindFramebuffer(GL_DRAW_FRAMEBUFFER, OpenGL::null); }
  void bindBufferForRead(size_t i);

  void bindColorTextureAt(size_t i, size_t index);
  void bindDepthTextureAt(size_t index);

  std::vector<OpenGL::Object> const& colorTextures() const { return mColorTextures; }
  OpenGL::Object depthTexture() const { return mDepthTexture; }

private:
  size_t mWidth;
  size_t mHeight;
  size_t mSize;

  OpenGL::Object mHandle = OpenGL::null;
  std::vector<OpenGL::Object> mColorTextures;
  OpenGL::Object mDepthTexture = OpenGL::null;
  OpenGL::Object mDepthRenderBuffer = OpenGL::null;
};

static_assert(std::move_constructible<FrameBuffer>);
static_assert(std::assignable_from<FrameBuffer&, FrameBuffer&&>);

#endif // FRAMEBUFFER_H_
