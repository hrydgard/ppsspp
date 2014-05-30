#ifndef _GL_COMMON_H
#define _GL_COMMON_H

#if defined(USING_GLES2)
#ifdef IOS
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#ifndef USING_EGL
#include <EGL/egl.h>
#endif // !USING_EGL
#endif // IOS
#if !defined(__SYMBIAN32__) && !defined(MEEGO_EDITION_HARMATTAN) && !defined(MAEMO)
// Support OpenGL ES 3.0
// This uses the "DYNAMIC" approach from the gles3jni NDK sample.
// Should work on desktop and non-Android mobile platforms too.
#define MAY_HAVE_GLES3 1
#include "../gfx_es2/gl3stub.h"
#endif
#else // OpenGL
// Now that glew is upgraded beyond 4.3, we can define MAY_HAVE_GLES3 on GL platforms
#define MAY_HAVE_GLES3 1
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#if !defined(GLchar) && !defined(__APPLE__)
typedef char GLchar;
#endif

#endif //_GL_COMMON_H
