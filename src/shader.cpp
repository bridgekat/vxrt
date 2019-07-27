#include "shader.h"
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include "logger.h"

void checkCompilation(GLuint shader, const std::string& msg) {
	int st = GL_TRUE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &st);
	if (st == GL_FALSE) {
		int infoLogLength = 0, charsWritten;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
		std::string infoLog(infoLogLength, ' ');
		glGetShaderInfoLog(shader, infoLogLength, &charsWritten, &infoLog[0]);
		std::stringstream ss;
		ss << msg << "\n" << infoLog;
		LogError(ss.str());
	}
}

void checkLinking(GLuint program, const std::string& msg) {
	int st = GL_TRUE;
	glGetProgramiv(program, GL_LINK_STATUS, &st);
	if (st == GL_FALSE) {
		int infoLogLength = 0, charsWritten;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
		std::string infoLog(infoLogLength, ' ');
		glGetProgramInfoLog(program, infoLogLength, &charsWritten, &infoLog[0]);
		std::stringstream ss;
		ss << msg << "\n" << infoLog;
		LogError(ss.str());
	}
}

void Shader::loadFromFile(GLenum type, const std::string& filename) {
	mType = type;
	std::string currLine, source;
	std::vector<int> lengths;
	std::ifstream sourceFile(filename);
	if (!sourceFile.is_open()) {
		std::stringstream ss;
		ss << "Could not open shader file:" << filename;
		LogError(ss.str());
		return;
	}
	while (!sourceFile.eof()) {
		std::getline(sourceFile, currLine);
		source += currLine + '\n';
	}
	sourceFile.close();
	mHandle = glCreateShader(type);
	const char* p = source.c_str();
	int size = source.size();
	glShaderSource(mHandle, 1, &p, &size);
	glCompileShader(mHandle);
	checkCompilation(mHandle, "Shader compilation error: \"" + filename + "\"");
}

ShaderProgram::~ShaderProgram() {
	GLint count = 0;
	glGetProgramiv(mHandle, GL_ATTACHED_SHADERS, &count);
	std::unique_ptr<GLuint[]> names(new GLuint[count]);
	glGetAttachedShaders(mHandle, count, nullptr, names.get());
	for (int i = 0; i < count; i++) {
		glDetachShader(mHandle, names[i]);
		std::stringstream ss;
		ss << "Detached shader " << names[i];
		LogInfo(ss.str());
	}
	glDeleteProgram(mHandle);
}

void ShaderProgram::loadShadersFromFile(const std::string& vertex, const std::string& fragment) {
	Shader vsh, fsh;
	vsh.loadFromFile(GL_VERTEX_SHADER, vertex);
	fsh.loadFromFile(GL_FRAGMENT_SHADER, fragment);
	if (vsh.type() != GL_VERTEX_SHADER || fsh.type() != GL_FRAGMENT_SHADER) {
		LogError("Shader type mismatch!");
		std::terminate();
	}
	mHandle = glCreateProgram();
	glAttachShader(mHandle, vsh.handle());
	glAttachShader(mHandle, fsh.handle());
	glLinkProgram(mHandle);
	checkLinking(mHandle, "Shader program linking error:");
}

void ShaderProgram::setUniform1f(const std::string& uniform, float v0) {
	int loc = getUniformLocation(uniform);
	if (loc == -1) {
//		LogVerbose("Shader uniform variable not found: " + uniform);
		return;
	}
	glUniform1f(loc, v0);
}

void ShaderProgram::setUniform2f(const std::string& uniform, float v0, float v1) {
	int loc = getUniformLocation(uniform);
	if (loc == -1) {
//		LogVerbose("Shader uniform variable not found: " + uniform);
		return;
	}
	glUniform2f(loc, v0, v1);
}

void ShaderProgram::setUniform3f(const std::string& uniform, float v0, float v1, float v2) {
	int loc = getUniformLocation(uniform);
	if (loc == -1) {
//		LogVerbose("Shader uniform variable not found: " + uniform);
		return;
	}
	glUniform3f(loc, v0, v1, v2);
}

void ShaderProgram::setUniform4f(const std::string& uniform, float v0, float v1, float v2, float v3) {
	int loc = getUniformLocation(uniform);
	if (loc == -1) {
//		LogVerbose("Shader uniform variable not found: " + uniform);
		return;
	}
	glUniform4f(loc, v0, v1, v2, v3);
}

void ShaderProgram::setUniform1i(const std::string& uniform, int v0) {
	int loc = getUniformLocation(uniform);
	if (loc == -1) {
//		LogVerbose("Shader uniform variable not found: " + uniform);
		return;
	}
	glUniform1i(loc, v0);
}

void ShaderProgram::setUniform1ui(const std::string& uniform, unsigned int v0) {
	int loc = getUniformLocation(uniform);
	if (loc == -1) {
//		LogVerbose("Shader uniform variable not found: " + uniform);
		return;
	}
	glUniform1ui(loc, v0);
}

void ShaderProgram::setUniformMatrix4fv(const std::string& uniform, float* v0) {
	int loc = getUniformLocation(uniform);
	if (loc == -1) {
//		LogVerbose("Shader uniform variable not found: " + uniform);
		return;
	}
	glUniformMatrix4fv(loc, 1, GL_FALSE, v0);
}

