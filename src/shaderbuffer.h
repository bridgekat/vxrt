#ifndef SHADERBUFFER_H_
#define SHADERBUFFER_H_

#include "shader.h"

class ShaderBuffer {
public:
	ShaderBuffer(const ShaderProgram& program, const std::string& name, GLuint binding);
	
	void update(size_t size, const void* data, bool staticDraw = false) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
		glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, staticDraw ? GL_STATIC_COPY : GL_DYNAMIC_COPY);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void download(size_t size, void* data) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, data);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	
private:
	GLuint mHandle;
	
};

#endif

