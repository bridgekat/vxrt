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

struct iAABB {
	ivec3 a, b;
};

bool inside(ivec3 a, iAABB box) {
	return a.x >= box.a.x && a.x < box.b.x && 
		a.y >= box.a.y && a.y < box.b.y &&
		a.z >= box.a.z && a.z < box.b.z;
}

const uint Root = 1u;
uint getPrimitiveData(uint ind) { return uint(data[ind]); }
bool isLeaf(uint ind) { return getPrimitiveData(ind) % 2u != 0u; }
uint getData(uint ind) { return getPrimitiveData(ind) / 2u; }
uint getChildrenPtr(uint ind) { return getPrimitiveData(ind) / 2u + Root; }

struct Node {
	uint ptr;
	iAABB box;
};

Node getNodeAt(ivec3 pos) {
	uint ptr = Root;
	iAABB box = iAABB(ivec3(0), ivec3(RootSize));
	if (!inside(pos, box)) return Node(0u, box); // Outside
	while (!isLeaf(ptr)) {
		ptr = getChildrenPtr(ptr);
		ivec3 mid = (box.a + box.b) / 2;
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
	for (int i = 0; i < 6; i++) if (scale[i] > 0.0f) {
		if (face == -1 || scale[i] < scale[face]) face = i;
	}
	if (face == -1) return Intersection(org, -1); // (Unreachable code...)
	return Intersection(org + dir * scale[face], face);
}

/*
Intersection outerIntersect(vec3 org, vec3 dir, iAABB box) {
	float scale[6];
	scale[0] = (box.a.x - org.x) / dir.x; // x-
	scale[1] = (box.b.x - org.x) / dir.x; // x+
	scale[2] = (box.a.y - org.y) / dir.y; // y-
	scale[3] = (box.b.y - org.y) / dir.y; // y+
	scale[4] = (box.a.z - org.z) / dir.z; // z-
	scale[5] = (box.b.z - org.z) / dir.z; // z+
	int face = -1;
	for (int i = 0; i < 6; i++) if (scale[i] > 0.0f) {
		if (face == -1 || scale[i] < scale[face] && inside(ivec3(floor(org + dir * scale[face] + dir)), box)) face = i;
	}
	if (face == -1) return Intersection(org, -1);
	return Intersection(org + dir * scale[face], face);
}
*/

vec3 Shift[7] = vec3[7](
	vec3( 0.0f, 0.0f, 0.0f),
	vec3(-0.1f, 0.0f, 0.0f),
	vec3(+0.1f, 0.0f, 0.0f),
	vec3( 0.0f,-0.1f, 0.0f),
	vec3( 0.0f,+0.1f, 0.0f),
	vec3( 0.0f, 0.0f,-0.1f),
	vec3( 0.0f, 0.0f,+0.1f)
);

Intersection rayMarch(vec3 org, vec3 dir) {
	dir = normalize(dir);
	vec3 curr = org;
	iAABB box = iAABB(ivec3(0), ivec3(RootSize));
//	if (!inside(ivec3(floor(curr)), box)) curr = outerIntersect(curr, dir, box).pos + dir;
	uint ptr = getNodeAt(ivec3(curr)).ptr;
	int face = -1;
	
	for (int i = 0; i <= RootSize && ptr != 0u; i++) {
		Node node = getNodeAt(ivec3(floor(curr + Shift[face + 1])));
		ptr = node.ptr;
		if (ptr != 0u && getData(ptr) != 0u) { // Opaque block
			return Intersection(curr, face);
			break;
		}
		Intersection p = innerIntersect(curr, dir, node.box);
		curr = p.pos; face = p.face;
	}	
	return Intersection(curr, -1);
}

void main() {
	vec4 fragPosition = ModelViewInverse * ProjectionInverse * vec4(FragCoords, 0.0f, 1.0f);
	vec3 dir = normalize(divide(fragPosition) - CameraPosition);
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
	
	// vec3(129.3f, 270.5f, 127.1f)
	Intersection res = rayMarch(CameraPosition + vec3(129.3f, 254.5f, 127.1f), dir);
	color = palette[res.face + 1];
	
    FragColor = vec4(color, 1.0f);
}
