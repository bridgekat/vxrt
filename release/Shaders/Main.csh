#version 430 core

// ===== Inputs =====

layout (local_size_x = 8u, local_size_y = 4u, local_size_z = 1u)
in;

// uniform mat4 ProjectionMatrix;
// uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;
uniform vec3 CameraPosition;
uniform float RandomSeed;
uniform bool PathTracing;
uniform bool ProfilerOn;
// uniform float Time;

uniform sampler2D PrevFrame;
uniform sampler2D NoiseTexture;
uniform sampler2D MaxTexture;
uniform uint SampleCount;
uniform uint FrameWidth;
uniform uint FrameHeight;
uniform bool DynamicMode;
uniform uint MaxNodes;
uniform uint MaxLevels;
uniform uint NoiseLevels; // Noise map detail level <= MaxLevels
uniform uint PartialLevels; // Min noise level (using part of the noise map)

layout (std430, binding = 1)
buffer TreeData {
  uint NodeCount;
  uint Data[];
};

// ===== Outputs =====

uniform restrict writeonly image2D FrameBuffer;

layout (std430, binding = 2)
buffer OutputData {
  uint OutputCount;
};

// ===== Structures and constants =====

#define Box vec4

struct Node {
  uint data;
  uint level;
  Box box;
};

struct Intersection {
  vec3 pos;
  vec3 offset; // Points forwards, axis aligned.
};

const float Pi = 3.14159265;
const float Gamma = 2.2;

float RootSize;
float NoiseTextureSize;
float HeightScale;

// Terrain generation.
// #define FRACTALNOISE_USE_TEXTURESAMPLER
// #define FRACTALNOISE_COSINE_INTERPOLATION

// Rendering distance.
const uint MaxIterations = 128u;

// Level of details.
#define LOD_CHECK
#define LOD_CHECK_ALWAYS
const float FoVy = 70.0;
const float VerticalResolution = 480.0; // 2160.0;
const float LODQuality = 1.0;
vec3 LodCenterPos;
vec3 LodViewDir;

// Path tracing.
#define TERRAIN_GRADIENT_NORMAL
#define HALTON_SEQUENCE
const int MaxTracedRays = 2;
const float ProbabilityToSun = 0.5;

// Lighting.
#define LIGHTING_SUN
#define LIGHTING_SKY
const vec3 SunlightColor = pow(vec3(1.0, 0.7, 0.5) * 1.5, vec3(Gamma));
const vec3 SunlightDirection = normalize(vec3(0.8, -1.0, 0.3));
const float SunlightAngle = 0.1; // In radians.
const vec3 SkyColorTop = pow(vec3(152.0, 211.0, 250.0) / 255.0, vec3(Gamma));
const vec3 SkyColorBottom = pow(vec3(90.0, 134.0, 206.0) / 255.0, vec3(Gamma));

// Colors.
vec3 Palette[6] = vec3[](
  pow(vec3(147.5, 166.4, 77.0) / 255.0, vec3(Gamma)),
  pow(vec3(147.5, 166.4, 77.0) / 255.0, vec3(Gamma)),
  pow(vec3(151.0, 228.0, 90.0) / 255.0, vec3(Gamma)),
  pow(vec3(144.0, 105.0, 64.0) / 255.0, vec3(Gamma)),
  pow(vec3(147.5, 166.4, 77.0) / 255.0, vec3(Gamma)),
  pow(vec3(147.5, 166.4, 77.0) / 255.0, vec3(Gamma))
);

// Random seeds.
vec3 Dither[3] = vec3[](
  vec3(12.1322, 23.1313423, 34.959),
  vec3(23.183, 11.232, 54.9923),
  vec3(345.99253, 2345.2323, 78.1233)
);

// Miscellaneous.
#define ANTI_ALIASING
// #define DEPTH_OF_FIELD

// ===== Utilities =====

vec3 divide(vec4 v) { return (v / v.w).xyz; }
bool inside(vec3 a, Box box) { return all(greaterThanEqual(a, box.xyz)) && all(lessThan(a, box.xyz + box.w)); }

vec3 getSkyColor(vec3 dir) {
  dir = normalize(dir);
#ifdef LIGHTING_SUN
  if (dot(-SunlightDirection, dir) >= cos(SunlightAngle / 2.0)) return SunlightColor;
#endif
#ifdef LIGHTING_SKY
  return mix(SkyColorTop, SkyColorBottom, smoothstep(0.0, 1.0, dir.y * 2.0));
#else
  return vec3(0.0);
#endif
}

vec3 getPalette(vec3 normal) {
  vec3 mag = abs(normal);
  if (mag.x >= mag.y && mag.x >= mag.z) {
    return normal.x >= 0.0 ? Palette[0] : Palette[1];
  } else if (mag.y >= mag.z) {
    return normal.y >= 0.0 ? Palette[2] : Palette[3];
  } else {
    return normal.z >= 0.0 ? Palette[4] : Palette[5];
  }
}

// ===== PRNG =====

// Returns a float in range [0, 1).
float constructFloat(uint m) {
  const uint IEEEMantissa = 0x007FFFFFu;
  const uint IEEEOne = 0x3F800000u;
  m = (m & IEEEMantissa) | IEEEOne;
  return uintBitsToFloat(m) - 1.0;
}

uint hash(uint x) { x += x << 10u; x ^= x >> 6u; x += x << 3u; x ^= x >> 11u; x += x << 15u; return x; }
uint hash(uvec2 v) { return hash(v.x ^ hash(v.y)); }
uint hash(uvec3 v) { return hash(v.x ^ hash(v.yz)); }
uint hash(uvec4 v) { return hash(v.x ^ hash(v.yzw)); }
float rand(vec3 v) { return constructFloat(hash(floatBitsToUint(vec4(v, RandomSeed)))); }

// See: https://en.wikipedia.org/wiki/Halton_sequence (b is prime)
float halton(uint i, uint b) {
  float f = 1, r = 0;
  while (i > 0) {
    f /= float(b);
    r += f * float(i % b);
    i /= b;
  }
  return r;
}

// ===== Main part =====

// Allocates new slot.
uint allocate(uint count) {
  uint res = atomicAdd(NodeCount, count);
  if (res + count > uint(MaxNodes)) return 0u;
  for (uint i = 0; i < count; i++) Data[res + i] = 0u;
  return res;
}

// Least significant 2bits: [00] not generated; [01] intermediate; [11] leaf
#define IS_LEAF(data) ((data & 3u) == 3u)
#define CHILD_PTR(data) (data >> 2u)
#define LEAF_DATA(data) (data >> 2u)
#define MAKE_INTERMEDIATE(ind) ((ind << 2u) + 1u)
#define MAKE_LEAF(data) ((data << 2u) + 3u)

// Level 0 is the least detailed level (one pixel).
#ifdef FRACTALNOISE_USE_TEXTURESAMPLER
float maxNoise2DSubpixel(uint level, uvec2 x) {
  float size = 1.0 / float(1u << level);
  vec2 pos = vec2(x) * size;
#define F(x, y) (textureLod(NoiseTexture, pos + vec2(x, y) + vec2(0.5) / NoiseTextureSize, 0.0).r)
  return max(max(F(0, 0), F(size, 0)), max(F(0, size), F(size, size)));
#undef F
}
#else
float maxNoise2DSubpixel(uint level, uvec2 x) {
  float size = NoiseTextureSize / float(1u << level);
  vec2 pos = vec2(x) * size;
  ivec2 p = ivec2(pos);
  int mask = int(NoiseTextureSize) - 1;
  vec4 tex = vec4(
    texelFetch(NoiseTexture, (p + ivec2(0, 0)) & mask, 0).r,
    texelFetch(NoiseTexture, (p + ivec2(1, 0)) & mask, 0).r,
    texelFetch(NoiseTexture, (p + ivec2(0, 1)) & mask, 0).r,
    texelFetch(NoiseTexture, (p + ivec2(1, 1)) & mask, 0).r
  );
  vec2 fpos = fract(pos);
  vec4 fx = vec4(fpos.x, fpos.x + size, fpos.x, fpos.x + size);
  vec4 fy = vec4(fpos.y, fpos.y, fpos.y + size, fpos.y + size);
#ifdef FRACTALNOISE_COSINE_INTERPOLATION
  fx = (vec4(1.0) - cos(Pi * fx)) / 2.0;
  fy = (vec4(1.0) - cos(Pi * fy)) / 2.0;
#endif
  vec4 res = mat4((1.0 - fx) * (1.0 - fy), fx * (1.0 - fy), (1.0 - fx) * fy, fx * fy) * tex;
  return max(max(res[0], res[1]), max(res[2], res[3]));
}
#endif

float maxNoise2D(uint level, uvec2 x) {
  if (level > NoiseLevels) return maxNoise2DSubpixel(level, x);
  return texelFetch(MaxTexture, ivec2(x), int(NoiseLevels - level)).r;
}

/*
float Factor[16] = float[](
  1.0, 1.0, 1.0, 0.0, // Low freq
  1.0, 0.0, 1.0, 1.0,
  1.0, 1.0, 0.0, 1.0,
  3.0, 1.0, 1.0, 1.0  // High freq
);
*/

float getMaxHeight(uint level, uvec2 pos) {
  float res = 0.0, amplitude = pow(2.0, float(PartialLevels));
  level += PartialLevels;
  for (uint i = 0u; i + NoiseLevels <= MaxLevels + PartialLevels; i++) {
    res += maxNoise2D(level, pos) * amplitude; // * Factor[i];
    amplitude *= 0.5;
    if (level > 0u) {
      level--;
      pos -= (pos & (1u << level));
    }
  }
  return res * HeightScale;
}

bool lodCheck(uint level, uvec3 pos) {
  vec3 rpos = (vec3(pos) + vec3(0.5)) * RootSize / float(1u << level) - LodCenterPos;
  float size = sqrt(3.0) * RootSize / float(1u << level);
  return tan(FoVy / 2.0 / 180.0 * Pi) * dot(rpos, LodViewDir) * 2.0 / VerticalResolution / LODQuality <= size;
}

int generateNodeTerrain(uint level, uvec3 pos) {
  if ((uint(getMaxHeight(level, pos.xz)) >> (MaxLevels - level)) < pos.y) return 0;
  return (level < MaxLevels) ? -1 : 1;
}

// Returns the innermost tree node at a given position.
Node getNodeAt(vec3 pos) {
  Box box = Box(vec3(0.0), RootSize);
  if (!inside(pos, box)) return Node(0u, 0u, box); // Outside
  uint ptr = 0u, cdata = 0u;
  for (uint level = 0u; level <= MaxLevels; level++) {
#ifdef LOD_CHECK
#ifdef LOD_CHECK_ALWAYS
    if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level))) return Node(MAKE_LEAF(1u), level, box);
#endif
#endif
    cdata = Data[ptr];
    if (DynamicMode && cdata == 0u) {
#ifdef LOD_CHECK
#ifndef LOD_CHECK_ALWAYS
      if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level))) return Node(MAKE_LEAF(1u), level, box);
#endif
#endif
      if (atomicCompSwap(Data[ptr], 0u, 1u) == 0u) { // Not generated, generate node!
        int curr = generateNodeTerrain(level, uvec3(pos) >> (MaxLevels - level));
        cdata = curr < 0 ? MAKE_INTERMEDIATE(allocate(8u)) : MAKE_LEAF(uint(curr));
        atomicExchange(Data[ptr], cdata);
      } else { // Being generated by another invocation. Will be rendered next frame.
        return Node(MAKE_LEAF(0u), level, box);
      }
    } else if (cdata == 1u) { // Being generated by another invocation. Will be rendered next frame.
      return Node(MAKE_LEAF(0u), level, box);
    }
    if (IS_LEAF(cdata)) break; // Reached leaf (monotonous node)
    ptr = CHILD_PTR(cdata);
    vec3 mid = box.xyz + box.w / 2.0;
    if (pos.x >= mid.x) { ptr += 1u; box.x = mid.x; }
    if (pos.y >= mid.y) { ptr += 2u; box.y = mid.y; }
    if (pos.z >= mid.z) { ptr += 4u; box.z = mid.z; }
    box.w /= 2.0;
  }
  return Node(cdata, MaxLevels, box);
}

// Intersects ray with box (assuming ray starts from inside).
Intersection innerIntersect(vec3 org, vec3 dir, Box box) {
  vec3 boxMin = box.xyz;
  vec3 boxMax = box.xyz + box.w;
  vec3 tMax = max((boxMin - org) / dir, (boxMax - org) / dir);
  float tFar = min(tMax.x, min(tMax.y, tMax.z));
  return Intersection(org + dir * tFar, mix(vec3(0.0), sign(dir), equal(tMax, vec3(tFar))));
}

// Intersects ray with box (assuming ray starts from outside).
// If there is no intersection, returns a structure with `offset == vec3(0.0)`.
Intersection outerIntersect(vec3 org, vec3 dir, Box box) {
  vec3 boxMin = box.xyz;
  vec3 boxMax = box.xyz + box.w;
  vec3 tMin = min((boxMin - org) / dir, (boxMax - org) / dir);
  vec3 tMax = max((boxMin - org) / dir, (boxMax - org) / dir);
  float tNear = max(tMin.x, max(tMin.y, tMin.z));
  float tFar = min(tMax.x, min(tMax.y, tMax.z));
  if (tFar < tNear || tNear < 0.0) return Intersection(org, vec3(0.0));
  return Intersection(org + dir * tNear, mix(vec3(0.0), sign(dir), equal(tMin, vec3(tNear))));
}

// Casts a ray through the octree.
// Returns the number of iterations divided by `MaxIterations`.
float castRay(inout vec3 testPoint, inout Intersection last, vec3 dir) {
  dir = normalize(dir);
  Box box = Box(vec3(0.0), RootSize); // Root box
  // Ensure that ray starts inside the root box.
  if (!inside(last.pos, box)) {
    last = outerIntersect(last.pos, dir, box);
    if (last.offset == vec3(0.0)) return -1.0; // Out of range.
    // Update the test point.
    testPoint = clamp(last.pos, box.xyz + 0.5, box.xyz + box.w - 0.5);
  }
  // Start from `org` each time to avoid accumulation of errors.
  Intersection org = last;
  for (int i = 0; i < MaxIterations; i++) {
    Node node = getNodeAt(testPoint);
    if (node.data == 0u) return -1.0; // Out of range.
    if (LEAF_DATA(node.data) != 0u) return float(i) / float(MaxIterations); // Opaque block.
    Intersection p = innerIntersect(org.pos, dir, node.box);
    // Update the test point.
    // Mid: `p.pos` lies on a box boundary, with normal = `p.offset`.
    testPoint = clamp(p.pos, node.box.xyz + 0.5, node.box.xyz + node.box.w - 0.5) + p.offset;
    last = p;
  }
  // Too many iterations.
  return 1.0;
}

// Use terrain-gradient-based normal for dynamic mode (experimental).
vec3 getNormal(vec3 testPoint, Intersection last) {
#ifdef TERRAIN_GRADIENT_NORMAL
  if (DynamicMode) {
    Node node = getNodeAt(testPoint);
    uint level = node.level;
    float H00 = getMaxHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(0u, 0u));
    float H10 = getMaxHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(1u, 0u));
    float H01 = getMaxHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(0u, 1u));
    float dHdx = (H10 - H00) / pow(2.0, float(MaxLevels - level));
    float dHdz = (H01 - H00) / pow(2.0, float(MaxLevels - level));
    return normalize(cross(vec3(0.0, dHdz, 1.0), vec3(1.0, dHdx, 0.0)));
  }
#endif
  return -last.offset;
}

#ifdef HALTON_SEQUENCE
uint Primes[16] = uint[16](2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53);
#define RAND(j) halton(SampleCount, Primes[(i * 3 + j) % 16])
#else
#define RAND(j) rand(last.pos + Dither[j])
#endif

// Traces a path.
vec3 tracePath(vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection last = Intersection(org, vec3(0.0));
  vec3 res = vec3(1.0);

  for (int i = 0; i < MaxTracedRays; i++) {
    float distance = castRay(testPoint, last, dir);
    if (distance < 0.0) return res * getSkyColor(dir);
    vec3 normal = getNormal(testPoint, last);

    // Russian roulette.
    // const float P = 0.2f;
    // if (rand(p.pos) <= P) return vec3(0.0);
    // res /= (1.0 - P);

    // Bounce (`dir` is updated later).
    testPoint -= last.offset;
    last.offset = -last.offset;

    // Surface sampleColor.
    vec3 col = getPalette(normal);
    res *= col;

    // Importance sampling.
    float prob = dot(normal, -SunlightDirection) > 0.0 ? ProbabilityToSun : 0.0;
    bool towardsSun = RAND(2) < prob;
    res /= (towardsSun? prob : 1.0 - prob);

    if (towardsSun) {
      float alpha = (RAND(0) - 0.5) * SunlightAngle;
      float beta = RAND(1) * 2.0 * Pi;
      dir = vec3(cos(alpha), sin(alpha) * sin(beta), sin(alpha) * cos(beta));

      vec3 tangent = normalize(cross(-SunlightDirection, vec3(1.0, 0.0, 0.0)));
      vec3 bitangent = cross(-SunlightDirection, tangent);
      dir = mat3(-SunlightDirection, tangent, bitangent) * dir;

      float proj = dot(normal, dir);
      res *= proj;

      bool clear = castRay(testPoint, last, dir) < 0.0;
      return clear ? res * getSkyColor(dir) : vec3(0.0);

    } else {
      float alpha = acos(1.0 - RAND(0) * 2.0); // Note: `acos(1.0 - RAND(0))` is for semisphere.
      float beta = RAND(1) * 2.0 * Pi;
      dir = vec3(cos(alpha), sin(alpha) * sin(beta), sin(alpha) * cos(beta));

      float proj = dot(normal, dir);
      if (proj < 0.0) {
        proj = -proj;
        dir += 2.0 * proj * normal;
      }
      res *= proj;
    }
  }

  return res * getSkyColor(dir);
}

#undef RAND

// Casts a single ray (interactive mode).
vec3 testCastRay(vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection last = Intersection(org, vec3(0.0));
  float distance = castRay(testPoint, last, dir);
  if (distance < 0.0) return getSkyColor(dir);
  vec3 normal = getNormal(testPoint, last);

  vec3 background = getSkyColor(dir);
  vec3 res = getPalette(normal);
  res = mix(res * 0.5, res, clamp(dot(normal, -SunlightDirection), 0.0, 1.0));
  res = mix(background, res, clamp((1.0 - distance) * 2.0, 0.0, 1.0));
  return res;
}

// Returns the number of iterations required for casting a single ray.
float profileCastRay(vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection last = Intersection(org, vec3(0.0));
  float distance = castRay(testPoint, last, dir);
  if (distance < 0.0) return 0.0;
  return distance;
}

// Depth of Field.
void apertureDither(inout vec3 pos, inout vec3 dir, float focalDist, float apertureSize) {
  vec3 focus = pos + dir * focalDist;
  float r = sqrt(rand(dir)), theta = rand(dir + Dither[0]) * 2.0 * Pi;
  vec4 shift = vec4(r * cos(theta), r * sin(theta), 0.0, 1.0) * apertureSize;
  pos += (ModelViewInverse * shift).xyz;
  dir = normalize(focus - pos);
}

void main() {
  // Obtain fragment coordinates.
  ivec2 pixelCoords = ivec3(gl_GlobalInvocationID).xy;
  if (pixelCoords.x >= FrameWidth || pixelCoords.y >= FrameHeight) return;
  vec2 fragCoords = vec2(pixelCoords) / vec2(float(FrameWidth), float(FrameHeight)) * 2.0 - 1.0;

  // Apply anti-aliasing.
  vec2 ditheredCoords = fragCoords;
#ifdef ANTI_ALIASING
  if (PathTracing) {
    float randx = rand(vec3(fragCoords, 1.0)) * 2.0 - 1.0;
    float randy = rand(vec3(fragCoords, -1.0)) * 2.0 - 1.0;
    ditheredCoords += vec2(randx / float(FrameWidth), randy / float(FrameHeight));
  }
#endif

  // Calculate parameters.
  RootSize = float(1u << MaxLevels);
  NoiseTextureSize = float(1u << NoiseLevels);
  HeightScale = RootSize / 256.0;
  vec3 dir = normalize(divide(ModelViewInverse * ProjectionInverse * vec4(ditheredCoords, 1.0, 1.0)));
  vec3 centerDir = normalize(divide(ModelViewInverse * ProjectionInverse * vec4(0.0, 0.0, 1.0, 1.0)));
  vec3 pos = CameraPosition + vec3(RootSize / 2.0 + 0.5, RootSize + 0.5, RootSize / 2.0 + 0.5);
#ifdef DEPTH_OF_FIELD
  apertureDither(pos, dir, RootSize / 6.0 / dot(dir, centerDir), 0.0);
#endif
  LodCenterPos = pos;
  LodViewDir = centerDir;

  // Calculate sample color.
  vec3 sampleColor;
  if (PathTracing) {
    sampleColor = tracePath(pos, dir);
  } else if (ProfilerOn) {
    sampleColor = vec3(profileCastRay(pos, dir));
  } else {
    sampleColor = testCastRay(pos, dir);
  }

  // Add sample color to average in path-tracing mode.
  vec4 fragColor;
  if (PathTracing && SampleCount != 0) {
    vec3 prevSamples = texelFetch(PrevFrame, pixelCoords, 0).rgb;
    fragColor = vec4((sampleColor + prevSamples * float(SampleCount)) / float(SampleCount + 1), 1.0);
  } else {
    fragColor = vec4(sampleColor, 1.0);
  }

  // Write outputs.
  imageStore(FrameBuffer, pixelCoords, fragColor);
  if (pixelCoords.xy == ivec2(0, 0)) OutputCount = NodeCount;
}
