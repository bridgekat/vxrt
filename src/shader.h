#pragma once

#include <string>
#include "opengl.h"

class Shader {
public:
	~Shader() { glDeleteShader(mHandle); }

	void checkCompilation(const std::string& msg);
	void loadFromFile(GLenum type, const std::string& filename);

	GLenum type() const noexcept { return mType; }
	GLuint handle() const noexcept { return mHandle; }

private:
	GLenum mType;
	GLuint mHandle;
};

class ShaderProgram {
public:
	~ShaderProgram();
	
	GLuint handle() const noexcept { return mHandle; }

	void create() { mHandle = glCreateProgram(); }
	void attach(const Shader& shader) { glAttachShader(mHandle, shader.handle()); }
	void link() { glLinkProgram(mHandle); }
	void checkLinking(const std::string& msg);
	void loadShadersFromFile(const std::string& vertex, const std::string& fragment);

	void bind() const { glUseProgram(mHandle); }
	static void unbind() { glUseProgram(0); }

	int getUniformLocation(const std::string& uniform) const { return glGetUniformLocation(mHandle, uniform.c_str()); }

	void setUniform1f(const std::string& uniform, float v0);
	void setUniform2f(const std::string& uniform, float v0, float v1);
	void setUniform3f(const std::string& uniform, float v0, float v1, float v2);
	void setUniform4f(const std::string& uniform, float v0, float v1, float v2, float v3);
	void setUniform1i(const std::string& uniform, int v0);
	void setUniform1ui(const std::string& uniform, unsigned int v0);
	void setUniformMatrix4fv(const std::string& uniform, float* v0);

private:
	GLuint mHandle = 0;
};

