#ifndef BITMAP_H_
#define BITMAP_H_

#include <string>
#include "vec.h"

class Bitmap {
public:
    int w = 0, h = 0, pitch = 0;
    unsigned char* data = 0;

    Bitmap() {}
    Bitmap(const Bitmap& rhs) { (*this) = rhs; }
    Bitmap(int w_, int h_, const Vec3i& bg): w(w_), h(h_) {
        pitch = align(w * 3, 4);
        data = new unsigned char[pitch * h];
        for (int i = 0; i < h; i++) for (int j = 0; j < w; j++) setPixel(j, i, bg);
    }
    ~Bitmap() { if (data != 0) delete[] data; }

    Bitmap& operator=(const Bitmap& rhs) {
        w = rhs.w; h = rhs.h; pitch = rhs.pitch;
        if (data != NULL) delete[] data;
        data = new unsigned char[h * pitch];
        memcpy(data, rhs.data, h * pitch);
        return *this;
    }
    
    Vec3i getPixel(int x, int y) {
        int pos = y * pitch + x * 3;
        Vec3i col(data[pos], data[pos+1], data[pos+2]);
        return col;
    }

    void setPixel(int x, int y, const Vec3i& col) {
        int pos = y * pitch + x * 3;
        data[pos] = col.x, data[pos + 1] = col.y, data[pos + 2] = col.z;
    }
    
    void swapRBChannels();
    void load(const std::string& filename);
    void save(const std::string& filename);
    
private:
	int align(int x, int a) {
		return x % a == 0 ? x : x + a - x % a;
	}
};

#endif

