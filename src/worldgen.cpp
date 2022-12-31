#include "worldgen.h"
#include "math.h"

int WorldGen::seed = 2333;
double WorldGen::NoiseScaleX = 64;
double WorldGen::NoiseScaleZ = 64;

double WorldGen::interpolatedNoise2D(double x, double y) {
  int ix = int(floor(x)), iy = int(floor(y));
  double i1 = interpolate(noise2D(ix, iy), noise2D(ix + 1, iy), x - ix);
  double i2 = interpolate(noise2D(ix, iy + 1), noise2D(ix + 1, iy + 1), x - ix);
  return interpolate(i1, i2, y - iy);
}

double WorldGen::interpolatedNoise3D(double x, double y, double z) {
  int ix = int(floor(x)), iy = int(floor(y)), iz = int(floor(z));
  double i1 = interpolate(noise3D(ix, iy, iz), noise3D(ix + 1, iy, iz), x - ix);
  double i2 = interpolate(noise3D(ix, iy + 1, iz), noise3D(ix + 1, iy + 1, iz), x - ix);
  double i3 = interpolate(noise3D(ix, iy, iz + 1), noise3D(ix + 1, iy, iz + 1), x - ix);
  double i4 = interpolate(noise3D(ix, iy + 1, iz + 1), noise3D(ix + 1, iy + 1, iz + 1), x - ix);
  return interpolate(interpolate(i1, i2, y - iy), interpolate(i3, i4, y - iy), z - iz);
}

double WorldGen::fractalNoise2D(double x, double y) {
  double total = 0, frequency = 1, amplitude = 1;
  for (int i = 0; i <= 4; i++) {
    total += interpolatedNoise2D(x * frequency, y * frequency) * amplitude;
    frequency *= 2;
    amplitude /= 2.0;
  }
  return total;
}

double WorldGen::fractalNoise3D(double x, double y, double z) {
  double total = 0, frequency = 1, amplitude = 1;
  for (int i = 0; i <= 4; i++) {
    total += interpolatedNoise3D(x * frequency, y * frequency, z * frequency) * amplitude;
    frequency *= 2;
    amplitude /= 2.0;
  }
  return total;
}

int WorldGen::getHeight(int x, int y) {
  int mountain = int(fractalNoise2D(x / NoiseScaleX / 2.0 + 113.0, y / NoiseScaleZ / 2.0 + 1301.0));
  int upper = (int(fractalNoise2D(x / NoiseScaleX + 0.125, y / NoiseScaleZ + 0.125)) >> 3) + 96;
  int transition = int(fractalNoise2D(x / NoiseScaleX + 113.0, y / NoiseScaleZ + 1301.0));
  int lower = (int(fractalNoise2D(x / NoiseScaleX + 0.125, y / NoiseScaleZ + 0.125)) >> 3);
  int base = int(fractalNoise2D(x / NoiseScaleX / 16.0, y / NoiseScaleZ / 16.0)) * 2 - 320;
  if (transition > upper) {
    if (mountain > upper) return mountain + base;
    return upper + base;
  }
  if (transition < lower) return lower + base;
  return transition + base;
}

double WorldGen::getDensity(int x, int y, int z) {
  const double NoiseScaleX3D = 100, NoiseScaleY3D = 100, NoiseScaleZ3D = 100;
  return fractalNoise3D(x / NoiseScaleX3D, y / NoiseScaleY3D, z / NoiseScaleZ3D) / 256.0;
}

int WorldGen::getBlock(int, int y, int, int height, double density) {
  density += (height - y + 64.0) / 512.0;
  return density > 0.6 ? 1 : 0;
}
