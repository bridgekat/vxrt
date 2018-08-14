#include "framebuffer.h"
#include "logger.h"

inline int log2Ceil(int x) {
	int res = 0;
	x--;
	while (x > 0) {
		x >>= 1;
		res++;
	}
	return res;
}

void FrameBuffer::create(int width, int height, int col, bool depth) {
	if (mCreated) destroy();

	mSize = width > height ? (1 << log2Ceil(width)) : (1 << log2Ceil(height));
	mColorAttachCount = col;
	mDepthAttach = depth;

	// Create framebuffer object
	glGenFramebuffers(1, &mID);
	glBindFramebuffer(GL_FRAMEBUFFER, mID);

	if (mDepthAttach) {
		// Create depth texture
		glGenTextures(1, &mDepthTexture);
		glBindTexture(GL_TEXTURE_2D, mDepthTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, mSize, mSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
		// Attach
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthTexture, 0);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mDepthTexture, 0);
	} else {
		// Create depth renderbuffer
		glGenRenderbuffers(1, &mDepthTexture);
		glBindRenderbuffer(GL_RENDERBUFFER, mDepthTexture);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, mSize, mSize);
		// Attach
		glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthTexture);
		glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mDepthTexture);
	}

	// Create color textures
	glGenTextures(mColorAttachCount, mColorTextures);
	for (int i = 0; i < mColorAttachCount; i++) {
		glBindTexture(GL_TEXTURE_2D, mColorTextures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, mSize, mSize, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
		// Attach
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mColorTextures[i], 0);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, mColorTextures[i], 0);
	}
	
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		LogError("Error creating framebuffer!");
	} else mCreated = true;
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FrameBuffer::destroy() {
	if (!mCreated) return;
	
	glDeleteFramebuffers(1, &mID);

	glDeleteTextures(mColorAttachCount, mColorTextures);
	if (mDepthAttach) glDeleteTextures(1, &mDepthTexture);
	else glDeleteRenderbuffers(1, &mDepthTexture);

	mCreated = false;
}

void FrameBuffer::bindBuffer(int index) {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mID);
	if (mColorAttachCount == 0) {
		glDrawBuffer(GL_NONE);
	} else {
		GLuint arr = GL_COLOR_ATTACHMENT0 + index;
		glDrawBuffers(1, &arr);
	}
}

void FrameBuffer::bind() {
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mID);
	if (mColorAttachCount == 0) {
		glDrawBuffer(GL_NONE);
	} else {
		GLuint arr[16];
		for (int i = 0; i < mColorAttachCount; i++) arr[i] = GL_COLOR_ATTACHMENT0 + i;
		glDrawBuffers(mColorAttachCount, arr);
	}
}

void FrameBuffer::bindBufferRead(int index) {
	glBindFramebuffer(GL_READ_FRAMEBUFFER, mID);
	if (mColorAttachCount == 0) {
		glReadBuffer(GL_NONE);
	} else {
		GLuint arr = GL_COLOR_ATTACHMENT0 + index;
		glReadBuffer(arr);
	}
}

