#ifndef _GL_COMMON_H
#define _GL_COMMON_H

#if defined(USING_GLES2)
#if defined(IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>

// I guess we can soon add ES 3.0 here too

#else

// Support OpenGL ES 3.0
// This uses the "DYNAMIC" approach from the gles3jni NDK sample.
// Should work on non-Android mobile platforms too.
#include "../gfx_es2/gl3stub.h"
#define MAY_HAVE_GLES3 1
// Old way: #include <GLES2/gl2.h>

#include <GLES2/gl2ext.h>
#ifndef MAEMO
#include <EGL/egl.h>
#endif

#endif
#else // OpenGL
// Now that glew is upgraded beyond 4.3, we can define MAY_HAVE_GLES3 on all platforms
// that include glew.
#define MAY_HAVE_GLES3
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#ifndef GLchar
typedef char GLchar;
#endif

#endif //_GL_COMMON_H
