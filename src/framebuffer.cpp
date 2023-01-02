#include "framebuffer.h"
#include <algorithm>
#include <vector>
#include "log.h"

FrameBuffer::FrameBuffer(size_t width, size_t height, size_t colorAttachCount, bool depthAttach):
  mWidth(width), mHeight(height), mSize(1u << ceilLog2(std::max(width, height))) {

  // Create framebuffer object.
  glGenFramebuffers(1, &mHandle);
  glBindFramebuffer(GL_FRAMEBUFFER, mHandle);

  if (depthAttach) {
    // Create depth texture and attach.
    mDepthTexture = 0;
    glGenTextures(1, &mDepthTexture);
    glBindTexture(GL_TEXTURE_2D, mDepthTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // See: https://www.khronos.org/opengl/wiki/Sampler_(GLSL)#Shadow_samplers
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, mSize, mSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthTexture, 0);
  } else {
    // Create depth renderbuffer and attach.
    mDepthRenderBuffer = 0;
    glGenRenderbuffers(1, &mDepthRenderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, mDepthRenderBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mSize, mSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthRenderBuffer);
  }

  // Create color textures and attach.
  mColorTextures.resize(colorAttachCount);
  glGenTextures(colorAttachCount, mColorTextures.data());
  for (size_t i = 0; i < colorAttachCount; i++) {
    glBindTexture(GL_TEXTURE_2D, mColorTextures[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, mSize, mSize, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mColorTextures[i], 0);
  }

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    Log::error("Error creating framebuffer.");
    assert(false);
  }

  // Switch back to default framebuffer.
  glBindFramebuffer(GL_FRAMEBUFFER, OpenGL::null);
}

FrameBuffer::~FrameBuffer() {
  if (mHandle != OpenGL::null) glDeleteFramebuffers(1, &mHandle);
  if (!mColorTextures.empty()) glDeleteTextures(mColorTextures.size(), mColorTextures.data());
  if (mDepthTexture != OpenGL::null) glDeleteTextures(1, &mDepthTexture);
  if (mDepthRenderBuffer != OpenGL::null) glDeleteRenderbuffers(1, &mDepthRenderBuffer);
}

void FrameBuffer::bindBuffers() {
  if (mColorTextures.empty()) {
    assert(false);
    return;
  }
  std::vector<GLenum> arr(mColorTextures.size());
  for (size_t i = 0; i < arr.size(); i++) arr[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mHandle);
  glDrawBuffers(arr.size(), arr.data());
}

void FrameBuffer::bindBufferForRead(size_t i) {
  if (i >= mColorTextures.size()) {
    assert(false);
    return;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, mHandle);
  glReadBuffer(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i));
}

void FrameBuffer::bindColorTextureAt(size_t i, size_t index) {
  if (i >= mColorTextures.size()) {
    assert(false);
    return;
  }
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index));
  glBindTexture(GL_TEXTURE_2D, mColorTextures[i]);
}

void FrameBuffer::bindDepthTextureAt(size_t index) {
  if (mDepthTexture == OpenGL::null) {
    assert(false);
    return;
  }
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + index));
  glBindTexture(GL_TEXTURE_2D, mDepthTexture);
}
