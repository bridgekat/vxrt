#version 430 core

layout(local_size_x = 8u, local_size_y = 8u, local_size_z = 1u) in;

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;
/*uniform */int RootSize;
uniform vec3 CameraPosition;
uniform float RandomSeed;
uniform sampler2D NoiseTexture;
uniform sampler2D MaxTexture;
uniform sampler2D MinTexture;
uniform float NoiseTextureSize;
uniform vec2 NoiseOffset;
uniform float Time;

uniform int PathTracing;
uniform sampler2D PrevFrame;
uniform int SampleCount;
uniform int FrameWidth;
uniform int FrameHeight;
uniform int FrameBufferSize;
/*
layout(std430) buffer TreeData {
	uint data[];
};
*/
//in vec2 FragCoords;
//out vec4 FragColor;
vec2 FragCoords;
vec4 FragColor;
uniform restrict writeonly layout(rgba32f) image2D FrameBuffer;

// Constants

const float Eps = 1e-4;
const float Pi = 3.14159265f;
const float Gamma = 2.2f;
const vec3 SunlightDirection = normalize(vec3(0.6f, -1.0f, 0.3f));
const float SunlightAngle = 0.996f;
const float ProbabilityToSun = 0.0f;
const uint MaxLevels = 12u; // Octree detail level
const uint NoiseLevels = 8u; // Noise map detail level <= MaxLevels, matches the noise map resolution in main program
const uint PartialLevels = 7u; // - Min noise level (using part of the noise map)
const float HeightScale = float(1u << MaxLevels) / 256.0f;

// Utilities

vec3 divide(vec4 v) { return (v / v.w).xyz; }

// PRNG

uint hash(uint x) {
	x += x << 10u;
	x ^= x >> 6u;
	x += x << 3u;
	x ^= x >> 11u;
	x += x << 15u;
	return x;
}

uint hash(uvec2 v) { return hash(v.x ^ hash(v.y)); }
uint hash(uvec3 v) { return hash(v.x ^ hash(v.yz)); }
uint hash(uvec4 v) { return hash(v.x ^ hash(v.yzw)); }

float constructFloat(uint m) {
	const uint IEEEMantissa = 0x007FFFFFu;
	const uint IEEEOne = 0x3F800000u;
	m = m & IEEEMantissa | IEEEOne;
	return uintBitsToFloat(m) - 1.0f;
}

float rand(vec3 v) { return constructFloat(hash(floatBitsToUint(vec4(v, RandomSeed)))); }

// Main Part

struct AABB {
	vec3 a, b;
};

bool inside(vec3 a, AABB box) {
	return a.x >= box.a.x && a.x < box.b.x && 
		a.y >= box.a.y && a.y < box.b.y &&
		a.z >= box.a.z && a.z < box.b.z;
}

const uint Root = 1u;
const int MaxTracedRays = 4;
const float DiffuseFactor = 0.5f;
#define LAMBERTIAN_DIFFUSE
//#define REDUNDANCY_CHECK

/*
#define getPrimitiveData(ind) uint(data[ind])
bool isLeaf(uint ind) { return (getPrimitiveData(ind) & 1u) != 0u; }
uint getData(uint ind) { return getPrimitiveData(ind) >> 1u; }
uint getChildrenPtr(uint ind) { return (getPrimitiveData(ind) >> 1u) + Root; }
*/

uint getData(uint ind) { return ind - 1u; }

struct Node {
	uint ptr;
	AABB box;
};

/*
Node getNodeAt(vec3 pos) {
	uint ptr = Root;
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize)));
	if (!inside(pos, box)) return Node(0u, box); // Outside
	while (!isLeaf(ptr)) {
		ptr = getChildrenPtr(ptr);
		vec3 mid = (box.a + box.b) / 2.0f;
		if (pos.x >= mid.x) {
			ptr += 1u;
			box.a.x = mid.x;
		} else box.b.x = mid.x;
		if (pos.y >= mid.y) {
			ptr += 2u;
			box.a.y = mid.y;
		} else box.b.y = mid.y;
		if (pos.z >= mid.z) {
			ptr += 4u;
			box.a.z = mid.z;
		} else box.b.z = mid.z;
	}
	return Node(ptr, box);
}
*/

// Level 0: least detailed (one pixel)
/*
float linearSample(vec2 pos) {
	ivec2 p = ivec2(pos * NoiseTextureSize);
	vec2 f = fract(pos * NoiseTextureSize);
	int size = int(NoiseTextureSize);
	float t00 = texelFetch(NoiseTexture, (p + ivec2(0, 0)) % size, 0).r, t10 = texelFetch(NoiseTexture, (p + ivec2(1, 0)) % size, 0).r;
	float t01 = texelFetch(NoiseTexture, (p + ivec2(0, 1)) % size, 0).r, t11 = texelFetch(NoiseTexture, (p + ivec2(1, 1)) % size, 0).r;
	return t00 * (1.0f - f.x) * (1.0f - f.y) + t01 * (1.0f - f.x) * f.y + t10 * f.x * (1.0f - f.y) + t11 * f.x * f.y;
}

#define F(x, y) (textureLod(NoiseTexture, pos + vec2(x, y) + vec2(0.5f) / NoiseTextureSize, 0.0f).r)
//#define F(x, y) (linearSample(pos + vec2(x, y)))

float maxNoise2DSubpixel(uint level, uvec2 x) {
	float size = 1.0f / float(1u << level);
	vec2 pos = vec2(x) * size;
	return max(max(F(0, 0), F(size, 0)), max(F(0, size), F(size, size)));
}

#undef F
*/
/*
float maxNoise2DSubpixel(uint level, uvec2 x) {
	float size = NoiseTextureSize / float(1u << level);
	vec2 pos = vec2(x) * size;
	ivec2 p = ivec2(pos);
	int mask = int(NoiseTextureSize) - 1;
	float t00 = texelFetch(NoiseTexture, (p + ivec2(0, 0)) & mask, 0).r;
	float t10 = texelFetch(NoiseTexture, (p + ivec2(1, 0)) & mask, 0).r;
	float t01 = texelFetch(NoiseTexture, (p + ivec2(0, 1)) & mask, 0).r;
	float t11 = texelFetch(NoiseTexture, (p + ivec2(1, 1)) & mask, 0).r;
	vec2 f00 = fract(pos) + vec2(0, 0), f10 = fract(pos) + vec2(size, 0), f01 = fract(pos) + vec2(0, size), f11 = fract(pos) + vec2(size, size);
	float r00 = t00 * (1.0f - f00.x) * (1.0f - f00.y) + t01 * (1.0f - f00.x) * f00.y + t10 * f00.x * (1.0f - f00.y) + t11 * f00.x * f00.y;
	float r10 = t00 * (1.0f - f10.x) * (1.0f - f10.y) + t01 * (1.0f - f10.x) * f10.y + t10 * f10.x * (1.0f - f10.y) + t11 * f10.x * f10.y;
	float r01 = t00 * (1.0f - f01.x) * (1.0f - f01.y) + t01 * (1.0f - f01.x) * f01.y + t10 * f01.x * (1.0f - f01.y) + t11 * f01.x * f01.y;
	float r11 = t00 * (1.0f - f11.x) * (1.0f - f11.y) + t01 * (1.0f - f11.x) * f11.y + t10 * f11.x * (1.0f - f11.y) + t11 * f11.x * f11.y;
	return max(max(r00, r01), max(r10, r11));
}
*/
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
	vec4 res = mat4((vec4(1.0f) - fx) * (vec4(1.0f) - fy), fx * (vec4(1.0f) - fy), (vec4(1.0f) - fx) * fy, fx * fy) * tex;
	return max(max(res[0], res[1]), max(res[2], res[3]));
}

float maxNoise2D(uint level, uvec2 x) {
	if (level > NoiseLevels) return maxNoise2DSubpixel(level, x);
//	if (x.x >= (1u << NoiseLevels) || x.y >= (1u << NoiseLevels)) discard;
	return texelFetch(MaxTexture, ivec2(x), int(NoiseLevels - level)).r;
}

uint getMaxHeight(uint level, uvec2 pos) {
	float res = 0.0f, amplitude = pow(2.0f, float(PartialLevels));
	level += PartialLevels;
	for (uint i = 0u; i <= MaxLevels - NoiseLevels + PartialLevels; i++) {
		float curr = maxNoise2D(level, pos);
		res += curr * amplitude;
		amplitude /= 2.0f;
		if (level > 0u) {
			level--;
			pos -= (pos & (1u << level));
		}
	}
	return uint(res * HeightScale);
}

// TODO: use an "averaging" approximation in LOD
vec3 lodCenterPos, lodViewDir;
bool lodCheck(uint level, uvec3 pos) {
	return true;
	vec3 rpos = (vec3(pos) + vec3(0.5f)) * float(RootSize) / float(1u << level) - lodCenterPos;
	float size = sqrt(3.0f) * float(RootSize) / float(1u << level);
	return tan(35.0f / 180.0f * Pi) * dot(rpos, lodViewDir) * 2.0f / 480.0f <= size; // 480p, vertical fov = 70 degrees
}

int generateNode(uint level, uvec3 pos) {
	if ((getMaxHeight(level, pos.xz) >> (MaxLevels - level)) < pos.y) return 0;
//	if (((getMinHeight(level, pos.xz) + 1u) >> (MaxLevels - level)) > pos.y) return 0;
	return (level < MaxLevels && lodCheck(level, pos)) ? -1 : 1;
}

int redundantSubdivisionCount = 0;
Node getNodeAt(vec3 pos) {
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize)));
	if (!inside(pos, box)) return Node(0u, box); // Outside
	int curr = 0;
	for (uint level = 0u; level <= MaxLevels; level++) {
		curr = generateNode(level, uvec3(pos) >> (MaxLevels - level));
		if (curr >= 0) break;
#ifdef REDUNDANCY_CHECK
		bool f = false;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(0u, 0u, 0u)) != 0) f = true;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(0u, 1u, 0u)) != 0) f = true;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(1u, 0u, 0u)) != 0) f = true;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(1u, 1u, 0u)) != 0) f = true;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(0u, 0u, 1u)) != 0) f = true;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(0u, 1u, 1u)) != 0) f = true;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(1u, 0u, 1u)) != 0) f = true;
		if (generateNode(level + 1u, (uvec3(pos) >> (MaxLevels - level)) * 2u + uvec3(1u, 1u, 1u)) != 0) f = true;
		if (!f) redundantSubdivisionCount++;
#endif
		vec3 mid = (box.a + box.b) / 2.0f;
		if (pos.x >= mid.x) box.a.x = mid.x; else box.b.x = mid.x;
		if (pos.y >= mid.y) box.a.y = mid.y; else box.b.y = mid.y;
		if (pos.z >= mid.z) box.a.z = mid.z; else box.b.z = mid.z;
	}
	return Node(uint(curr + 1), box);
}

struct Intersection {
	vec3 pos;
	int face; // 0 for undefined, 1 ~ 6 for x+, x-, y+, y-, z+, z-
};

int BackFace[7] = int[7](0, 2, 1, 4, 3, 6, 5);
vec3 Normal[7] = vec3[7](
	vec3( 0.0f, 0.0f, 0.0f),
	vec3(+1.0f, 0.0f, 0.0f),
	vec3(-1.0f, 0.0f, 0.0f),
	vec3( 0.0f,+1.0f, 0.0f),
	vec3( 0.0f,-1.0f, 0.0f),
	vec3( 0.0f, 0.0f,+1.0f),
	vec3( 0.0f, 0.0f,-1.0f)
);

Intersection innerIntersect(vec3 org, vec3 dir, AABB box, int ignore) {
	float scale[7];
	scale[0] = 0.0f;
	scale[1] = (box.a.x - org.x) / dir.x; // x- (Reversed from normal)
	scale[2] = (box.b.x - org.x) / dir.x; // x+
	scale[3] = (box.a.y - org.y) / dir.y; // y-
	scale[4] = (box.b.y - org.y) / dir.y; // y+
	scale[5] = (box.a.z - org.z) / dir.z; // z-
	scale[6] = (box.b.z - org.z) / dir.z; // z+
	int face = 0;
	for (int i = 1; i <= 6; i++) if (dot(dir, Normal[i]) < 0.0f && scale[i] > 0.0f) {
		if (face == 0 || scale[i] < scale[face]) face = i;
	}
	return Intersection(org + dir * scale[face], face);
}

Intersection outerIntersect(vec3 org, vec3 dir, AABB box) {
	float scale[7];
	scale[0] = 0.0f;
	scale[1] = (box.b.x - org.x) / dir.x; // x+
	scale[2] = (box.a.x - org.x) / dir.x; // x-
	scale[3] = (box.b.y - org.y) / dir.y; // y+
	scale[4] = (box.a.y - org.y) / dir.y; // y-
	scale[5] = (box.b.z - org.z) / dir.z; // z+
	scale[6] = (box.a.z - org.z) / dir.z; // z-
	int face = 0;
	for (int i = 1; i <= 6; i++) if (scale[i] > 0.0f) {
		vec3 curr = org + dir * scale[i];
		if ((face == 0 || scale[i] < scale[face]) && inside(curr - 0.1f * Normal[i], box)) face = i;
	}
	return Intersection(org + dir * scale[face], face);
}

Intersection rayMarch(Intersection p, vec3 dir) {
	dir = normalize(dir);
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize))); // Root box
	if (!inside(p.pos, box)) p = outerIntersect(p.pos, dir, box);
	
	for (int i = 0; i < RootSize; i++) {
		Node node = getNodeAt(p.pos - 0.1f * Normal[p.face]);
		if (node.ptr == 0u) break; // Out of range
		if (getData(node.ptr) != 0u) return p; // Opaque block
		p = innerIntersect(p.pos, dir, node.box, BackFace[p.face]);
	}	
	return Intersection(p.pos, 0);
}

int marchProfiler(vec3 org, vec3 dir) {
	dir = normalize(dir);
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize))); // Root box
	Intersection p = Intersection(org, 0);
	if (!inside(p.pos, box)) p = outerIntersect(p.pos, dir, box);
	
	for (int i = 0; i < RootSize; i++) {
		Node node = getNodeAt(p.pos - 0.1f * Normal[p.face]);
		if (node.ptr == 0u || getData(node.ptr) != 0u) return i;
		p = innerIntersect(p.pos, dir, node.box, BackFace[p.face]);
	}	
	return RootSize;
}

vec3 getSkyColor(in vec3 org, in vec3 dir) {
	return vec3(1.0f);
	float sun = mix(0.0f, 0.7f, clamp(smoothstep(SunlightAngle, 1.0f, dot(dir, -SunlightDirection)), 0.0f, 1.0f)) * 400.0f;
	return vec3(sun);
	sun += mix(0.0f, 0.3f, clamp(smoothstep(0.1f, 1.0f, dot(dir, -SunlightDirection)), 0.0f, 1.0f));
	vec3 sky = mix(
		vec3(152.0f / 255.0f, 211.0f / 255.0f, 250.0f / 255.0f),
		vec3(90.0f / 255.0f, 134.0f / 255.0f, 206.0f / 255.0f),
		smoothstep(0.0f, 1.0f, normalize(dir).y * 2.0f)
	);
	vec3 res = mix(sky, vec3(1.0f, 1.0f, 1.0f), sun);
//	vec4 cloudColor = cloud(org, dir, CloudStep, CloudDistance);
//	res = cloudColor.rgb + (1.0 - cloudColor.a) * res;
	return res;
}

///*
vec3 Palette[7] = vec3[7](
	vec3(0.0f, 0.0f, 0.0f),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(151.0f, 228.0f, 90.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(144.0f, 105.0f, 64.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma))
);
//*/
/*
vec3 Palette[7] = vec3[7](
	vec3(0.0f, 0.0f, 0.0f),
	pow(vec3(0.5f, 0.8f, 0.9f), vec3(Gamma)),
	pow(vec3(0.5f, 0.8f, 0.9f), vec3(Gamma)),
	pow(vec3(0.5f, 0.8f, 0.9f), vec3(Gamma)),
	pow(vec3(0.5f, 0.8f, 0.9f), vec3(Gamma)),
	pow(vec3(0.5f, 0.8f, 0.9f), vec3(Gamma)),
	pow(vec3(0.5f, 0.8f, 0.9f), vec3(Gamma))
);
*/
/*
vec3 Palette[7] = vec3[7](
	vec3(0.0f, 0.0f, 0.0f),
	pow(vec3(238.0f, 213.0f, 255.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(238.0f, 213.0f, 255.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(238.0f, 213.0f, 255.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(238.0f, 213.0f, 255.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(238.0f, 213.0f, 255.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(238.0f, 213.0f, 255.0f) / 255.0f, vec3(Gamma))
);
*/
vec3 Dither[3] = vec3[3](
	vec3(12.1322f, 23.1313423f, 34.959f),
	vec3(23.183f, 11.232f, 54.9923f),
	vec3(345.99253f, 2345.2323f, 78.1233f)
);

vec3 rayTrace(vec3 org, vec3 dir) {
	dir = normalize(dir);
	vec3 res = vec3(1.0f);
	Intersection p = Intersection(org, 0);
	
	for (int i = 0; i < MaxTracedRays; i++) {
		p = rayMarch(p, dir);
		if (p.face == 0) return res * getSkyColor(org, dir);
		
		const float P = 0.2f;
		if (rand(p.pos) <= P) return vec3(0.0f);
		res /= (1.0f - P);
		
		vec3 col = Palette[p.face];
		res *= col;
		org = p.pos;
		
#ifdef LAMBERTIAN_DIFFUSE
		float pr = (dot(Normal[p.face], -SunlightDirection) > 0.1f ? ProbabilityToSun : 0.0f);
		bool towardsSun = (rand(org + Dither[0]) <= pr);
		float pdf = 1.0f - pr;
		
		float alpha = acos(rand(org + Dither[1]) * 2.0f - 1.0f);
		if (towardsSun) alpha = acos(1.0f - rand(org + Dither[1]) * (1.0f - SunlightAngle));
		float beta = rand(org + Dither[2]) * 2.0f * Pi;
		dir = vec3(cos(alpha), sin(alpha) * cos(beta), sin(alpha) * sin(beta));
		
		if (towardsSun) {
			vec3 tangent = normalize(cross(-SunlightDirection, vec3(1.0f, 0.0f, 0.0f)));
			vec3 bitangent = cross(-SunlightDirection, tangent);
			dir = mat3(-SunlightDirection, tangent, bitangent) * dir;
			pdf += pr / (1.0f - SunlightAngle);
		}
		res /= pdf;
#else
		vec3 normal = Normal[p.face];
		vec3 shift = vec3(rand(org + Dither[0]), rand(org + Dither[1]), rand(org + Dither[2])) - vec3(0.5f);
		shift = normalize(shift) * rand(shift);
		normal = normalize(normal + shift * DiffuseFactor);
		dir = reflect(dir, normal);
#endif
		
		float proj = dot(Normal[p.face], dir);
		if (proj < 0.0f) dir -= 2.0f * proj * Normal[p.face], proj *= -1.0f;
		res *= proj;
		
		p.face = BackFace[p.face];
	}
	
	return res * getSkyColor(org, dir);
}

vec3 shadowTrace(vec3 org, vec3 dir) {
	vec3 res = vec3(1.0f);
	Intersection p = Intersection(org, 0);
	
	p = rayMarch(p, dir);
	if (p.face == 0) return res * getSkyColor(org, dir);
	
	vec3 col = Palette[p.face];
	res *= col;
	org = p.pos;
	vec3 normal = Normal[p.face];
	dir = reflect(dir, normal);
	int face = p.face;
	p.pos += Eps * dir;
	
	vec3 SunlightDirection = normalize(vec3(0.6f, -1.0f, 0.3f));
	p = rayMarch(Intersection(p.pos, 0), -SunlightDirection);
	float sunlight = clamp(dot(Normal[face], -SunlightDirection), -1.0f, 1.0f);
	sunlight = sunlight / 2.0f + 0.5f;
	if (p.face != 0) sunlight *= 0.5f;
	res *= sunlight;
	
	return res;
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
	ivec2 outputPixelCoords = ivec3(gl_GlobalInvocationID).xy;
	if (outputPixelCoords.x >= FrameWidth || outputPixelCoords.y >= FrameHeight) return;
	FragCoords = vec2(outputPixelCoords) / vec2(float(FrameWidth), float(FrameHeight)) * 2.0f - vec2(1.0f);
	
	RootSize = 1 << int(MaxLevels);
	
	float randx = rand(vec3(FragCoords, 1.0f)) * 2.0f - 1.0f, randy = rand(vec3(FragCoords, -1.0f)) * 2.0f - 1.0f;
	vec2 ditheredCoords = FragCoords + vec2(randx / float(FrameWidth), randy / float(FrameHeight)); // Anti-aliasing
	
	vec4 fragPosition = ModelViewInverse * ProjectionInverse * vec4(ditheredCoords, 1.0f, 1.0f);
	vec4 centerFragPosition = ModelViewInverse * ProjectionInverse * vec4(0.0f, 0.0f, 1.0f, 1.0f);
	
	vec3 dir = normalize(divide(fragPosition));
	vec3 centerDir = normalize(divide(centerFragPosition));
	
	vec3 pos = CameraPosition * 160.0f + vec3(23.3f, float(RootSize) / 8.0f + 23.3f, 23.3f);
	apertureDither(pos, dir, float(RootSize) / 6.0f / dot(dir, centerDir), 0.0f);
	
	lodCenterPos = pos, lodViewDir = centerDir;
	
	vec3 color = vec3(0.0f);
	if (PathTracing == 0) color = vec3(float(marchProfiler(pos, dir)) / 256.0f); //shadowTrace(pos, dir);
	else color = rayTrace(pos, dir);
	
#ifdef REDUNDANCY_CHECK
	if (redundantSubdivisionCount > 0) color = vec3(1.0f * float(redundantSubdivisionCount) / float(MaxLevels), color.gb);
#endif
	
	if (PathTracing == 0) FragColor = vec4(pow(color, vec3(1.0f / Gamma)), 1.0f);
	else if (SampleCount == 0) FragColor = vec4(color, 1.0f);
	else {
		vec2 texCoord = FragCoords / 2.0f + vec2(0.5f);
		texCoord *= vec2(float(FrameWidth), float(FrameHeight)) / vec2(float(FrameBufferSize));
		vec3 texel = texture(PrevFrame, texCoord).rgb;
		FragColor = vec4((color + texel * float(SampleCount)) / float(SampleCount + 1), 1.0f);
	}
	
	imageStore(FrameBuffer, outputPixelCoords, FragColor);
}
