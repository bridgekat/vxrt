#version 330 core

// The basic shaders implement a tiny fragment of the fixed-function pipeline.
// Currently supports simple vertex transformation, textures and colors.

in vec2 TexCoord;
in vec4 Color;
in vec3 Normal;

layout (location = 0)
out vec4 FragColor;

uniform sampler2D Texture2D;
uniform bool Texture2DEnabled;
uniform bool ColorEnabled;
uniform bool GammaConversion; // Whether to interpret input as linear and convert it to sRGB.

const float Gamma = 2.2;

void main() {
  vec4 res = vec4(1.0);
  if (Texture2DEnabled) res *= texture(Texture2D, TexCoord.xy);
  if (ColorEnabled) res *= Color;
  if (GammaConversion) res.rgb = pow(res.rgb, vec3(1.0 / Gamma));
  FragColor = res;
}
