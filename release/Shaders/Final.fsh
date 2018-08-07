#version 330 core
#extension GL_ARB_shader_storage_buffer_object: enable

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;
uniform int RootSize;
uniform vec3 CameraPosition;

layout (std430, binding = 0)
buffer TreeData {
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
uint getPrimitiveData(uint ind) { return uint(data[ind]); }
bool isLeaf(uint ind) { return getPrimitiveData(ind) % 2u != 0u; }
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

Intersection innerIntersect(vec3 org, vec3 dir, AABB box) {
	float scale[7];
	scale[0] = 0.0f;
	scale[1] = (box.a.x - org.x) / dir.x; // x- (Reversed from normal)
	scale[2] = (box.b.x - org.x) / dir.x; // x+
	scale[3] = (box.a.y - org.y) / dir.y; // y-
	scale[4] = (box.b.y - org.y) / dir.y; // y+
	scale[5] = (box.a.z - org.z) / dir.z; // z-
	scale[6] = (box.b.z - org.z) / dir.z; // z+
	int face = 0;
	for (int i = 1; i <= 6; i++) if (scale[i] > 0.0f) {
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

Intersection rayMarch(vec3 org, vec3 dir) {
	dir = normalize(dir);
	AABB box = AABB(vec3(0.0f), vec3(float(RootSize))); // Root box
	Intersection p = Intersection(org, 0);
	if (!inside(p.pos, box)) p = outerIntersect(p.pos, dir, box);
	
	for (int i = 0; i < RootSize; i++) {
		Node node = getNodeAt(p.pos - 0.1f * Normal[p.face]);
		if (node.ptr == 0u) { // Out of range
			break;
		} else if (getData(node.ptr) != 0u) { // Opaque block
			return p;
		}
		p = innerIntersect(p.pos + Eps * dir, dir, node.box);
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
		p = innerIntersect(p.pos + Eps * dir, dir, node.box);
	}	
	return RootSize;
}

void main() {
	vec4 fragPosition = ModelViewInverse * ProjectionInverse * vec4(FragCoords, 1.0f, 1.0f);
	vec3 dir = normalize(divide(fragPosition));
	vec3 color = vec3(0.0f);
	
	vec3 palette[7] = vec3[7](
		vec3(0.7f, 0.9f, 1.0f),
		vec3(1.0f, 0.0f, 0.0f),
		vec3(1.0f, 0.5f, 0.0f),
		vec3(1.0f, 1.0f, 0.0f),
		vec3(1.0f, 1.0f, 1.0f),
		vec3(0.0f, 0.0f, 1.0f),
		vec3(0.0f, 1.0f, 0.0f)
	);
	
	Intersection res = rayMarch(CameraPosition + vec3(float(RootSize) / 2.0f + 1e-2), dir);
	color = palette[res.face];
	//int res = marchProfiler(CameraPosition + vec3(float(RootSize) / 2.0f), dir);
	//color = vec3(float(res) / 100.0f);
	
    FragColor = vec4(color, 1.0f);
}