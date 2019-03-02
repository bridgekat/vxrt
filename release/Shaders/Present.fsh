#version 330 core

uniform sampler2D Texture;
uniform int FrameWidth;
uniform int FrameHeight;
uniform int FrameBufferSize;

in vec2 FragCoords;
out vec4 FragColor;

const float Gamma = 2.2f;

void main() {
	vec2 texCoord = FragCoords / 2.0f + vec2(0.5f);
	texCoord *= vec2(float(FrameWidth), float(FrameHeight)) / vec2(float(FrameBufferSize));
	vec3 texel = texture(Texture, texCoord).rgb;
	texel = pow(texel, vec3(1.0f / Gamma)); // Gamma correction
    FragColor = vec4(texel, 1.0f);
}
