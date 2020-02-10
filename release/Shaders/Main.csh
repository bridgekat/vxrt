#version 430 core

// ===== Input constants =====

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
//uniform int RenderMode; // 0 for test, 1 for shadowed, 2 for profiling, 3 for pathtracing
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

#define AABB mat2x3

struct Node {
	uint data;
	AABB box;
};

struct Intersection {
	vec3 pos;
	int face; // 0 for undefined, 1 ~ 6 for x+, x-, y+, y-, z+, z-
};

const float Eps = 1e-4;
const float Pi = 3.14159265f;
const float Gamma = 2.2f;
//#define LIGHTING_UNIFORM 1.0f
//#define LIGHTING_SKY
const vec3 SunlightDirection = normalize(vec3(0.6f, -1.0f, 0.3f)); //normalize(vec3(cos(Time), -0.1f, sin(Time)));
const float SunlightAngle = 0.996f;

// World size (maximum octree detail level)
const uint MaxLevels = 16u;

// Terrain generation
//#define FRACTALNOISE_USE_TEXTURESAMPLER
#define FRACTALNOISE_COSINE_INTERPOLATION
const uint NoiseLevels = 8u; // Noise map detail level <= MaxLevels, matches the noise map resolution in main program
const uint PartialLevels = 7u; // - Min noise level (using part of the noise map)
const float HeightScale = float(1u << MaxLevels) / 256.0f;

// Level of details
#define LEVEL_OF_DETAILS
const float FoVy = 70.0f;
const float VerticalResolution = 480.0f;
const float LODQuality = 1.0f;

// Path tracing
const int MaxTracedRays = 4;
//#define ANTIALIASING
#define LAMBERTIAN_DIFFUSE
const float DiffuseFactor = 0.5f;
const float ProbabilityToSun = 0.5f;

// Fps will be effectively halved (at least on GTX1060 Max-Q, driver 441.20) if you make these arrays constant!
int BackFace[7] = int[](0, 2, 1, 4, 3, 6, 5);
vec3 Normal[7] = vec3[](
	vec3( 0.0f, 0.0f, 0.0f),
	vec3(+1.0f, 0.0f, 0.0f),
	vec3(-1.0f, 0.0f, 0.0f),
	vec3( 0.0f,+1.0f, 0.0f),
	vec3( 0.0f,-1.0f, 0.0f),
	vec3( 0.0f, 0.0f,+1.0f),
	vec3( 0.0f, 0.0f,-1.0f)
);
vec3 Palette[7] = vec3[](
	vec3(0.0f, 0.0f, 0.0f),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(151.0f, 228.0f, 90.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(144.0f, 105.0f, 64.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma)),
	pow(vec3(147.5f, 166.4f, 77.0f) / 255.0f, vec3(Gamma))
);
vec3 Dither[3] = vec3[](
	vec3(12.1322f, 23.1313423f, 34.959f),
	vec3(23.183f, 11.232f, 54.9923f),
	vec3(345.99253f, 2345.2323f, 78.1233f)
);

// ===== Utilities =====

vec3 divide(vec4 v) { return (v / v.w).xyz; }
bool inside(vec3 a, AABB box) { return all(greaterThanEqual(a, box[0])) && all(lessThan(a, box[1])); }

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
//	if (x.x >= (1u << NoiseLevels) || x.y >= (1u << NoiseLevels)) discard;
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
	vec3 rpos = (vec3(pos) + vec3(0.5f)) * float(RootSize) / float(1u << level) - lodCenterPos;
	float size = sqrt(3.0f) * float(RootSize) / float(1u << level);
	return tan(FoVy / 2.0f / 180.0f * Pi) * dot(rpos, lodViewDir) * 2.0f / VerticalResolution / LODQuality <= size;
}

int generateNodeTerrain(uint level, uvec3 pos) {
	if ((uint(getMaxHeight(level, pos.xz)) >> (MaxLevels - level)) < pos.y) return 0;
//	if (((getMinHeight(level, pos.xz) + 1u) >> (MaxLevels - level)) > pos.y) return 0;
	return (level < MaxLevels) ? -1 : 1;
}

Node getNodeAt(vec3 pos) {
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize)));
	if (!inside(pos, box)) return Node(0u, box); // Outside
	uint ptr = 0u, cdata = 0u;
	for (uint level = 0u; level <= MaxLevels; level++) {
//		if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level))) return Node(makeLeafNodeData(1u), box);
		cdata = Data[ptr];
		if (cdata == 0u) {
			if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level))) return Node(makeLeafNodeData(1u), box);
			if (atomicCompSwap(Data[ptr], 0u, 1u) == 0u) { // Not generated, generate node!
				int curr = generateNodeTerrain(level, uvec3(pos) >> (MaxLevels - level));
				cdata = curr < 0 ? makeIntermediateNodeData(allocateNodes(8)) : makeLeafNodeData(curr);
				atomicExchange(Data[ptr], cdata);
			} else return Node(makeLeafNodeData(0u), box); // Being generated by another invocation. Will be rendered next frame.
		}
		if (cdata == 1u) return Node(makeLeafNodeData(0u), box); // Being generated by another invocation. Will be rendered next frame.
		if (isLeaf(cdata)) break; // Reached leaf (monotonous node)
		ptr = getChildrenPtr(cdata);
		vec3 mid = (box[0] + box[1]) * 0.5f;
		if (pos.x >= mid.x) { ptr += 1u; box[0].x = mid.x; } else box[1].x = mid.x;
		if (pos.y >= mid.y) { ptr += 2u; box[0].y = mid.y; } else box[1].y = mid.y;
		if (pos.z >= mid.z) { ptr += 4u; box[0].z = mid.z; } else box[1].z = mid.z;
/*
		bvec3 k = greaterThanEqual(pos, mid);
		ptr += uint(k.x) * 1u + uint(k.y) * 2u + uint(k.z) * 4u;
		box[0] = mix(box[0], mid, k);
		box[1] = mix(mid, box[1], k);
*/
	}
	return Node(cdata, box);
}

uint getNodeLevel(vec3 pos) {
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize)));
	if (!inside(pos, box)) return 0u; // Outside
	uint ptr = 0u, cdata = 0u;
	for (uint level = 0u; level <= MaxLevels; level++) {
		cdata = Data[ptr];
		if (!lodCheck(level, uvec3(pos) >> (MaxLevels - level)) || cdata <= 1u) return level;
		if (isLeaf(cdata) || level == MaxLevels) return MaxLevels; // Reached leaf (monotonous node)
		ptr = getChildrenPtr(cdata);
		vec3 mid = (box[0] + box[1]) * 0.5f;
		if (pos.x >= mid.x) { ptr += 1u; box[0].x = mid.x; } else box[1].x = mid.x;
		if (pos.y >= mid.y) { ptr += 2u; box[0].y = mid.y; } else box[1].y = mid.y;
		if (pos.z >= mid.z) { ptr += 4u; box[0].z = mid.z; } else box[1].z = mid.z;
	}
	return MaxLevels;
}

Intersection innerIntersect(vec3 org, vec3 dir, AABB box/*, int ignore*/) {
	float scale[7] = float[](
		0.0f,
		(box[0].x - org.x) / dir.x, // x- (Reversed from normal)
		(box[1].x - org.x) / dir.x, // x+
		(box[0].y - org.y) / dir.y, // y-
		(box[1].y - org.y) / dir.y, // y+
		(box[0].z - org.z) / dir.z, // z-
		(box[1].z - org.z) / dir.z  // z+
	);
	int face = 0;
	for (int i = 1; i <= 6; i++) if (dot(dir, Normal[i]) < 0.0f && scale[i] > 0.0f && 
			(face == 0 || scale[i] < scale[face])) face = i;
	return Intersection(org + dir * scale[face], face);
}

Intersection outerIntersect(vec3 org, vec3 dir, AABB box) {
	float scale[7] = float[](
		0.0f,
		(box[1].x - org.x) / dir.x, // x+
		(box[0].x - org.x) / dir.x, // x-
		(box[1].y - org.y) / dir.y, // y+
		(box[0].y - org.y) / dir.y, // y-
		(box[1].z - org.z) / dir.z, // z+
		(box[0].z - org.z) / dir.z  // z-
	);
	int face = 0;
	for (int i = 1; i <= 6; i++) if (scale[i] > 0.0f && inside((org + dir * scale[i]) - 0.1f * Normal[i], box) &&
		(face == 0 || scale[i] < scale[face])) face = i;
	return Intersection(org + dir * scale[face], face);
}

Intersection rayMarch(Intersection p, vec3 dir) {
	dir = normalize(dir);
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize))); // Root box
	if (!inside(p.pos, box)) p = outerIntersect(p.pos, dir, box);
	for (int i = 0; i < RootSize; i++) {
		Node node = getNodeAt(p.pos - 0.1f * Normal[p.face]);
		if (node.data == 0u) break; // Out of range
		if (getLeafData(node.data) != 0u) return p; // Opaque block
		p = innerIntersect(p.pos, dir, node.box/*, BackFace[p.face]*/);
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
		if (node.data == 0u || getLeafData(node.data) != 0u) return i;
		p = innerIntersect(p.pos, dir, node.box/*, BackFace[p.face]*/);
	}	
	return RootSize;
}

vec3 getSkyColor(in vec3 org, in vec3 dir) {
#ifdef LIGHTING_UNIFORM
	return vec3(LIGHTING_UNIFORM);
#endif
	float sun = mix(0.0f, 0.7f, clamp(smoothstep(SunlightAngle, 1.0f, dot(dir, -SunlightDirection)), 0.0f, 1.0f)) * 400.0f;
#ifndef LIGHTING_SKY
	return vec3(sun);
#endif
	sun += mix(0.0f, 0.3f, clamp(smoothstep(0.1f, 1.0f, dot(dir, -SunlightDirection)), 0.0f, 1.0f));
	vec3 sky = mix(
		vec3(152.0f / 255.0f, 211.0f / 255.0f, 250.0f / 255.0f),
		vec3(90.0f / 255.0f, 134.0f / 255.0f, 206.0f / 255.0f),
		smoothstep(0.0f, 1.0f, normalize(dir).y * 2.0f)
	);
	vec3 res = mix(sky, vec3(1.0f, 1.0f, 1.0f), sun);
	return res;
}

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
	
	float sunlight = clamp(dot(Normal[p.face], -SunlightDirection), -1.0f, 1.0f);
	sunlight = sunlight / 2.0f + 0.5f;
	p.pos += Eps * (-SunlightDirection);
	if (dot(Normal[p.face], -SunlightDirection) <= 0.0f ||
			rayMarch(Intersection(p.pos, 0), -SunlightDirection).face != 0u) sunlight *= 0.5f;
	res *= sunlight;
	
	return res;
}

vec3 testTrace(vec3 org, vec3 dir) {
	vec3 res = vec3(1.0f);
	Intersection p = Intersection(org, 0);
	
	p = rayMarch(p, dir);
	if (p.face == 0) return res * getSkyColor(org, dir);
	
	vec3 col = Palette[p.face];
//	res *= col;
	org = p.pos - 0.1f * Normal[p.face];
	
	uint level = getNodeLevel(org);
	float H00 = getMaxHeight(level, (uvec2(org.xz) >> (MaxLevels - level)) + uvec2(0u, 0u));
	float H10 = getMaxHeight(level, (uvec2(org.xz) >> (MaxLevels - level)) + uvec2(1u, 0u));
	float H01 = getMaxHeight(level, (uvec2(org.xz) >> (MaxLevels - level)) + uvec2(0u, 1u));
	float dHdx = (H10 - H00) / pow(2.0f, float(MaxLevels - level));
	float dHdz = (H01 - H00) / pow(2.0f, float(MaxLevels - level));
	vec3 normal = normalize(cross(vec3(0.0f, dHdz, 1.0f), vec3(1.0f, dHdx, 0.0f)));
	
	float sunlight = clamp(dot(normal, -SunlightDirection), 0.0f, 1.0f);
//	sunlight = sunlight / 2.0f + 0.5f;
//	p.pos += Eps * (-SunlightDirection);
//	if (dot(Normal[p.face], -SunlightDirection) <= 0.0f ||
//			rayMarch(Intersection(p.pos, 0), -SunlightDirection).face != 0u) sunlight *= 0.5f;
	res *= sunlight;
	
	return res; //vec3(float(level) / 17.0f);
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
	vec2 FragCoords = vec2(outputPixelCoords) / vec2(float(FrameWidth), float(FrameHeight)) * 2.0f - vec2(1.0f);
	
	RootSize = 1 << int(MaxLevels);
	
	float randx = rand(vec3(FragCoords, 1.0f)) * 2.0f - 1.0f, randy = rand(vec3(FragCoords, -1.0f)) * 2.0f - 1.0f;
	vec2 ditheredCoords = FragCoords;
#ifdef ANTIALIASING
	if (PathTracing != 0) ditheredCoords += vec2(randx / float(FrameWidth), randy / float(FrameHeight)); // Anti-aliasing
#endif
	
	vec4 fragPosition = ModelViewInverse * ProjectionInverse * vec4(ditheredCoords, 1.0f, 1.0f);
	vec4 centerFragPosition = ModelViewInverse * ProjectionInverse * vec4(0.0f, 0.0f, 1.0f, 1.0f);
	
	vec3 dir = normalize(divide(fragPosition));
	vec3 centerDir = normalize(divide(centerFragPosition));
	
	vec3 pos = CameraPosition + vec3(float(RootSize) / 2.0f + 0.233f, float(RootSize) + 0.233f, float(RootSize) / 2.0f + 0.233f);
	apertureDither(pos, dir, float(RootSize) / 6.0f / dot(dir, centerDir), 0.0f);
	
	lodCenterPos = pos, lodViewDir = centerDir;
	
	vec3 color = vec3(0.0f);
	if (PathTracing == 0) color = (ProfilerOn == 0) ? testTrace(pos, dir) : vec3(float(marchProfiler(pos, dir)) / 64.0f);
	else color = rayTrace(pos, dir);
	
	if (PathTracing == 0) FragColor = vec4(color, 1.0f);
	else if (SampleCount == 0) FragColor = vec4(color, 1.0f);
	else {
		vec3 texel = texelFetch(PrevFrame, outputPixelCoords, 0).rgb;
		FragColor = vec4((color + texel * float(SampleCount)) / float(SampleCount + 1), 1.0f);
	}
	
	imageStore(FrameBuffer, outputPixelCoords, FragColor);
	if (outputPixelCoords.xy == ivec2(0, 0)) OutputCount = NodeCount;
}
