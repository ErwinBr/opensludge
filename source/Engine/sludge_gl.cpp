#include <stdio.h>

#include <SDL3/SDL.h>

#include "sludge_gl.h"
#include "renderer.h"

#if !defined(HAVE_GLES2)

#define SLUDGE_GL_DEFINE(type, name) type sludge_##name = NULL;
SLUDGE_GL_FUNCTIONS(SLUDGE_GL_DEFINE)
SLUDGE_GL_FBO_FUNCTIONS(SLUDGE_GL_DEFINE)
#undef SLUDGE_GL_DEFINE

const char * loadGLFunctions () {
#define SLUDGE_GL_LOAD(type, name) \
	sludge_##name = (type) SDL_GL_GetProcAddress(#name); \
	if (! sludge_##name) return #name;
	SLUDGE_GL_FUNCTIONS(SLUDGE_GL_LOAD)
#undef SLUDGE_GL_LOAD
#define SLUDGE_GL_LOAD_FBO(type, name) \
	sludge_##name = (type) SDL_GL_GetProcAddress(#name); \
	if (! sludge_##name) sludge_##name = (type) SDL_GL_GetProcAddress(#name "EXT"); \
	if (! sludge_##name) return #name;
	SLUDGE_GL_FBO_FUNCTIONS(SLUDGE_GL_LOAD_FBO)
#undef SLUDGE_GL_LOAD_FBO
	return NULL;
}

#else

const char * loadGLFunctions () {
	return NULL;
}

#endif

int getGLVersion () {
	const char * version = (const char *) glGetString (GL_VERSION);
	int major = 0, minor = 0;
	if (! version) return 0;
	// With OpenGL ES, the version string starts with "OpenGL ES ".
	while (*version && (*version < '0' || *version > '9')) version ++;
	if (sscanf (version, "%d.%d", &major, &minor) != 2) return 0;
	return major * 10 + minor;
}

int printOglError (const char *file, int line)
{
	GLenum glErr;
	int    retCode = 0;

	glErr = glGetError ();
	while (glErr != GL_NO_ERROR)
    {
		gfx::logDebug ("glError in file %s @ line %d: error code 0x%x\n", file, line, glErr);
		retCode = 1;
		glErr = glGetError ();
    }
	return retCode;
}
