/* Copyright (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this libretro SDK code part (glsym).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef RGLGEN_HEADERS_H__
#define RGLGEN_HEADERS_H__

#ifdef HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#if defined(IOS)

#if defined(HAVE_OPENGLES3)
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#endif

#elif defined(__APPLE__)
#include <compat/apple_compat.h>
#if MAC_OS_X_VERSION_10_7
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#elif defined(HAVE_PSGL)
#include <PSGL/psgl.h>
#include <GLES/glext.h>
#elif defined(HAVE_OPENGL_MODERN)
#include <GL3/gl3.h>
#include <GL3/gl3ext.h>
#elif defined(HAVE_OPENGLES3)
#include <GLES3/gl3.h>
#define __gl2_h_
#include <GLES2/gl2ext.h>
#elif defined(HAVE_OPENGLES2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(HAVE_OPENGLES1)
#include <GLES/gl.h>
#include <GLES/glext.h>
#else
#if defined(_WIN32) && !defined(_XBOX)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifndef HAVE_LIBNX
#include <GL/gl.h>
#include <GL/glext.h>
#else
/* We need to avoid including <GL/gl.h> on this platform */
#include "switch/nx_gl.h"
#include <GL/glext.h>
#endif /* SWITCH */
#endif

#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif

#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif

#ifndef GL_RED_INTEGER
#define GL_RED_INTEGER 0x8D94
#endif

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT GL_BGRA
#endif

#ifndef GL_LUMINANCE_ALPHA
#define GL_LUMINANCE_ALPHA 0x190A
#endif

#endif
