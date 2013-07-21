#ifndef _GL_COMMON_H
#define _GL_COMMON_H

#if defined(USING_GLES2)
#if defined(IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>

// Additional extensions not included in GLES2/gl2ext.h from the NDK

/* GL_QCOM_alpha_test */
#ifndef GL_QCOM_alpha_test
#define GL_ALPHA_TEST_QCOM                                      0x0BC0
#define GL_ALPHA_TEST_FUNC_QCOM                                 0x0BC1
#define GL_ALPHA_TEST_REF_QCOM                                  0x0BC2
#endif

#ifndef GL_QCOM_alpha_test
#define GL_QCOM_alpha_test 1
#ifdef GL_GLEXT_PROTOTYPES
//GL_APICALL void GL_APIENTRY glAlphaFuncQCOM (GLenum func, GLclampf ref);
#endif
typedef void (GL_APIENTRYP PFNGLALPHAFUNCQCOMPROC) (GLenum func, GLclampf ref);
extern PFNGLALPHAFUNCQCOMPROC glAlphaFuncQCOM;
#endif

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
