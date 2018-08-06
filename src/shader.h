#pragma once

#include <string>
#include "opengl.h"

class Shader {
public:
	~Shader() { glDeleteShader(mHandle); }

	void loadFromFile(GLenum type, const std::string& filename);

	GLenum type() const noexcept { return mType; }
	GLuint handle() const noexcept { return mHandle; }

private:
	GLenum mType;
	GLuint mHandle;
};

class ShaderProgram {
public:
	~ShaderProgram() {
		glDetachShader(mHandle, mVertex.handle());
		glDetachShader(mHandle, mFragment.handle());
		glDeleteProgram(mHandle);
	}
	
	GLuint handle() const noexcept { return mHandle; }

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
	Shader mVertex, mFragment;
	GLuint mHandle;
};

