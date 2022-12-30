#version 430 core

// ===== Input constants =====

layout(local_size_x = 8u, local_size_y = 8u, local_size_z = 1u) in;

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;
uniform vec3 CameraPosition;
uniform float RandomSeed;
uniform sampler2D NoiseTexture;
uniform sampler2D MaxTexture;
uniform sampler2D MinTexture;
uniform float NoiseTextureSize;
uniform vec2 NoiseOffset;
uniform float Time;
uniform int PathTracing;
uniform int ProfilerOn;

uniform sampler2D PrevFrame;
uniform int SampleCount;
uniform int FrameWidth;
uniform int FrameHeight;
uniform int FrameBufferSize;
uniform int MaxNodes;

layout(std430, binding = 1) buffer TreeData {
  uint NodeCount;
  uint Data[];
};

// ===== Output variables =====

vec4 FragColor;
uniform restrict writeonly image2D FrameBuffer;

layout(std430, binding = 2) buffer OutputData {
  uint OutputCount;
};

// ===== Structures & Constants =====

#define Box vec4

struct Node {
  uint data;
  Box box;
};

struct Intersection {
  vec3 pos;
  vec3 offset; // Points forwards, axis aligned
};

// const float Eps = 1e-4;
const float Pi = 3.14159265f;
const float Gamma = 2.2f;

// Lighting
#define LIGHTING_SUN (vec3(1.0f, 0.7f, 0.5f) * 1.5f)
// #define LIGHTING_UNIFORM
// #define LIGHTING_SKY
const vec3 SunlightDirection = normalize(vec3(0.8f, -1.0f, 0.3f)); // normalize(vec3(cos(Time), -0.1f, sin(Time)));
const float SunlightAngle = 0.1f;

// World size (maximum octree detail level)
const uint MaxLevels = 16u;
float RootSize;

// Terrain generation
// #define FRACTALNOISE_USE_TEXTURESAMPLER
// #define FRACTALNOISE_COSINE_INTERPOLATION
const uint NoiseLevels = 8u; // Noise map detail level <= MaxLevels, matches the noise map resolution in main program
const uint PartialLevels = 7u; // - Min noise level (using part of the noise map)
const float HeightScale = float(1u << MaxLevels) / 256.0f;

// Rendering distance
const uint MaxIterations = 512u;

// Level of details
#define LEVEL_OF_DETAILS
#define LOD_CHECK_ALWAYS
const float FoVy = 70.0f;
const float VerticalResolution = 480.0f; // 2160.0f;
const float LODQuality = 1.0f;

// Path tracing
const int MaxTracedRays = 2;
const float ProbabilityToSun = 0.5f;
#define HALTON_SEQUENCE
#define ANTI_ALIASING

// FPS will be effectively halved (at least on GTX1060 Max-Q, driver 441.20) if you make these arrays constant!
/*
vec3 Palette[7] = vec3[](
  vec3(0.0f, 0.0f, 0.0f),
  pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
  pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
  pow(vec3(151.0f, 228.0f, 90.0f) / 255.0f, vec3(Gamma)),
  pow(vec3(144.0f, 105.0f, 64.0f) / 255.0f, vec3(Gamma)),
  pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
  pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma))
);
*/
vec3 Palette = pow(vec3(200.0f, 200.0f, 200.0f) / 255.0f, vec3(Gamma));
vec3 Dither[3] = vec3[](
  vec3(12.1322f, 23.1313423f, 34.959f),
  vec3(23.183f, 11.232f, 54.9923f),
  vec3(345.99253f, 2345.2323f, 78.1233f)
);

// ===== Utilities =====

vec3 divide(vec4 v) { return (v / v.w).xyz; }
bool inside(vec3 a, Box box) { return all(greaterThanEqual(a, box.xyz)) && all(lessThan(a, box.xyz + box.w)); }

// ===== PRNG =====

float constructFloat(uint m) {
  const uint IEEEMantissa = 0x007FFFFFu;
  const uint IEEEOne = 0x3F800000u;
  m = m & IEEEMantissa | IEEEOne;
  return uintBitsToFloat(m) - 1.0f;
}
uint hash(uint x) { x += x << 10u; x ^= x >> 6u; x += x << 3u; x ^= x >> 11u; x += x << 15u; return x; }
uint hash(uvec2 v) { return hash(v.x ^ hash(v.y)); }
uint hash(uvec3 v) { return hash(v.x ^ hash(v.yz)); }
uint hash(uvec4 v) { return hash(v.x ^ hash(v.yzw)); }
float rand(vec3 v) { return constructFloat(hash(floatBitsToUint(vec4(v, RandomSeed)))); }
float rand3(vec3 v) { return constructFloat(hash(floatBitsToUint(v))); }

// https://en.wikipedia.org/wiki/Halton_sequence (b is prime)
float halton(int i, int b) {
  float f = 1, r = 0;
  while (i > 0) {
    f /= float(b);
    r += f * float(i % b);
    i /= b;
  }
  return r;
}

// ===== Main Part =====

uint allocateNodes(uint count) {
  uint res = atomicAdd(NodeCount, count);
  if (res + count > uint(MaxNodes)) return 0u;
  for (uint i = 0; i < count; i++) Data[res + i] = 0u;
  return res;
}

// Least significant 2bits: [00] not generated; [01] intermediate; [11] leaf
#define getPrimitiveData(ind) Data[ind]
#define isLeaf(data) ((data & 3u) == 3u)
#define getChildrenPtr(data) (data >> 2u)
#define getLeafData(data) (data >> 2u)
#define makeIntermediateNodeData(ind) ((ind << 2u) + 1u)
#define makeLeafNodeData(data) ((data << 2u) + 3u)

// Level 0: least detailed (one pixel)

#ifdef FRACTALNOISE_USE_TEXTURESAMPLER

#define F(x, y) (textureLod(NoiseTexture, pos + vec2(x, y) + vec2(0.5f) / NoiseTextureSize, 0.0f).r)
float maxNoise2DSubpixel(uint level, uvec2 x) {
  float size = 1.0f / float(1u << level);
  vec2 pos = vec2(x) * size;
  return max(max(F(0, 0), F(size, 0)), max(F(0, size), F(size, size)));
}
#undef F

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
  fx = (vec4(1.0f) - cos(Pi * fx)) / 2.0f;
  fy = (vec4(1.0f) - cos(Pi * fy)) / 2.0f;
#endif
  vec4 res = mat4((vec4(1.0f) - fx) * (vec4(1.0f) - fy), fx * (vec4(1.0f) - fy), (vec4(1.0f) - fx) * fy, fx * fy) * tex;
  return max(max(res[0], res[1]), max(res[2], res[3]));
}

#endif

float maxNoise2D(uint level, uvec2 x) {
  if (level > NoiseLevels) return maxNoise2DSubpixel(level, x);
  return texelFetch(MaxTexture, ivec2(x), int(NoiseLevels - level)).r;
}
/*
float Factor[16] = float[](
  1.0f, 1.0f, 1.0f, 1.0f, // Low freq
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f,
  1.0f, 1.0f, 1.0f, 1.0f  // High freq
);
*/
float getMaxHeight(uint level, uvec2 pos) {
  float res = 0.0f, amplitude = pow(2.0f, float(PartialLevels));
  level += PartialLevels;
  for (uint i = 0u; i <= MaxLevels - NoiseLevels + PartialLevels; i++) {
    res += maxNoise2D(level, pos) * amplitude; //* Factor[i];
    amplitude *= 0.5f;
    if (level > 0u) {
      level--;
      pos -= (pos & (1u << level));
    }
  }
  return res * HeightScale;
}

// TODO: use an "averaging" approximation in LOD
vec3 lodCenterPos, lodViewDir;
bool lodCheck(uint level, uvec3 pos) {
#ifndef LEVEL_OF_DETAILS
  return true;
#endif
  vec3 rpos = (vec3(pos) + vec3(0.5f)) * RootSize / float(1u << level) - lodCenterPos;
  float size = sqrt(3.0f) * RootSize / float(1u << level);
  return tan(FoVy / 2.0f / 180.0f * Pi) * dot(rpos, lodViewDir) * 2.0f / VerticalResolution / LODQuality <= size;
}

int generateNodeTerrain(uint level, uvec3 pos) {
  if ((uint(getMaxHeight(level, pos.xz)) >> (MaxLevels - level)) < pos.y) return 0;
  return (level < MaxLevels) ? -1 : 1;
}

Node getNodeAt(vec3 pos) {
  Box box = Box(vec3(0.0f), RootSize);
  if (!inside(pos, box)) return Node(0u, box); // Outside
  uint ptr = 0u, cdata = 0u;
  for (uint level = 0u; level <= MaxLevels; level++) {
#ifdef LOD_CHECK_ALWAYS
    if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level))) return Node(makeLeafNodeData(1u), box);
#endif
    cdata = Data[ptr];
    if (cdata == 0u) {
#ifndef LOD_CHECK_ALWAYS
      if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level))) return Node(makeLeafNodeData(1u), box);
#endif
      if (atomicCompSwap(Data[ptr], 0u, 1u) == 0u) { // Not generated, generate node!
        int curr = generateNodeTerrain(level, uvec3(pos) >> (MaxLevels - level));
        cdata = curr < 0 ? makeIntermediateNodeData(allocateNodes(8u)) : makeLeafNodeData(uint(curr));
        atomicExchange(Data[ptr], cdata);
      } else { // Being generated by another invocation. Will be rendered next frame.
        return Node(makeLeafNodeData(0u), box);
      }
    } else if (cdata == 1u) { // Being generated by another invocation. Will be rendered next frame.
      return Node(makeLeafNodeData(0u), box);
    }
    if (isLeaf(cdata)) break; // Reached leaf (monotonous node)
    ptr = getChildrenPtr(cdata);
    vec3 mid = box.xyz + box.w / 2.0f;
    if (pos.x >= mid.x) { ptr += 1u; box.x = mid.x; }
    if (pos.y >= mid.y) { ptr += 2u; box.y = mid.y; }
    if (pos.z >= mid.z) { ptr += 4u; box.z = mid.z; }
    box.w /= 2.0f;
  }
  return Node(cdata, box);
}

uint getNodeLevel(vec3 pos) {
  Box box = Box(vec3(0.0f), RootSize);
  if (!inside(pos, box)) return 0u; // Outside
  uint ptr = 0u, cdata = 0u;
  for (uint level = 0u; level <= MaxLevels; level++) {
    cdata = Data[ptr];
    if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level)) || cdata <= 1u) return level;
    if (isLeaf(cdata) || level == MaxLevels) return MaxLevels; // Reached leaf (monotonous node)
    ptr = getChildrenPtr(cdata);
    vec3 mid = box.xyz + box.w / 2.0f;
    if (pos.x >= mid.x) { ptr += 1u; box.x = mid.x; }
    if (pos.y >= mid.y) { ptr += 2u; box.y = mid.y; }
    if (pos.z >= mid.z) { ptr += 4u; box.z = mid.z; }
    box.w /= 2.0f;
  }
  return MaxLevels;
}

Intersection innerIntersect(vec3 org, vec3 dir, Box box) {
  vec3 boxMin = box.xyz;
  vec3 boxMax = box.xyz + box.w;
  vec3 tMax = max((boxMin - org) / dir, (boxMax - org) / dir);
  float tMaxMin = min(tMax.x, min(tMax.y, tMax.z));
  return Intersection(
    org + dir * tMaxMin,
    mix(vec3(0.0f), sign(dir), equal(tMax, vec3(tMaxMin)))
  );
}

Intersection outerIntersect(vec3 org, vec3 dir, Box box) {
  vec3 boxMin = box.xyz;
  vec3 boxMax = box.xyz + box.w;
  vec3 tMin = min((boxMin - org) / dir, (boxMax - org) / dir);
  vec3 tMax = max((boxMin - org) / dir, (boxMax - org) / dir);
  float tMinMax = max(tMin.x, max(tMin.y, tMin.z));
  float tMaxMin = min(tMax.x, min(tMax.y, tMax.z));
  if (tMaxMin <= tMinMax || tMinMax <= 0) return Intersection(org, vec3(0.0f));
  return Intersection(
    org + dir * tMinMax,
    mix(vec3(0.0f), sign(dir), equal(vec3(tMinMax), tMin))
  );
}

float rayMarch(inout vec3 testPoint, inout Intersection last, in vec3 dir) {
  dir = normalize(dir);
  Box box = Box(vec3(0.0f), RootSize); // Root box
  // Ensure that ray starts inside the root box.
  if (!inside(last.pos, box)) {
    last = outerIntersect(last.pos, dir, box);
    if (last.offset == vec3(0.0f)) return -1.0; // Out of range.
    // Update the test point.
    testPoint = clamp(last.pos, box.xyz + 0.5, box.xyz + box.w - 0.5);
  }
  // Start from `org` each time to avoid accumulation of errors.
  Intersection org = last;
  for (int i = 0; i < MaxIterations; i++) {
    Node node = getNodeAt(testPoint);
    if (node.data == 0u) return -1.0; // Out of range.
    if (getLeafData(node.data) != 0u) return float(i) / float(MaxIterations); // Opaque block.
    Intersection p = innerIntersect(org.pos, dir, node.box);
    // Update the test point.
    // Mid: `p.pos` lies on a box boundary, with normal = `p.offset`.
    testPoint = clamp(p.pos, node.box.xyz + 0.5, node.box.xyz + node.box.w - 0.5) + p.offset;
    last = p;
  }
  // Too many iterations.
  return 1.0f;
}

vec3 getSkyLight(vec3 dir) {
  dir = normalize(dir);
  if (dot(-SunlightDirection, dir) >= cos(SunlightAngle / 2.0f)) {
    return LIGHTING_SUN;
  }
#if defined(LIGHTING_SKY)
  vec3 sky = mix(
    vec3(152.0f / 255.0f, 211.0f / 255.0f, 250.0f / 255.0f),
    vec3(90.0f / 255.0f, 134.0f / 255.0f, 206.0f / 255.0f),
    smoothstep(0.0f, 1.0f, dir.y * 2.0f)
  );
  return sky;
#elif defined(LIGHTING_UNIFORM)
  return vec3(1.0f);
#else
  return vec3(0.0f);
#endif
}

#ifdef HALTON_SEQUENCE
int Primes[16] = int[16](2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53);
#define RAND(j) halton(SampleCount, Primes[(i * 3 + j) % 16])
#else
#define RAND(j) rand(p.pos + Dither[j])
#endif

vec3 rayTrace(vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection p = Intersection(org, vec3(0.0f));
  vec3 res = vec3(1.0f);

  for (int i = 0; i < MaxTracedRays; i++) {
    float distance = rayMarch(testPoint, p, dir);
    if (distance < 0.0f) return res * getSkyLight(dir);

    // Russian roulette.
    // const float P = 0.2f;
    // if (rand(p.pos) <= P) return vec3(0.0f);
    // res /= (1.0f - P);

    // Bounce.
    testPoint -= p.offset;
    p.offset = -p.offset;
    // float proj = dot(p.offset, dir);
    // dir += 2.0f * proj * p.offset;

    // Surface color.
    vec3 col = Palette;
    res *= col;

    // Importance sampling.
    float pr = (dot(p.offset, -SunlightDirection) > -0.1f ? ProbabilityToSun : 0.0f);
    bool towardsSun = RAND(2) < pr;
    res /= (towardsSun? pr : 1.0f - pr);

    if (towardsSun) {
      float alpha = (RAND(0) - 0.5f) * SunlightAngle;
      float beta = RAND(1) * 2.0f * Pi;
      dir = vec3(cos(alpha), sin(alpha) * sin(beta), sin(alpha) * cos(beta));

      vec3 tangent = normalize(cross(-SunlightDirection, vec3(1.0f, 0.0f, 0.0f)));
      vec3 bitangent = cross(-SunlightDirection, tangent);
      dir = mat3(-SunlightDirection, tangent, bitangent) * dir;

      bool clear = rayMarch(testPoint, p, dir) < 0.0f;
      return clear ? res * getSkyLight(dir) : vec3(0.0f);
    }

    float alpha = acos(1.0f - RAND(0) * 2.0f); // `acos(1.0f - RAND(0))` for semisphere
    float beta = RAND(1) * 2.0f * Pi;
    dir = vec3(cos(alpha), sin(alpha) * sin(beta), sin(alpha) * cos(beta));

    float proj = dot(p.offset, dir);
    if (proj < 0.0f) {
      proj = -proj;
      dir += 2.0f * proj * p.offset;
    }
    res *= proj;
  }

  return res * getSkyLight(dir);
}

vec3 testTrace(vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection p = Intersection(org, vec3(0.0f));
  float distance = rayMarch(testPoint, p, dir);
  if (distance < 0.0f) return getSkyLight(dir);

  uint level = getNodeLevel(testPoint);
  float H00 = getMaxHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(0u, 0u));
  float H10 = getMaxHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(1u, 0u));
  float H01 = getMaxHeight(level, (uvec2(testPoint.xz) >> (MaxLevels - level)) + uvec2(0u, 1u));
  float dHdx = (H10 - H00) / pow(2.0f, float(MaxLevels - level));
  float dHdz = (H01 - H00) / pow(2.0f, float(MaxLevels - level));
  vec3 normal = normalize(cross(vec3(0.0f, dHdz, 1.0f), vec3(1.0f, dHdx, 0.0f)));

  vec3 background = getSkyLight(vec3(0.0f, 1.0f, 0.0f));
  vec3 res = vec3(1.0f);
  res = mix(background, res, clamp(dot(normal, -SunlightDirection), 0.0f, 1.0f));
  res = mix(background, res, clamp((1.0f - distance) * 2.0f, 0.0f, 1.0f));
  return res;
}

float marchProfiler(vec3 org, vec3 dir) {
  vec3 testPoint = org;
  Intersection p = Intersection(org, vec3(0.0f));
  float distance = rayMarch(testPoint, p, dir);
  if (distance < 0.0f) return 0.0f;
  return distance;
}

// Depth of Field
void apertureDither(inout vec3 pos, inout vec3 dir, float focalDist, float apertureSize) {
  vec3 focus = pos + dir * focalDist;
  float r = sqrt(rand(dir)), theta = rand(dir + Dither[0]) * 2.0f * Pi;
  vec4 shift = vec4(r * cos(theta), r * sin(theta), 0.0f, 1.0f) * apertureSize;
  pos += (ModelViewInverse * shift).xyz;
  dir = normalize(focus - pos);
}

void main() {
  RootSize = float(1u << MaxLevels);

  ivec2 outputPixelCoords = ivec3(gl_GlobalInvocationID).xy;
  if (outputPixelCoords.x >= FrameWidth || outputPixelCoords.y >= FrameHeight) return;
  vec2 FragCoords = vec2(outputPixelCoords) / vec2(float(FrameWidth), float(FrameHeight)) * 2.0f - vec2(1.0f);

  float randx = rand(vec3(FragCoords, 1.0f)) * 2.0f - 1.0f, randy = rand(vec3(FragCoords, -1.0f)) * 2.0f - 1.0f;
  vec2 ditheredCoords = FragCoords;
#ifdef ANTI_ALIASING
  if (PathTracing != 0) ditheredCoords = FragCoords + vec2(randx / float(FrameWidth), randy / float(FrameHeight)); // Anti-aliasing
#endif

  vec4 fragPosition = ModelViewInverse * ProjectionInverse * vec4(ditheredCoords, 1.0f, 1.0f);
  vec4 centerFragPosition = ModelViewInverse * ProjectionInverse * vec4(0.0f, 0.0f, 1.0f, 1.0f);

  vec3 dir = normalize(divide(fragPosition));
  vec3 centerDir = normalize(divide(centerFragPosition));

  vec3 pos = CameraPosition + vec3(RootSize / 2.0f + 0.5f, RootSize + 0.5f, RootSize / 2.0f + 0.5f);
  apertureDither(pos, dir, RootSize / 6.0f / dot(dir, centerDir), 0.0f);

  lodCenterPos = pos, lodViewDir = centerDir;

  vec3 color;
  if (PathTracing != 0) {
    color = rayTrace(pos, dir);
  } else if (ProfilerOn != 0) {
    color = vec3(marchProfiler(pos, dir));
  } else {
    color = testTrace(pos, dir);
  }

  if (PathTracing == 0) FragColor = vec4(color, 1.0f);
  else if (SampleCount == 0) FragColor = vec4(color, 1.0f);
  else {
    vec3 texel = texelFetch(PrevFrame, outputPixelCoords, 0).rgb;
    FragColor = vec4((color + texel * float(SampleCount)) / float(SampleCount + 1), 1.0f);
  }

  imageStore(FrameBuffer, outputPixelCoords, FragColor);
  if (outputPixelCoords.xy == ivec2(0, 0)) OutputCount = NodeCount;
}
