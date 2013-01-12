#ifndef _GL_COMMON_H
#define _GL_COMMON_H

#if defined(USING_GLES2)
#if defined(IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif
#else // OpenGL
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
