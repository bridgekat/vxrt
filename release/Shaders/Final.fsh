#version 330 core

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform mat4 ProjectionInverse;
uniform mat4 ModelViewInverse;

in vec2 FragCoords;

out vec4 FragColor;

vec3 divide(in vec4 v) {
	return (v / v.w).xyz;
}

bool inside(in vec3 pos, in vec3 pmin, in vec3 pmax) {
	return pos.x >= pmin.x && pos.x <= pmax.x && pos.y >= pmin.y && pos.y <= pmax.y && pos.z >= pmin.z && pos.z <= pmax.z;
}

bool rayTrace(in vec3 org, in vec3 dir, out vec3 res) {
	vec3 curr = org;
	
	for (int i = 0; i < 300; i++) {
		curr += dir * 0.01f;
		if (inside(curr, vec3(0.1f, 0.1f, -2.0f), vec3(0.5f, 0.5f, -1.0f))) {
			res = curr;
			return true;
		}
	}
	
	return false;
}

void main() {
	vec3 dir = normalize(divide(ModelViewInverse * ProjectionInverse * vec4(FragCoords, 0.0f, 1.0f)));
	
	vec3 color = vec3(0.0f);
	vec3 res;
	
	if (rayTrace(vec3(0.0f), dir, res)) {
		color = vec3(1.0f);
	}
	
    FragColor = vec4(color, 1.0f);
}
