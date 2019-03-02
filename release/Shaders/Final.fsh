#version 430 core
#extension GL_ARB_shader_storage_buffer_object: enable

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;
uniform int RootSize;
uniform vec3 CameraPosition;
uniform float RandomSeed;

uniform int PathTracing;
uniform sampler2D PrevFrame;
uniform int SampleCount;
uniform int FrameWidth;
uniform int FrameHeight;
uniform int FrameBufferSize;

layout(std430) buffer TreeData {
	uint data[];
};

in vec2 FragCoords;
out vec4 FragColor;

// Constants

const float Eps = 1e-4;
const float Pi = 3.14159265f;
const float Gamma = 2.2f;

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
uint hash(uvec3 v) { return hash(v.x ^ hash(v.y) ^ hash(v.z)); }
uint hash(uvec4 v) { return hash(v.x ^ hash(v.y) ^ hash(v.z) ^ hash(v.w)); }

float constructFloat(uint m) {
	const uint IEEEMantissa = 0x007FFFFFu;
	const uint IEEEOne = 0x3F800000u;
	m = m & IEEEMantissa | IEEEOne;
	return uintBitsToFloat(m) - 1.0;
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
const int MaxTracedRays = 16;
const float DiffuseFactor = 1.0f;

#define getPrimitiveData(ind) uint(data[ind])
bool isLeaf(uint ind) { return (getPrimitiveData(ind) & 1u) != 0u; }
uint getData(uint ind) { return getPrimitiveData(ind) >> 1u; }
uint getChildrenPtr(uint ind) { return (getPrimitiveData(ind) >> 1u) + Root; }

struct Node {
	uint ptr;
	AABB box;
};

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
	for (int i = 1; i <= 6; i++) if (/*dot(dir, Normal[i]) < 0.0f && */i != ignore && scale[i] > 0.0f) {
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

vec3 getSkyColor(in vec3 dir) {
	vec3 SunlightDirection = normalize(vec3(0.6f, -1.0f, 0.3f));
	float sun = mix(0.0f, 0.7f, clamp(smoothstep(0.996f, 1.0f, dot(dir, -SunlightDirection)), 0.0f, 1.0f)) * 4.0f;
//	/*
	sun += mix(0.0f, 0.3f, clamp(smoothstep(0.1f, 1.0f, dot(dir, -SunlightDirection)), 0.0f, 1.0f));
	vec3 sky = mix(
		vec3(152.0f / 255.0f, 211.0f / 255.0f, 250.0f / 255.0f),
		vec3(90.0f / 255.0f, 134.0f / 255.0f, 206.0f / 255.0f),
		smoothstep(0.0f, 1.0f, normalize(dir).y * 2.0f)
	);
	return mix(sky, vec3(1.0f, 1.0f, 1.0f), sun);
//	*/
	return vec3(sun);
}

/*
vec3 Palette[7] = vec3[7](
	vec3(0.0f, 0.0f, 0.0f),
	vec3(147.5f, 166.4f, 77.0f) / 255.0f,
	vec3(147.5f, 166.4f, 77.0f) / 255.0f,
	vec3(151.0f, 228.0f, 90.0f) / 255.0f,
	vec3(144.0f, 105.0f, 64.0f) / 255.0f,
	vec3(147.5f, 166.4f, 77.0f) / 255.0f,
	vec3(147.5f, 166.4f, 77.0f) / 255.0f
);
*/
vec3 Palette[7] = vec3[7](
	vec3(0.0f, 0.0f, 0.0f),
	vec3(0.5f, 0.8f, 0.9f),
	vec3(0.5f, 0.8f, 0.9f),
	vec3(0.5f, 0.8f, 0.9f),
	vec3(0.5f, 0.8f, 0.9f),
	vec3(0.5f, 0.8f, 0.9f),
	vec3(0.5f, 0.8f, 0.9f)
);
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
		if (p.face == 0) return res * getSkyColor(dir);
		
		const float P = 0.2f;
		if (rand(p.pos) <= P) return vec3(0.0f);
		res /= (1.0f - P);
		
		vec3 col = Palette[p.face];
		res *= col;
		org = p.pos;
		
		/*
		vec3 normal = Normal[p.face];
		vec3 shift = vec3(rand(org + Dither[0]), rand(org + Dither[1]), rand(org + Dither[2])) - vec3(0.5f);
		shift = normalize(shift) * rand(shift);
		normal = normalize(normal + shift * DiffuseFactor);
		dir = reflect(dir, normal);
		*/
		
		float alpha = acos(rand(org + Dither[1]) * 2.0f - 1.0f);
		float beta = rand(org + Dither[2]) * 2.0f * Pi;
		dir = vec3(cos(alpha), sin(alpha) * cos(beta), sin(alpha) * sin(beta));
		
		float proj = dot(Normal[p.face], dir);
		if (proj < 0.0f) dir -= 2.0f * proj * Normal[p.face], proj *= -1.0f;
		res *= proj;
		
		p.face = BackFace[p.face];
	}
	
	return vec3(0.0f);// * getSkyColor(dir);
}

vec3 shadowTrace(vec3 org, vec3 dir) {
	vec3 res = vec3(1.0f);
	Intersection p = Intersection(org, 0);
	
	p = rayMarch(p, dir);
	if (p.face == 0) return res * getSkyColor(dir);
	
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
	float randx = rand(vec3(FragCoords, 1.0f)) * 2.0f - 1.0f, randy = rand(vec3(FragCoords, -1.0f)) * 2.0f - 1.0f;
	vec2 ditheredCoords = FragCoords + vec2(randx / float(FrameWidth), randy / float(FrameHeight)); // Anti-aliasing
	
	vec4 fragPosition = ModelViewInverse * ProjectionInverse * vec4(ditheredCoords, 1.0f, 1.0f);
	vec4 centerFragPosition = ModelViewInverse * ProjectionInverse * vec4(0.0f, 0.0f, 1.0f, 1.0f);
	
	vec3 dir = normalize(divide(fragPosition));
	vec3 centerDir = normalize(divide(centerFragPosition));
	
	vec3 pos = CameraPosition + vec3(float(RootSize) / 2.0f + 23.3f);
	apertureDither(pos, dir, float(RootSize) / 8.0f / dot(dir, centerDir), 0.0f);
	
	vec3 color = vec3(0.0f);
	if (PathTracing == 0) color = shadowTrace(pos, dir);
	else color = rayTrace(pos, dir);
//	color = (shadowTrace(pos, dir) + rayTrace(pos, dir)) / 2.0f;
	
//	int res = marchProfiler(pos, dir);
//	color = vec3(float(res) / 100.0f);
	
	if (PathTracing == 0) FragColor = vec4(pow(color, vec3(1.0f / Gamma)), 1.0f);
	else if (SampleCount == 0) FragColor = vec4(color, 1.0f);
    else {
		vec2 texCoord = FragCoords / 2.0f + vec2(0.5f);
		texCoord *= vec2(float(FrameWidth), float(FrameHeight)) / vec2(float(FrameBufferSize));
		vec3 texel = texture(PrevFrame, texCoord).rgb;
		FragColor = vec4((color + texel * float(SampleCount)) / float(SampleCount + 1), 1.0f);
	}
}
