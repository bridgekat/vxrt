#ifndef BITMAP_H_
#define BITMAP_H_

#include <cassert>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// A slice of a source bitmap (itself being immutable).
// Lifetime is bounded by source bitmap.
class BitmapSlice {
public:
  size_t const width, height;
  std::vector<size_t> const channels;
  size_t const bytesPerPixel, pitch;
  uint8_t* const data;

  // Pre: slices must not overlap.
  BitmapSlice& operator=(BitmapSlice const& r);

  void fill(std::vector<uint8_t> const& color);
};

// A bitmap stores the pixel data of an image.
// Each row of pixels is 4-byte aligned.
class Bitmap {
public:
  Bitmap(size_t width, size_t height, size_t bytesPerPixel):
    mWidth(width), mHeight(height), mBytesPerPixel(bytesPerPixel), mPitch(align(width * bytesPerPixel)),
    mData(new uint8_t[mHeight * mPitch]) {
    std::fill(mData.get(), mData.get() + mHeight * mPitch, 0);
  }
  Bitmap(std::string const& filename);

  // See: https://stackoverflow.com/questions/3279543/what-is-the-copy-and-swap-idiom
  Bitmap(Bitmap&& r) noexcept:
    mWidth(r.mWidth), mHeight(r.mHeight), mBytesPerPixel(r.mBytesPerPixel), mPitch(r.mPitch),
    mData(std::move(r.mData)) {}
  Bitmap(Bitmap const& r):
    mWidth(r.mWidth), mHeight(r.mHeight), mBytesPerPixel(r.mBytesPerPixel), mPitch(r.mPitch),
    mData(new uint8_t[mHeight * mPitch]) {
    std::copy(r.mData.get(), r.mData.get() + mHeight * mPitch, mData.get());
  }
  Bitmap& operator=(Bitmap r) noexcept {
    swap(*this, r);
    return *this;
  }

  friend void swap(Bitmap& l, Bitmap& r) noexcept {
    using std::swap;
    swap(l.mWidth, r.mWidth);
    swap(l.mHeight, r.mHeight);
    swap(l.mBytesPerPixel, r.mBytesPerPixel);
    swap(l.mPitch, r.mPitch);
    swap(l.mData, r.mData);
  }

  size_t width() const { return mWidth; }
  size_t height() const { return mHeight; }
  size_t bytesPerPixel() const { return mBytesPerPixel; }
  size_t pitch() const { return mPitch; }
  size_t size() const { return mHeight * mPitch; }
  uint8_t* data() { return mData.get(); }
  uint8_t const* data() const { return mData.get(); }

  uint8_t& at(size_t x, size_t y, size_t c) {
    assert(x < mWidth && y < mHeight && c < mBytesPerPixel);
    return mData[y * mPitch + x * mBytesPerPixel + c];
  }

  uint8_t const& at(size_t x, size_t y, size_t c) const {
    assert(x < mWidth && y < mHeight && c < mBytesPerPixel);
    return mData[y * mPitch + x * mBytesPerPixel + c];
  }

  BitmapSlice slice(size_t x, size_t y, size_t w, size_t h, std::vector<size_t> channels) {
    assert(x + w <= mWidth && y + h <= mHeight);
    for (auto channel: channels) assert(channel < mBytesPerPixel);
    return BitmapSlice{w, h, channels, mBytesPerPixel, mPitch, mData.get() + y * mPitch + x * mBytesPerPixel};
  }

  BitmapSlice all() {
    std::vector<size_t> channels;
    for (size_t i = 0; i < mBytesPerPixel; i++) channels.push_back(i);
    return slice(0, 0, mWidth, mHeight, channels);
  }

  void save(std::string const& filename) const;
  void verticalFlip();
  void swapChannels(size_t c, size_t d);
  Bitmap enlarge(size_t scale) const;
  Bitmap shrink(size_t scale) const;
  Bitmap resample(size_t width, size_t height) const;

private:
  size_t mWidth;
  size_t mHeight;
  size_t mBytesPerPixel;
  size_t mPitch;
  std::unique_ptr<uint8_t[]> mData;

  static size_t align(size_t x) { return x % 4 == 0 ? x : x - x % 4 + 4; }
};

static_assert(std::move_constructible<Bitmap>);
static_assert(std::assignable_from<Bitmap&, Bitmap&&>);
static_assert(std::copy_constructible<Bitmap>);
static_assert(std::assignable_from<Bitmap&, Bitmap&>);

#endif // BITMAP_H_
