#ifndef SHADERBUFFER_H_
#define SHADERBUFFER_H_

#include "shader.h"

class ShaderBuffer {
public:
	ShaderBuffer(const ShaderProgram& program, const std::string& name, GLuint binding);
	
	void allocateImmutable(size_t size, bool staticDraw = false) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
		glBufferStorage(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_STORAGE_BIT | (staticDraw ? 0u : GL_CLIENT_STORAGE_BIT));
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void upload(size_t size, const void* data, bool staticDraw = false) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
		glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, staticDraw ? GL_STATIC_COPY : GL_DYNAMIC_COPY);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void uploadSubData(size_t offset, size_t size, const void* data) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void download(size_t size, void* data) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, data);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	void downloadSubData(size_t offset, size_t size, void* data) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, mHandle);
		glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}
	
private:
	GLuint mHandle;
	
};

#endif

