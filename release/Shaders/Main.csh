#version 430 core
// #extension GL_NV_shader_thread_shuffle : require

// ===== Inputs =====

layout (local_size_x = 8u, local_size_y = 8u, local_size_z = 1u)
in;

#define NUM_FRAMES 2
uniform sampler2D FrameTexture[NUM_FRAMES];
uniform sampler2D NoiseTexture;
uniform sampler2D MaxTexture;
uniform sampler2D MinTexture;

uniform uint FrameWidth;
uniform uint FrameHeight;
uniform bool PathTracing;
uniform bool ProfilerOn;
uniform uint PrevBeamSize; // = 1u if no previous beam results.
uniform uint PrevFrameIndex;
uniform uint CurrBeamSize; // = 1u if not generating new beams.
uniform uint CurrFrameIndex;

// uniform mat4 ProjectionMatrix;
// uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;
uniform vec3 CameraPosition;
uniform float CameraFov;
// uniform float Time;
uniform float RandomSeed;

uniform bool DynamicMode;
uniform uint MaxNodes;
uniform uint MaxLevels;
uniform uint NoiseLevels; // Noise map detail level `<= MaxLevels`.
uniform uint PartialLevels; // Min noise level (using part of the noise map.)
uniform float LodQuality; // 1.0 = high quality (side length = 1px.)

layout (std430, binding = 0) restrict
buffer TreeData {
  uint NodeCount;
  uint Data[];
};

// ===== Outputs =====

layout (rgba32f) restrict writeonly
uniform image2D FrameImage[NUM_FRAMES];

layout (std430, binding = 1) restrict writeonly
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
// #define FRACTAL_NOISE_USE_TEXTURESAMPLER
// #define FRACTAL_NOISE_COSINE_INTERPOLATION

// Rendering distance.
const uint MaxIterations = 256u; // 64u;

// Level of details.
vec3 LodCenterPos;
vec3 LodViewDir;

// Path tracing.
// #define CAST_RAY_USE_KD_RESTART
// #define CAST_RAY_USE_MULTICAST
// const uint MultiCastX = 2u;
// const uint MultiCastY = 2u;
#define TERRAIN_GRADIENT_NORMAL
#define HALTON_SEQUENCE
const uint MaxTracedRays = 2u;
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
  pow(vec3(144.0, 105.0, 64.0) / 255.0, vec3(Gamma)),
  pow(vec3(151.0, 228.0, 90.0) / 255.0, vec3(Gamma)),
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
  uvec3 sgn = uvec3(greaterThanEqual(normal, vec3(0.0)));
  mat3 col = mat3(Palette[0 + sgn.x], Palette[2 + sgn.y], Palette[4 + sgn.z]);
  vec3 mag = abs(normal);
  return col * (mag * mag);
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

// Allocates new slots.
uint allocate(uint count) {
  uint res = atomicAdd(NodeCount, count);
  if (res + count > uint(MaxNodes)) return 0u;
  for (uint i = 0; i < count; i++) Data[res + i] = 0u;
  return res;
}

// Special states.
#define IS_INVALID(data) (data == 0u)
#define IS_LOCKED(data) (data == 1u)

// Least significant 2bits: [01] intermediate; [11] leaf.
#define IS_LEAF(data) ((data & 3u) == 3u)
#define CHILD_PTR(data) (data >> 2u)
#define LEAF_DATA(data) (data >> 2u)
#define MAKE_INTERMEDIATE(ind) ((ind << 2u) + 1u)
#define MAKE_LEAF(data) ((data << 2u) + 3u)

// Level 0 is the least detailed level (one pixel).
#ifdef FRACTAL_NOISE_USE_TEXTURESAMPLER
float noise2DSubpixel(uint level, uvec2 x, bool maximum) {
  float size = 1.0 / float(1u << level);
  vec2 pos = vec2(x) * size;
#define F(x, y) (textureLod(NoiseTexture, pos + vec2(x, y) + vec2(0.5) / NoiseTextureSize, 0.0).r)
  vec4 res = vec4(F(0, 0), F(size, 0), F(0, size), F(size, size));
  return maximum ?
    max(max(res[0], res[1]), max(res[2], res[3])) :
    min(min(res[0], res[1]), min(res[2], res[3]));
#undef F
}
#else
float noise2DSubpixel(uint level, uvec2 x, bool maximum) {
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
#ifdef FRACTAL_NOISE_COSINE_INTERPOLATION
  fx = (vec4(1.0) - cos(Pi * fx)) / 2.0;
  fy = (vec4(1.0) - cos(Pi * fy)) / 2.0;
#endif
  vec4 res = mat4((1.0 - fx) * (1.0 - fy), fx * (1.0 - fy), (1.0 - fx) * fy, fx * fy) * tex;
  return maximum ?
    max(max(res[0], res[1]), max(res[2], res[3])) :
    min(min(res[0], res[1]), min(res[2], res[3]));
}
#endif

float noise2D(uint level, uvec2 x, bool maximum) {
  if (level > NoiseLevels) return noise2DSubpixel(level, x, maximum);
  return maximum ?
    texelFetch(MaxTexture, ivec2(x), int(NoiseLevels - level)).r :
    texelFetch(MinTexture, ivec2(x), int(NoiseLevels - level)).r;
}

/*
float Factor[16] = float[](
  1.0, 1.0, 1.0, 1.0, // Low freq
  1.0, 1.0, 0.7, 0.5,
  0.3, 0.2, 0.1, 0.0,
  0.0, 0.0, 0.0, 0.0  // High freq
);
*/

float getHeight(uint level, uvec2 lpos, bool maximum) {
  float res = 0.0, amplitude = pow(2.0, float(PartialLevels));
  level += PartialLevels;
  for (uint i = 0u; i + NoiseLevels <= MaxLevels + PartialLevels; i++) {
    res += noise2D(level, lpos, maximum) * amplitude; // * Factor[i];
    amplitude *= 0.5;
    if (level > 0u) {
      level--;
      lpos -= (lpos & (1u << level));
    }
  }
  return res * HeightScale;
}

// Returns `true` if node is large enough. Modified in beam mode.
bool lodCheck(uint level, uvec3 pos) {
  vec3 rpos = (vec3(pos) + 0.5) * RootSize / float(1u << level) - LodCenterPos;
  float size = RootSize / float(1u << level);
  float real = tan(CameraFov / 2.0) * dot(rpos, LodViewDir) * 2.0 / float(FrameHeight);
  return (CurrBeamSize > 1u) ?
    size > real / LodQuality && size > real * float(CurrBeamSize) :
    size > real / LodQuality /* / (1.0 + 1.0 * constructFloat(hash(pos))) */;
}

uint generateNode(uint level, uvec3 lpos) {
  uint maxHeight = uint(getHeight(level, lpos.xz, true));
  if (maxHeight <= (lpos.y << (MaxLevels - level))) {
    return MAKE_LEAF(0u);
  }
  uint minHeight = uint(getHeight(level, lpos.xz, false));
  if (minHeight >= ((lpos.y + 1u) << (MaxLevels - level))) {
    return MAKE_LEAF(1u);
  }
  if (level >= MaxLevels) {
    return MAKE_LEAF(1u);
  }
  return MAKE_INTERMEDIATE(allocate(8u));
}

// Returns node at `ptr`, generating it if needed.
// If node is being generated by another invocation, returns 1u.
uint getNode(uint ptr, uint level, uvec3 lpos) {
  uint cdata = Data[ptr];
  if (DynamicMode) {
    if (cdata == 0u) {
      cdata = atomicCompSwap(Data[ptr], 0u, 1u);
      if (cdata == 0u) {
        cdata = generateNode(level, lpos);
        uint tmp = cdata;
        atomicExchange(Data[ptr], tmp);
      }
    }
  }
  return cdata;
}

// Intersects ray with box (assuming ray starts from inside).
Intersection innerIntersect(vec3 org, vec3 dir, Box box) {
  vec3 tMax = max((box.xyz - org) / dir, (box.xyz - org + box.w) / dir);
  float tFar = min(tMax.x, min(tMax.y, tMax.z));
  return Intersection(org + dir * tFar, mix(vec3(0.0), sign(dir), equal(tMax, vec3(tFar))));
}

// Intersects ray with box (assuming ray starts from outside).
// If there is no intersection, returns a structure with `offset == vec3(0.0)`.
Intersection outerIntersect(vec3 org, vec3 dir, Box box) {
  vec3 tMin = min((box.xyz - org) / dir, (box.xyz - org + box.w) / dir);
  vec3 tMax = max((box.xyz - org) / dir, (box.xyz - org + box.w) / dir);
  float tNear = max(tMin.x, max(tMin.y, tMin.z));
  float tFar = min(tMax.x, min(tMax.y, tMax.z));
  if (tFar < tNear || tNear < 0.0) return Intersection(org, vec3(0.0));
  return Intersection(org + dir * tNear, mix(vec3(0.0), sign(dir), equal(tMin, vec3(tNear))));
}

// Returns the innermost tree node (either leaf or locked) at a given position.
// Pre: `pos` must be inside the root box.
Node getNodeAt(uvec3 pos) {
  Box box = Box(vec3(0.0), RootSize);
  uint ptr = 0u;
  for (uint level = 0u; level <= MaxLevels; level++) {
    uint cdata = getNode(ptr, level, pos >> (MaxLevels - level));
    if (cdata == 1u) return Node(1u, level, box); // Locked.
    // Check if reached leaf.
    if (IS_LEAF(cdata)) return Node(cdata, level, box);
    // Check if out of LOD.
    if (!lodCheck(level, pos >> (MaxLevels - level))) return Node(1u, level, box);
    ptr = CHILD_PTR(cdata);
    vec3 mid = box.xyz + box.w / 2.0;
    if (pos.x >= mid.x) { ptr += 1u; box.x = mid.x; }
    if (pos.y >= mid.y) { ptr += 2u; box.y = mid.y; }
    if (pos.z >= mid.z) { ptr += 4u; box.z = mid.z; }
    box.w /= 2.0;
  }
  return Node(1u, MaxLevels, box);
}

#ifdef CAST_RAY_USE_KD_RESTART

// Casts a ray through the octree.
// Returns the number of iterations divided by `MaxIterations`.
float castRay(inout vec3 testPoint, inout Intersection last, vec3 ref, vec3 dir) {
  dir = normalize(dir);
  Box box = Box(vec3(0.0), RootSize); // Root box.
  // Ensure that ray starts inside the root box.
  if (!inside(last.pos, box)) {
    last = outerIntersect(last.pos, dir, box);
    if (last.offset == vec3(0.0)) return -1.0; // Out of range.
    // Update the test point.
    testPoint = clamp(last.pos, box.xyz + 0.5, box.xyz + box.w - 0.5);
  }
  for (uint i = 0u; i < MaxIterations; i++) {
    Node node = getNodeAt(uvec3(testPoint));
    if (node.data == 1u) return float(i) / float(MaxIterations); // Locked.
    if (LEAF_DATA(node.data) != 0u) return float(i) / float(MaxIterations); // Opaque block.
    // Start from `ref` each time to avoid accumulation of errors.
    Intersection p = innerIntersect(ref, dir, node.box);
    // Update the test point.
    // Mid: `p.pos` lies on a box boundary, with normal = `p.offset`.
    testPoint = clamp(p.pos, node.box.xyz + 0.5, node.box.xyz + node.box.w - 0.5) + p.offset;
    last = p;
    if (!inside(testPoint, box)) return -1.0; // Out of range.
  }
  // Too many iterations.
  return 1.0;
}

#else

#define STACK_SIZE 20

struct Entry {
  uint data;
  Box box;
} stack[STACK_SIZE];

// Saved stack pointer.
uint stp = 0u;

// Casts a ray through the octree.
// Returns the number of iterations divided by `MaxIterations`.
float castRay(inout vec3 testPoint, inout Intersection last, vec3 ref, vec3 dir) {
  dir = normalize(dir);
  Box box = Box(vec3(0.0), RootSize); // Root box.

  // Ensure that ray starts inside the root box.
  if (!inside(last.pos, box)) {
    last = outerIntersect(last.pos, dir, box);
    if (last.offset == vec3(0.0)) return -1.0; // Out of range.
    // Update the test point.
    testPoint = clamp(last.pos, box.xyz + 0.5, box.xyz + box.w - 0.5);
  }

  Entry curr;
  if (stp == 0u) {
    // Set up stack.
    curr.box = box; // Root box.
    curr.data = getNode(0u, stp, uvec3(0u)); // Root data.
  } else {
    // Reuse stack (pop top element).
    stp--;
    curr = stack[stp];
    while (!inside(testPoint, curr.box)) {
      if (stp == 0) return -1.0; // Out of range.
      stp--;
      curr = stack[stp];
    }
  }

  // Inv: current detail level == `stp`.
  for (uint i = 0u; i < MaxIterations; i++) {
    uvec3 pos = uvec3(testPoint);

    // Check if reached leaf.
    while (curr.data != 1u && !IS_LEAF(curr.data)) {
      if (stp >= STACK_SIZE) return -1.0; // Stack overflow.
      stack[stp] = curr;
      stp++;

      uint ptr = CHILD_PTR(curr.data);
      vec3 mid = curr.box.xyz + curr.box.w / 2.0;
      if (testPoint.x >= mid.x) { ptr += 1u; curr.box.x = mid.x; }
      if (testPoint.y >= mid.y) { ptr += 2u; curr.box.y = mid.y; }
      if (testPoint.z >= mid.z) { ptr += 4u; curr.box.z = mid.z; }
      curr.box.w /= 2.0;

      curr.data = getNode(ptr, stp, pos >> (MaxLevels - stp));
      // Check if out of LOD.
      if (!IS_LEAF(curr.data) && !lodCheck(stp, pos >> (MaxLevels - stp))) curr.data = 1u;
    }

    if (curr.data == 1u) return float(i) / float(MaxIterations); // Locked.
    if (LEAF_DATA(curr.data) != 0u) return float(i) / float(MaxIterations); // Opaque block.

    // Start from `ref` each time to avoid accumulation of errors.
    Intersection p = innerIntersect(ref, dir, curr.box);
    // Update the test point.
    // Mid: `p.pos` lies on a box boundary, with normal = `p.offset`.
    testPoint = clamp(p.pos, curr.box.xyz + 0.5, curr.box.xyz + curr.box.w - 0.5) + p.offset;
    last = p;

    while (!inside(testPoint, curr.box)) {
      if (stp == 0) return -1.0; // Out of range.
      stp--;
      curr = stack[stp];
    }
  }

  // Too many iterations.
  return 1.0;
}

#endif

// Use terrain-gradient-based normal for dynamic mode (experimental).
vec3 getNormal(vec3 testPoint, Intersection last) {
#ifdef TERRAIN_GRADIENT_NORMAL
  if (DynamicMode) {
    Node node = getNodeAt(uvec3(testPoint));
    uint level = node.level;
    float h00 = getHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(0u, 0u), true);
    float h10 = getHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(1u, 0u), true);
    float h01 = getHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(0u, 1u), true);
    float dhdx = (h10 - h00) / pow(2.0, float(MaxLevels - level));
    float dhdz = (h01 - h00) / pow(2.0, float(MaxLevels - level));
    return normalize(cross(vec3(0.0, dhdz, 1.0), vec3(1.0, dhdx, 0.0)));
  }
#endif
  return -last.offset;
}

#ifdef HALTON_SEQUENCE
uint Primes[16] = uint[16](2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53);
#define RAND(j) halton(floatBitsToUint(RandomSeed), Primes[(i * 3 + j) % 16])
#else
#define RAND(j) rand(last.pos + Dither[j])
#endif

// Traces a path.
vec3 tracePath(vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection last = Intersection(org, vec3(0.0));
  vec3 res = vec3(1.0);

  for (uint i = 0u; i < MaxTracedRays; i++) {
    float distance = castRay(testPoint, last, last.pos, dir);
    if (distance < 0.0) return res * getSkyColor(dir);
    vec3 normal = getNormal(testPoint, last);

    // Russian roulette.
    // const float P = 0.2f;
    // if (rand(p.pos) <= P) return vec3(0.0);
    // res /= (1.0 - P);

    // Bounce (`dir` is updated later).
    testPoint -= last.offset;
    last.offset = -last.offset;

    // Surface color.
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

      bool clear = castRay(testPoint, last, last.pos, dir) < 0.0;
      return clear ? res * getSkyColor(dir) : vec3(0.0);

    } else {
      float alpha = acos(1.0 - RAND(0) * 2.0); // Note: `acos(1.0 - RAND(0))` is for semisphere.
      float beta = RAND(1) * 2.0 * Pi;
      dir = vec3(cos(alpha), sin(alpha) * sin(beta), sin(alpha) * cos(beta));

      float proj = dot(normal, dir);
      res *= proj;
    }
  }

  return res * getSkyColor(dir);
}

#undef RAND

// Casts a beam.
float beamCastRay(vec3 ref, vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection last = Intersection(org, vec3(0.0));
  float distance = castRay(testPoint, last, ref, dir);
  if (distance < 0.0) return 1e18;
  // Back offset (account for possible edges and corners.)
  float real = tan(CameraFov / 2.0) * dot(last.pos - ref, LodViewDir) * 2.0 / float(FrameHeight);
  return length(last.pos - ref) - real * float(CurrBeamSize) / sqrt(2.0);
}

// Casts a single ray (interactive mode).
vec3 testCastRay(vec3 ref, vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection last = Intersection(org, vec3(0.0));
  float distance = castRay(testPoint, last, ref, dir);
  if (distance < 0.0) return getSkyColor(dir);
  vec3 normal = getNormal(testPoint, last);

  vec3 background = getSkyColor(dir);
  vec3 res = getPalette(normal);
  res = mix(res * 0.2, res, clamp(dot(normal, -SunlightDirection), 0.0, 1.0));
  res = mix(background, res, clamp((1.0 - distance) * 2.0, 0.0, 1.0));
  // Wireframe outlines.
  uvec3 fs = uvec3(lessThanEqual(abs(last.pos - round(last.pos)), vec3(0.05)));
  if (fs.x + fs.y + fs.z >= 2u) res = mix(res * 0.2, res, clamp(length(last.pos - ref) / 256.0, 0.0, 1.0));
  return res;
}

// Returns the number of iterations required for casting a single ray.
vec3 profileCastRay(vec3 ref, vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection last = Intersection(org, vec3(0.0));
  float distance = castRay(testPoint, last, ref, dir);
  if (distance < 0.0) return vec3(0.0);
  /*
  // Warp-wide maximum.
  float warpMax = distance;
  warpMax = max(warpMax, shuffleXorNV(warpMax, 16,32));
  warpMax = max(warpMax, shuffleXorNV(warpMax, 8, 32));
  warpMax = max(warpMax, shuffleXorNV(warpMax, 4, 32));
  warpMax = max(warpMax, shuffleXorNV(warpMax, 2, 32));
  warpMax = max(warpMax, shuffleXorNV(warpMax, 1, 32));
  distance = warpMax;
  */
  return vec3(distance);
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
  // Calculate additional constants from uniforms.
  RootSize = float(1u << MaxLevels);
  NoiseTextureSize = float(1u << NoiseLevels);
  HeightScale = RootSize / 256.0;
  const vec3 pos = CameraPosition;
  const vec3 centerDir = normalize(divide(ModelViewInverse * ProjectionInverse * vec4(0.0, 0.0, 1.0, 1.0)));
  LodCenterPos = floor(pos) + 0.5;
  LodViewDir = centerDir;

  // Obtain pixel coordinates.
  uvec2 pixelIndices00 = gl_GlobalInvocationID.xy;
#ifdef CAST_RAY_USE_MULTICAST
  if (CurrBeamSize == 1u) pixelIndices00 *= uvec2(MultiCastX, MultiCastY);
#endif
  if (pixelIndices00.x >= FrameWidth || pixelIndices00.y >= FrameHeight) return;

  // Retrieve previous beam results.
  float beamResult = 0.0;
  if (PrevBeamSize > 1u) {
    uint k = PrevBeamSize / CurrBeamSize;
    float b00 = texelFetch(FrameTexture[PrevFrameIndex], ivec2(pixelIndices00 / k) + ivec2(0, 0), 0).r;
    float b10 = texelFetch(FrameTexture[PrevFrameIndex], ivec2(pixelIndices00 / k) + ivec2(1, 0), 0).r;
    float b01 = texelFetch(FrameTexture[PrevFrameIndex], ivec2(pixelIndices00 / k) + ivec2(0, 1), 0).r;
    float b11 = texelFetch(FrameTexture[PrevFrameIndex], ivec2(pixelIndices00 / k) + ivec2(1, 1), 0).r;
    beamResult = min(min(b00, b10), min(b01, b11)) - 1e-2;
  }
  /*
  if (CurrBeamSize == 1u) {
    imageStore(FrameBuffer, ivec2(pixelIndices00), vec4(vec3(beamResult / 65536.0), 1.0));
    return;
  }
  */
  // Beam mode.
  if (CurrBeamSize > 1u) {
    vec2 fragCoords00 = vec2(pixelIndices00 * CurrBeamSize) / vec2(float(FrameWidth), float(FrameHeight)) * 2.0 - 1.0;
    vec3 dir00 = normalize(divide(ModelViewInverse * ProjectionInverse * vec4(fragCoords00, 1.0, 1.0)));
    float value = beamCastRay(pos, pos + dir00 * beamResult, dir00);
    imageStore(FrameImage[CurrFrameIndex], ivec2(pixelIndices00), vec4(value));
    return;
  }

#ifdef CAST_RAY_USE_MULTICAST
  for (uint i = 0u; i < MultiCastX; i++) {
    for (uint j = 0u; j < MultiCastY; j++) {
      uvec2 pixelIndices = pixelIndices00 + uvec2(i, j);
      if (pixelIndices.x >= FrameWidth || pixelIndices.y >= FrameHeight) continue;
#else
  uvec2 pixelIndices = pixelIndices00;
#endif

  // Apply anti-aliasing.
  vec2 fragCoords = vec2(pixelIndices) / vec2(float(FrameWidth), float(FrameHeight)) * 2.0 - 1.0;
  vec2 ditheredCoords = fragCoords;
#ifdef ANTI_ALIASING
  if (PathTracing) {
    float randx = rand(vec3(fragCoords, 1.0)) * 2.0 - 1.0;
    float randy = rand(vec3(fragCoords, -1.0)) * 2.0 - 1.0;
    ditheredCoords += vec2(randx / float(FrameWidth), randy / float(FrameHeight));
  }
#endif

  // Apply depth-of-field.
  vec3 dir = normalize(divide(ModelViewInverse * ProjectionInverse * vec4(ditheredCoords, 1.0, 1.0)));
#ifdef DEPTH_OF_FIELD
  apertureDither(pos, dir, RootSize / 6.0 / dot(dir, centerDir), 0.0);
#endif

  // Calculate fragment color.
  vec3 fragColor =
    PathTracing ? tracePath(pos + dir * beamResult, dir) :
    ProfilerOn ? profileCastRay(pos, pos + dir * beamResult, dir) :
    testCastRay(pos, pos + dir * beamResult, dir);

  // Write destination pixel.
  imageStore(FrameImage[CurrFrameIndex], ivec2(pixelIndices), vec4(fragColor, 1.0));

#ifdef CAST_RAY_USE_MULTICAST
    }
  }
#endif

  // Additional outputs.
  if (pixelIndices00.xy == uvec2(0, 0)) {
    OutputCount = NodeCount;
  }
}
