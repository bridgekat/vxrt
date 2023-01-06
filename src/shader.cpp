#include "shader.h"
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>
#include "log.h"

ShaderStage::ShaderStage(OpenGL::ShaderStage stage, std::string const& filename): mStage(stage) {
  // Load shader source.
  std::ifstream ifs(filename);
  if (!ifs.is_open()) {
    Log::error("Could not open shader file: " + filename);
    return;
  }
  std::string source;
  while (!ifs.eof()) {
    std::string line;
    std::getline(ifs, line);
    source += line + '\n';
  }

  // Compile shader.
  mHandle = glCreateShader(mStage);
  char const* str = source.c_str();
  glShaderSource(mHandle, 1, &str, nullptr);
  glCompileShader(mHandle);

  // Check if compilation is successful.
  GLint success = GL_TRUE;
  glGetShaderiv(mHandle, GL_COMPILE_STATUS, &success);
  if (success == GL_FALSE) {
    GLint infoLogLength = 0, charsWritten = 0;
    glGetShaderiv(mHandle, GL_INFO_LOG_LENGTH, &infoLogLength);
    std::string infoLog(infoLogLength, ' ');
    glGetShaderInfoLog(mHandle, infoLogLength, &charsWritten, infoLog.data());
    Log::error("OpenGL shader stage compilation error: " + filename + ":\n" + infoLog);
  }
}

ShaderStage::~ShaderStage() {
  if (mHandle != OpenGL::null) glDeleteShader(mHandle);
}

ShaderProgram::ShaderProgram(std::initializer_list<ShaderStage> stages) {
  // Link shader stages.
  mHandle = glCreateProgram();
  for (auto const& stage: stages) glAttachShader(mHandle, stage.handle());
  glLinkProgram(mHandle);

  // Check if linking is successful.
  GLint success = GL_TRUE;
  glGetProgramiv(mHandle, GL_LINK_STATUS, &success);
  if (success == GL_FALSE) {
    GLint infoLogLength = 0, charsWritten = 0;
    glGetProgramiv(mHandle, GL_INFO_LOG_LENGTH, &infoLogLength);
    std::string infoLog(infoLogLength, ' ');
    glGetProgramInfoLog(mHandle, infoLogLength, &charsWritten, infoLog.data());
    Log::error("OpenGL shader program linking error:\n" + infoLog);
  }

  // Detach shader stages.
  // See: https://www.khronos.org/opengl/wiki/Shader_Compilation
  for (auto const& stage: stages) glDetachShader(mHandle, stage.handle());
}

ShaderProgram::~ShaderProgram() {
  if (mHandle != OpenGL::null) glDeleteProgram(mHandle);
}

OpenGL::UniformLocation ShaderProgram::uniformLocation(std::string const& name) const {
  auto loc = glGetUniformLocation(mHandle, name.c_str());
  if (loc == -1) Log::verbose("Specifying unused uniform variable: " + name);
  return loc;
}
