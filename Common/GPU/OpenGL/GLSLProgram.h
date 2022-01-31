// Utility code for loading GLSL shaders.
// Has support for auto-reload, see glsl_refresh

#pragma once

#include <string>
#include <time.h>

#include "Common/GPU/OpenGL/GLCommon.h"

// Represent a compiled and linked vshader/fshader pair.
// A just-constructed object is valid but cannot be used as a shader program, meaning that
// yes, you can declare these as globals if you like.
struct GLSLProgram {
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
};

// C API, old skool. Not much point either...

// From files (VFS)
GLSLProgram *glsl_create(const char *vshader_file, const char *fshader_file, std::string *error_message = 0);
// Directly from source code
GLSLProgram *glsl_create_source(const char *vshader_src, const char *fshader_src, std::string *error_message = 0);
void glsl_destroy(GLSLProgram *program);

// If recompilation of the program fails, the program is untouched and error messages
// are logged and the function returns false.
bool glsl_recompile(GLSLProgram *program, std::string *error_message = 0);
void glsl_bind(const GLSLProgram *program);
const GLSLProgram *glsl_get_program();
void glsl_unbind();
int glsl_attrib_loc(const GLSLProgram *program, const char *name);
int glsl_uniform_loc(const GLSLProgram *program, const char *name);
