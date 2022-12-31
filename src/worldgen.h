#ifndef WORLDGEN_H_
#define WORLDGEN_H_

// Perlin Noise 2D
namespace WorldGen {
  extern int seed;
  extern double NoiseScaleX;
  extern double NoiseScaleZ;

  inline double interpolate(double a, double b, double x) { return a * (1.0 - x) + b * x; }
  inline double noise2D(int x, int y) {
    long long xx = x * 107 + y * 13258953287;
    xx = xx >> 13 ^ xx;
    return ((xx * (xx * xx * 15731 + 789221) + 1376312589) & 0x7fffffff) / 16777216.0; // 0 ~ 127
  }
  inline double noise3D(int x, int y, int z) {
    long long xx = x * 107 + y * 13258953287 + z * 11399999;
    xx = xx >> 13 ^ xx;
    return ((xx * (xx * xx * 15731 + 789221) + 1376312589) & 0x7fffffff) / 16777216.0; // 0 ~ 127
  }
  double interpolatedNoise2D(double x, double y);
  double interpolatedNoise3D(double x, double y, double z);
  double fractalNoise2D(double x, double y);           // 0 ~ 255
  double fractalNoise3D(double x, double y, double z); // 0 ~ 255
  int getHeight(int x, int y);
  double getDensity(int x, int y, int z);
  int getBlock(int x, int y, int z, int height, double density);
}

#endif // WORLDGEN_H_
