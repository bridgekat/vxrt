#include "framebuffer.h"
#include <algorithm>
#include <cassert>
#include <vector>
#include "log.h"

FrameBuffer::FrameBuffer(size_t width, size_t height, size_t colorAttachCount, bool depthAttach):
  mWidth(width), mHeight(height), mSize(1u << ceilLog2(std::max(width, height))) {

  // Create framebuffer object.
  glGenFramebuffers(1, &mHandle);
  glBindFramebuffer(GL_FRAMEBUFFER, mHandle);

  if (depthAttach) {
    // Create depth texture.
    mDepthTexture = 0;
    glGenTextures(1, &mDepthTexture.value());
    glBindTexture(GL_TEXTURE_2D, mDepthTexture.value());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, mSize, mSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    // Attach.
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthTexture.value(), 0);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthTexture.value(), 0);
  } else {
    // Create depth renderbuffer.
    mDepthRenderBuffer = 0;
    glGenRenderbuffers(1, &mDepthRenderBuffer.value());
    glBindRenderbuffer(GL_RENDERBUFFER, mDepthRenderBuffer.value());
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, mSize, mSize);
    // Attach.
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthRenderBuffer.value());
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthRenderBuffer.value());
  }

  // Create color textures.
  mColorTextures.resize(colorAttachCount);
  glGenTextures(colorAttachCount, mColorTextures.data());
  for (size_t i = 0; i < colorAttachCount; i++) {
    glBindTexture(GL_TEXTURE_2D, mColorTextures[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, mSize, mSize, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    // Attach.
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mColorTextures[i], 0);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mColorTextures[i], 0);
  }

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    Log::error("Error creating framebuffer.");
    assert(false);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, OpenGL::null);
}

FrameBuffer::~FrameBuffer() {
  if (mHandle != OpenGL::null) glDeleteFramebuffers(1, &mHandle);
  if (!mColorTextures.empty()) glDeleteTextures(mColorTextures.size(), mColorTextures.data());
  if (mDepthTexture.has_value()) glDeleteTextures(1, &mDepthTexture.value());
  if (mDepthRenderBuffer.has_value()) glDeleteRenderbuffers(1, &mDepthRenderBuffer.value());
}

void FrameBuffer::bindBuffer(size_t index) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mHandle);
  if (mColorTextures.empty()) {
    glDrawBuffer(GL_NONE);
  } else {
    GLenum arr = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + index);
    glDrawBuffers(1, &arr);
  }
}

void FrameBuffer::bindBufferRead(size_t index) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, mHandle);
  if (mColorTextures.empty()) {
    glReadBuffer(GL_NONE);
  } else {
    GLenum arr = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + index);
    glReadBuffer(arr);
  }
}

void FrameBuffer::bindBuffers() {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mHandle);
  if (mColorTextures.empty()) {
    glDrawBuffer(GL_NONE);
  } else {
    std::vector<GLenum> arr(mColorTextures.size());
    for (size_t i = 0; i < arr.size(); i++) arr[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
    glDrawBuffers(arr.size(), arr.data());
  }
}
