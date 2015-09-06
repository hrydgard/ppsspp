#pragma once

// Utility to be able to liberally sprinkle GL error checks around your code
// and easily disable them all in release builds - just undefine DEBUG_OPENGL.


void gl_log_enable();

#if !defined(USING_GLES2)
//#define DEBUG_OPENGL
#endif

#if defined(DEBUG_OPENGL)

void glCheckzor(const char *file, int line);
#define GL_CHECK() glCheckzor(__FILE__, __LINE__)

#else

#define GL_CHECK()

#endif



