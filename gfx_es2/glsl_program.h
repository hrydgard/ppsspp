// Utility code for loading GLSL shaders.
// Has support for auto-reload, see glsl_refresh

#ifndef _RENDER_UTIL
#define _RENDER_UTIL

#include <map>
#include <time.h>

#include "gfx/gl_lost_manager.h"
#include "gfx/gl_common.h"

// Represent a compiled and linked vshader/fshader pair.
// A just-constructed object is valid but cannot be used as a shader program, meaning that
// yes, you can declare these as globals if you like.
struct GLSLProgram : public GfxResourceHolder {
	char name[16];
	char vshader_filename[256];
	char fshader_filename[256];
	const char *vshader_source;
	const char *fshader_source;
	time_t vshader_mtime;
	time_t fshader_mtime;

	// Locations to some common uniforms. Hardcoded for speed.
	GLint sampler0;
	GLint sampler1;
	GLint u_worldviewproj;
	GLint u_world;
	GLint u_viewproj;
	GLint u_fog;	// rgb = color, a = density
	GLint u_sundir;
	GLint u_camerapos;

	GLint a_position;
	GLint a_color;
	GLint a_normal;
	GLint a_texcoord0;
	GLint a_texcoord1;

	// Private to the implementation, do not touch
	GLuint vsh_;
	GLuint fsh_;
	GLuint program_;

	void GLLost();
};


// C API, old skool

// From files (VFS)
GLSLProgram *glsl_create(const char *vshader_file, const char *fshader_file);
// Directly from source code
GLSLProgram *glsl_create_source(const char *vshader_src, const char *fshader_src);
void glsl_destroy(GLSLProgram *program);

// If recompilation of the program fails, the program is untouched and error messages
// are logged and the function returns false.
bool glsl_recompile(GLSLProgram *program);
void glsl_bind(const GLSLProgram *program);
void glsl_unbind();
int glsl_attrib_loc(const GLSLProgram *program, const char *name);
int glsl_uniform_loc(const GLSLProgram *program, const char *name);

// Expensive, try to only call this once per second or so and only when developing.
// fstat-s all the source files of all the shaders to see if they
// should be recompiled, and recompiles them if so.
void glsl_refresh();

// Use glUseProgramObjectARB(NULL); to unset.

#endif	// _RENDER_UTIL
