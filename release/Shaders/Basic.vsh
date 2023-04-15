#version 330 core

// The basic shaders implement a tiny fragment of the fixed-function pipeline.
// Currently supports simple vertex transformation, textures and colors.

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;

layout (location = 0)
in vec4 PositionAttrib;

layout (location = 1)
in vec2 TexCoordAttrib;

layout (location = 2)
in vec4 ColorAttrib;

layout (location = 3)
in vec3 NormalAttrib;

out vec2 TexCoord;
out vec4 Color;
out vec3 Normal;

void main() {
  gl_Position = ProjectionMatrix * ModelViewMatrix * PositionAttrib;
  TexCoord = TexCoordAttrib;
  Color = ColorAttrib;
  Normal = NormalAttrib;
}
