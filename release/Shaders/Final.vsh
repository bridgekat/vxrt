#version 430 core

layout (location = 0) in vec2 Position;
out vec2 FragCoords;

void main() {
	FragCoords = Position;
    gl_Position = vec4(Position, 0.0f, 1.0f);
}
