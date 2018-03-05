#include "opengl.h"
#include "logger.h"
#include "debug.h"
#include "config.h"

bool OpenGL::mCoreProfile;

void OpenGL::init(bool coreProfile) {
	mCoreProfile = coreProfile;
	if (glewInit() != GLEW_OK) {
		LogFatal("Failed to initialize GLEW!");
		Assert(false);
	}
}

