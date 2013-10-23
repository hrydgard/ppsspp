#ifndef _GL_COMMON_H
#define _GL_COMMON_H

#if defined(USING_GLES2)
#ifdef IOS
// I guess we can soon add ES 3.0 here too
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#ifndef MAEMO
#include <EGL/egl.h>
#endif // !MAEMO
#endif // IOS
#if defined(ANDROID) || defined(BLACKBERRY)
// Support OpenGL ES 3.0
// This uses the "DYNAMIC" approach from the gles3jni NDK sample.
// Should work on non-Android mobile platforms too.
#include "../gfx_es2/gl3stub.h"
#define MAY_HAVE_GLES3 1
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
