#version 330 core
#extension GL_ARB_shader_storage_buffer_object: enable

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;

layout (std430, binding = 0)
buffer TreeData {
	uint data[];
};

in vec2 FragCoords;
out vec4 FragColor;

vec3 divide(vec4 v) { return (v / v.w).xyz; }

struct iAABB {
	ivec3 a, b;
};

bool inside(ivec3 a, iAABB box) {
	return a.x >= box.a.x && a.x <= box.b.x && a.y >= box.a.y && a.y <= box.b.y && a.z >= box.a.z && a.z <= box.b.z;
}

uint getPrimitiveData(uint ind) { return data[ind]; }
bool isLeaf(uint ind) { return (data[ind] / 2u) % 2u != 0u; }
uint getData(uint ind) { return data[ind] % 2u; }
uint getChildrenPtr(uint ind) { return data[ind] / 4u; }

const int Size = 256;
const uint Root = 1u;

struct Node {
	uint ptr;
	iAABB box;
};

Node getNodeAt(ivec3 pos) {
	uint ptr = Root;
	iAABB box = iAABB(ivec3(0), ivec3(Size));
	if (!inside(pos, box)) return Node(0u, box); // Outside
	while (!isLeaf(ptr)) {
		ivec3 mid = (box.a + box.b) / 2;
		ptr = getChildrenPtr(ptr);
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
	int face;
};

Intersection innerIntersect(vec3 org, vec3 dir, iAABB box) {
	float scale[6];
	scale[0] = (box.a.x - org.x) / dir.x; // x-
	scale[1] = (box.b.x - org.x) / dir.x; // x+
	scale[2] = (box.a.y - org.y) / dir.y; // y-
	scale[3] = (box.b.y - org.y) / dir.y; // y+
	scale[4] = (box.a.z - org.z) / dir.z; // z-
	scale[5] = (box.b.z - org.z) / dir.z; // z+
	int face = -1;
	for (int i = 0; i < 6; i++) if (scale[i] > 0.0) {
		if (face == -1 || scale[i] < scale[face]) face = i;
	}
	return Intersection(org + dir * scale[face], face);
}

Intersection rayMarch(vec3 org, vec3 dir) {
	dir = normalize(dir) / 2.0f;
	vec3 curr = org;
	iAABB box = iAABB(ivec3(0), ivec3(Size));
	//if (!inside(ivec3(floor(curr + vec3(0.5))), box)) curr = innerIntersect(curr, dir, box).pos + dir; // Outer intersect
	uint ptr = getNodeAt(ivec3(curr)).ptr;
	
	for (int i = 0; i < 100 && ptr != 0u; i++) {
		Node node = getNodeAt(ivec3(floor(curr + vec3(0.5f))));
		Intersection p = innerIntersect(curr, dir, node.box);
		curr = p.pos + dir;
		Node node2 = getNodeAt(ivec3(floor(curr + vec3(0.5f))));
		ptr = node2.ptr;
		if (getData(ptr) != 0u) { // Opaque block
			return p;
			break;
		}
	}
	
	return Intersection(curr, -1);
}

Intersection rayMarch2(vec3 org, vec3 dir) {
	dir = normalize(dir);
	vec3 curr = org;
	
	for (int i = 0; i < 100; i++) {
		curr += dir * 0.2f;
		Node node2 = getNodeAt(ivec3(curr + vec3(0.5f)));
		uint ptr = node2.ptr;
		if (getData(ptr) != 0u) { // Opaque block
			return Intersection(curr, 1);
			break;
		}
	}
	
	return Intersection(curr, -1);
}

void main() {
	vec3 dir = normalize(divide(ModelViewInverse * ProjectionInverse * vec4(FragCoords, 0.0f, 1.0f)));
	
	vec3 color = vec3(0.0f);
	
	vec3 palette[7];
	palette[0] = vec3(0.75f, 0.9f, 1.0f);
	palette[1] = vec3(1.0f, 0.5f, 0.0f);
	palette[2] = vec3(1.0f, 0.0f, 0.0f);
	palette[3] = vec3(1.0f, 1.0f, 1.0f);
	palette[4] = vec3(1.0f, 1.0f, 0.0f);
	palette[5] = vec3(0.0f, 1.0f, 0.0f);
	palette[6] = vec3(0.0f, 0.0f, 1.0f);
	
	Intersection res = rayMarch(vec3(129.3f, 200.5f, 127.1f), dir);
	color = palette[res.face + 1];
	
    FragColor = vec4(color, 1.0f);
}
