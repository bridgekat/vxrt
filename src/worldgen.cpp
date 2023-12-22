#include "worldgen.h"
#include <cmath>

double WorldGen::interpolatedNoise2D(double x, double y) {
  auto ix = static_cast<int64_t>(std::floor(x));
  auto iy = static_cast<int64_t>(std::floor(y));
  auto fx = x - std::floor(x);
  auto fy = y - std::floor(y);
  double i1 = interpolate(noise2D(ix, iy), noise2D(ix + 1, iy), fx);
  double i2 = interpolate(noise2D(ix, iy + 1), noise2D(ix + 1, iy + 1), fx);
  return interpolate(i1, i2, fy);
}

double WorldGen::interpolatedNoise3D(double x, double y, double z) {
  auto ix = static_cast<int64_t>(std::floor(x));
  auto iy = static_cast<int64_t>(std::floor(y));
  auto iz = static_cast<int64_t>(std::floor(z));
  auto fx = x - std::floor(x);
  auto fy = y - std::floor(y);
  auto fz = z - std::floor(z);
  double i1 = interpolate(noise3D(ix, iy, iz), noise3D(ix + 1, iy, iz), fx);
  double i2 = interpolate(noise3D(ix, iy + 1, iz), noise3D(ix + 1, iy + 1, iz), fx);
  double i3 = interpolate(noise3D(ix, iy, iz + 1), noise3D(ix + 1, iy, iz + 1), fx);
  double i4 = interpolate(noise3D(ix, iy + 1, iz + 1), noise3D(ix + 1, iy + 1, iz + 1), fx);
  return interpolate(interpolate(i1, i2, fy), interpolate(i3, i4, fy), fz);
}

double WorldGen::fractalNoise2D(double x, double y) {
  double total = 0, frequency = 1, amplitude = 1;
  for (int32_t i = 0; i <= 4; i++) {
    total += interpolatedNoise2D(x * frequency, y * frequency) * amplitude;
    frequency *= 2;
    amplitude /= 2.0;
  }
  return total;
}

double WorldGen::fractalNoise3D(double x, double y, double z) {
  double total = 0, frequency = 1, amplitude = 1;
  for (int32_t i = 0; i <= 4; i++) {
    total += interpolatedNoise3D(x * frequency, y * frequency, z * frequency) * amplitude;
    frequency *= 2;
    amplitude /= 2.0;
  }
  return total;
}

int64_t WorldGen::getHeight(double x, double y) {
  auto mountain = static_cast<int64_t>(fractalNoise2D(x / NoiseScaleX / 2.0 + 113.0, y / NoiseScaleZ / 2.0 + 1301.0));
  auto upper = (static_cast<int64_t>(fractalNoise2D(x / NoiseScaleX + 0.125, y / NoiseScaleZ + 0.125)) >> 3) + 96;
  auto transition = static_cast<int64_t>(fractalNoise2D(x / NoiseScaleX + 113.0, y / NoiseScaleZ + 1301.0));
  auto lower = (static_cast<int64_t>(fractalNoise2D(x / NoiseScaleX + 0.125, y / NoiseScaleZ + 0.125)) >> 3);
  auto base = static_cast<int64_t>(fractalNoise2D(x / NoiseScaleX / 16.0, y / NoiseScaleZ / 16.0)) * 2 - 320;
  if (transition > upper) {
    if (mountain > upper)
      return mountain + base;
    return upper + base;
  }
  if (transition < lower)
    return lower + base;
  return transition + base;
}

double WorldGen::getDensity(double x, double y, double z) {
  return fractalNoise3D(x / NoiseScaleX3D, y / NoiseScaleY3D, z / NoiseScaleZ3D) / 256.0;
}

bool WorldGen::getBlock(int64_t, int64_t y, int64_t, int64_t height, double density) {
  density += (static_cast<double>(height - y) + 64.0) / 512.0;
  return density > 0.6;
}
