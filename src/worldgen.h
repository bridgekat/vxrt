#ifndef WORLDGEN_H_
#define WORLDGEN_H_

#include "common.h"

// Fractal noise.
namespace WorldGen {
  constexpr int seed = 2333;
  constexpr double NoiseScaleX = 64;
  constexpr double NoiseScaleZ = 64;
  constexpr double NoiseScaleX3D = 100;
  constexpr double NoiseScaleY3D = 100;
  constexpr double NoiseScaleZ3D = 100;

  inline double interpolate(double a, double b, double x) {
    return a * (1.0 - x) + b * x;
  }
  inline double noise2D(int64_t x, int64_t y) {
    int64_t xx = x * 107 + y * 13258953287;
    xx = xx >> 13 ^ xx;
    return ((xx * (xx * xx * 15731 + 789221) + 1376312589) & 0x7fffffff) / 16777216.0; // 0 ~ 127
  }
  inline double noise3D(int64_t x, int64_t y, int64_t z) {
    int64_t xx = x * 107 + y * 13258953287 + z * 11399999;
    xx = xx >> 13 ^ xx;
    return ((xx * (xx * xx * 15731 + 789221) + 1376312589) & 0x7fffffff) / 16777216.0; // 0 ~ 127
  }
  double interpolatedNoise2D(double x, double y);
  double interpolatedNoise3D(double x, double y, double z);
  double fractalNoise2D(double x, double y);           // 0 ~ 255
  double fractalNoise3D(double x, double y, double z); // 0 ~ 255
  int64_t getHeight(double x, double y);
  double getDensity(double x, double y, double z);
  bool getBlock(int64_t x, int64_t y, int64_t z, int64_t height, double density);
}

#endif // WORLDGEN_H_
