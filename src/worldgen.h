#ifndef WORLDGEN_H_
#define WORLDGEN_H_

// Perlin Noise 2D
namespace WorldGen {
	extern int seed;
	extern double NoiseScaleX;
	extern double NoiseScaleZ;

	inline double Noise(int x, int y) {
		long long xx = x * 107 + y * 13258953287;
		xx = xx >> 13 ^ xx;
		return (xx*(xx*xx * 15731 + 789221) + 1376312589 & 0x7fffffff) / 16777216.0;
	}
	inline double Interpolate(double a, double b, double x) { return a * (1.0 - x) + b * x; }
	double InterpolatedNoise(double x, double y);
	double PerlinNoise2D(double x, double y);
	inline int getHeight(int x, int y) { return int(PerlinNoise2D(x / NoiseScaleX, y / NoiseScaleZ)) / 2 - 64; }
}

#endif

