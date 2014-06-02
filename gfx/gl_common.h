#pragma once

#ifdef IOS
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#elif defined(USING_GLES2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h> // TODO: Does Maemo like this?
// At least Nokia platforms (Symbian/Maemo/Meego) need the three below?
#include <KHR/khrplatform.h>
typedef char GLchar;
#define GL_BGRA_EXT 0x80E1
#else // OpenGL
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#ifdef USING_GLES2
// Support OpenGL ES 3.0
// This uses the "DYNAMIC" approach from the gles3jni NDK sample.
#include "../gfx_es2/gl3stub.h"
#endif

