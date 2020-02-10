#include "shaderbuffer.h"

ShaderBuffer::ShaderBuffer(const ShaderProgram& program, const std::string& name, GLuint binding) {
	glGenBuffers(1, &mHandle);
	GLuint index = glGetProgramResourceIndex(program.handle(), GL_SHADER_STORAGE_BLOCK, name.c_str());
	glShaderStorageBlockBinding(program.handle(), index, binding);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, mHandle);
}

