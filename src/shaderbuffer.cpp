#include "shaderbuffer.h"

ShaderBuffer::ShaderBuffer(const ShaderProgram& program, const std::string& name) {
	glGenBuffers(1, &mHandle);
	update(0, nullptr);
	
	GLuint index = glGetProgramResourceIndex(program.handle(), GL_SHADER_STORAGE_BLOCK, name.c_str());
	GLuint binding = 0;
	//glShaderStorageBlockBinding(program, index, binding);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, mHandle);
}

