#ifndef OPENGL_H_
#define OPENGL_H_

#include "common.h"
#include <GL/glew.h>

using VertexBufferID = GLuint;
using TextureID = GLuint;
using TextureFormat = GLenum;
constexpr TextureFormat TextureFormatRGB = GL_RGB;
constexpr TextureFormat TextureFormatRGBA = GL_RGBA;

class OpenGL {
public:
	static void init(bool coreProfile);
	static bool coreProfile() { return mCoreProfile; }
//	static bool arbShaderImageLoadStore() { return GLEW_ARB_shader_image_load_store; }
	static bool arbShaderStorageBufferObject() { return GLEW_ARB_shader_storage_buffer_object; }

private:
	static bool mCoreProfile;
};

#endif // !OPENGL_H_

