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

vec3 divide(vec4 v) { return (v / v.w).xyz; }

struct AABB {
	vec3 a, b;
};

bool inside(vec3 a, AABB box) {
	return a.x >= box.a.x && a.x < box.b.x && 
		a.y >= box.a.y && a.y < box.b.y &&
		a.z >= box.a.z && a.z < box.b.z;
}

const uint Root = 1u;
const float Eps = 1e-4;
const int MaxTracedRays = 16;
const float DiffuseFactor = 0.05f;
uint getPrimitiveData(uint ind) { return uint(data[ind]); }
bool isLeaf(uint ind) { return mod(getPrimitiveData(ind), 2u) != 0u; }
uint getData(uint ind) { return getPrimitiveData(ind) / 2u; }
uint getChildrenPtr(uint ind) { return getPrimitiveData(ind) / 2u + Root; }

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
	sun += mix(0.0f, 0.3f, clamp(smoothstep(0.1f, 1.0f, dot(dir, -SunlightDirection)), 0.0f, 1.0f));
	vec3 sky = mix(
		vec3(152.0f / 255.0f, 211.0f / 255.0f, 250.0f / 255.0f),
		vec3(90.0f / 255.0f, 134.0f / 255.0f, 206.0f / 255.0f),
		smoothstep(0.0f, 1.0f, normalize(dir).y * 2.0f)
	);
	return mix(sky, vec3(1.0f, 1.0f, 1.0f), sun);
}

float noise3D(vec3 c) {
    return fract(sin(dot(c.xyz, vec3(12.9898f, 78.233f + RandomSeed, 233.666f))) * 43758.5453f);
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
	vec3(0.1f, 0.13f, 0.99f),
	vec3(0.13f, 0.1f, 0.99f),
	vec3(0.99f, 0.1f, 0.13f)
);

vec3 rayTrace(vec3 org, vec3 dir) {
	dir = normalize(dir);
	vec3 res = vec3(1.0f);
	Intersection p = Intersection(org, 0);
	
	for (int i = 0; i < MaxTracedRays; i++) {
		p = rayMarch(p, dir);
		if (p.face == 0) return res * getSkyColor(dir);
		vec3 col = Palette[p.face];
		res *= col;
		org = p.pos;
		vec3 normal = Normal[p.face];
		vec3 shift = vec3(noise3D(org + Dither[0]), noise3D(org + Dither[1]), noise3D(org + Dither[2])) - vec3(0.5f);
		shift = normalize(shift) * noise3D(shift);
		normal = normalize(normal + shift * DiffuseFactor);
		dir = reflect(dir, normal);
		float proj = dot(Normal[p.face], dir);
		if (proj < 0.0) dir -= 2.0f * proj * Normal[p.face];
		p.face = BackFace[p.face];
	}
	
	return res;// * getSkyColor(dir);
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

void main() {
	vec4 fragPosition = ModelViewInverse * ProjectionInverse * vec4(FragCoords, 1.0f, 1.0f);
	vec3 dir = normalize(divide(fragPosition));
	vec3 color = vec3(0.0f);
	
//	int res = marchProfiler(CameraPosition + vec3(float(RootSize) / 2.0f + 1e-2), dir);
//	color = vec3(float(res) / 100.0f);
	vec3 pos = CameraPosition + vec3(float(RootSize) / 2.0f + 23.3f);
	
	vec3 focal = pos + dir * float(RootSize) / 8.0f;
	vec3 shift = vec3(noise3D(dir + Dither[0]), noise3D(dir + Dither[1]), noise3D(dir + Dither[2])) - vec3(0.5f);
	shift = normalize(shift - dir * dot(shift, dir));
	pos += shift * 0.5f;
	dir = normalize(focal - pos);
	
	if (PathTracing == 0) color = shadowTrace(pos, dir);
	else color = rayTrace(pos, dir);
//	color = (shadowTrace(pos, dir) + rayTrace(pos, dir)) / 2.0f;
	
	color = pow(color, vec3(1.0f / 2.2f)); // Gamma correction
	
	if (PathTracing == 0 || SampleCount == 0) FragColor = vec4(color, 1.0f);
    else {
		vec2 texCoord = FragCoords / 2.0f + vec2(0.5f);
		texCoord *= vec2(float(FrameWidth), float(FrameHeight)) / vec2(float(FrameBufferSize));
		vec3 texel = texture(PrevFrame, texCoord).rgb;
		FragColor = vec4((color + texel * float(SampleCount)) / float(SampleCount + 1), 1.0f);
	}
}
