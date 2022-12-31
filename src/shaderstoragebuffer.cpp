#include "shaderstoragebuffer.h"

ShaderStorageBuffer::ShaderStorageBuffer(ShaderProgram const& program, std::string const& name, GLuint binding) {
  glGenBuffers(1, &mHandle);
  GLuint index = glGetProgramResourceIndex(program.handle(), GL_SHADER_STORAGE_BLOCK, name.c_str());
  glShaderStorageBlockBinding(program.handle(), index, binding);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, mHandle);
}

ShaderStorageBuffer::~ShaderStorageBuffer() {
  if (mHandle != OpenGL::null) glDeleteBuffers(1, &mHandle);
}
