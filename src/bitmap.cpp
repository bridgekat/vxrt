#include "bitmap.h"
#include <fstream>

#pragma pack(push, 1)
struct BitmapFileHeader {
  uint16_t bfType = 0x4d42;
  uint32_t bfSize;
  uint16_t bfReserved1 = 0;
  uint16_t bfReserved2 = 0;
  uint32_t bfOffBits = 54;
};
struct BitmapInfoHeader {
  uint32_t biSize = 40;
  int32_t biWidth;
  int32_t biHeight;
  uint16_t biPlanes = 1;
  uint16_t biBitCount;
  uint32_t biCompression = 0;
  uint32_t biSizeImage = 0;
  int32_t biXPelsPerMeter = 3780;
  int32_t biYPelsPerMeter = 3780;
  uint32_t biClrUsed = 0;
  uint32_t biClrImportant = 0;
};
#pragma pack(pop)

BitmapSlice& BitmapSlice::operator=(BitmapSlice const& r) {
  if (width != r.width || height != r.height || channels.size() != r.channels.size()) {
    assert(false);
    return *this;
  }
  for (size_t i = 0; i < height; i++) {
    for (size_t j = 0; j < width; j++) {
      size_t ind = i * pitch + j * bytesPerPixel;
      size_t rind = i * r.pitch + j * r.bytesPerPixel;
      for (size_t k = 0; k < channels.size(); k++) {
        size_t c = channels[k];
        size_t rc = r.channels[k];
        data[ind + c] = r.data[rind + rc];
      }
    }
  }
  return *this;
}

void BitmapSlice::fill(std::vector<uint8_t> const& color) {
  if (channels.size() != color.size()) {
    assert(false);
    return;
  }
  for (size_t i = 0; i < height; i++) {
    for (size_t j = 0; j < width; j++) {
      size_t ind = i * pitch + j * bytesPerPixel;
      for (size_t k = 0; k < channels.size(); k++) {
        size_t c = channels[k];
        data[ind + c] = color[k];
      }
    }
  }
}

Bitmap::Bitmap(std::string const& filename) {
  BitmapFileHeader bfh;
  BitmapInfoHeader bih;
  std::ifstream ifs(filename, std::ios::in | std::ios::binary);
  ifs.read(reinterpret_cast<char*>(&bfh), sizeof(BitmapFileHeader));
  ifs.read(reinterpret_cast<char*>(&bih), sizeof(BitmapInfoHeader));
  mWidth = static_cast<size_t>(bih.biWidth);
  mHeight = static_cast<size_t>(bih.biHeight);
  mBytesPerPixel = static_cast<size_t>(bih.biBitCount) / 8;
  mPitch = align(mWidth * mBytesPerPixel);
  mData.reset(new uint8_t[mHeight * mPitch]);
  ifs.read(reinterpret_cast<char*>(mData.get()), static_cast<std::streamsize>(mHeight * mPitch));
}

void Bitmap::save(std::string const& filename) const {
  BitmapFileHeader bfh;
  BitmapInfoHeader bih;
  bfh.bfSize = static_cast<uint32_t>(mHeight * mPitch + 54);
  bih.biWidth = static_cast<int32_t>(mWidth);
  bih.biHeight = static_cast<int32_t>(mHeight);
  bih.biBitCount = static_cast<uint16_t>(mBytesPerPixel * 8);
  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  ofs.write(reinterpret_cast<char*>(&bfh), sizeof(BitmapFileHeader));
  ofs.write(reinterpret_cast<char*>(&bih), sizeof(BitmapInfoHeader));
  ofs.write(reinterpret_cast<char*>(mData.get()), static_cast<std::streamsize>(mHeight * mPitch));
}

void Bitmap::verticalFlip() {
  for (size_t i = 0; i < mHeight / 2; i++) {
    for (size_t j = 0; j < mPitch; j++) {
      size_t ind = i * mPitch + j;
      size_t rind = (mHeight - 1 - i) * mPitch + j;
      std::swap(mData[ind], mData[rind]);
    }
  }
}

void Bitmap::swapChannels(size_t c, size_t d) {
  assert(c <= mBytesPerPixel && d <= mBytesPerPixel);
  for (size_t i = 0; i < mHeight; i++) {
    for (size_t j = 0; j < mWidth; j++) {
      size_t ind = i * mPitch + j * mBytesPerPixel;
      std::swap(mData[ind + c], mData[ind + d]);
    }
  }
}

Bitmap Bitmap::enlarge(size_t scale) const {
  Bitmap res(mWidth * scale, mHeight * scale, mBytesPerPixel);
  for (size_t i = 0; i < mHeight * scale; i++)
    for (size_t j = 0; j < mWidth * scale; j++)
      for (size_t k = 0; k < mBytesPerPixel; k++) {
        res.mData[i * res.mPitch + j * res.mBytesPerPixel + k] =
          mData[i / scale * mPitch + j / scale * mBytesPerPixel + k];
      }
  return res;
}

Bitmap Bitmap::shrink(size_t scale) const {
  Bitmap res(mWidth / scale, mHeight / scale, mBytesPerPixel);
  for (size_t i = 0; i < mHeight / scale; i++)
    for (size_t j = 0; j < mWidth / scale; j++)
      for (size_t k = 0; k < mBytesPerPixel; k++) {
        uint32_t sum = 0;
        for (size_t i1 = 0; i1 < scale; i1++)
          for (size_t j1 = 0; j1 < scale; j1++)
            sum += static_cast<uint32_t>(mData[(i * scale + i1) * mPitch + (j * scale + j1) * mBytesPerPixel + k]);
        res.mData[i * res.mPitch + j * res.mBytesPerPixel + k] = static_cast<uint8_t>(sum / scale / scale);
      }
  return res;
}

Bitmap Bitmap::resample(size_t width, size_t height) const {
  Bitmap res(width, height, mBytesPerPixel);
  for (size_t i = 0; i < height; i++)
    for (size_t j = 0; j < width; j++)
      for (size_t k = 0; k < mBytesPerPixel; k++) {
        size_t i1 = i * mHeight / height, j1 = j * mWidth / width;
        res.mData[i * res.mPitch + j * res.mBytesPerPixel + k] = mData[i1 * mPitch + j1 * mBytesPerPixel + k];
      }
  return res;
}
