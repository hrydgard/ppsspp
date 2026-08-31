// Utility code for loading GLSL shaders.
//
// This is a small leftover from the old "native" library, only used by the Win32 GE debugger's
// preview windows, which talk to GL directly rather than going through thin3d. Everything here
// works on shader source strings - the file loading and auto-reload support that used to live
// here was never used in PPSSPP and is gone.

#pragma once

#include <string>

#include "Common/GPU/OpenGL/GLCommon.h"

// Represent a compiled and linked vshader/fshader pair.
struct GLSLProgram {
	// Locations of the uniforms and attributes the callers use, looked up once at link time.
	// Add to these as needed - each one costs a lookup per link.
	GLint sampler0;
	GLint u_viewproj;
	GLint a_position;
	GLint a_texcoord0;

	// Private to the implementation, do not touch
	GLuint vsh_;
	GLuint fsh_;
	GLuint program_;
};

// Compiles and links a program. Returns nullptr on failure, having logged the reason and,
// if error_message is non-null, copied it there.
GLSLProgram *glsl_create_source(const char *vshader_src, const char *fshader_src, std::string *error_message = nullptr);
void glsl_destroy(GLSLProgram *program);

void glsl_bind(const GLSLProgram *program);
void glsl_unbind();
