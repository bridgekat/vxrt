#ifndef TEXTURE_H_
#define TEXTURE_H_

#include <string>
#include <SDL2/SDL_image.h>
#include "logger.h"
#include "opengl.h"

// RGB/RGBA texture image, pixels aligned
class TextureImage {
public:
	TextureImage(int width, int height, int bytesPerPixel):
		mWidth(width), mHeight(height), mBytesPerPixel(bytesPerPixel), mPitch(alignedPitch(mWidth * bytesPerPixel)),
		mData(new unsigned char[mHeight * mPitch]) {
		memset(mData, 0, mHeight * mPitch * sizeof(unsigned char));
	}
	TextureImage(TextureImage&& r):
		mWidth(r.mWidth), mHeight(r.mHeight), mBytesPerPixel(r.mBytesPerPixel), mPitch(r.mPitch) {
		std::swap(mData, r.mData);
	}
	TextureImage(const std::string& filename) { loadFromPNG(filename); }
	~TextureImage() { if (mData != nullptr) delete[] mData; }

	TextureImage& operator= (const TextureImage& r) {
		if (mData != nullptr) delete[] mData;
		mHeight = r.mHeight, mWidth = r.mWidth, mPitch = r.mPitch, mBytesPerPixel = r.mBytesPerPixel;
		mData = new unsigned char[mHeight * mPitch];
		memcpy(mData, r.mData, mHeight * mPitch * sizeof(unsigned char));
		return (*this);
	}

	int width() const { return mWidth; }
	int height() const { return mHeight; }
	int pitch() const { return mPitch; }
	int bytesPerPixel() const { return mBytesPerPixel; }
	const unsigned char* data() const { return mData; }

	void copyFrom(const TextureImage& src, int x, int y, int srcx = 0, int srcy = 0);

	TextureImage convert(int bytesPerPixel) const;
	TextureImage enlarge(int scale) const;
	TextureImage shrink(int scale) const;
	TextureImage resample(int width, int height) const;

	static int alignedPitch(int pitch, int align = 4) {
		if (pitch % align == 0) return pitch;
		return pitch + align - pitch % align;
	}

private:
	int mWidth = 0, mHeight = 0, mPitch = 0, mBytesPerPixel = 0;
	unsigned char* mData = nullptr;

	void loadFromPNG(const std::string& filename, bool checkSize = true);
};

class Texture {
public:
	Texture() = default;
	Texture(const TextureImage& image, bool alpha = false, int maxLevels = -1) { load(image, alpha, maxLevels); }

	void load(const TextureImage& image, bool alpha = false, int maxLevels = -1);
	TextureID id() const { return mID; }
	void bind() const { glBindTexture(GL_TEXTURE_2D, mID); }
	static void unbind() { glBindTexture(GL_TEXTURE_2D, 0); }

	static int maxSize() {
		GLint res;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &res);
		return res;
	}

private:
	TextureID mID = 0;
};

#endif

