#pragma once

// New skool debugging
void gl_log_enable();

// Old skool debugging

#ifndef ANDROID
//#define DEBUG_OPENGL
#endif

#if defined(DEBUG_OPENGL)

void glCheckzor(const char *file, int line);
#define GL_CHECK() glCheckzor(__FILE__, __LINE__)

#else

#define GL_CHECK()

#endif



