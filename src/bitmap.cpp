#include "bitmap.h"

#include <fstream>

#pragma pack(push)
#pragma pack(1) // Avoid structure alignment

struct BitmapInfoHeader {
    int biSize = 40, biWidth, biHeight;
    short biPlanes = 1, biBitCount = 24;
    int biCompression = 0, biSizeImage = 0, biXPelsPerMeter = 3780, biYPelsPerMeter = 3780, biClrUsed = 0, biClrImportant = 0;
};

struct BitmapFileHeader {
    short bfType = 0x4D42;
    int bfSize;
    short bfReserved1 = 0, bfReserved2 = 0;
    int bfOffBits = 54;
};

#pragma pack(pop)

// Convert between BGR and RGB
void Bitmap::swapRBChannels() {
    int p = 0;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int ind = p + j * 3;
            unsigned char t = data[ind];
            data[ind] = data[ind + 2];
            data[ind + 2] = t;
        }
        p += pitch;
    }
}

void Bitmap::load(const std::string& filename) {
    data = NULL, w = h = 0;
    std::ifstream bmpfile(filename.c_str(), std::ios::binary | std::ios::in);
    BitmapFileHeader bfh;
    BitmapInfoHeader bih;
    bmpfile.read((char*)&bfh, sizeof(BitmapFileHeader));
    bmpfile.read((char*)&bih, sizeof(BitmapInfoHeader));
    w = bih.biWidth;
    h = bih.biHeight;
    pitch = align(bih.biWidth * 3, 4);
    data = new unsigned char[pitch * h];
    bmpfile.read((char*)data, pitch * h);
    bmpfile.close();
    swapRBChannels();
}

void Bitmap::save(const std::string& filename) {
    BitmapFileHeader bfh;
    BitmapInfoHeader bih;
    bfh.bfSize = pitch * h + 54;
    bih.biWidth = w;
    bih.biHeight = h;
	Bitmap tmp = *this;
    tmp.swapRBChannels();
    std::ofstream ofs(filename, std::ios::out | std::ios::binary);
    ofs.write((char*)&bfh, sizeof(BitmapFileHeader));
    ofs.write((char*)&bih, sizeof(BitmapInfoHeader));
    ofs.write((char*)tmp.data, pitch * h);
    ofs.close();
}

