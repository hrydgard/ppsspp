/*
** The OpenGL Extension Wrangler Library
** Copyright (C) 2008-2017, Nigel Stewart <nigels[]users sourceforge net>
** Copyright (C) 2002-2008, Milan Ikits <milan ikits[]ieee org>
** Copyright (C) 2002-2008, Marcelo E. Magallon <mmagallo[]debian org>
** Copyright (C) 2002, Lev Povalahev
** All rights reserved.
** 
** Redistribution and use in source and binary forms, with or without 
** modification, are permitted provided that the following conditions are met:
** 
** * Redistributions of source code must retain the above copyright notice, 
**   this list of conditions and the following disclaimer.
** * Redistributions in binary form must reproduce the above copyright notice, 
**   this list of conditions and the following disclaimer in the documentation 
**   and/or other materials provided with the distribution.
** * The name of the author may be used to endorse or promote products 
**   derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
** THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glew.h>
#if defined(GLEW_EGL)
#include <GL/eglew.h>
#elif defined(GLEW_OSMESA)
#define GLAPI extern
#include <GL/osmesa.h>
#elif defined(_WIN32)
#include <GL/wglew.h>
#elif !defined(__APPLE__) && !defined(__HAIKU__) || defined(GLEW_APPLE_GLX)
#include <GL/glxew.h>
#endif

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#endif

#ifdef GLEW_REGAL
#include <GL/Regal.h>
#endif

static FILE* f;

/* Command-line parameters for GL context creation */

struct createParams
{
#if defined(GLEW_OSMESA)
#elif defined(GLEW_EGL)
#elif defined(_WIN32)
  int         pixelformat;
#elif !defined(__APPLE__) && !defined(__HAIKU__) || defined(GLEW_APPLE_GLX)
  const char* display;
  int         visual;
#endif
  int         major, minor;  /* GL context version number */

  /* https://www.opengl.org/registry/specs/ARB/glx_create_context.txt */
  int         profile;       /* core = 1, compatibility = 2 */
  int         flags;         /* debug = 1, forward compatible = 2 */
};

GLboolean glewCreateContext (struct createParams *params);

GLboolean glewParseArgs (int argc, char** argv, struct createParams *);

void glewDestroyContext ();

/* ------------------------------------------------------------------------- */

static void glewPrintExt (const char* name, GLboolean def1, GLboolean def2, GLboolean def3)
{
  unsigned int i;
  fprintf(f, "\n%s:", name);
  for (i=0; i<62-strlen(name); i++) fprintf(f, " ");
  fprintf(f, "%s ", def1 ? "OK" : "MISSING");
  if (def1 != def2)
    fprintf(f, "[%s] ", def2 ? "OK" : "MISSING");
  if (def1 != def3)
    fprintf(f, "[%s]\n", def3 ? "OK" : "MISSING");
  else
    fprintf(f, "\n");
  for (i=0; i<strlen(name)+1; i++) fprintf(f, "-");
  fprintf(f, "\n");
  fflush(f);
}

static void glewInfoFunc (const char* name, GLint undefined)
{
  unsigned int i;
  fprintf(f, "  %s:", name);
  for (i=0; i<60-strlen(name); i++) fprintf(f, " ");
  fprintf(f, "%s\n", undefined ? "MISSING" : "OK");
  fflush(f);
}

/* ----------------------------- GL_VERSION_1_1 ---------------------------- */

#ifdef GL_VERSION_1_1

static void _glewInfo_GL_VERSION_1_1 (void)
{
  glewPrintExt("GL_VERSION_1_1", GLEW_VERSION_1_1, GLEW_VERSION_1_1, GLEW_VERSION_1_1);
}

#endif /* GL_VERSION_1_1 */

#ifdef GL_VERSION_1_2

static void _glewInfo_GL_VERSION_1_2 (void)
{
  glewPrintExt("GL_VERSION_1_2", GLEW_VERSION_1_2, GLEW_VERSION_1_2, GLEW_VERSION_1_2);

  glewInfoFunc("glCopyTexSubImage3D", glCopyTexSubImage3D == NULL);
  glewInfoFunc("glDrawRangeElements", glDrawRangeElements == NULL);
  glewInfoFunc("glTexImage3D", glTexImage3D == NULL);
  glewInfoFunc("glTexSubImage3D", glTexSubImage3D == NULL);
}

#endif /* GL_VERSION_1_2 */

#ifdef GL_VERSION_1_2_1

static void _glewInfo_GL_VERSION_1_2_1 (void)
{
  glewPrintExt("GL_VERSION_1_2_1", GLEW_VERSION_1_2_1, GLEW_VERSION_1_2_1, GLEW_VERSION_1_2_1);
}

#endif /* GL_VERSION_1_2_1 */

#ifdef GL_VERSION_1_3

static void _glewInfo_GL_VERSION_1_3 (void)
{
  glewPrintExt("GL_VERSION_1_3", GLEW_VERSION_1_3, GLEW_VERSION_1_3, GLEW_VERSION_1_3);

  glewInfoFunc("glActiveTexture", glActiveTexture == NULL);
  glewInfoFunc("glClientActiveTexture", glClientActiveTexture == NULL);
  glewInfoFunc("glCompressedTexImage1D", glCompressedTexImage1D == NULL);
  glewInfoFunc("glCompressedTexImage2D", glCompressedTexImage2D == NULL);
  glewInfoFunc("glCompressedTexImage3D", glCompressedTexImage3D == NULL);
  glewInfoFunc("glCompressedTexSubImage1D", glCompressedTexSubImage1D == NULL);
  glewInfoFunc("glCompressedTexSubImage2D", glCompressedTexSubImage2D == NULL);
  glewInfoFunc("glCompressedTexSubImage3D", glCompressedTexSubImage3D == NULL);
  glewInfoFunc("glGetCompressedTexImage", glGetCompressedTexImage == NULL);
  glewInfoFunc("glLoadTransposeMatrixd", glLoadTransposeMatrixd == NULL);
  glewInfoFunc("glLoadTransposeMatrixf", glLoadTransposeMatrixf == NULL);
  glewInfoFunc("glMultTransposeMatrixd", glMultTransposeMatrixd == NULL);
  glewInfoFunc("glMultTransposeMatrixf", glMultTransposeMatrixf == NULL);
  glewInfoFunc("glMultiTexCoord1d", glMultiTexCoord1d == NULL);
  glewInfoFunc("glMultiTexCoord1dv", glMultiTexCoord1dv == NULL);
  glewInfoFunc("glMultiTexCoord1f", glMultiTexCoord1f == NULL);
  glewInfoFunc("glMultiTexCoord1fv", glMultiTexCoord1fv == NULL);
  glewInfoFunc("glMultiTexCoord1i", glMultiTexCoord1i == NULL);
  glewInfoFunc("glMultiTexCoord1iv", glMultiTexCoord1iv == NULL);
  glewInfoFunc("glMultiTexCoord1s", glMultiTexCoord1s == NULL);
  glewInfoFunc("glMultiTexCoord1sv", glMultiTexCoord1sv == NULL);
  glewInfoFunc("glMultiTexCoord2d", glMultiTexCoord2d == NULL);
  glewInfoFunc("glMultiTexCoord2dv", glMultiTexCoord2dv == NULL);
  glewInfoFunc("glMultiTexCoord2f", glMultiTexCoord2f == NULL);
  glewInfoFunc("glMultiTexCoord2fv", glMultiTexCoord2fv == NULL);
  glewInfoFunc("glMultiTexCoord2i", glMultiTexCoord2i == NULL);
  glewInfoFunc("glMultiTexCoord2iv", glMultiTexCoord2iv == NULL);
  glewInfoFunc("glMultiTexCoord2s", glMultiTexCoord2s == NULL);
  glewInfoFunc("glMultiTexCoord2sv", glMultiTexCoord2sv == NULL);
  glewInfoFunc("glMultiTexCoord3d", glMultiTexCoord3d == NULL);
  glewInfoFunc("glMultiTexCoord3dv", glMultiTexCoord3dv == NULL);
  glewInfoFunc("glMultiTexCoord3f", glMultiTexCoord3f == NULL);
  glewInfoFunc("glMultiTexCoord3fv", glMultiTexCoord3fv == NULL);
  glewInfoFunc("glMultiTexCoord3i", glMultiTexCoord3i == NULL);
  glewInfoFunc("glMultiTexCoord3iv", glMultiTexCoord3iv == NULL);
  glewInfoFunc("glMultiTexCoord3s", glMultiTexCoord3s == NULL);
  glewInfoFunc("glMultiTexCoord3sv", glMultiTexCoord3sv == NULL);
  glewInfoFunc("glMultiTexCoord4d", glMultiTexCoord4d == NULL);
  glewInfoFunc("glMultiTexCoord4dv", glMultiTexCoord4dv == NULL);
  glewInfoFunc("glMultiTexCoord4f", glMultiTexCoord4f == NULL);
  glewInfoFunc("glMultiTexCoord4fv", glMultiTexCoord4fv == NULL);
  glewInfoFunc("glMultiTexCoord4i", glMultiTexCoord4i == NULL);
  glewInfoFunc("glMultiTexCoord4iv", glMultiTexCoord4iv == NULL);
  glewInfoFunc("glMultiTexCoord4s", glMultiTexCoord4s == NULL);
  glewInfoFunc("glMultiTexCoord4sv", glMultiTexCoord4sv == NULL);
  glewInfoFunc("glSampleCoverage", glSampleCoverage == NULL);
}

#endif /* GL_VERSION_1_3 */

#ifdef GL_VERSION_1_4

static void _glewInfo_GL_VERSION_1_4 (void)
{
  glewPrintExt("GL_VERSION_1_4", GLEW_VERSION_1_4, GLEW_VERSION_1_4, GLEW_VERSION_1_4);

  glewInfoFunc("glBlendColor", glBlendColor == NULL);
  glewInfoFunc("glBlendEquation", glBlendEquation == NULL);
  glewInfoFunc("glBlendFuncSeparate", glBlendFuncSeparate == NULL);
  glewInfoFunc("glFogCoordPointer", glFogCoordPointer == NULL);
  glewInfoFunc("glFogCoordd", glFogCoordd == NULL);
  glewInfoFunc("glFogCoorddv", glFogCoorddv == NULL);
  glewInfoFunc("glFogCoordf", glFogCoordf == NULL);
  glewInfoFunc("glFogCoordfv", glFogCoordfv == NULL);
  glewInfoFunc("glMultiDrawArrays", glMultiDrawArrays == NULL);
  glewInfoFunc("glMultiDrawElements", glMultiDrawElements == NULL);
  glewInfoFunc("glPointParameterf", glPointParameterf == NULL);
  glewInfoFunc("glPointParameterfv", glPointParameterfv == NULL);
  glewInfoFunc("glPointParameteri", glPointParameteri == NULL);
  glewInfoFunc("glPointParameteriv", glPointParameteriv == NULL);
  glewInfoFunc("glSecondaryColor3b", glSecondaryColor3b == NULL);
  glewInfoFunc("glSecondaryColor3bv", glSecondaryColor3bv == NULL);
  glewInfoFunc("glSecondaryColor3d", glSecondaryColor3d == NULL);
  glewInfoFunc("glSecondaryColor3dv", glSecondaryColor3dv == NULL);
  glewInfoFunc("glSecondaryColor3f", glSecondaryColor3f == NULL);
  glewInfoFunc("glSecondaryColor3fv", glSecondaryColor3fv == NULL);
  glewInfoFunc("glSecondaryColor3i", glSecondaryColor3i == NULL);
  glewInfoFunc("glSecondaryColor3iv", glSecondaryColor3iv == NULL);
  glewInfoFunc("glSecondaryColor3s", glSecondaryColor3s == NULL);
  glewInfoFunc("glSecondaryColor3sv", glSecondaryColor3sv == NULL);
  glewInfoFunc("glSecondaryColor3ub", glSecondaryColor3ub == NULL);
  glewInfoFunc("glSecondaryColor3ubv", glSecondaryColor3ubv == NULL);
  glewInfoFunc("glSecondaryColor3ui", glSecondaryColor3ui == NULL);
  glewInfoFunc("glSecondaryColor3uiv", glSecondaryColor3uiv == NULL);
  glewInfoFunc("glSecondaryColor3us", glSecondaryColor3us == NULL);
  glewInfoFunc("glSecondaryColor3usv", glSecondaryColor3usv == NULL);
  glewInfoFunc("glSecondaryColorPointer", glSecondaryColorPointer == NULL);
  glewInfoFunc("glWindowPos2d", glWindowPos2d == NULL);
  glewInfoFunc("glWindowPos2dv", glWindowPos2dv == NULL);
  glewInfoFunc("glWindowPos2f", glWindowPos2f == NULL);
  glewInfoFunc("glWindowPos2fv", glWindowPos2fv == NULL);
  glewInfoFunc("glWindowPos2i", glWindowPos2i == NULL);
  glewInfoFunc("glWindowPos2iv", glWindowPos2iv == NULL);
  glewInfoFunc("glWindowPos2s", glWindowPos2s == NULL);
  glewInfoFunc("glWindowPos2sv", glWindowPos2sv == NULL);
  glewInfoFunc("glWindowPos3d", glWindowPos3d == NULL);
  glewInfoFunc("glWindowPos3dv", glWindowPos3dv == NULL);
  glewInfoFunc("glWindowPos3f", glWindowPos3f == NULL);
  glewInfoFunc("glWindowPos3fv", glWindowPos3fv == NULL);
  glewInfoFunc("glWindowPos3i", glWindowPos3i == NULL);
  glewInfoFunc("glWindowPos3iv", glWindowPos3iv == NULL);
  glewInfoFunc("glWindowPos3s", glWindowPos3s == NULL);
  glewInfoFunc("glWindowPos3sv", glWindowPos3sv == NULL);
}

#endif /* GL_VERSION_1_4 */

#ifdef GL_VERSION_1_5

static void _glewInfo_GL_VERSION_1_5 (void)
{
  glewPrintExt("GL_VERSION_1_5", GLEW_VERSION_1_5, GLEW_VERSION_1_5, GLEW_VERSION_1_5);

  glewInfoFunc("glBeginQuery", glBeginQuery == NULL);
  glewInfoFunc("glBindBuffer", glBindBuffer == NULL);
  glewInfoFunc("glBufferData", glBufferData == NULL);
  glewInfoFunc("glBufferSubData", glBufferSubData == NULL);
  glewInfoFunc("glDeleteBuffers", glDeleteBuffers == NULL);
  glewInfoFunc("glDeleteQueries", glDeleteQueries == NULL);
  glewInfoFunc("glEndQuery", glEndQuery == NULL);
  glewInfoFunc("glGenBuffers", glGenBuffers == NULL);
  glewInfoFunc("glGenQueries", glGenQueries == NULL);
  glewInfoFunc("glGetBufferParameteriv", glGetBufferParameteriv == NULL);
  glewInfoFunc("glGetBufferPointerv", glGetBufferPointerv == NULL);
  glewInfoFunc("glGetBufferSubData", glGetBufferSubData == NULL);
  glewInfoFunc("glGetQueryObjectiv", glGetQueryObjectiv == NULL);
  glewInfoFunc("glGetQueryObjectuiv", glGetQueryObjectuiv == NULL);
  glewInfoFunc("glGetQueryiv", glGetQueryiv == NULL);
  glewInfoFunc("glIsBuffer", glIsBuffer == NULL);
  glewInfoFunc("glIsQuery", glIsQuery == NULL);
  glewInfoFunc("glMapBuffer", glMapBuffer == NULL);
  glewInfoFunc("glUnmapBuffer", glUnmapBuffer == NULL);
}

#endif /* GL_VERSION_1_5 */

#ifdef GL_VERSION_2_0

static void _glewInfo_GL_VERSION_2_0 (void)
{
  glewPrintExt("GL_VERSION_2_0", GLEW_VERSION_2_0, GLEW_VERSION_2_0, GLEW_VERSION_2_0);

  glewInfoFunc("glAttachShader", glAttachShader == NULL);
  glewInfoFunc("glBindAttribLocation", glBindAttribLocation == NULL);
  glewInfoFunc("glBlendEquationSeparate", glBlendEquationSeparate == NULL);
  glewInfoFunc("glCompileShader", glCompileShader == NULL);
  glewInfoFunc("glCreateProgram", glCreateProgram == NULL);
  glewInfoFunc("glCreateShader", glCreateShader == NULL);
  glewInfoFunc("glDeleteProgram", glDeleteProgram == NULL);
  glewInfoFunc("glDeleteShader", glDeleteShader == NULL);
  glewInfoFunc("glDetachShader", glDetachShader == NULL);
  glewInfoFunc("glDisableVertexAttribArray", glDisableVertexAttribArray == NULL);
  glewInfoFunc("glDrawBuffers", glDrawBuffers == NULL);
  glewInfoFunc("glEnableVertexAttribArray", glEnableVertexAttribArray == NULL);
  glewInfoFunc("glGetActiveAttrib", glGetActiveAttrib == NULL);
  glewInfoFunc("glGetActiveUniform", glGetActiveUniform == NULL);
  glewInfoFunc("glGetAttachedShaders", glGetAttachedShaders == NULL);
  glewInfoFunc("glGetAttribLocation", glGetAttribLocation == NULL);
  glewInfoFunc("glGetProgramInfoLog", glGetProgramInfoLog == NULL);
  glewInfoFunc("glGetProgramiv", glGetProgramiv == NULL);
  glewInfoFunc("glGetShaderInfoLog", glGetShaderInfoLog == NULL);
  glewInfoFunc("glGetShaderSource", glGetShaderSource == NULL);
  glewInfoFunc("glGetShaderiv", glGetShaderiv == NULL);
  glewInfoFunc("glGetUniformLocation", glGetUniformLocation == NULL);
  glewInfoFunc("glGetUniformfv", glGetUniformfv == NULL);
  glewInfoFunc("glGetUniformiv", glGetUniformiv == NULL);
  glewInfoFunc("glGetVertexAttribPointerv", glGetVertexAttribPointerv == NULL);
  glewInfoFunc("glGetVertexAttribdv", glGetVertexAttribdv == NULL);
  glewInfoFunc("glGetVertexAttribfv", glGetVertexAttribfv == NULL);
  glewInfoFunc("glGetVertexAttribiv", glGetVertexAttribiv == NULL);
  glewInfoFunc("glIsProgram", glIsProgram == NULL);
  glewInfoFunc("glIsShader", glIsShader == NULL);
  glewInfoFunc("glLinkProgram", glLinkProgram == NULL);
  glewInfoFunc("glShaderSource", glShaderSource == NULL);
  glewInfoFunc("glStencilFuncSeparate", glStencilFuncSeparate == NULL);
  glewInfoFunc("glStencilMaskSeparate", glStencilMaskSeparate == NULL);
  glewInfoFunc("glStencilOpSeparate", glStencilOpSeparate == NULL);
  glewInfoFunc("glUniform1f", glUniform1f == NULL);
  glewInfoFunc("glUniform1fv", glUniform1fv == NULL);
  glewInfoFunc("glUniform1i", glUniform1i == NULL);
  glewInfoFunc("glUniform1iv", glUniform1iv == NULL);
  glewInfoFunc("glUniform2f", glUniform2f == NULL);
  glewInfoFunc("glUniform2fv", glUniform2fv == NULL);
  glewInfoFunc("glUniform2i", glUniform2i == NULL);
  glewInfoFunc("glUniform2iv", glUniform2iv == NULL);
  glewInfoFunc("glUniform3f", glUniform3f == NULL);
  glewInfoFunc("glUniform3fv", glUniform3fv == NULL);
  glewInfoFunc("glUniform3i", glUniform3i == NULL);
  glewInfoFunc("glUniform3iv", glUniform3iv == NULL);
  glewInfoFunc("glUniform4f", glUniform4f == NULL);
  glewInfoFunc("glUniform4fv", glUniform4fv == NULL);
  glewInfoFunc("glUniform4i", glUniform4i == NULL);
  glewInfoFunc("glUniform4iv", glUniform4iv == NULL);
  glewInfoFunc("glUniformMatrix2fv", glUniformMatrix2fv == NULL);
  glewInfoFunc("glUniformMatrix3fv", glUniformMatrix3fv == NULL);
  glewInfoFunc("glUniformMatrix4fv", glUniformMatrix4fv == NULL);
  glewInfoFunc("glUseProgram", glUseProgram == NULL);
  glewInfoFunc("glValidateProgram", glValidateProgram == NULL);
  glewInfoFunc("glVertexAttrib1d", glVertexAttrib1d == NULL);
  glewInfoFunc("glVertexAttrib1dv", glVertexAttrib1dv == NULL);
  glewInfoFunc("glVertexAttrib1f", glVertexAttrib1f == NULL);
  glewInfoFunc("glVertexAttrib1fv", glVertexAttrib1fv == NULL);
  glewInfoFunc("glVertexAttrib1s", glVertexAttrib1s == NULL);
  glewInfoFunc("glVertexAttrib1sv", glVertexAttrib1sv == NULL);
  glewInfoFunc("glVertexAttrib2d", glVertexAttrib2d == NULL);
  glewInfoFunc("glVertexAttrib2dv", glVertexAttrib2dv == NULL);
  glewInfoFunc("glVertexAttrib2f", glVertexAttrib2f == NULL);
  glewInfoFunc("glVertexAttrib2fv", glVertexAttrib2fv == NULL);
  glewInfoFunc("glVertexAttrib2s", glVertexAttrib2s == NULL);
  glewInfoFunc("glVertexAttrib2sv", glVertexAttrib2sv == NULL);
  glewInfoFunc("glVertexAttrib3d", glVertexAttrib3d == NULL);
  glewInfoFunc("glVertexAttrib3dv", glVertexAttrib3dv == NULL);
  glewInfoFunc("glVertexAttrib3f", glVertexAttrib3f == NULL);
  glewInfoFunc("glVertexAttrib3fv", glVertexAttrib3fv == NULL);
  glewInfoFunc("glVertexAttrib3s", glVertexAttrib3s == NULL);
  glewInfoFunc("glVertexAttrib3sv", glVertexAttrib3sv == NULL);
  glewInfoFunc("glVertexAttrib4Nbv", glVertexAttrib4Nbv == NULL);
  glewInfoFunc("glVertexAttrib4Niv", glVertexAttrib4Niv == NULL);
  glewInfoFunc("glVertexAttrib4Nsv", glVertexAttrib4Nsv == NULL);
  glewInfoFunc("glVertexAttrib4Nub", glVertexAttrib4Nub == NULL);
  glewInfoFunc("glVertexAttrib4Nubv", glVertexAttrib4Nubv == NULL);
  glewInfoFunc("glVertexAttrib4Nuiv", glVertexAttrib4Nuiv == NULL);
  glewInfoFunc("glVertexAttrib4Nusv", glVertexAttrib4Nusv == NULL);
  glewInfoFunc("glVertexAttrib4bv", glVertexAttrib4bv == NULL);
  glewInfoFunc("glVertexAttrib4d", glVertexAttrib4d == NULL);
  glewInfoFunc("glVertexAttrib4dv", glVertexAttrib4dv == NULL);
  glewInfoFunc("glVertexAttrib4f", glVertexAttrib4f == NULL);
  glewInfoFunc("glVertexAttrib4fv", glVertexAttrib4fv == NULL);
  glewInfoFunc("glVertexAttrib4iv", glVertexAttrib4iv == NULL);
  glewInfoFunc("glVertexAttrib4s", glVertexAttrib4s == NULL);
  glewInfoFunc("glVertexAttrib4sv", glVertexAttrib4sv == NULL);
  glewInfoFunc("glVertexAttrib4ubv", glVertexAttrib4ubv == NULL);
  glewInfoFunc("glVertexAttrib4uiv", glVertexAttrib4uiv == NULL);
  glewInfoFunc("glVertexAttrib4usv", glVertexAttrib4usv == NULL);
  glewInfoFunc("glVertexAttribPointer", glVertexAttribPointer == NULL);
}

#endif /* GL_VERSION_2_0 */

#ifdef GL_VERSION_2_1

static void _glewInfo_GL_VERSION_2_1 (void)
{
  glewPrintExt("GL_VERSION_2_1", GLEW_VERSION_2_1, GLEW_VERSION_2_1, GLEW_VERSION_2_1);

  glewInfoFunc("glUniformMatrix2x3fv", glUniformMatrix2x3fv == NULL);
  glewInfoFunc("glUniformMatrix2x4fv", glUniformMatrix2x4fv == NULL);
  glewInfoFunc("glUniformMatrix3x2fv", glUniformMatrix3x2fv == NULL);
  glewInfoFunc("glUniformMatrix3x4fv", glUniformMatrix3x4fv == NULL);
  glewInfoFunc("glUniformMatrix4x2fv", glUniformMatrix4x2fv == NULL);
  glewInfoFunc("glUniformMatrix4x3fv", glUniformMatrix4x3fv == NULL);
}

#endif /* GL_VERSION_2_1 */

#ifdef GL_VERSION_3_0

static void _glewInfo_GL_VERSION_3_0 (void)
{
  glewPrintExt("GL_VERSION_3_0", GLEW_VERSION_3_0, GLEW_VERSION_3_0, GLEW_VERSION_3_0);

  glewInfoFunc("glBeginConditionalRender", glBeginConditionalRender == NULL);
  glewInfoFunc("glBeginTransformFeedback", glBeginTransformFeedback == NULL);
  glewInfoFunc("glBindFragDataLocation", glBindFragDataLocation == NULL);
  glewInfoFunc("glClampColor", glClampColor == NULL);
  glewInfoFunc("glClearBufferfi", glClearBufferfi == NULL);
  glewInfoFunc("glClearBufferfv", glClearBufferfv == NULL);
  glewInfoFunc("glClearBufferiv", glClearBufferiv == NULL);
  glewInfoFunc("glClearBufferuiv", glClearBufferuiv == NULL);
  glewInfoFunc("glColorMaski", glColorMaski == NULL);
  glewInfoFunc("glDisablei", glDisablei == NULL);
  glewInfoFunc("glEnablei", glEnablei == NULL);
  glewInfoFunc("glEndConditionalRender", glEndConditionalRender == NULL);
  glewInfoFunc("glEndTransformFeedback", glEndTransformFeedback == NULL);
  glewInfoFunc("glGetBooleani_v", glGetBooleani_v == NULL);
  glewInfoFunc("glGetFragDataLocation", glGetFragDataLocation == NULL);
  glewInfoFunc("glGetStringi", glGetStringi == NULL);
  glewInfoFunc("glGetTexParameterIiv", glGetTexParameterIiv == NULL);
  glewInfoFunc("glGetTexParameterIuiv", glGetTexParameterIuiv == NULL);
  glewInfoFunc("glGetTransformFeedbackVarying", glGetTransformFeedbackVarying == NULL);
  glewInfoFunc("glGetUniformuiv", glGetUniformuiv == NULL);
  glewInfoFunc("glGetVertexAttribIiv", glGetVertexAttribIiv == NULL);
  glewInfoFunc("glGetVertexAttribIuiv", glGetVertexAttribIuiv == NULL);
  glewInfoFunc("glIsEnabledi", glIsEnabledi == NULL);
  glewInfoFunc("glTexParameterIiv", glTexParameterIiv == NULL);
  glewInfoFunc("glTexParameterIuiv", glTexParameterIuiv == NULL);
  glewInfoFunc("glTransformFeedbackVaryings", glTransformFeedbackVaryings == NULL);
  glewInfoFunc("glUniform1ui", glUniform1ui == NULL);
  glewInfoFunc("glUniform1uiv", glUniform1uiv == NULL);
  glewInfoFunc("glUniform2ui", glUniform2ui == NULL);
  glewInfoFunc("glUniform2uiv", glUniform2uiv == NULL);
  glewInfoFunc("glUniform3ui", glUniform3ui == NULL);
  glewInfoFunc("glUniform3uiv", glUniform3uiv == NULL);
  glewInfoFunc("glUniform4ui", glUniform4ui == NULL);
  glewInfoFunc("glUniform4uiv", glUniform4uiv == NULL);
  glewInfoFunc("glVertexAttribI1i", glVertexAttribI1i == NULL);
  glewInfoFunc("glVertexAttribI1iv", glVertexAttribI1iv == NULL);
  glewInfoFunc("glVertexAttribI1ui", glVertexAttribI1ui == NULL);
  glewInfoFunc("glVertexAttribI1uiv", glVertexAttribI1uiv == NULL);
  glewInfoFunc("glVertexAttribI2i", glVertexAttribI2i == NULL);
  glewInfoFunc("glVertexAttribI2iv", glVertexAttribI2iv == NULL);
  glewInfoFunc("glVertexAttribI2ui", glVertexAttribI2ui == NULL);
  glewInfoFunc("glVertexAttribI2uiv", glVertexAttribI2uiv == NULL);
  glewInfoFunc("glVertexAttribI3i", glVertexAttribI3i == NULL);
  glewInfoFunc("glVertexAttribI3iv", glVertexAttribI3iv == NULL);
  glewInfoFunc("glVertexAttribI3ui", glVertexAttribI3ui == NULL);
  glewInfoFunc("glVertexAttribI3uiv", glVertexAttribI3uiv == NULL);
  glewInfoFunc("glVertexAttribI4bv", glVertexAttribI4bv == NULL);
  glewInfoFunc("glVertexAttribI4i", glVertexAttribI4i == NULL);
  glewInfoFunc("glVertexAttribI4iv", glVertexAttribI4iv == NULL);
  glewInfoFunc("glVertexAttribI4sv", glVertexAttribI4sv == NULL);
  glewInfoFunc("glVertexAttribI4ubv", glVertexAttribI4ubv == NULL);
  glewInfoFunc("glVertexAttribI4ui", glVertexAttribI4ui == NULL);
  glewInfoFunc("glVertexAttribI4uiv", glVertexAttribI4uiv == NULL);
  glewInfoFunc("glVertexAttribI4usv", glVertexAttribI4usv == NULL);
  glewInfoFunc("glVertexAttribIPointer", glVertexAttribIPointer == NULL);
}

#endif /* GL_VERSION_3_0 */

#ifdef GL_VERSION_3_1

static void _glewInfo_GL_VERSION_3_1 (void)
{
  glewPrintExt("GL_VERSION_3_1", GLEW_VERSION_3_1, GLEW_VERSION_3_1, GLEW_VERSION_3_1);

  glewInfoFunc("glDrawArraysInstanced", glDrawArraysInstanced == NULL);
  glewInfoFunc("glDrawElementsInstanced", glDrawElementsInstanced == NULL);
  glewInfoFunc("glPrimitiveRestartIndex", glPrimitiveRestartIndex == NULL);
  glewInfoFunc("glTexBuffer", glTexBuffer == NULL);
}

#endif /* GL_VERSION_3_1 */

#ifdef GL_VERSION_3_2

static void _glewInfo_GL_VERSION_3_2 (void)
{
  glewPrintExt("GL_VERSION_3_2", GLEW_VERSION_3_2, GLEW_VERSION_3_2, GLEW_VERSION_3_2);

  glewInfoFunc("glFramebufferTexture", glFramebufferTexture == NULL);
  glewInfoFunc("glGetBufferParameteri64v", glGetBufferParameteri64v == NULL);
  glewInfoFunc("glGetInteger64i_v", glGetInteger64i_v == NULL);
}

#endif /* GL_VERSION_3_2 */

#ifdef GL_VERSION_3_3

static void _glewInfo_GL_VERSION_3_3 (void)
{
  glewPrintExt("GL_VERSION_3_3", GLEW_VERSION_3_3, GLEW_VERSION_3_3, GLEW_VERSION_3_3);

  glewInfoFunc("glVertexAttribDivisor", glVertexAttribDivisor == NULL);
}

#endif /* GL_VERSION_3_3 */

#ifdef GL_VERSION_4_0

static void _glewInfo_GL_VERSION_4_0 (void)
{
  glewPrintExt("GL_VERSION_4_0", GLEW_VERSION_4_0, GLEW_VERSION_4_0, GLEW_VERSION_4_0);

  glewInfoFunc("glBlendEquationSeparatei", glBlendEquationSeparatei == NULL);
  glewInfoFunc("glBlendEquationi", glBlendEquationi == NULL);
  glewInfoFunc("glBlendFuncSeparatei", glBlendFuncSeparatei == NULL);
  glewInfoFunc("glBlendFunci", glBlendFunci == NULL);
  glewInfoFunc("glMinSampleShading", glMinSampleShading == NULL);
}

#endif /* GL_VERSION_4_0 */

#ifdef GL_VERSION_4_1

static void _glewInfo_GL_VERSION_4_1 (void)
{
  glewPrintExt("GL_VERSION_4_1", GLEW_VERSION_4_1, GLEW_VERSION_4_1, GLEW_VERSION_4_1);
}

#endif /* GL_VERSION_4_1 */

#ifdef GL_VERSION_4_2

static void _glewInfo_GL_VERSION_4_2 (void)
{
  glewPrintExt("GL_VERSION_4_2", GLEW_VERSION_4_2, GLEW_VERSION_4_2, GLEW_VERSION_4_2);
}

#endif /* GL_VERSION_4_2 */

#ifdef GL_VERSION_4_3

static void _glewInfo_GL_VERSION_4_3 (void)
{
  glewPrintExt("GL_VERSION_4_3", GLEW_VERSION_4_3, GLEW_VERSION_4_3, GLEW_VERSION_4_3);
}

#endif /* GL_VERSION_4_3 */

#ifdef GL_VERSION_4_4

static void _glewInfo_GL_VERSION_4_4 (void)
{
  glewPrintExt("GL_VERSION_4_4", GLEW_VERSION_4_4, GLEW_VERSION_4_4, GLEW_VERSION_4_4);
}

#endif /* GL_VERSION_4_4 */

#ifdef GL_VERSION_4_5

static void _glewInfo_GL_VERSION_4_5 (void)
{
  glewPrintExt("GL_VERSION_4_5", GLEW_VERSION_4_5, GLEW_VERSION_4_5, GLEW_VERSION_4_5);

  glewInfoFunc("glGetGraphicsResetStatus", glGetGraphicsResetStatus == NULL);
  glewInfoFunc("glGetnCompressedTexImage", glGetnCompressedTexImage == NULL);
  glewInfoFunc("glGetnTexImage", glGetnTexImage == NULL);
  glewInfoFunc("glGetnUniformdv", glGetnUniformdv == NULL);
}

#endif /* GL_VERSION_4_5 */

#ifdef GL_VERSION_4_6

static void _glewInfo_GL_VERSION_4_6 (void)
{
  glewPrintExt("GL_VERSION_4_6", GLEW_VERSION_4_6, GLEW_VERSION_4_6, GLEW_VERSION_4_6);

  glewInfoFunc("glMultiDrawArraysIndirectCount", glMultiDrawArraysIndirectCount == NULL);
  glewInfoFunc("glMultiDrawElementsIndirectCount", glMultiDrawElementsIndirectCount == NULL);
  glewInfoFunc("glSpecializeShader", glSpecializeShader == NULL);
}

#endif /* GL_VERSION_4_6 */

#ifdef GL_3DFX_multisample

static void _glewInfo_GL_3DFX_multisample (void)
{
  glewPrintExt("GL_3DFX_multisample", GLEW_3DFX_multisample, glewIsSupported("GL_3DFX_multisample"), glewGetExtension("GL_3DFX_multisample"));
}

#endif /* GL_3DFX_multisample */

#ifdef GL_3DFX_tbuffer

static void _glewInfo_GL_3DFX_tbuffer (void)
{
  glewPrintExt("GL_3DFX_tbuffer", GLEW_3DFX_tbuffer, glewIsSupported("GL_3DFX_tbuffer"), glewGetExtension("GL_3DFX_tbuffer"));

  glewInfoFunc("glTbufferMask3DFX", glTbufferMask3DFX == NULL);
}

#endif /* GL_3DFX_tbuffer */

#ifdef GL_3DFX_texture_compression_FXT1

static void _glewInfo_GL_3DFX_texture_compression_FXT1 (void)
{
  glewPrintExt("GL_3DFX_texture_compression_FXT1", GLEW_3DFX_texture_compression_FXT1, glewIsSupported("GL_3DFX_texture_compression_FXT1"), glewGetExtension("GL_3DFX_texture_compression_FXT1"));
}

#endif /* GL_3DFX_texture_compression_FXT1 */

#ifdef GL_AMD_blend_minmax_factor

static void _glewInfo_GL_AMD_blend_minmax_factor (void)
{
  glewPrintExt("GL_AMD_blend_minmax_factor", GLEW_AMD_blend_minmax_factor, glewIsSupported("GL_AMD_blend_minmax_factor"), glewGetExtension("GL_AMD_blend_minmax_factor"));
}

#endif /* GL_AMD_blend_minmax_factor */

#ifdef GL_AMD_compressed_3DC_texture

static void _glewInfo_GL_AMD_compressed_3DC_texture (void)
{
  glewPrintExt("GL_AMD_compressed_3DC_texture", GLEW_AMD_compressed_3DC_texture, glewIsSupported("GL_AMD_compressed_3DC_texture"), glewGetExtension("GL_AMD_compressed_3DC_texture"));
}

#endif /* GL_AMD_compressed_3DC_texture */

#ifdef GL_AMD_compressed_ATC_texture

static void _glewInfo_GL_AMD_compressed_ATC_texture (void)
{
  glewPrintExt("GL_AMD_compressed_ATC_texture", GLEW_AMD_compressed_ATC_texture, glewIsSupported("GL_AMD_compressed_ATC_texture"), glewGetExtension("GL_AMD_compressed_ATC_texture"));
}

#endif /* GL_AMD_compressed_ATC_texture */

#ifdef GL_AMD_conservative_depth

static void _glewInfo_GL_AMD_conservative_depth (void)
{
  glewPrintExt("GL_AMD_conservative_depth", GLEW_AMD_conservative_depth, glewIsSupported("GL_AMD_conservative_depth"), glewGetExtension("GL_AMD_conservative_depth"));
}

#endif /* GL_AMD_conservative_depth */

#ifdef GL_AMD_debug_output

static void _glewInfo_GL_AMD_debug_output (void)
{
  glewPrintExt("GL_AMD_debug_output", GLEW_AMD_debug_output, glewIsSupported("GL_AMD_debug_output"), glewGetExtension("GL_AMD_debug_output"));

  glewInfoFunc("glDebugMessageCallbackAMD", glDebugMessageCallbackAMD == NULL);
  glewInfoFunc("glDebugMessageEnableAMD", glDebugMessageEnableAMD == NULL);
  glewInfoFunc("glDebugMessageInsertAMD", glDebugMessageInsertAMD == NULL);
  glewInfoFunc("glGetDebugMessageLogAMD", glGetDebugMessageLogAMD == NULL);
}

#endif /* GL_AMD_debug_output */

#ifdef GL_AMD_depth_clamp_separate

static void _glewInfo_GL_AMD_depth_clamp_separate (void)
{
  glewPrintExt("GL_AMD_depth_clamp_separate", GLEW_AMD_depth_clamp_separate, glewIsSupported("GL_AMD_depth_clamp_separate"), glewGetExtension("GL_AMD_depth_clamp_separate"));
}

#endif /* GL_AMD_depth_clamp_separate */

#ifdef GL_AMD_draw_buffers_blend

static void _glewInfo_GL_AMD_draw_buffers_blend (void)
{
  glewPrintExt("GL_AMD_draw_buffers_blend", GLEW_AMD_draw_buffers_blend, glewIsSupported("GL_AMD_draw_buffers_blend"), glewGetExtension("GL_AMD_draw_buffers_blend"));

  glewInfoFunc("glBlendEquationIndexedAMD", glBlendEquationIndexedAMD == NULL);
  glewInfoFunc("glBlendEquationSeparateIndexedAMD", glBlendEquationSeparateIndexedAMD == NULL);
  glewInfoFunc("glBlendFuncIndexedAMD", glBlendFuncIndexedAMD == NULL);
  glewInfoFunc("glBlendFuncSeparateIndexedAMD", glBlendFuncSeparateIndexedAMD == NULL);
}

#endif /* GL_AMD_draw_buffers_blend */

#ifdef GL_AMD_framebuffer_sample_positions

static void _glewInfo_GL_AMD_framebuffer_sample_positions (void)
{
  glewPrintExt("GL_AMD_framebuffer_sample_positions", GLEW_AMD_framebuffer_sample_positions, glewIsSupported("GL_AMD_framebuffer_sample_positions"), glewGetExtension("GL_AMD_framebuffer_sample_positions"));

  glewInfoFunc("glFramebufferSamplePositionsfvAMD", glFramebufferSamplePositionsfvAMD == NULL);
  glewInfoFunc("glGetFramebufferParameterfvAMD", glGetFramebufferParameterfvAMD == NULL);
  glewInfoFunc("glGetNamedFramebufferParameterfvAMD", glGetNamedFramebufferParameterfvAMD == NULL);
  glewInfoFunc("glNamedFramebufferSamplePositionsfvAMD", glNamedFramebufferSamplePositionsfvAMD == NULL);
}

#endif /* GL_AMD_framebuffer_sample_positions */

#ifdef GL_AMD_gcn_shader

static void _glewInfo_GL_AMD_gcn_shader (void)
{
  glewPrintExt("GL_AMD_gcn_shader", GLEW_AMD_gcn_shader, glewIsSupported("GL_AMD_gcn_shader"), glewGetExtension("GL_AMD_gcn_shader"));
}

#endif /* GL_AMD_gcn_shader */

#ifdef GL_AMD_gpu_shader_half_float

static void _glewInfo_GL_AMD_gpu_shader_half_float (void)
{
  glewPrintExt("GL_AMD_gpu_shader_half_float", GLEW_AMD_gpu_shader_half_float, glewIsSupported("GL_AMD_gpu_shader_half_float"), glewGetExtension("GL_AMD_gpu_shader_half_float"));
}

#endif /* GL_AMD_gpu_shader_half_float */

#ifdef GL_AMD_gpu_shader_int16

static void _glewInfo_GL_AMD_gpu_shader_int16 (void)
{
  glewPrintExt("GL_AMD_gpu_shader_int16", GLEW_AMD_gpu_shader_int16, glewIsSupported("GL_AMD_gpu_shader_int16"), glewGetExtension("GL_AMD_gpu_shader_int16"));
}

#endif /* GL_AMD_gpu_shader_int16 */

#ifdef GL_AMD_gpu_shader_int64

static void _glewInfo_GL_AMD_gpu_shader_int64 (void)
{
  glewPrintExt("GL_AMD_gpu_shader_int64", GLEW_AMD_gpu_shader_int64, glewIsSupported("GL_AMD_gpu_shader_int64"), glewGetExtension("GL_AMD_gpu_shader_int64"));
}

#endif /* GL_AMD_gpu_shader_int64 */

#ifdef GL_AMD_interleaved_elements

static void _glewInfo_GL_AMD_interleaved_elements (void)
{
  glewPrintExt("GL_AMD_interleaved_elements", GLEW_AMD_interleaved_elements, glewIsSupported("GL_AMD_interleaved_elements"), glewGetExtension("GL_AMD_interleaved_elements"));

  glewInfoFunc("glVertexAttribParameteriAMD", glVertexAttribParameteriAMD == NULL);
}

#endif /* GL_AMD_interleaved_elements */

#ifdef GL_AMD_multi_draw_indirect

static void _glewInfo_GL_AMD_multi_draw_indirect (void)
{
  glewPrintExt("GL_AMD_multi_draw_indirect", GLEW_AMD_multi_draw_indirect, glewIsSupported("GL_AMD_multi_draw_indirect"), glewGetExtension("GL_AMD_multi_draw_indirect"));

  glewInfoFunc("glMultiDrawArraysIndirectAMD", glMultiDrawArraysIndirectAMD == NULL);
  glewInfoFunc("glMultiDrawElementsIndirectAMD", glMultiDrawElementsIndirectAMD == NULL);
}

#endif /* GL_AMD_multi_draw_indirect */

#ifdef GL_AMD_name_gen_delete

static void _glewInfo_GL_AMD_name_gen_delete (void)
{
  glewPrintExt("GL_AMD_name_gen_delete", GLEW_AMD_name_gen_delete, glewIsSupported("GL_AMD_name_gen_delete"), glewGetExtension("GL_AMD_name_gen_delete"));

  glewInfoFunc("glDeleteNamesAMD", glDeleteNamesAMD == NULL);
  glewInfoFunc("glGenNamesAMD", glGenNamesAMD == NULL);
  glewInfoFunc("glIsNameAMD", glIsNameAMD == NULL);
}

#endif /* GL_AMD_name_gen_delete */

#ifdef GL_AMD_occlusion_query_event

static void _glewInfo_GL_AMD_occlusion_query_event (void)
{
  glewPrintExt("GL_AMD_occlusion_query_event", GLEW_AMD_occlusion_query_event, glewIsSupported("GL_AMD_occlusion_query_event"), glewGetExtension("GL_AMD_occlusion_query_event"));

  glewInfoFunc("glQueryObjectParameteruiAMD", glQueryObjectParameteruiAMD == NULL);
}

#endif /* GL_AMD_occlusion_query_event */

#ifdef GL_AMD_performance_monitor

static void _glewInfo_GL_AMD_performance_monitor (void)
{
  glewPrintExt("GL_AMD_performance_monitor", GLEW_AMD_performance_monitor, glewIsSupported("GL_AMD_performance_monitor"), glewGetExtension("GL_AMD_performance_monitor"));

  glewInfoFunc("glBeginPerfMonitorAMD", glBeginPerfMonitorAMD == NULL);
  glewInfoFunc("glDeletePerfMonitorsAMD", glDeletePerfMonitorsAMD == NULL);
  glewInfoFunc("glEndPerfMonitorAMD", glEndPerfMonitorAMD == NULL);
  glewInfoFunc("glGenPerfMonitorsAMD", glGenPerfMonitorsAMD == NULL);
  glewInfoFunc("glGetPerfMonitorCounterDataAMD", glGetPerfMonitorCounterDataAMD == NULL);
  glewInfoFunc("glGetPerfMonitorCounterInfoAMD", glGetPerfMonitorCounterInfoAMD == NULL);
  glewInfoFunc("glGetPerfMonitorCounterStringAMD", glGetPerfMonitorCounterStringAMD == NULL);
  glewInfoFunc("glGetPerfMonitorCountersAMD", glGetPerfMonitorCountersAMD == NULL);
  glewInfoFunc("glGetPerfMonitorGroupStringAMD", glGetPerfMonitorGroupStringAMD == NULL);
  glewInfoFunc("glGetPerfMonitorGroupsAMD", glGetPerfMonitorGroupsAMD == NULL);
  glewInfoFunc("glSelectPerfMonitorCountersAMD", glSelectPerfMonitorCountersAMD == NULL);
}

#endif /* GL_AMD_performance_monitor */

#ifdef GL_AMD_pinned_memory

static void _glewInfo_GL_AMD_pinned_memory (void)
{
  glewPrintExt("GL_AMD_pinned_memory", GLEW_AMD_pinned_memory, glewIsSupported("GL_AMD_pinned_memory"), glewGetExtension("GL_AMD_pinned_memory"));
}

#endif /* GL_AMD_pinned_memory */

#ifdef GL_AMD_program_binary_Z400

static void _glewInfo_GL_AMD_program_binary_Z400 (void)
{
  glewPrintExt("GL_AMD_program_binary_Z400", GLEW_AMD_program_binary_Z400, glewIsSupported("GL_AMD_program_binary_Z400"), glewGetExtension("GL_AMD_program_binary_Z400"));
}

#endif /* GL_AMD_program_binary_Z400 */

#ifdef GL_AMD_query_buffer_object

static void _glewInfo_GL_AMD_query_buffer_object (void)
{
  glewPrintExt("GL_AMD_query_buffer_object", GLEW_AMD_query_buffer_object, glewIsSupported("GL_AMD_query_buffer_object"), glewGetExtension("GL_AMD_query_buffer_object"));
}

#endif /* GL_AMD_query_buffer_object */

#ifdef GL_AMD_sample_positions

static void _glewInfo_GL_AMD_sample_positions (void)
{
  glewPrintExt("GL_AMD_sample_positions", GLEW_AMD_sample_positions, glewIsSupported("GL_AMD_sample_positions"), glewGetExtension("GL_AMD_sample_positions"));

  glewInfoFunc("glSetMultisamplefvAMD", glSetMultisamplefvAMD == NULL);
}

#endif /* GL_AMD_sample_positions */

#ifdef GL_AMD_seamless_cubemap_per_texture

static void _glewInfo_GL_AMD_seamless_cubemap_per_texture (void)
{
  glewPrintExt("GL_AMD_seamless_cubemap_per_texture", GLEW_AMD_seamless_cubemap_per_texture, glewIsSupported("GL_AMD_seamless_cubemap_per_texture"), glewGetExtension("GL_AMD_seamless_cubemap_per_texture"));
}

#endif /* GL_AMD_seamless_cubemap_per_texture */

#ifdef GL_AMD_shader_atomic_counter_ops

static void _glewInfo_GL_AMD_shader_atomic_counter_ops (void)
{
  glewPrintExt("GL_AMD_shader_atomic_counter_ops", GLEW_AMD_shader_atomic_counter_ops, glewIsSupported("GL_AMD_shader_atomic_counter_ops"), glewGetExtension("GL_AMD_shader_atomic_counter_ops"));
}

#endif /* GL_AMD_shader_atomic_counter_ops */

#ifdef GL_AMD_shader_ballot

static void _glewInfo_GL_AMD_shader_ballot (void)
{
  glewPrintExt("GL_AMD_shader_ballot", GLEW_AMD_shader_ballot, glewIsSupported("GL_AMD_shader_ballot"), glewGetExtension("GL_AMD_shader_ballot"));
}

#endif /* GL_AMD_shader_ballot */

#ifdef GL_AMD_shader_explicit_vertex_parameter

static void _glewInfo_GL_AMD_shader_explicit_vertex_parameter (void)
{
  glewPrintExt("GL_AMD_shader_explicit_vertex_parameter", GLEW_AMD_shader_explicit_vertex_parameter, glewIsSupported("GL_AMD_shader_explicit_vertex_parameter"), glewGetExtension("GL_AMD_shader_explicit_vertex_parameter"));
}

#endif /* GL_AMD_shader_explicit_vertex_parameter */

#ifdef GL_AMD_shader_stencil_export

static void _glewInfo_GL_AMD_shader_stencil_export (void)
{
  glewPrintExt("GL_AMD_shader_stencil_export", GLEW_AMD_shader_stencil_export, glewIsSupported("GL_AMD_shader_stencil_export"), glewGetExtension("GL_AMD_shader_stencil_export"));
}

#endif /* GL_AMD_shader_stencil_export */

#ifdef GL_AMD_shader_stencil_value_export

static void _glewInfo_GL_AMD_shader_stencil_value_export (void)
{
  glewPrintExt("GL_AMD_shader_stencil_value_export", GLEW_AMD_shader_stencil_value_export, glewIsSupported("GL_AMD_shader_stencil_value_export"), glewGetExtension("GL_AMD_shader_stencil_value_export"));
}

#endif /* GL_AMD_shader_stencil_value_export */

#ifdef GL_AMD_shader_trinary_minmax

static void _glewInfo_GL_AMD_shader_trinary_minmax (void)
{
  glewPrintExt("GL_AMD_shader_trinary_minmax", GLEW_AMD_shader_trinary_minmax, glewIsSupported("GL_AMD_shader_trinary_minmax"), glewGetExtension("GL_AMD_shader_trinary_minmax"));
}

#endif /* GL_AMD_shader_trinary_minmax */

#ifdef GL_AMD_sparse_texture

static void _glewInfo_GL_AMD_sparse_texture (void)
{
  glewPrintExt("GL_AMD_sparse_texture", GLEW_AMD_sparse_texture, glewIsSupported("GL_AMD_sparse_texture"), glewGetExtension("GL_AMD_sparse_texture"));

  glewInfoFunc("glTexStorageSparseAMD", glTexStorageSparseAMD == NULL);
  glewInfoFunc("glTextureStorageSparseAMD", glTextureStorageSparseAMD == NULL);
}

#endif /* GL_AMD_sparse_texture */

#ifdef GL_AMD_stencil_operation_extended

static void _glewInfo_GL_AMD_stencil_operation_extended (void)
{
  glewPrintExt("GL_AMD_stencil_operation_extended", GLEW_AMD_stencil_operation_extended, glewIsSupported("GL_AMD_stencil_operation_extended"), glewGetExtension("GL_AMD_stencil_operation_extended"));

  glewInfoFunc("glStencilOpValueAMD", glStencilOpValueAMD == NULL);
}

#endif /* GL_AMD_stencil_operation_extended */

#ifdef GL_AMD_texture_gather_bias_lod

static void _glewInfo_GL_AMD_texture_gather_bias_lod (void)
{
  glewPrintExt("GL_AMD_texture_gather_bias_lod", GLEW_AMD_texture_gather_bias_lod, glewIsSupported("GL_AMD_texture_gather_bias_lod"), glewGetExtension("GL_AMD_texture_gather_bias_lod"));
}

#endif /* GL_AMD_texture_gather_bias_lod */

#ifdef GL_AMD_texture_texture4

static void _glewInfo_GL_AMD_texture_texture4 (void)
{
  glewPrintExt("GL_AMD_texture_texture4", GLEW_AMD_texture_texture4, glewIsSupported("GL_AMD_texture_texture4"), glewGetExtension("GL_AMD_texture_texture4"));
}

#endif /* GL_AMD_texture_texture4 */

#ifdef GL_AMD_transform_feedback3_lines_triangles

static void _glewInfo_GL_AMD_transform_feedback3_lines_triangles (void)
{
  glewPrintExt("GL_AMD_transform_feedback3_lines_triangles", GLEW_AMD_transform_feedback3_lines_triangles, glewIsSupported("GL_AMD_transform_feedback3_lines_triangles"), glewGetExtension("GL_AMD_transform_feedback3_lines_triangles"));
}

#endif /* GL_AMD_transform_feedback3_lines_triangles */

#ifdef GL_AMD_transform_feedback4

static void _glewInfo_GL_AMD_transform_feedback4 (void)
{
  glewPrintExt("GL_AMD_transform_feedback4", GLEW_AMD_transform_feedback4, glewIsSupported("GL_AMD_transform_feedback4"), glewGetExtension("GL_AMD_transform_feedback4"));
}

#endif /* GL_AMD_transform_feedback4 */

#ifdef GL_AMD_vertex_shader_layer

static void _glewInfo_GL_AMD_vertex_shader_layer (void)
{
  glewPrintExt("GL_AMD_vertex_shader_layer", GLEW_AMD_vertex_shader_layer, glewIsSupported("GL_AMD_vertex_shader_layer"), glewGetExtension("GL_AMD_vertex_shader_layer"));
}

#endif /* GL_AMD_vertex_shader_layer */

#ifdef GL_AMD_vertex_shader_tessellator

static void _glewInfo_GL_AMD_vertex_shader_tessellator (void)
{
  glewPrintExt("GL_AMD_vertex_shader_tessellator", GLEW_AMD_vertex_shader_tessellator, glewIsSupported("GL_AMD_vertex_shader_tessellator"), glewGetExtension("GL_AMD_vertex_shader_tessellator"));

  glewInfoFunc("glTessellationFactorAMD", glTessellationFactorAMD == NULL);
  glewInfoFunc("glTessellationModeAMD", glTessellationModeAMD == NULL);
}

#endif /* GL_AMD_vertex_shader_tessellator */

#ifdef GL_AMD_vertex_shader_viewport_index

static void _glewInfo_GL_AMD_vertex_shader_viewport_index (void)
{
  glewPrintExt("GL_AMD_vertex_shader_viewport_index", GLEW_AMD_vertex_shader_viewport_index, glewIsSupported("GL_AMD_vertex_shader_viewport_index"), glewGetExtension("GL_AMD_vertex_shader_viewport_index"));
}

#endif /* GL_AMD_vertex_shader_viewport_index */

#ifdef GL_ANDROID_extension_pack_es31a

static void _glewInfo_GL_ANDROID_extension_pack_es31a (void)
{
  glewPrintExt("GL_ANDROID_extension_pack_es31a", GLEW_ANDROID_extension_pack_es31a, glewIsSupported("GL_ANDROID_extension_pack_es31a"), glewGetExtension("GL_ANDROID_extension_pack_es31a"));
}

#endif /* GL_ANDROID_extension_pack_es31a */

#ifdef GL_ANGLE_depth_texture

static void _glewInfo_GL_ANGLE_depth_texture (void)
{
  glewPrintExt("GL_ANGLE_depth_texture", GLEW_ANGLE_depth_texture, glewIsSupported("GL_ANGLE_depth_texture"), glewGetExtension("GL_ANGLE_depth_texture"));
}

#endif /* GL_ANGLE_depth_texture */

#ifdef GL_ANGLE_framebuffer_blit

static void _glewInfo_GL_ANGLE_framebuffer_blit (void)
{
  glewPrintExt("GL_ANGLE_framebuffer_blit", GLEW_ANGLE_framebuffer_blit, glewIsSupported("GL_ANGLE_framebuffer_blit"), glewGetExtension("GL_ANGLE_framebuffer_blit"));

  glewInfoFunc("glBlitFramebufferANGLE", glBlitFramebufferANGLE == NULL);
}

#endif /* GL_ANGLE_framebuffer_blit */

#ifdef GL_ANGLE_framebuffer_multisample

static void _glewInfo_GL_ANGLE_framebuffer_multisample (void)
{
  glewPrintExt("GL_ANGLE_framebuffer_multisample", GLEW_ANGLE_framebuffer_multisample, glewIsSupported("GL_ANGLE_framebuffer_multisample"), glewGetExtension("GL_ANGLE_framebuffer_multisample"));

  glewInfoFunc("glRenderbufferStorageMultisampleANGLE", glRenderbufferStorageMultisampleANGLE == NULL);
}

#endif /* GL_ANGLE_framebuffer_multisample */

#ifdef GL_ANGLE_instanced_arrays

static void _glewInfo_GL_ANGLE_instanced_arrays (void)
{
  glewPrintExt("GL_ANGLE_instanced_arrays", GLEW_ANGLE_instanced_arrays, glewIsSupported("GL_ANGLE_instanced_arrays"), glewGetExtension("GL_ANGLE_instanced_arrays"));

  glewInfoFunc("glDrawArraysInstancedANGLE", glDrawArraysInstancedANGLE == NULL);
  glewInfoFunc("glDrawElementsInstancedANGLE", glDrawElementsInstancedANGLE == NULL);
  glewInfoFunc("glVertexAttribDivisorANGLE", glVertexAttribDivisorANGLE == NULL);
}

#endif /* GL_ANGLE_instanced_arrays */

#ifdef GL_ANGLE_pack_reverse_row_order

static void _glewInfo_GL_ANGLE_pack_reverse_row_order (void)
{
  glewPrintExt("GL_ANGLE_pack_reverse_row_order", GLEW_ANGLE_pack_reverse_row_order, glewIsSupported("GL_ANGLE_pack_reverse_row_order"), glewGetExtension("GL_ANGLE_pack_reverse_row_order"));
}

#endif /* GL_ANGLE_pack_reverse_row_order */

#ifdef GL_ANGLE_program_binary

static void _glewInfo_GL_ANGLE_program_binary (void)
{
  glewPrintExt("GL_ANGLE_program_binary", GLEW_ANGLE_program_binary, glewIsSupported("GL_ANGLE_program_binary"), glewGetExtension("GL_ANGLE_program_binary"));
}

#endif /* GL_ANGLE_program_binary */

#ifdef GL_ANGLE_texture_compression_dxt1

static void _glewInfo_GL_ANGLE_texture_compression_dxt1 (void)
{
  glewPrintExt("GL_ANGLE_texture_compression_dxt1", GLEW_ANGLE_texture_compression_dxt1, glewIsSupported("GL_ANGLE_texture_compression_dxt1"), glewGetExtension("GL_ANGLE_texture_compression_dxt1"));
}

#endif /* GL_ANGLE_texture_compression_dxt1 */

#ifdef GL_ANGLE_texture_compression_dxt3

static void _glewInfo_GL_ANGLE_texture_compression_dxt3 (void)
{
  glewPrintExt("GL_ANGLE_texture_compression_dxt3", GLEW_ANGLE_texture_compression_dxt3, glewIsSupported("GL_ANGLE_texture_compression_dxt3"), glewGetExtension("GL_ANGLE_texture_compression_dxt3"));
}

#endif /* GL_ANGLE_texture_compression_dxt3 */

#ifdef GL_ANGLE_texture_compression_dxt5

static void _glewInfo_GL_ANGLE_texture_compression_dxt5 (void)
{
  glewPrintExt("GL_ANGLE_texture_compression_dxt5", GLEW_ANGLE_texture_compression_dxt5, glewIsSupported("GL_ANGLE_texture_compression_dxt5"), glewGetExtension("GL_ANGLE_texture_compression_dxt5"));
}

#endif /* GL_ANGLE_texture_compression_dxt5 */

#ifdef GL_ANGLE_texture_usage

static void _glewInfo_GL_ANGLE_texture_usage (void)
{
  glewPrintExt("GL_ANGLE_texture_usage", GLEW_ANGLE_texture_usage, glewIsSupported("GL_ANGLE_texture_usage"), glewGetExtension("GL_ANGLE_texture_usage"));
}

#endif /* GL_ANGLE_texture_usage */

#ifdef GL_ANGLE_timer_query

static void _glewInfo_GL_ANGLE_timer_query (void)
{
  glewPrintExt("GL_ANGLE_timer_query", GLEW_ANGLE_timer_query, glewIsSupported("GL_ANGLE_timer_query"), glewGetExtension("GL_ANGLE_timer_query"));

  glewInfoFunc("glBeginQueryANGLE", glBeginQueryANGLE == NULL);
  glewInfoFunc("glDeleteQueriesANGLE", glDeleteQueriesANGLE == NULL);
  glewInfoFunc("glEndQueryANGLE", glEndQueryANGLE == NULL);
  glewInfoFunc("glGenQueriesANGLE", glGenQueriesANGLE == NULL);
  glewInfoFunc("glGetQueryObjecti64vANGLE", glGetQueryObjecti64vANGLE == NULL);
  glewInfoFunc("glGetQueryObjectivANGLE", glGetQueryObjectivANGLE == NULL);
  glewInfoFunc("glGetQueryObjectui64vANGLE", glGetQueryObjectui64vANGLE == NULL);
  glewInfoFunc("glGetQueryObjectuivANGLE", glGetQueryObjectuivANGLE == NULL);
  glewInfoFunc("glGetQueryivANGLE", glGetQueryivANGLE == NULL);
  glewInfoFunc("glIsQueryANGLE", glIsQueryANGLE == NULL);
  glewInfoFunc("glQueryCounterANGLE", glQueryCounterANGLE == NULL);
}

#endif /* GL_ANGLE_timer_query */

#ifdef GL_ANGLE_translated_shader_source

static void _glewInfo_GL_ANGLE_translated_shader_source (void)
{
  glewPrintExt("GL_ANGLE_translated_shader_source", GLEW_ANGLE_translated_shader_source, glewIsSupported("GL_ANGLE_translated_shader_source"), glewGetExtension("GL_ANGLE_translated_shader_source"));

  glewInfoFunc("glGetTranslatedShaderSourceANGLE", glGetTranslatedShaderSourceANGLE == NULL);
}

#endif /* GL_ANGLE_translated_shader_source */

#ifdef GL_APPLE_aux_depth_stencil

static void _glewInfo_GL_APPLE_aux_depth_stencil (void)
{
  glewPrintExt("GL_APPLE_aux_depth_stencil", GLEW_APPLE_aux_depth_stencil, glewIsSupported("GL_APPLE_aux_depth_stencil"), glewGetExtension("GL_APPLE_aux_depth_stencil"));
}

#endif /* GL_APPLE_aux_depth_stencil */

#ifdef GL_APPLE_client_storage

static void _glewInfo_GL_APPLE_client_storage (void)
{
  glewPrintExt("GL_APPLE_client_storage", GLEW_APPLE_client_storage, glewIsSupported("GL_APPLE_client_storage"), glewGetExtension("GL_APPLE_client_storage"));
}

#endif /* GL_APPLE_client_storage */

#ifdef GL_APPLE_clip_distance

static void _glewInfo_GL_APPLE_clip_distance (void)
{
  glewPrintExt("GL_APPLE_clip_distance", GLEW_APPLE_clip_distance, glewIsSupported("GL_APPLE_clip_distance"), glewGetExtension("GL_APPLE_clip_distance"));
}

#endif /* GL_APPLE_clip_distance */

#ifdef GL_APPLE_color_buffer_packed_float

static void _glewInfo_GL_APPLE_color_buffer_packed_float (void)
{
  glewPrintExt("GL_APPLE_color_buffer_packed_float", GLEW_APPLE_color_buffer_packed_float, glewIsSupported("GL_APPLE_color_buffer_packed_float"), glewGetExtension("GL_APPLE_color_buffer_packed_float"));
}

#endif /* GL_APPLE_color_buffer_packed_float */

#ifdef GL_APPLE_copy_texture_levels

static void _glewInfo_GL_APPLE_copy_texture_levels (void)
{
  glewPrintExt("GL_APPLE_copy_texture_levels", GLEW_APPLE_copy_texture_levels, glewIsSupported("GL_APPLE_copy_texture_levels"), glewGetExtension("GL_APPLE_copy_texture_levels"));

  glewInfoFunc("glCopyTextureLevelsAPPLE", glCopyTextureLevelsAPPLE == NULL);
}

#endif /* GL_APPLE_copy_texture_levels */

#ifdef GL_APPLE_element_array

static void _glewInfo_GL_APPLE_element_array (void)
{
  glewPrintExt("GL_APPLE_element_array", GLEW_APPLE_element_array, glewIsSupported("GL_APPLE_element_array"), glewGetExtension("GL_APPLE_element_array"));

  glewInfoFunc("glDrawElementArrayAPPLE", glDrawElementArrayAPPLE == NULL);
  glewInfoFunc("glDrawRangeElementArrayAPPLE", glDrawRangeElementArrayAPPLE == NULL);
  glewInfoFunc("glElementPointerAPPLE", glElementPointerAPPLE == NULL);
  glewInfoFunc("glMultiDrawElementArrayAPPLE", glMultiDrawElementArrayAPPLE == NULL);
  glewInfoFunc("glMultiDrawRangeElementArrayAPPLE", glMultiDrawRangeElementArrayAPPLE == NULL);
}

#endif /* GL_APPLE_element_array */

#ifdef GL_APPLE_fence

static void _glewInfo_GL_APPLE_fence (void)
{
  glewPrintExt("GL_APPLE_fence", GLEW_APPLE_fence, glewIsSupported("GL_APPLE_fence"), glewGetExtension("GL_APPLE_fence"));

  glewInfoFunc("glDeleteFencesAPPLE", glDeleteFencesAPPLE == NULL);
  glewInfoFunc("glFinishFenceAPPLE", glFinishFenceAPPLE == NULL);
  glewInfoFunc("glFinishObjectAPPLE", glFinishObjectAPPLE == NULL);
  glewInfoFunc("glGenFencesAPPLE", glGenFencesAPPLE == NULL);
  glewInfoFunc("glIsFenceAPPLE", glIsFenceAPPLE == NULL);
  glewInfoFunc("glSetFenceAPPLE", glSetFenceAPPLE == NULL);
  glewInfoFunc("glTestFenceAPPLE", glTestFenceAPPLE == NULL);
  glewInfoFunc("glTestObjectAPPLE", glTestObjectAPPLE == NULL);
}

#endif /* GL_APPLE_fence */

#ifdef GL_APPLE_float_pixels

static void _glewInfo_GL_APPLE_float_pixels (void)
{
  glewPrintExt("GL_APPLE_float_pixels", GLEW_APPLE_float_pixels, glewIsSupported("GL_APPLE_float_pixels"), glewGetExtension("GL_APPLE_float_pixels"));
}

#endif /* GL_APPLE_float_pixels */

#ifdef GL_APPLE_flush_buffer_range

static void _glewInfo_GL_APPLE_flush_buffer_range (void)
{
  glewPrintExt("GL_APPLE_flush_buffer_range", GLEW_APPLE_flush_buffer_range, glewIsSupported("GL_APPLE_flush_buffer_range"), glewGetExtension("GL_APPLE_flush_buffer_range"));

  glewInfoFunc("glBufferParameteriAPPLE", glBufferParameteriAPPLE == NULL);
  glewInfoFunc("glFlushMappedBufferRangeAPPLE", glFlushMappedBufferRangeAPPLE == NULL);
}

#endif /* GL_APPLE_flush_buffer_range */

#ifdef GL_APPLE_framebuffer_multisample

static void _glewInfo_GL_APPLE_framebuffer_multisample (void)
{
  glewPrintExt("GL_APPLE_framebuffer_multisample", GLEW_APPLE_framebuffer_multisample, glewIsSupported("GL_APPLE_framebuffer_multisample"), glewGetExtension("GL_APPLE_framebuffer_multisample"));

  glewInfoFunc("glRenderbufferStorageMultisampleAPPLE", glRenderbufferStorageMultisampleAPPLE == NULL);
  glewInfoFunc("glResolveMultisampleFramebufferAPPLE", glResolveMultisampleFramebufferAPPLE == NULL);
}

#endif /* GL_APPLE_framebuffer_multisample */

#ifdef GL_APPLE_object_purgeable

static void _glewInfo_GL_APPLE_object_purgeable (void)
{
  glewPrintExt("GL_APPLE_object_purgeable", GLEW_APPLE_object_purgeable, glewIsSupported("GL_APPLE_object_purgeable"), glewGetExtension("GL_APPLE_object_purgeable"));

  glewInfoFunc("glGetObjectParameterivAPPLE", glGetObjectParameterivAPPLE == NULL);
  glewInfoFunc("glObjectPurgeableAPPLE", glObjectPurgeableAPPLE == NULL);
  glewInfoFunc("glObjectUnpurgeableAPPLE", glObjectUnpurgeableAPPLE == NULL);
}

#endif /* GL_APPLE_object_purgeable */

#ifdef GL_APPLE_pixel_buffer

static void _glewInfo_GL_APPLE_pixel_buffer (void)
{
  glewPrintExt("GL_APPLE_pixel_buffer", GLEW_APPLE_pixel_buffer, glewIsSupported("GL_APPLE_pixel_buffer"), glewGetExtension("GL_APPLE_pixel_buffer"));
}

#endif /* GL_APPLE_pixel_buffer */

#ifdef GL_APPLE_rgb_422

static void _glewInfo_GL_APPLE_rgb_422 (void)
{
  glewPrintExt("GL_APPLE_rgb_422", GLEW_APPLE_rgb_422, glewIsSupported("GL_APPLE_rgb_422"), glewGetExtension("GL_APPLE_rgb_422"));
}

#endif /* GL_APPLE_rgb_422 */

#ifdef GL_APPLE_row_bytes

static void _glewInfo_GL_APPLE_row_bytes (void)
{
  glewPrintExt("GL_APPLE_row_bytes", GLEW_APPLE_row_bytes, glewIsSupported("GL_APPLE_row_bytes"), glewGetExtension("GL_APPLE_row_bytes"));
}

#endif /* GL_APPLE_row_bytes */

#ifdef GL_APPLE_specular_vector

static void _glewInfo_GL_APPLE_specular_vector (void)
{
  glewPrintExt("GL_APPLE_specular_vector", GLEW_APPLE_specular_vector, glewIsSupported("GL_APPLE_specular_vector"), glewGetExtension("GL_APPLE_specular_vector"));
}

#endif /* GL_APPLE_specular_vector */

#ifdef GL_APPLE_sync

static void _glewInfo_GL_APPLE_sync (void)
{
  glewPrintExt("GL_APPLE_sync", GLEW_APPLE_sync, glewIsSupported("GL_APPLE_sync"), glewGetExtension("GL_APPLE_sync"));

  glewInfoFunc("glClientWaitSyncAPPLE", glClientWaitSyncAPPLE == NULL);
  glewInfoFunc("glDeleteSyncAPPLE", glDeleteSyncAPPLE == NULL);
  glewInfoFunc("glFenceSyncAPPLE", glFenceSyncAPPLE == NULL);
  glewInfoFunc("glGetInteger64vAPPLE", glGetInteger64vAPPLE == NULL);
  glewInfoFunc("glGetSyncivAPPLE", glGetSyncivAPPLE == NULL);
  glewInfoFunc("glIsSyncAPPLE", glIsSyncAPPLE == NULL);
  glewInfoFunc("glWaitSyncAPPLE", glWaitSyncAPPLE == NULL);
}

#endif /* GL_APPLE_sync */

#ifdef GL_APPLE_texture_2D_limited_npot

static void _glewInfo_GL_APPLE_texture_2D_limited_npot (void)
{
  glewPrintExt("GL_APPLE_texture_2D_limited_npot", GLEW_APPLE_texture_2D_limited_npot, glewIsSupported("GL_APPLE_texture_2D_limited_npot"), glewGetExtension("GL_APPLE_texture_2D_limited_npot"));
}

#endif /* GL_APPLE_texture_2D_limited_npot */

#ifdef GL_APPLE_texture_format_BGRA8888

static void _glewInfo_GL_APPLE_texture_format_BGRA8888 (void)
{
  glewPrintExt("GL_APPLE_texture_format_BGRA8888", GLEW_APPLE_texture_format_BGRA8888, glewIsSupported("GL_APPLE_texture_format_BGRA8888"), glewGetExtension("GL_APPLE_texture_format_BGRA8888"));
}

#endif /* GL_APPLE_texture_format_BGRA8888 */

#ifdef GL_APPLE_texture_max_level

static void _glewInfo_GL_APPLE_texture_max_level (void)
{
  glewPrintExt("GL_APPLE_texture_max_level", GLEW_APPLE_texture_max_level, glewIsSupported("GL_APPLE_texture_max_level"), glewGetExtension("GL_APPLE_texture_max_level"));
}

#endif /* GL_APPLE_texture_max_level */

#ifdef GL_APPLE_texture_packed_float

static void _glewInfo_GL_APPLE_texture_packed_float (void)
{
  glewPrintExt("GL_APPLE_texture_packed_float", GLEW_APPLE_texture_packed_float, glewIsSupported("GL_APPLE_texture_packed_float"), glewGetExtension("GL_APPLE_texture_packed_float"));
}

#endif /* GL_APPLE_texture_packed_float */

#ifdef GL_APPLE_texture_range

static void _glewInfo_GL_APPLE_texture_range (void)
{
  glewPrintExt("GL_APPLE_texture_range", GLEW_APPLE_texture_range, glewIsSupported("GL_APPLE_texture_range"), glewGetExtension("GL_APPLE_texture_range"));

  glewInfoFunc("glGetTexParameterPointervAPPLE", glGetTexParameterPointervAPPLE == NULL);
  glewInfoFunc("glTextureRangeAPPLE", glTextureRangeAPPLE == NULL);
}

#endif /* GL_APPLE_texture_range */

#ifdef GL_APPLE_transform_hint

static void _glewInfo_GL_APPLE_transform_hint (void)
{
  glewPrintExt("GL_APPLE_transform_hint", GLEW_APPLE_transform_hint, glewIsSupported("GL_APPLE_transform_hint"), glewGetExtension("GL_APPLE_transform_hint"));
}

#endif /* GL_APPLE_transform_hint */

#ifdef GL_APPLE_vertex_array_object

static void _glewInfo_GL_APPLE_vertex_array_object (void)
{
  glewPrintExt("GL_APPLE_vertex_array_object", GLEW_APPLE_vertex_array_object, glewIsSupported("GL_APPLE_vertex_array_object"), glewGetExtension("GL_APPLE_vertex_array_object"));

  glewInfoFunc("glBindVertexArrayAPPLE", glBindVertexArrayAPPLE == NULL);
  glewInfoFunc("glDeleteVertexArraysAPPLE", glDeleteVertexArraysAPPLE == NULL);
  glewInfoFunc("glGenVertexArraysAPPLE", glGenVertexArraysAPPLE == NULL);
  glewInfoFunc("glIsVertexArrayAPPLE", glIsVertexArrayAPPLE == NULL);
}

#endif /* GL_APPLE_vertex_array_object */

#ifdef GL_APPLE_vertex_array_range

static void _glewInfo_GL_APPLE_vertex_array_range (void)
{
  glewPrintExt("GL_APPLE_vertex_array_range", GLEW_APPLE_vertex_array_range, glewIsSupported("GL_APPLE_vertex_array_range"), glewGetExtension("GL_APPLE_vertex_array_range"));

  glewInfoFunc("glFlushVertexArrayRangeAPPLE", glFlushVertexArrayRangeAPPLE == NULL);
  glewInfoFunc("glVertexArrayParameteriAPPLE", glVertexArrayParameteriAPPLE == NULL);
  glewInfoFunc("glVertexArrayRangeAPPLE", glVertexArrayRangeAPPLE == NULL);
}

#endif /* GL_APPLE_vertex_array_range */

#ifdef GL_APPLE_vertex_program_evaluators

static void _glewInfo_GL_APPLE_vertex_program_evaluators (void)
{
  glewPrintExt("GL_APPLE_vertex_program_evaluators", GLEW_APPLE_vertex_program_evaluators, glewIsSupported("GL_APPLE_vertex_program_evaluators"), glewGetExtension("GL_APPLE_vertex_program_evaluators"));

  glewInfoFunc("glDisableVertexAttribAPPLE", glDisableVertexAttribAPPLE == NULL);
  glewInfoFunc("glEnableVertexAttribAPPLE", glEnableVertexAttribAPPLE == NULL);
  glewInfoFunc("glIsVertexAttribEnabledAPPLE", glIsVertexAttribEnabledAPPLE == NULL);
  glewInfoFunc("glMapVertexAttrib1dAPPLE", glMapVertexAttrib1dAPPLE == NULL);
  glewInfoFunc("glMapVertexAttrib1fAPPLE", glMapVertexAttrib1fAPPLE == NULL);
  glewInfoFunc("glMapVertexAttrib2dAPPLE", glMapVertexAttrib2dAPPLE == NULL);
  glewInfoFunc("glMapVertexAttrib2fAPPLE", glMapVertexAttrib2fAPPLE == NULL);
}

#endif /* GL_APPLE_vertex_program_evaluators */

#ifdef GL_APPLE_ycbcr_422

static void _glewInfo_GL_APPLE_ycbcr_422 (void)
{
  glewPrintExt("GL_APPLE_ycbcr_422", GLEW_APPLE_ycbcr_422, glewIsSupported("GL_APPLE_ycbcr_422"), glewGetExtension("GL_APPLE_ycbcr_422"));
}

#endif /* GL_APPLE_ycbcr_422 */

#ifdef GL_ARB_ES2_compatibility

static void _glewInfo_GL_ARB_ES2_compatibility (void)
{
  glewPrintExt("GL_ARB_ES2_compatibility", GLEW_ARB_ES2_compatibility, glewIsSupported("GL_ARB_ES2_compatibility"), glewGetExtension("GL_ARB_ES2_compatibility"));

  glewInfoFunc("glClearDepthf", glClearDepthf == NULL);
  glewInfoFunc("glDepthRangef", glDepthRangef == NULL);
  glewInfoFunc("glGetShaderPrecisionFormat", glGetShaderPrecisionFormat == NULL);
  glewInfoFunc("glReleaseShaderCompiler", glReleaseShaderCompiler == NULL);
  glewInfoFunc("glShaderBinary", glShaderBinary == NULL);
}

#endif /* GL_ARB_ES2_compatibility */

#ifdef GL_ARB_ES3_1_compatibility

static void _glewInfo_GL_ARB_ES3_1_compatibility (void)
{
  glewPrintExt("GL_ARB_ES3_1_compatibility", GLEW_ARB_ES3_1_compatibility, glewIsSupported("GL_ARB_ES3_1_compatibility"), glewGetExtension("GL_ARB_ES3_1_compatibility"));

  glewInfoFunc("glMemoryBarrierByRegion", glMemoryBarrierByRegion == NULL);
}

#endif /* GL_ARB_ES3_1_compatibility */

#ifdef GL_ARB_ES3_2_compatibility

static void _glewInfo_GL_ARB_ES3_2_compatibility (void)
{
  glewPrintExt("GL_ARB_ES3_2_compatibility", GLEW_ARB_ES3_2_compatibility, glewIsSupported("GL_ARB_ES3_2_compatibility"), glewGetExtension("GL_ARB_ES3_2_compatibility"));

  glewInfoFunc("glPrimitiveBoundingBoxARB", glPrimitiveBoundingBoxARB == NULL);
}

#endif /* GL_ARB_ES3_2_compatibility */

#ifdef GL_ARB_ES3_compatibility

static void _glewInfo_GL_ARB_ES3_compatibility (void)
{
  glewPrintExt("GL_ARB_ES3_compatibility", GLEW_ARB_ES3_compatibility, glewIsSupported("GL_ARB_ES3_compatibility"), glewGetExtension("GL_ARB_ES3_compatibility"));
}

#endif /* GL_ARB_ES3_compatibility */

#ifdef GL_ARB_arrays_of_arrays

static void _glewInfo_GL_ARB_arrays_of_arrays (void)
{
  glewPrintExt("GL_ARB_arrays_of_arrays", GLEW_ARB_arrays_of_arrays, glewIsSupported("GL_ARB_arrays_of_arrays"), glewGetExtension("GL_ARB_arrays_of_arrays"));
}

#endif /* GL_ARB_arrays_of_arrays */

#ifdef GL_ARB_base_instance

static void _glewInfo_GL_ARB_base_instance (void)
{
  glewPrintExt("GL_ARB_base_instance", GLEW_ARB_base_instance, glewIsSupported("GL_ARB_base_instance"), glewGetExtension("GL_ARB_base_instance"));

  glewInfoFunc("glDrawArraysInstancedBaseInstance", glDrawArraysInstancedBaseInstance == NULL);
  glewInfoFunc("glDrawElementsInstancedBaseInstance", glDrawElementsInstancedBaseInstance == NULL);
  glewInfoFunc("glDrawElementsInstancedBaseVertexBaseInstance", glDrawElementsInstancedBaseVertexBaseInstance == NULL);
}

#endif /* GL_ARB_base_instance */

#ifdef GL_ARB_bindless_texture

static void _glewInfo_GL_ARB_bindless_texture (void)
{
  glewPrintExt("GL_ARB_bindless_texture", GLEW_ARB_bindless_texture, glewIsSupported("GL_ARB_bindless_texture"), glewGetExtension("GL_ARB_bindless_texture"));

  glewInfoFunc("glGetImageHandleARB", glGetImageHandleARB == NULL);
  glewInfoFunc("glGetTextureHandleARB", glGetTextureHandleARB == NULL);
  glewInfoFunc("glGetTextureSamplerHandleARB", glGetTextureSamplerHandleARB == NULL);
  glewInfoFunc("glGetVertexAttribLui64vARB", glGetVertexAttribLui64vARB == NULL);
  glewInfoFunc("glIsImageHandleResidentARB", glIsImageHandleResidentARB == NULL);
  glewInfoFunc("glIsTextureHandleResidentARB", glIsTextureHandleResidentARB == NULL);
  glewInfoFunc("glMakeImageHandleNonResidentARB", glMakeImageHandleNonResidentARB == NULL);
  glewInfoFunc("glMakeImageHandleResidentARB", glMakeImageHandleResidentARB == NULL);
  glewInfoFunc("glMakeTextureHandleNonResidentARB", glMakeTextureHandleNonResidentARB == NULL);
  glewInfoFunc("glMakeTextureHandleResidentARB", glMakeTextureHandleResidentARB == NULL);
  glewInfoFunc("glProgramUniformHandleui64ARB", glProgramUniformHandleui64ARB == NULL);
  glewInfoFunc("glProgramUniformHandleui64vARB", glProgramUniformHandleui64vARB == NULL);
  glewInfoFunc("glUniformHandleui64ARB", glUniformHandleui64ARB == NULL);
  glewInfoFunc("glUniformHandleui64vARB", glUniformHandleui64vARB == NULL);
  glewInfoFunc("glVertexAttribL1ui64ARB", glVertexAttribL1ui64ARB == NULL);
  glewInfoFunc("glVertexAttribL1ui64vARB", glVertexAttribL1ui64vARB == NULL);
}

#endif /* GL_ARB_bindless_texture */

#ifdef GL_ARB_blend_func_extended

static void _glewInfo_GL_ARB_blend_func_extended (void)
{
  glewPrintExt("GL_ARB_blend_func_extended", GLEW_ARB_blend_func_extended, glewIsSupported("GL_ARB_blend_func_extended"), glewGetExtension("GL_ARB_blend_func_extended"));

  glewInfoFunc("glBindFragDataLocationIndexed", glBindFragDataLocationIndexed == NULL);
  glewInfoFunc("glGetFragDataIndex", glGetFragDataIndex == NULL);
}

#endif /* GL_ARB_blend_func_extended */

#ifdef GL_ARB_buffer_storage

static void _glewInfo_GL_ARB_buffer_storage (void)
{
  glewPrintExt("GL_ARB_buffer_storage", GLEW_ARB_buffer_storage, glewIsSupported("GL_ARB_buffer_storage"), glewGetExtension("GL_ARB_buffer_storage"));

  glewInfoFunc("glBufferStorage", glBufferStorage == NULL);
}

#endif /* GL_ARB_buffer_storage */

#ifdef GL_ARB_cl_event

static void _glewInfo_GL_ARB_cl_event (void)
{
  glewPrintExt("GL_ARB_cl_event", GLEW_ARB_cl_event, glewIsSupported("GL_ARB_cl_event"), glewGetExtension("GL_ARB_cl_event"));

  glewInfoFunc("glCreateSyncFromCLeventARB", glCreateSyncFromCLeventARB == NULL);
}

#endif /* GL_ARB_cl_event */

#ifdef GL_ARB_clear_buffer_object

static void _glewInfo_GL_ARB_clear_buffer_object (void)
{
  glewPrintExt("GL_ARB_clear_buffer_object", GLEW_ARB_clear_buffer_object, glewIsSupported("GL_ARB_clear_buffer_object"), glewGetExtension("GL_ARB_clear_buffer_object"));

  glewInfoFunc("glClearBufferData", glClearBufferData == NULL);
  glewInfoFunc("glClearBufferSubData", glClearBufferSubData == NULL);
  glewInfoFunc("glClearNamedBufferDataEXT", glClearNamedBufferDataEXT == NULL);
  glewInfoFunc("glClearNamedBufferSubDataEXT", glClearNamedBufferSubDataEXT == NULL);
}

#endif /* GL_ARB_clear_buffer_object */

#ifdef GL_ARB_clear_texture

static void _glewInfo_GL_ARB_clear_texture (void)
{
  glewPrintExt("GL_ARB_clear_texture", GLEW_ARB_clear_texture, glewIsSupported("GL_ARB_clear_texture"), glewGetExtension("GL_ARB_clear_texture"));

  glewInfoFunc("glClearTexImage", glClearTexImage == NULL);
  glewInfoFunc("glClearTexSubImage", glClearTexSubImage == NULL);
}

#endif /* GL_ARB_clear_texture */

#ifdef GL_ARB_clip_control

static void _glewInfo_GL_ARB_clip_control (void)
{
  glewPrintExt("GL_ARB_clip_control", GLEW_ARB_clip_control, glewIsSupported("GL_ARB_clip_control"), glewGetExtension("GL_ARB_clip_control"));

  glewInfoFunc("glClipControl", glClipControl == NULL);
}

#endif /* GL_ARB_clip_control */

#ifdef GL_ARB_color_buffer_float

static void _glewInfo_GL_ARB_color_buffer_float (void)
{
  glewPrintExt("GL_ARB_color_buffer_float", GLEW_ARB_color_buffer_float, glewIsSupported("GL_ARB_color_buffer_float"), glewGetExtension("GL_ARB_color_buffer_float"));

  glewInfoFunc("glClampColorARB", glClampColorARB == NULL);
}

#endif /* GL_ARB_color_buffer_float */

#ifdef GL_ARB_compatibility

static void _glewInfo_GL_ARB_compatibility (void)
{
  glewPrintExt("GL_ARB_compatibility", GLEW_ARB_compatibility, glewIsSupported("GL_ARB_compatibility"), glewGetExtension("GL_ARB_compatibility"));
}

#endif /* GL_ARB_compatibility */

#ifdef GL_ARB_compressed_texture_pixel_storage

static void _glewInfo_GL_ARB_compressed_texture_pixel_storage (void)
{
  glewPrintExt("GL_ARB_compressed_texture_pixel_storage", GLEW_ARB_compressed_texture_pixel_storage, glewIsSupported("GL_ARB_compressed_texture_pixel_storage"), glewGetExtension("GL_ARB_compressed_texture_pixel_storage"));
}

#endif /* GL_ARB_compressed_texture_pixel_storage */

#ifdef GL_ARB_compute_shader

static void _glewInfo_GL_ARB_compute_shader (void)
{
  glewPrintExt("GL_ARB_compute_shader", GLEW_ARB_compute_shader, glewIsSupported("GL_ARB_compute_shader"), glewGetExtension("GL_ARB_compute_shader"));

  glewInfoFunc("glDispatchCompute", glDispatchCompute == NULL);
  glewInfoFunc("glDispatchComputeIndirect", glDispatchComputeIndirect == NULL);
}

#endif /* GL_ARB_compute_shader */

#ifdef GL_ARB_compute_variable_group_size

static void _glewInfo_GL_ARB_compute_variable_group_size (void)
{
  glewPrintExt("GL_ARB_compute_variable_group_size", GLEW_ARB_compute_variable_group_size, glewIsSupported("GL_ARB_compute_variable_group_size"), glewGetExtension("GL_ARB_compute_variable_group_size"));

  glewInfoFunc("glDispatchComputeGroupSizeARB", glDispatchComputeGroupSizeARB == NULL);
}

#endif /* GL_ARB_compute_variable_group_size */

#ifdef GL_ARB_conditional_render_inverted

static void _glewInfo_GL_ARB_conditional_render_inverted (void)
{
  glewPrintExt("GL_ARB_conditional_render_inverted", GLEW_ARB_conditional_render_inverted, glewIsSupported("GL_ARB_conditional_render_inverted"), glewGetExtension("GL_ARB_conditional_render_inverted"));
}

#endif /* GL_ARB_conditional_render_inverted */

#ifdef GL_ARB_conservative_depth

static void _glewInfo_GL_ARB_conservative_depth (void)
{
  glewPrintExt("GL_ARB_conservative_depth", GLEW_ARB_conservative_depth, glewIsSupported("GL_ARB_conservative_depth"), glewGetExtension("GL_ARB_conservative_depth"));
}

#endif /* GL_ARB_conservative_depth */

#ifdef GL_ARB_copy_buffer

static void _glewInfo_GL_ARB_copy_buffer (void)
{
  glewPrintExt("GL_ARB_copy_buffer", GLEW_ARB_copy_buffer, glewIsSupported("GL_ARB_copy_buffer"), glewGetExtension("GL_ARB_copy_buffer"));

  glewInfoFunc("glCopyBufferSubData", glCopyBufferSubData == NULL);
}

#endif /* GL_ARB_copy_buffer */

#ifdef GL_ARB_copy_image

static void _glewInfo_GL_ARB_copy_image (void)
{
  glewPrintExt("GL_ARB_copy_image", GLEW_ARB_copy_image, glewIsSupported("GL_ARB_copy_image"), glewGetExtension("GL_ARB_copy_image"));

  glewInfoFunc("glCopyImageSubData", glCopyImageSubData == NULL);
}

#endif /* GL_ARB_copy_image */

#ifdef GL_ARB_cull_distance

static void _glewInfo_GL_ARB_cull_distance (void)
{
  glewPrintExt("GL_ARB_cull_distance", GLEW_ARB_cull_distance, glewIsSupported("GL_ARB_cull_distance"), glewGetExtension("GL_ARB_cull_distance"));
}

#endif /* GL_ARB_cull_distance */

#ifdef GL_ARB_debug_output

static void _glewInfo_GL_ARB_debug_output (void)
{
  glewPrintExt("GL_ARB_debug_output", GLEW_ARB_debug_output, glewIsSupported("GL_ARB_debug_output"), glewGetExtension("GL_ARB_debug_output"));

  glewInfoFunc("glDebugMessageCallbackARB", glDebugMessageCallbackARB == NULL);
  glewInfoFunc("glDebugMessageControlARB", glDebugMessageControlARB == NULL);
  glewInfoFunc("glDebugMessageInsertARB", glDebugMessageInsertARB == NULL);
  glewInfoFunc("glGetDebugMessageLogARB", glGetDebugMessageLogARB == NULL);
}

#endif /* GL_ARB_debug_output */

#ifdef GL_ARB_depth_buffer_float

static void _glewInfo_GL_ARB_depth_buffer_float (void)
{
  glewPrintExt("GL_ARB_depth_buffer_float", GLEW_ARB_depth_buffer_float, glewIsSupported("GL_ARB_depth_buffer_float"), glewGetExtension("GL_ARB_depth_buffer_float"));
}

#endif /* GL_ARB_depth_buffer_float */

#ifdef GL_ARB_depth_clamp

static void _glewInfo_GL_ARB_depth_clamp (void)
{
  glewPrintExt("GL_ARB_depth_clamp", GLEW_ARB_depth_clamp, glewIsSupported("GL_ARB_depth_clamp"), glewGetExtension("GL_ARB_depth_clamp"));
}

#endif /* GL_ARB_depth_clamp */

#ifdef GL_ARB_depth_texture

static void _glewInfo_GL_ARB_depth_texture (void)
{
  glewPrintExt("GL_ARB_depth_texture", GLEW_ARB_depth_texture, glewIsSupported("GL_ARB_depth_texture"), glewGetExtension("GL_ARB_depth_texture"));
}

#endif /* GL_ARB_depth_texture */

#ifdef GL_ARB_derivative_control

static void _glewInfo_GL_ARB_derivative_control (void)
{
  glewPrintExt("GL_ARB_derivative_control", GLEW_ARB_derivative_control, glewIsSupported("GL_ARB_derivative_control"), glewGetExtension("GL_ARB_derivative_control"));
}

#endif /* GL_ARB_derivative_control */

#ifdef GL_ARB_direct_state_access

static void _glewInfo_GL_ARB_direct_state_access (void)
{
  glewPrintExt("GL_ARB_direct_state_access", GLEW_ARB_direct_state_access, glewIsSupported("GL_ARB_direct_state_access"), glewGetExtension("GL_ARB_direct_state_access"));

  glewInfoFunc("glBindTextureUnit", glBindTextureUnit == NULL);
  glewInfoFunc("glBlitNamedFramebuffer", glBlitNamedFramebuffer == NULL);
  glewInfoFunc("glCheckNamedFramebufferStatus", glCheckNamedFramebufferStatus == NULL);
  glewInfoFunc("glClearNamedBufferData", glClearNamedBufferData == NULL);
  glewInfoFunc("glClearNamedBufferSubData", glClearNamedBufferSubData == NULL);
  glewInfoFunc("glClearNamedFramebufferfi", glClearNamedFramebufferfi == NULL);
  glewInfoFunc("glClearNamedFramebufferfv", glClearNamedFramebufferfv == NULL);
  glewInfoFunc("glClearNamedFramebufferiv", glClearNamedFramebufferiv == NULL);
  glewInfoFunc("glClearNamedFramebufferuiv", glClearNamedFramebufferuiv == NULL);
  glewInfoFunc("glCompressedTextureSubImage1D", glCompressedTextureSubImage1D == NULL);
  glewInfoFunc("glCompressedTextureSubImage2D", glCompressedTextureSubImage2D == NULL);
  glewInfoFunc("glCompressedTextureSubImage3D", glCompressedTextureSubImage3D == NULL);
  glewInfoFunc("glCopyNamedBufferSubData", glCopyNamedBufferSubData == NULL);
  glewInfoFunc("glCopyTextureSubImage1D", glCopyTextureSubImage1D == NULL);
  glewInfoFunc("glCopyTextureSubImage2D", glCopyTextureSubImage2D == NULL);
  glewInfoFunc("glCopyTextureSubImage3D", glCopyTextureSubImage3D == NULL);
  glewInfoFunc("glCreateBuffers", glCreateBuffers == NULL);
  glewInfoFunc("glCreateFramebuffers", glCreateFramebuffers == NULL);
  glewInfoFunc("glCreateProgramPipelines", glCreateProgramPipelines == NULL);
  glewInfoFunc("glCreateQueries", glCreateQueries == NULL);
  glewInfoFunc("glCreateRenderbuffers", glCreateRenderbuffers == NULL);
  glewInfoFunc("glCreateSamplers", glCreateSamplers == NULL);
  glewInfoFunc("glCreateTextures", glCreateTextures == NULL);
  glewInfoFunc("glCreateTransformFeedbacks", glCreateTransformFeedbacks == NULL);
  glewInfoFunc("glCreateVertexArrays", glCreateVertexArrays == NULL);
  glewInfoFunc("glDisableVertexArrayAttrib", glDisableVertexArrayAttrib == NULL);
  glewInfoFunc("glEnableVertexArrayAttrib", glEnableVertexArrayAttrib == NULL);
  glewInfoFunc("glFlushMappedNamedBufferRange", glFlushMappedNamedBufferRange == NULL);
  glewInfoFunc("glGenerateTextureMipmap", glGenerateTextureMipmap == NULL);
  glewInfoFunc("glGetCompressedTextureImage", glGetCompressedTextureImage == NULL);
  glewInfoFunc("glGetNamedBufferParameteri64v", glGetNamedBufferParameteri64v == NULL);
  glewInfoFunc("glGetNamedBufferParameteriv", glGetNamedBufferParameteriv == NULL);
  glewInfoFunc("glGetNamedBufferPointerv", glGetNamedBufferPointerv == NULL);
  glewInfoFunc("glGetNamedBufferSubData", glGetNamedBufferSubData == NULL);
  glewInfoFunc("glGetNamedFramebufferAttachmentParameteriv", glGetNamedFramebufferAttachmentParameteriv == NULL);
  glewInfoFunc("glGetNamedFramebufferParameteriv", glGetNamedFramebufferParameteriv == NULL);
  glewInfoFunc("glGetNamedRenderbufferParameteriv", glGetNamedRenderbufferParameteriv == NULL);
  glewInfoFunc("glGetQueryBufferObjecti64v", glGetQueryBufferObjecti64v == NULL);
  glewInfoFunc("glGetQueryBufferObjectiv", glGetQueryBufferObjectiv == NULL);
  glewInfoFunc("glGetQueryBufferObjectui64v", glGetQueryBufferObjectui64v == NULL);
  glewInfoFunc("glGetQueryBufferObjectuiv", glGetQueryBufferObjectuiv == NULL);
  glewInfoFunc("glGetTextureImage", glGetTextureImage == NULL);
  glewInfoFunc("glGetTextureLevelParameterfv", glGetTextureLevelParameterfv == NULL);
  glewInfoFunc("glGetTextureLevelParameteriv", glGetTextureLevelParameteriv == NULL);
  glewInfoFunc("glGetTextureParameterIiv", glGetTextureParameterIiv == NULL);
  glewInfoFunc("glGetTextureParameterIuiv", glGetTextureParameterIuiv == NULL);
  glewInfoFunc("glGetTextureParameterfv", glGetTextureParameterfv == NULL);
  glewInfoFunc("glGetTextureParameteriv", glGetTextureParameteriv == NULL);
  glewInfoFunc("glGetTransformFeedbacki64_v", glGetTransformFeedbacki64_v == NULL);
  glewInfoFunc("glGetTransformFeedbacki_v", glGetTransformFeedbacki_v == NULL);
  glewInfoFunc("glGetTransformFeedbackiv", glGetTransformFeedbackiv == NULL);
  glewInfoFunc("glGetVertexArrayIndexed64iv", glGetVertexArrayIndexed64iv == NULL);
  glewInfoFunc("glGetVertexArrayIndexediv", glGetVertexArrayIndexediv == NULL);
  glewInfoFunc("glGetVertexArrayiv", glGetVertexArrayiv == NULL);
  glewInfoFunc("glInvalidateNamedFramebufferData", glInvalidateNamedFramebufferData == NULL);
  glewInfoFunc("glInvalidateNamedFramebufferSubData", glInvalidateNamedFramebufferSubData == NULL);
  glewInfoFunc("glMapNamedBuffer", glMapNamedBuffer == NULL);
  glewInfoFunc("glMapNamedBufferRange", glMapNamedBufferRange == NULL);
  glewInfoFunc("glNamedBufferData", glNamedBufferData == NULL);
  glewInfoFunc("glNamedBufferStorage", glNamedBufferStorage == NULL);
  glewInfoFunc("glNamedBufferSubData", glNamedBufferSubData == NULL);
  glewInfoFunc("glNamedFramebufferDrawBuffer", glNamedFramebufferDrawBuffer == NULL);
  glewInfoFunc("glNamedFramebufferDrawBuffers", glNamedFramebufferDrawBuffers == NULL);
  glewInfoFunc("glNamedFramebufferParameteri", glNamedFramebufferParameteri == NULL);
  glewInfoFunc("glNamedFramebufferReadBuffer", glNamedFramebufferReadBuffer == NULL);
  glewInfoFunc("glNamedFramebufferRenderbuffer", glNamedFramebufferRenderbuffer == NULL);
  glewInfoFunc("glNamedFramebufferTexture", glNamedFramebufferTexture == NULL);
  glewInfoFunc("glNamedFramebufferTextureLayer", glNamedFramebufferTextureLayer == NULL);
  glewInfoFunc("glNamedRenderbufferStorage", glNamedRenderbufferStorage == NULL);
  glewInfoFunc("glNamedRenderbufferStorageMultisample", glNamedRenderbufferStorageMultisample == NULL);
  glewInfoFunc("glTextureBuffer", glTextureBuffer == NULL);
  glewInfoFunc("glTextureBufferRange", glTextureBufferRange == NULL);
  glewInfoFunc("glTextureParameterIiv", glTextureParameterIiv == NULL);
  glewInfoFunc("glTextureParameterIuiv", glTextureParameterIuiv == NULL);
  glewInfoFunc("glTextureParameterf", glTextureParameterf == NULL);
  glewInfoFunc("glTextureParameterfv", glTextureParameterfv == NULL);
  glewInfoFunc("glTextureParameteri", glTextureParameteri == NULL);
  glewInfoFunc("glTextureParameteriv", glTextureParameteriv == NULL);
  glewInfoFunc("glTextureStorage1D", glTextureStorage1D == NULL);
  glewInfoFunc("glTextureStorage2D", glTextureStorage2D == NULL);
  glewInfoFunc("glTextureStorage2DMultisample", glTextureStorage2DMultisample == NULL);
  glewInfoFunc("glTextureStorage3D", glTextureStorage3D == NULL);
  glewInfoFunc("glTextureStorage3DMultisample", glTextureStorage3DMultisample == NULL);
  glewInfoFunc("glTextureSubImage1D", glTextureSubImage1D == NULL);
  glewInfoFunc("glTextureSubImage2D", glTextureSubImage2D == NULL);
  glewInfoFunc("glTextureSubImage3D", glTextureSubImage3D == NULL);
  glewInfoFunc("glTransformFeedbackBufferBase", glTransformFeedbackBufferBase == NULL);
  glewInfoFunc("glTransformFeedbackBufferRange", glTransformFeedbackBufferRange == NULL);
  glewInfoFunc("glUnmapNamedBuffer", glUnmapNamedBuffer == NULL);
  glewInfoFunc("glVertexArrayAttribBinding", glVertexArrayAttribBinding == NULL);
  glewInfoFunc("glVertexArrayAttribFormat", glVertexArrayAttribFormat == NULL);
  glewInfoFunc("glVertexArrayAttribIFormat", glVertexArrayAttribIFormat == NULL);
  glewInfoFunc("glVertexArrayAttribLFormat", glVertexArrayAttribLFormat == NULL);
  glewInfoFunc("glVertexArrayBindingDivisor", glVertexArrayBindingDivisor == NULL);
  glewInfoFunc("glVertexArrayElementBuffer", glVertexArrayElementBuffer == NULL);
  glewInfoFunc("glVertexArrayVertexBuffer", glVertexArrayVertexBuffer == NULL);
  glewInfoFunc("glVertexArrayVertexBuffers", glVertexArrayVertexBuffers == NULL);
}

#endif /* GL_ARB_direct_state_access */

#ifdef GL_ARB_draw_buffers

static void _glewInfo_GL_ARB_draw_buffers (void)
{
  glewPrintExt("GL_ARB_draw_buffers", GLEW_ARB_draw_buffers, glewIsSupported("GL_ARB_draw_buffers"), glewGetExtension("GL_ARB_draw_buffers"));

  glewInfoFunc("glDrawBuffersARB", glDrawBuffersARB == NULL);
}

#endif /* GL_ARB_draw_buffers */

#ifdef GL_ARB_draw_buffers_blend

static void _glewInfo_GL_ARB_draw_buffers_blend (void)
{
  glewPrintExt("GL_ARB_draw_buffers_blend", GLEW_ARB_draw_buffers_blend, glewIsSupported("GL_ARB_draw_buffers_blend"), glewGetExtension("GL_ARB_draw_buffers_blend"));

  glewInfoFunc("glBlendEquationSeparateiARB", glBlendEquationSeparateiARB == NULL);
  glewInfoFunc("glBlendEquationiARB", glBlendEquationiARB == NULL);
  glewInfoFunc("glBlendFuncSeparateiARB", glBlendFuncSeparateiARB == NULL);
  glewInfoFunc("glBlendFunciARB", glBlendFunciARB == NULL);
}

#endif /* GL_ARB_draw_buffers_blend */

#ifdef GL_ARB_draw_elements_base_vertex

static void _glewInfo_GL_ARB_draw_elements_base_vertex (void)
{
  glewPrintExt("GL_ARB_draw_elements_base_vertex", GLEW_ARB_draw_elements_base_vertex, glewIsSupported("GL_ARB_draw_elements_base_vertex"), glewGetExtension("GL_ARB_draw_elements_base_vertex"));

  glewInfoFunc("glDrawElementsBaseVertex", glDrawElementsBaseVertex == NULL);
  glewInfoFunc("glDrawElementsInstancedBaseVertex", glDrawElementsInstancedBaseVertex == NULL);
  glewInfoFunc("glDrawRangeElementsBaseVertex", glDrawRangeElementsBaseVertex == NULL);
  glewInfoFunc("glMultiDrawElementsBaseVertex", glMultiDrawElementsBaseVertex == NULL);
}

#endif /* GL_ARB_draw_elements_base_vertex */

#ifdef GL_ARB_draw_indirect

static void _glewInfo_GL_ARB_draw_indirect (void)
{
  glewPrintExt("GL_ARB_draw_indirect", GLEW_ARB_draw_indirect, glewIsSupported("GL_ARB_draw_indirect"), glewGetExtension("GL_ARB_draw_indirect"));

  glewInfoFunc("glDrawArraysIndirect", glDrawArraysIndirect == NULL);
  glewInfoFunc("glDrawElementsIndirect", glDrawElementsIndirect == NULL);
}

#endif /* GL_ARB_draw_indirect */

#ifdef GL_ARB_draw_instanced

static void _glewInfo_GL_ARB_draw_instanced (void)
{
  glewPrintExt("GL_ARB_draw_instanced", GLEW_ARB_draw_instanced, glewIsSupported("GL_ARB_draw_instanced"), glewGetExtension("GL_ARB_draw_instanced"));
}

#endif /* GL_ARB_draw_instanced */

#ifdef GL_ARB_enhanced_layouts

static void _glewInfo_GL_ARB_enhanced_layouts (void)
{
  glewPrintExt("GL_ARB_enhanced_layouts", GLEW_ARB_enhanced_layouts, glewIsSupported("GL_ARB_enhanced_layouts"), glewGetExtension("GL_ARB_enhanced_layouts"));
}

#endif /* GL_ARB_enhanced_layouts */

#ifdef GL_ARB_explicit_attrib_location

static void _glewInfo_GL_ARB_explicit_attrib_location (void)
{
  glewPrintExt("GL_ARB_explicit_attrib_location", GLEW_ARB_explicit_attrib_location, glewIsSupported("GL_ARB_explicit_attrib_location"), glewGetExtension("GL_ARB_explicit_attrib_location"));
}

#endif /* GL_ARB_explicit_attrib_location */

#ifdef GL_ARB_explicit_uniform_location

static void _glewInfo_GL_ARB_explicit_uniform_location (void)
{
  glewPrintExt("GL_ARB_explicit_uniform_location", GLEW_ARB_explicit_uniform_location, glewIsSupported("GL_ARB_explicit_uniform_location"), glewGetExtension("GL_ARB_explicit_uniform_location"));
}

#endif /* GL_ARB_explicit_uniform_location */

#ifdef GL_ARB_fragment_coord_conventions

static void _glewInfo_GL_ARB_fragment_coord_conventions (void)
{
  glewPrintExt("GL_ARB_fragment_coord_conventions", GLEW_ARB_fragment_coord_conventions, glewIsSupported("GL_ARB_fragment_coord_conventions"), glewGetExtension("GL_ARB_fragment_coord_conventions"));
}

#endif /* GL_ARB_fragment_coord_conventions */

#ifdef GL_ARB_fragment_layer_viewport

static void _glewInfo_GL_ARB_fragment_layer_viewport (void)
{
  glewPrintExt("GL_ARB_fragment_layer_viewport", GLEW_ARB_fragment_layer_viewport, glewIsSupported("GL_ARB_fragment_layer_viewport"), glewGetExtension("GL_ARB_fragment_layer_viewport"));
}

#endif /* GL_ARB_fragment_layer_viewport */

#ifdef GL_ARB_fragment_program

static void _glewInfo_GL_ARB_fragment_program (void)
{
  glewPrintExt("GL_ARB_fragment_program", GLEW_ARB_fragment_program, glewIsSupported("GL_ARB_fragment_program"), glewGetExtension("GL_ARB_fragment_program"));
}

#endif /* GL_ARB_fragment_program */

#ifdef GL_ARB_fragment_program_shadow

static void _glewInfo_GL_ARB_fragment_program_shadow (void)
{
  glewPrintExt("GL_ARB_fragment_program_shadow", GLEW_ARB_fragment_program_shadow, glewIsSupported("GL_ARB_fragment_program_shadow"), glewGetExtension("GL_ARB_fragment_program_shadow"));
}

#endif /* GL_ARB_fragment_program_shadow */

#ifdef GL_ARB_fragment_shader

static void _glewInfo_GL_ARB_fragment_shader (void)
{
  glewPrintExt("GL_ARB_fragment_shader", GLEW_ARB_fragment_shader, glewIsSupported("GL_ARB_fragment_shader"), glewGetExtension("GL_ARB_fragment_shader"));
}

#endif /* GL_ARB_fragment_shader */

#ifdef GL_ARB_fragment_shader_interlock

static void _glewInfo_GL_ARB_fragment_shader_interlock (void)
{
  glewPrintExt("GL_ARB_fragment_shader_interlock", GLEW_ARB_fragment_shader_interlock, glewIsSupported("GL_ARB_fragment_shader_interlock"), glewGetExtension("GL_ARB_fragment_shader_interlock"));
}

#endif /* GL_ARB_fragment_shader_interlock */

#ifdef GL_ARB_framebuffer_no_attachments

static void _glewInfo_GL_ARB_framebuffer_no_attachments (void)
{
  glewPrintExt("GL_ARB_framebuffer_no_attachments", GLEW_ARB_framebuffer_no_attachments, glewIsSupported("GL_ARB_framebuffer_no_attachments"), glewGetExtension("GL_ARB_framebuffer_no_attachments"));

  glewInfoFunc("glFramebufferParameteri", glFramebufferParameteri == NULL);
  glewInfoFunc("glGetFramebufferParameteriv", glGetFramebufferParameteriv == NULL);
  glewInfoFunc("glGetNamedFramebufferParameterivEXT", glGetNamedFramebufferParameterivEXT == NULL);
  glewInfoFunc("glNamedFramebufferParameteriEXT", glNamedFramebufferParameteriEXT == NULL);
}

#endif /* GL_ARB_framebuffer_no_attachments */

#ifdef GL_ARB_framebuffer_object

static void _glewInfo_GL_ARB_framebuffer_object (void)
{
  glewPrintExt("GL_ARB_framebuffer_object", GLEW_ARB_framebuffer_object, glewIsSupported("GL_ARB_framebuffer_object"), glewGetExtension("GL_ARB_framebuffer_object"));

  glewInfoFunc("glBindFramebuffer", glBindFramebuffer == NULL);
  glewInfoFunc("glBindRenderbuffer", glBindRenderbuffer == NULL);
  glewInfoFunc("glBlitFramebuffer", glBlitFramebuffer == NULL);
  glewInfoFunc("glCheckFramebufferStatus", glCheckFramebufferStatus == NULL);
  glewInfoFunc("glDeleteFramebuffers", glDeleteFramebuffers == NULL);
  glewInfoFunc("glDeleteRenderbuffers", glDeleteRenderbuffers == NULL);
  glewInfoFunc("glFramebufferRenderbuffer", glFramebufferRenderbuffer == NULL);
  glewInfoFunc("glFramebufferTexture1D", glFramebufferTexture1D == NULL);
  glewInfoFunc("glFramebufferTexture2D", glFramebufferTexture2D == NULL);
  glewInfoFunc("glFramebufferTexture3D", glFramebufferTexture3D == NULL);
  glewInfoFunc("glFramebufferTextureLayer", glFramebufferTextureLayer == NULL);
  glewInfoFunc("glGenFramebuffers", glGenFramebuffers == NULL);
  glewInfoFunc("glGenRenderbuffers", glGenRenderbuffers == NULL);
  glewInfoFunc("glGenerateMipmap", glGenerateMipmap == NULL);
  glewInfoFunc("glGetFramebufferAttachmentParameteriv", glGetFramebufferAttachmentParameteriv == NULL);
  glewInfoFunc("glGetRenderbufferParameteriv", glGetRenderbufferParameteriv == NULL);
  glewInfoFunc("glIsFramebuffer", glIsFramebuffer == NULL);
  glewInfoFunc("glIsRenderbuffer", glIsRenderbuffer == NULL);
  glewInfoFunc("glRenderbufferStorage", glRenderbufferStorage == NULL);
  glewInfoFunc("glRenderbufferStorageMultisample", glRenderbufferStorageMultisample == NULL);
}

#endif /* GL_ARB_framebuffer_object */

#ifdef GL_ARB_framebuffer_sRGB

static void _glewInfo_GL_ARB_framebuffer_sRGB (void)
{
  glewPrintExt("GL_ARB_framebuffer_sRGB", GLEW_ARB_framebuffer_sRGB, glewIsSupported("GL_ARB_framebuffer_sRGB"), glewGetExtension("GL_ARB_framebuffer_sRGB"));
}

#endif /* GL_ARB_framebuffer_sRGB */

#ifdef GL_ARB_geometry_shader4

static void _glewInfo_GL_ARB_geometry_shader4 (void)
{
  glewPrintExt("GL_ARB_geometry_shader4", GLEW_ARB_geometry_shader4, glewIsSupported("GL_ARB_geometry_shader4"), glewGetExtension("GL_ARB_geometry_shader4"));

  glewInfoFunc("glFramebufferTextureARB", glFramebufferTextureARB == NULL);
  glewInfoFunc("glFramebufferTextureFaceARB", glFramebufferTextureFaceARB == NULL);
  glewInfoFunc("glFramebufferTextureLayerARB", glFramebufferTextureLayerARB == NULL);
  glewInfoFunc("glProgramParameteriARB", glProgramParameteriARB == NULL);
}

#endif /* GL_ARB_geometry_shader4 */

#ifdef GL_ARB_get_program_binary

static void _glewInfo_GL_ARB_get_program_binary (void)
{
  glewPrintExt("GL_ARB_get_program_binary", GLEW_ARB_get_program_binary, glewIsSupported("GL_ARB_get_program_binary"), glewGetExtension("GL_ARB_get_program_binary"));

  glewInfoFunc("glGetProgramBinary", glGetProgramBinary == NULL);
  glewInfoFunc("glProgramBinary", glProgramBinary == NULL);
  glewInfoFunc("glProgramParameteri", glProgramParameteri == NULL);
}

#endif /* GL_ARB_get_program_binary */

#ifdef GL_ARB_get_texture_sub_image

static void _glewInfo_GL_ARB_get_texture_sub_image (void)
{
  glewPrintExt("GL_ARB_get_texture_sub_image", GLEW_ARB_get_texture_sub_image, glewIsSupported("GL_ARB_get_texture_sub_image"), glewGetExtension("GL_ARB_get_texture_sub_image"));

  glewInfoFunc("glGetCompressedTextureSubImage", glGetCompressedTextureSubImage == NULL);
  glewInfoFunc("glGetTextureSubImage", glGetTextureSubImage == NULL);
}

#endif /* GL_ARB_get_texture_sub_image */

#ifdef GL_ARB_gl_spirv

static void _glewInfo_GL_ARB_gl_spirv (void)
{
  glewPrintExt("GL_ARB_gl_spirv", GLEW_ARB_gl_spirv, glewIsSupported("GL_ARB_gl_spirv"), glewGetExtension("GL_ARB_gl_spirv"));

  glewInfoFunc("glSpecializeShaderARB", glSpecializeShaderARB == NULL);
}

#endif /* GL_ARB_gl_spirv */

#ifdef GL_ARB_gpu_shader5

static void _glewInfo_GL_ARB_gpu_shader5 (void)
{
  glewPrintExt("GL_ARB_gpu_shader5", GLEW_ARB_gpu_shader5, glewIsSupported("GL_ARB_gpu_shader5"), glewGetExtension("GL_ARB_gpu_shader5"));
}

#endif /* GL_ARB_gpu_shader5 */

#ifdef GL_ARB_gpu_shader_fp64

static void _glewInfo_GL_ARB_gpu_shader_fp64 (void)
{
  glewPrintExt("GL_ARB_gpu_shader_fp64", GLEW_ARB_gpu_shader_fp64, glewIsSupported("GL_ARB_gpu_shader_fp64"), glewGetExtension("GL_ARB_gpu_shader_fp64"));

  glewInfoFunc("glGetUniformdv", glGetUniformdv == NULL);
  glewInfoFunc("glUniform1d", glUniform1d == NULL);
  glewInfoFunc("glUniform1dv", glUniform1dv == NULL);
  glewInfoFunc("glUniform2d", glUniform2d == NULL);
  glewInfoFunc("glUniform2dv", glUniform2dv == NULL);
  glewInfoFunc("glUniform3d", glUniform3d == NULL);
  glewInfoFunc("glUniform3dv", glUniform3dv == NULL);
  glewInfoFunc("glUniform4d", glUniform4d == NULL);
  glewInfoFunc("glUniform4dv", glUniform4dv == NULL);
  glewInfoFunc("glUniformMatrix2dv", glUniformMatrix2dv == NULL);
  glewInfoFunc("glUniformMatrix2x3dv", glUniformMatrix2x3dv == NULL);
  glewInfoFunc("glUniformMatrix2x4dv", glUniformMatrix2x4dv == NULL);
  glewInfoFunc("glUniformMatrix3dv", glUniformMatrix3dv == NULL);
  glewInfoFunc("glUniformMatrix3x2dv", glUniformMatrix3x2dv == NULL);
  glewInfoFunc("glUniformMatrix3x4dv", glUniformMatrix3x4dv == NULL);
  glewInfoFunc("glUniformMatrix4dv", glUniformMatrix4dv == NULL);
  glewInfoFunc("glUniformMatrix4x2dv", glUniformMatrix4x2dv == NULL);
  glewInfoFunc("glUniformMatrix4x3dv", glUniformMatrix4x3dv == NULL);
}

#endif /* GL_ARB_gpu_shader_fp64 */

#ifdef GL_ARB_gpu_shader_int64

static void _glewInfo_GL_ARB_gpu_shader_int64 (void)
{
  glewPrintExt("GL_ARB_gpu_shader_int64", GLEW_ARB_gpu_shader_int64, glewIsSupported("GL_ARB_gpu_shader_int64"), glewGetExtension("GL_ARB_gpu_shader_int64"));

  glewInfoFunc("glGetUniformi64vARB", glGetUniformi64vARB == NULL);
  glewInfoFunc("glGetUniformui64vARB", glGetUniformui64vARB == NULL);
  glewInfoFunc("glGetnUniformi64vARB", glGetnUniformi64vARB == NULL);
  glewInfoFunc("glGetnUniformui64vARB", glGetnUniformui64vARB == NULL);
  glewInfoFunc("glProgramUniform1i64ARB", glProgramUniform1i64ARB == NULL);
  glewInfoFunc("glProgramUniform1i64vARB", glProgramUniform1i64vARB == NULL);
  glewInfoFunc("glProgramUniform1ui64ARB", glProgramUniform1ui64ARB == NULL);
  glewInfoFunc("glProgramUniform1ui64vARB", glProgramUniform1ui64vARB == NULL);
  glewInfoFunc("glProgramUniform2i64ARB", glProgramUniform2i64ARB == NULL);
  glewInfoFunc("glProgramUniform2i64vARB", glProgramUniform2i64vARB == NULL);
  glewInfoFunc("glProgramUniform2ui64ARB", glProgramUniform2ui64ARB == NULL);
  glewInfoFunc("glProgramUniform2ui64vARB", glProgramUniform2ui64vARB == NULL);
  glewInfoFunc("glProgramUniform3i64ARB", glProgramUniform3i64ARB == NULL);
  glewInfoFunc("glProgramUniform3i64vARB", glProgramUniform3i64vARB == NULL);
  glewInfoFunc("glProgramUniform3ui64ARB", glProgramUniform3ui64ARB == NULL);
  glewInfoFunc("glProgramUniform3ui64vARB", glProgramUniform3ui64vARB == NULL);
  glewInfoFunc("glProgramUniform4i64ARB", glProgramUniform4i64ARB == NULL);
  glewInfoFunc("glProgramUniform4i64vARB", glProgramUniform4i64vARB == NULL);
  glewInfoFunc("glProgramUniform4ui64ARB", glProgramUniform4ui64ARB == NULL);
  glewInfoFunc("glProgramUniform4ui64vARB", glProgramUniform4ui64vARB == NULL);
  glewInfoFunc("glUniform1i64ARB", glUniform1i64ARB == NULL);
  glewInfoFunc("glUniform1i64vARB", glUniform1i64vARB == NULL);
  glewInfoFunc("glUniform1ui64ARB", glUniform1ui64ARB == NULL);
  glewInfoFunc("glUniform1ui64vARB", glUniform1ui64vARB == NULL);
  glewInfoFunc("glUniform2i64ARB", glUniform2i64ARB == NULL);
  glewInfoFunc("glUniform2i64vARB", glUniform2i64vARB == NULL);
  glewInfoFunc("glUniform2ui64ARB", glUniform2ui64ARB == NULL);
  glewInfoFunc("glUniform2ui64vARB", glUniform2ui64vARB == NULL);
  glewInfoFunc("glUniform3i64ARB", glUniform3i64ARB == NULL);
  glewInfoFunc("glUniform3i64vARB", glUniform3i64vARB == NULL);
  glewInfoFunc("glUniform3ui64ARB", glUniform3ui64ARB == NULL);
  glewInfoFunc("glUniform3ui64vARB", glUniform3ui64vARB == NULL);
  glewInfoFunc("glUniform4i64ARB", glUniform4i64ARB == NULL);
  glewInfoFunc("glUniform4i64vARB", glUniform4i64vARB == NULL);
  glewInfoFunc("glUniform4ui64ARB", glUniform4ui64ARB == NULL);
  glewInfoFunc("glUniform4ui64vARB", glUniform4ui64vARB == NULL);
}

#endif /* GL_ARB_gpu_shader_int64 */

#ifdef GL_ARB_half_float_pixel

static void _glewInfo_GL_ARB_half_float_pixel (void)
{
  glewPrintExt("GL_ARB_half_float_pixel", GLEW_ARB_half_float_pixel, glewIsSupported("GL_ARB_half_float_pixel"), glewGetExtension("GL_ARB_half_float_pixel"));
}

#endif /* GL_ARB_half_float_pixel */

#ifdef GL_ARB_half_float_vertex

static void _glewInfo_GL_ARB_half_float_vertex (void)
{
  glewPrintExt("GL_ARB_half_float_vertex", GLEW_ARB_half_float_vertex, glewIsSupported("GL_ARB_half_float_vertex"), glewGetExtension("GL_ARB_half_float_vertex"));
}

#endif /* GL_ARB_half_float_vertex */

#ifdef GL_ARB_imaging

static void _glewInfo_GL_ARB_imaging (void)
{
  glewPrintExt("GL_ARB_imaging", GLEW_ARB_imaging, glewIsSupported("GL_ARB_imaging"), glewGetExtension("GL_ARB_imaging"));

  glewInfoFunc("glBlendEquation", glBlendEquation == NULL);
  glewInfoFunc("glColorSubTable", glColorSubTable == NULL);
  glewInfoFunc("glColorTable", glColorTable == NULL);
  glewInfoFunc("glColorTableParameterfv", glColorTableParameterfv == NULL);
  glewInfoFunc("glColorTableParameteriv", glColorTableParameteriv == NULL);
  glewInfoFunc("glConvolutionFilter1D", glConvolutionFilter1D == NULL);
  glewInfoFunc("glConvolutionFilter2D", glConvolutionFilter2D == NULL);
  glewInfoFunc("glConvolutionParameterf", glConvolutionParameterf == NULL);
  glewInfoFunc("glConvolutionParameterfv", glConvolutionParameterfv == NULL);
  glewInfoFunc("glConvolutionParameteri", glConvolutionParameteri == NULL);
  glewInfoFunc("glConvolutionParameteriv", glConvolutionParameteriv == NULL);
  glewInfoFunc("glCopyColorSubTable", glCopyColorSubTable == NULL);
  glewInfoFunc("glCopyColorTable", glCopyColorTable == NULL);
  glewInfoFunc("glCopyConvolutionFilter1D", glCopyConvolutionFilter1D == NULL);
  glewInfoFunc("glCopyConvolutionFilter2D", glCopyConvolutionFilter2D == NULL);
  glewInfoFunc("glGetColorTable", glGetColorTable == NULL);
  glewInfoFunc("glGetColorTableParameterfv", glGetColorTableParameterfv == NULL);
  glewInfoFunc("glGetColorTableParameteriv", glGetColorTableParameteriv == NULL);
  glewInfoFunc("glGetConvolutionFilter", glGetConvolutionFilter == NULL);
  glewInfoFunc("glGetConvolutionParameterfv", glGetConvolutionParameterfv == NULL);
  glewInfoFunc("glGetConvolutionParameteriv", glGetConvolutionParameteriv == NULL);
  glewInfoFunc("glGetHistogram", glGetHistogram == NULL);
  glewInfoFunc("glGetHistogramParameterfv", glGetHistogramParameterfv == NULL);
  glewInfoFunc("glGetHistogramParameteriv", glGetHistogramParameteriv == NULL);
  glewInfoFunc("glGetMinmax", glGetMinmax == NULL);
  glewInfoFunc("glGetMinmaxParameterfv", glGetMinmaxParameterfv == NULL);
  glewInfoFunc("glGetMinmaxParameteriv", glGetMinmaxParameteriv == NULL);
  glewInfoFunc("glGetSeparableFilter", glGetSeparableFilter == NULL);
  glewInfoFunc("glHistogram", glHistogram == NULL);
  glewInfoFunc("glMinmax", glMinmax == NULL);
  glewInfoFunc("glResetHistogram", glResetHistogram == NULL);
  glewInfoFunc("glResetMinmax", glResetMinmax == NULL);
  glewInfoFunc("glSeparableFilter2D", glSeparableFilter2D == NULL);
}

#endif /* GL_ARB_imaging */

#ifdef GL_ARB_indirect_parameters

static void _glewInfo_GL_ARB_indirect_parameters (void)
{
  glewPrintExt("GL_ARB_indirect_parameters", GLEW_ARB_indirect_parameters, glewIsSupported("GL_ARB_indirect_parameters"), glewGetExtension("GL_ARB_indirect_parameters"));

  glewInfoFunc("glMultiDrawArraysIndirectCountARB", glMultiDrawArraysIndirectCountARB == NULL);
  glewInfoFunc("glMultiDrawElementsIndirectCountARB", glMultiDrawElementsIndirectCountARB == NULL);
}

#endif /* GL_ARB_indirect_parameters */

#ifdef GL_ARB_instanced_arrays

static void _glewInfo_GL_ARB_instanced_arrays (void)
{
  glewPrintExt("GL_ARB_instanced_arrays", GLEW_ARB_instanced_arrays, glewIsSupported("GL_ARB_instanced_arrays"), glewGetExtension("GL_ARB_instanced_arrays"));

  glewInfoFunc("glDrawArraysInstancedARB", glDrawArraysInstancedARB == NULL);
  glewInfoFunc("glDrawElementsInstancedARB", glDrawElementsInstancedARB == NULL);
  glewInfoFunc("glVertexAttribDivisorARB", glVertexAttribDivisorARB == NULL);
}

#endif /* GL_ARB_instanced_arrays */

#ifdef GL_ARB_internalformat_query

static void _glewInfo_GL_ARB_internalformat_query (void)
{
  glewPrintExt("GL_ARB_internalformat_query", GLEW_ARB_internalformat_query, glewIsSupported("GL_ARB_internalformat_query"), glewGetExtension("GL_ARB_internalformat_query"));

  glewInfoFunc("glGetInternalformativ", glGetInternalformativ == NULL);
}

#endif /* GL_ARB_internalformat_query */

#ifdef GL_ARB_internalformat_query2

static void _glewInfo_GL_ARB_internalformat_query2 (void)
{
  glewPrintExt("GL_ARB_internalformat_query2", GLEW_ARB_internalformat_query2, glewIsSupported("GL_ARB_internalformat_query2"), glewGetExtension("GL_ARB_internalformat_query2"));

  glewInfoFunc("glGetInternalformati64v", glGetInternalformati64v == NULL);
}

#endif /* GL_ARB_internalformat_query2 */

#ifdef GL_ARB_invalidate_subdata

static void _glewInfo_GL_ARB_invalidate_subdata (void)
{
  glewPrintExt("GL_ARB_invalidate_subdata", GLEW_ARB_invalidate_subdata, glewIsSupported("GL_ARB_invalidate_subdata"), glewGetExtension("GL_ARB_invalidate_subdata"));

  glewInfoFunc("glInvalidateBufferData", glInvalidateBufferData == NULL);
  glewInfoFunc("glInvalidateBufferSubData", glInvalidateBufferSubData == NULL);
  glewInfoFunc("glInvalidateFramebuffer", glInvalidateFramebuffer == NULL);
  glewInfoFunc("glInvalidateSubFramebuffer", glInvalidateSubFramebuffer == NULL);
  glewInfoFunc("glInvalidateTexImage", glInvalidateTexImage == NULL);
  glewInfoFunc("glInvalidateTexSubImage", glInvalidateTexSubImage == NULL);
}

#endif /* GL_ARB_invalidate_subdata */

#ifdef GL_ARB_map_buffer_alignment

static void _glewInfo_GL_ARB_map_buffer_alignment (void)
{
  glewPrintExt("GL_ARB_map_buffer_alignment", GLEW_ARB_map_buffer_alignment, glewIsSupported("GL_ARB_map_buffer_alignment"), glewGetExtension("GL_ARB_map_buffer_alignment"));
}

#endif /* GL_ARB_map_buffer_alignment */

#ifdef GL_ARB_map_buffer_range

static void _glewInfo_GL_ARB_map_buffer_range (void)
{
  glewPrintExt("GL_ARB_map_buffer_range", GLEW_ARB_map_buffer_range, glewIsSupported("GL_ARB_map_buffer_range"), glewGetExtension("GL_ARB_map_buffer_range"));

  glewInfoFunc("glFlushMappedBufferRange", glFlushMappedBufferRange == NULL);
  glewInfoFunc("glMapBufferRange", glMapBufferRange == NULL);
}

#endif /* GL_ARB_map_buffer_range */

#ifdef GL_ARB_matrix_palette

static void _glewInfo_GL_ARB_matrix_palette (void)
{
  glewPrintExt("GL_ARB_matrix_palette", GLEW_ARB_matrix_palette, glewIsSupported("GL_ARB_matrix_palette"), glewGetExtension("GL_ARB_matrix_palette"));

  glewInfoFunc("glCurrentPaletteMatrixARB", glCurrentPaletteMatrixARB == NULL);
  glewInfoFunc("glMatrixIndexPointerARB", glMatrixIndexPointerARB == NULL);
  glewInfoFunc("glMatrixIndexubvARB", glMatrixIndexubvARB == NULL);
  glewInfoFunc("glMatrixIndexuivARB", glMatrixIndexuivARB == NULL);
  glewInfoFunc("glMatrixIndexusvARB", glMatrixIndexusvARB == NULL);
}

#endif /* GL_ARB_matrix_palette */

#ifdef GL_ARB_multi_bind

static void _glewInfo_GL_ARB_multi_bind (void)
{
  glewPrintExt("GL_ARB_multi_bind", GLEW_ARB_multi_bind, glewIsSupported("GL_ARB_multi_bind"), glewGetExtension("GL_ARB_multi_bind"));

  glewInfoFunc("glBindBuffersBase", glBindBuffersBase == NULL);
  glewInfoFunc("glBindBuffersRange", glBindBuffersRange == NULL);
  glewInfoFunc("glBindImageTextures", glBindImageTextures == NULL);
  glewInfoFunc("glBindSamplers", glBindSamplers == NULL);
  glewInfoFunc("glBindTextures", glBindTextures == NULL);
  glewInfoFunc("glBindVertexBuffers", glBindVertexBuffers == NULL);
}

#endif /* GL_ARB_multi_bind */

#ifdef GL_ARB_multi_draw_indirect

static void _glewInfo_GL_ARB_multi_draw_indirect (void)
{
  glewPrintExt("GL_ARB_multi_draw_indirect", GLEW_ARB_multi_draw_indirect, glewIsSupported("GL_ARB_multi_draw_indirect"), glewGetExtension("GL_ARB_multi_draw_indirect"));

  glewInfoFunc("glMultiDrawArraysIndirect", glMultiDrawArraysIndirect == NULL);
  glewInfoFunc("glMultiDrawElementsIndirect", glMultiDrawElementsIndirect == NULL);
}

#endif /* GL_ARB_multi_draw_indirect */

#ifdef GL_ARB_multisample

static void _glewInfo_GL_ARB_multisample (void)
{
  glewPrintExt("GL_ARB_multisample", GLEW_ARB_multisample, glewIsSupported("GL_ARB_multisample"), glewGetExtension("GL_ARB_multisample"));

  glewInfoFunc("glSampleCoverageARB", glSampleCoverageARB == NULL);
}

#endif /* GL_ARB_multisample */

#ifdef GL_ARB_multitexture

static void _glewInfo_GL_ARB_multitexture (void)
{
  glewPrintExt("GL_ARB_multitexture", GLEW_ARB_multitexture, glewIsSupported("GL_ARB_multitexture"), glewGetExtension("GL_ARB_multitexture"));

  glewInfoFunc("glActiveTextureARB", glActiveTextureARB == NULL);
  glewInfoFunc("glClientActiveTextureARB", glClientActiveTextureARB == NULL);
  glewInfoFunc("glMultiTexCoord1dARB", glMultiTexCoord1dARB == NULL);
  glewInfoFunc("glMultiTexCoord1dvARB", glMultiTexCoord1dvARB == NULL);
  glewInfoFunc("glMultiTexCoord1fARB", glMultiTexCoord1fARB == NULL);
  glewInfoFunc("glMultiTexCoord1fvARB", glMultiTexCoord1fvARB == NULL);
  glewInfoFunc("glMultiTexCoord1iARB", glMultiTexCoord1iARB == NULL);
  glewInfoFunc("glMultiTexCoord1ivARB", glMultiTexCoord1ivARB == NULL);
  glewInfoFunc("glMultiTexCoord1sARB", glMultiTexCoord1sARB == NULL);
  glewInfoFunc("glMultiTexCoord1svARB", glMultiTexCoord1svARB == NULL);
  glewInfoFunc("glMultiTexCoord2dARB", glMultiTexCoord2dARB == NULL);
  glewInfoFunc("glMultiTexCoord2dvARB", glMultiTexCoord2dvARB == NULL);
  glewInfoFunc("glMultiTexCoord2fARB", glMultiTexCoord2fARB == NULL);
  glewInfoFunc("glMultiTexCoord2fvARB", glMultiTexCoord2fvARB == NULL);
  glewInfoFunc("glMultiTexCoord2iARB", glMultiTexCoord2iARB == NULL);
  glewInfoFunc("glMultiTexCoord2ivARB", glMultiTexCoord2ivARB == NULL);
  glewInfoFunc("glMultiTexCoord2sARB", glMultiTexCoord2sARB == NULL);
  glewInfoFunc("glMultiTexCoord2svARB", glMultiTexCoord2svARB == NULL);
  glewInfoFunc("glMultiTexCoord3dARB", glMultiTexCoord3dARB == NULL);
  glewInfoFunc("glMultiTexCoord3dvARB", glMultiTexCoord3dvARB == NULL);
  glewInfoFunc("glMultiTexCoord3fARB", glMultiTexCoord3fARB == NULL);
  glewInfoFunc("glMultiTexCoord3fvARB", glMultiTexCoord3fvARB == NULL);
  glewInfoFunc("glMultiTexCoord3iARB", glMultiTexCoord3iARB == NULL);
  glewInfoFunc("glMultiTexCoord3ivARB", glMultiTexCoord3ivARB == NULL);
  glewInfoFunc("glMultiTexCoord3sARB", glMultiTexCoord3sARB == NULL);
  glewInfoFunc("glMultiTexCoord3svARB", glMultiTexCoord3svARB == NULL);
  glewInfoFunc("glMultiTexCoord4dARB", glMultiTexCoord4dARB == NULL);
  glewInfoFunc("glMultiTexCoord4dvARB", glMultiTexCoord4dvARB == NULL);
  glewInfoFunc("glMultiTexCoord4fARB", glMultiTexCoord4fARB == NULL);
  glewInfoFunc("glMultiTexCoord4fvARB", glMultiTexCoord4fvARB == NULL);
  glewInfoFunc("glMultiTexCoord4iARB", glMultiTexCoord4iARB == NULL);
  glewInfoFunc("glMultiTexCoord4ivARB", glMultiTexCoord4ivARB == NULL);
  glewInfoFunc("glMultiTexCoord4sARB", glMultiTexCoord4sARB == NULL);
  glewInfoFunc("glMultiTexCoord4svARB", glMultiTexCoord4svARB == NULL);
}

#endif /* GL_ARB_multitexture */

#ifdef GL_ARB_occlusion_query

static void _glewInfo_GL_ARB_occlusion_query (void)
{
  glewPrintExt("GL_ARB_occlusion_query", GLEW_ARB_occlusion_query, glewIsSupported("GL_ARB_occlusion_query"), glewGetExtension("GL_ARB_occlusion_query"));

  glewInfoFunc("glBeginQueryARB", glBeginQueryARB == NULL);
  glewInfoFunc("glDeleteQueriesARB", glDeleteQueriesARB == NULL);
  glewInfoFunc("glEndQueryARB", glEndQueryARB == NULL);
  glewInfoFunc("glGenQueriesARB", glGenQueriesARB == NULL);
  glewInfoFunc("glGetQueryObjectivARB", glGetQueryObjectivARB == NULL);
  glewInfoFunc("glGetQueryObjectuivARB", glGetQueryObjectuivARB == NULL);
  glewInfoFunc("glGetQueryivARB", glGetQueryivARB == NULL);
  glewInfoFunc("glIsQueryARB", glIsQueryARB == NULL);
}

#endif /* GL_ARB_occlusion_query */

#ifdef GL_ARB_occlusion_query2

static void _glewInfo_GL_ARB_occlusion_query2 (void)
{
  glewPrintExt("GL_ARB_occlusion_query2", GLEW_ARB_occlusion_query2, glewIsSupported("GL_ARB_occlusion_query2"), glewGetExtension("GL_ARB_occlusion_query2"));
}

#endif /* GL_ARB_occlusion_query2 */

#ifdef GL_ARB_parallel_shader_compile

static void _glewInfo_GL_ARB_parallel_shader_compile (void)
{
  glewPrintExt("GL_ARB_parallel_shader_compile", GLEW_ARB_parallel_shader_compile, glewIsSupported("GL_ARB_parallel_shader_compile"), glewGetExtension("GL_ARB_parallel_shader_compile"));

  glewInfoFunc("glMaxShaderCompilerThreadsARB", glMaxShaderCompilerThreadsARB == NULL);
}

#endif /* GL_ARB_parallel_shader_compile */

#ifdef GL_ARB_pipeline_statistics_query

static void _glewInfo_GL_ARB_pipeline_statistics_query (void)
{
  glewPrintExt("GL_ARB_pipeline_statistics_query", GLEW_ARB_pipeline_statistics_query, glewIsSupported("GL_ARB_pipeline_statistics_query"), glewGetExtension("GL_ARB_pipeline_statistics_query"));
}

#endif /* GL_ARB_pipeline_statistics_query */

#ifdef GL_ARB_pixel_buffer_object

static void _glewInfo_GL_ARB_pixel_buffer_object (void)
{
  glewPrintExt("GL_ARB_pixel_buffer_object", GLEW_ARB_pixel_buffer_object, glewIsSupported("GL_ARB_pixel_buffer_object"), glewGetExtension("GL_ARB_pixel_buffer_object"));
}

#endif /* GL_ARB_pixel_buffer_object */

#ifdef GL_ARB_point_parameters

static void _glewInfo_GL_ARB_point_parameters (void)
{
  glewPrintExt("GL_ARB_point_parameters", GLEW_ARB_point_parameters, glewIsSupported("GL_ARB_point_parameters"), glewGetExtension("GL_ARB_point_parameters"));

  glewInfoFunc("glPointParameterfARB", glPointParameterfARB == NULL);
  glewInfoFunc("glPointParameterfvARB", glPointParameterfvARB == NULL);
}

#endif /* GL_ARB_point_parameters */

#ifdef GL_ARB_point_sprite

static void _glewInfo_GL_ARB_point_sprite (void)
{
  glewPrintExt("GL_ARB_point_sprite", GLEW_ARB_point_sprite, glewIsSupported("GL_ARB_point_sprite"), glewGetExtension("GL_ARB_point_sprite"));
}

#endif /* GL_ARB_point_sprite */

#ifdef GL_ARB_polygon_offset_clamp

static void _glewInfo_GL_ARB_polygon_offset_clamp (void)
{
  glewPrintExt("GL_ARB_polygon_offset_clamp", GLEW_ARB_polygon_offset_clamp, glewIsSupported("GL_ARB_polygon_offset_clamp"), glewGetExtension("GL_ARB_polygon_offset_clamp"));

  glewInfoFunc("glPolygonOffsetClamp", glPolygonOffsetClamp == NULL);
}

#endif /* GL_ARB_polygon_offset_clamp */

#ifdef GL_ARB_post_depth_coverage

static void _glewInfo_GL_ARB_post_depth_coverage (void)
{
  glewPrintExt("GL_ARB_post_depth_coverage", GLEW_ARB_post_depth_coverage, glewIsSupported("GL_ARB_post_depth_coverage"), glewGetExtension("GL_ARB_post_depth_coverage"));
}

#endif /* GL_ARB_post_depth_coverage */

#ifdef GL_ARB_program_interface_query

static void _glewInfo_GL_ARB_program_interface_query (void)
{
  glewPrintExt("GL_ARB_program_interface_query", GLEW_ARB_program_interface_query, glewIsSupported("GL_ARB_program_interface_query"), glewGetExtension("GL_ARB_program_interface_query"));

  glewInfoFunc("glGetProgramInterfaceiv", glGetProgramInterfaceiv == NULL);
  glewInfoFunc("glGetProgramResourceIndex", glGetProgramResourceIndex == NULL);
  glewInfoFunc("glGetProgramResourceLocation", glGetProgramResourceLocation == NULL);
  glewInfoFunc("glGetProgramResourceLocationIndex", glGetProgramResourceLocationIndex == NULL);
  glewInfoFunc("glGetProgramResourceName", glGetProgramResourceName == NULL);
  glewInfoFunc("glGetProgramResourceiv", glGetProgramResourceiv == NULL);
}

#endif /* GL_ARB_program_interface_query */

#ifdef GL_ARB_provoking_vertex

static void _glewInfo_GL_ARB_provoking_vertex (void)
{
  glewPrintExt("GL_ARB_provoking_vertex", GLEW_ARB_provoking_vertex, glewIsSupported("GL_ARB_provoking_vertex"), glewGetExtension("GL_ARB_provoking_vertex"));

  glewInfoFunc("glProvokingVertex", glProvokingVertex == NULL);
}

#endif /* GL_ARB_provoking_vertex */

#ifdef GL_ARB_query_buffer_object

static void _glewInfo_GL_ARB_query_buffer_object (void)
{
  glewPrintExt("GL_ARB_query_buffer_object", GLEW_ARB_query_buffer_object, glewIsSupported("GL_ARB_query_buffer_object"), glewGetExtension("GL_ARB_query_buffer_object"));
}

#endif /* GL_ARB_query_buffer_object */

#ifdef GL_ARB_robust_buffer_access_behavior

static void _glewInfo_GL_ARB_robust_buffer_access_behavior (void)
{
  glewPrintExt("GL_ARB_robust_buffer_access_behavior", GLEW_ARB_robust_buffer_access_behavior, glewIsSupported("GL_ARB_robust_buffer_access_behavior"), glewGetExtension("GL_ARB_robust_buffer_access_behavior"));
}

#endif /* GL_ARB_robust_buffer_access_behavior */

#ifdef GL_ARB_robustness

static void _glewInfo_GL_ARB_robustness (void)
{
  glewPrintExt("GL_ARB_robustness", GLEW_ARB_robustness, glewIsSupported("GL_ARB_robustness"), glewGetExtension("GL_ARB_robustness"));

  glewInfoFunc("glGetGraphicsResetStatusARB", glGetGraphicsResetStatusARB == NULL);
  glewInfoFunc("glGetnColorTableARB", glGetnColorTableARB == NULL);
  glewInfoFunc("glGetnCompressedTexImageARB", glGetnCompressedTexImageARB == NULL);
  glewInfoFunc("glGetnConvolutionFilterARB", glGetnConvolutionFilterARB == NULL);
  glewInfoFunc("glGetnHistogramARB", glGetnHistogramARB == NULL);
  glewInfoFunc("glGetnMapdvARB", glGetnMapdvARB == NULL);
  glewInfoFunc("glGetnMapfvARB", glGetnMapfvARB == NULL);
  glewInfoFunc("glGetnMapivARB", glGetnMapivARB == NULL);
  glewInfoFunc("glGetnMinmaxARB", glGetnMinmaxARB == NULL);
  glewInfoFunc("glGetnPixelMapfvARB", glGetnPixelMapfvARB == NULL);
  glewInfoFunc("glGetnPixelMapuivARB", glGetnPixelMapuivARB == NULL);
  glewInfoFunc("glGetnPixelMapusvARB", glGetnPixelMapusvARB == NULL);
  glewInfoFunc("glGetnPolygonStippleARB", glGetnPolygonStippleARB == NULL);
  glewInfoFunc("glGetnSeparableFilterARB", glGetnSeparableFilterARB == NULL);
  glewInfoFunc("glGetnTexImageARB", glGetnTexImageARB == NULL);
  glewInfoFunc("glGetnUniformdvARB", glGetnUniformdvARB == NULL);
  glewInfoFunc("glGetnUniformfvARB", glGetnUniformfvARB == NULL);
  glewInfoFunc("glGetnUniformivARB", glGetnUniformivARB == NULL);
  glewInfoFunc("glGetnUniformuivARB", glGetnUniformuivARB == NULL);
  glewInfoFunc("glReadnPixelsARB", glReadnPixelsARB == NULL);
}

#endif /* GL_ARB_robustness */

#ifdef GL_ARB_robustness_application_isolation

static void _glewInfo_GL_ARB_robustness_application_isolation (void)
{
  glewPrintExt("GL_ARB_robustness_application_isolation", GLEW_ARB_robustness_application_isolation, glewIsSupported("GL_ARB_robustness_application_isolation"), glewGetExtension("GL_ARB_robustness_application_isolation"));
}

#endif /* GL_ARB_robustness_application_isolation */

#ifdef GL_ARB_robustness_share_group_isolation

static void _glewInfo_GL_ARB_robustness_share_group_isolation (void)
{
  glewPrintExt("GL_ARB_robustness_share_group_isolation", GLEW_ARB_robustness_share_group_isolation, glewIsSupported("GL_ARB_robustness_share_group_isolation"), glewGetExtension("GL_ARB_robustness_share_group_isolation"));
}

#endif /* GL_ARB_robustness_share_group_isolation */

#ifdef GL_ARB_sample_locations

static void _glewInfo_GL_ARB_sample_locations (void)
{
  glewPrintExt("GL_ARB_sample_locations", GLEW_ARB_sample_locations, glewIsSupported("GL_ARB_sample_locations"), glewGetExtension("GL_ARB_sample_locations"));

  glewInfoFunc("glFramebufferSampleLocationsfvARB", glFramebufferSampleLocationsfvARB == NULL);
  glewInfoFunc("glNamedFramebufferSampleLocationsfvARB", glNamedFramebufferSampleLocationsfvARB == NULL);
}

#endif /* GL_ARB_sample_locations */

#ifdef GL_ARB_sample_shading

static void _glewInfo_GL_ARB_sample_shading (void)
{
  glewPrintExt("GL_ARB_sample_shading", GLEW_ARB_sample_shading, glewIsSupported("GL_ARB_sample_shading"), glewGetExtension("GL_ARB_sample_shading"));

  glewInfoFunc("glMinSampleShadingARB", glMinSampleShadingARB == NULL);
}

#endif /* GL_ARB_sample_shading */

#ifdef GL_ARB_sampler_objects

static void _glewInfo_GL_ARB_sampler_objects (void)
{
  glewPrintExt("GL_ARB_sampler_objects", GLEW_ARB_sampler_objects, glewIsSupported("GL_ARB_sampler_objects"), glewGetExtension("GL_ARB_sampler_objects"));

  glewInfoFunc("glBindSampler", glBindSampler == NULL);
  glewInfoFunc("glDeleteSamplers", glDeleteSamplers == NULL);
  glewInfoFunc("glGenSamplers", glGenSamplers == NULL);
  glewInfoFunc("glGetSamplerParameterIiv", glGetSamplerParameterIiv == NULL);
  glewInfoFunc("glGetSamplerParameterIuiv", glGetSamplerParameterIuiv == NULL);
  glewInfoFunc("glGetSamplerParameterfv", glGetSamplerParameterfv == NULL);
  glewInfoFunc("glGetSamplerParameteriv", glGetSamplerParameteriv == NULL);
  glewInfoFunc("glIsSampler", glIsSampler == NULL);
  glewInfoFunc("glSamplerParameterIiv", glSamplerParameterIiv == NULL);
  glewInfoFunc("glSamplerParameterIuiv", glSamplerParameterIuiv == NULL);
  glewInfoFunc("glSamplerParameterf", glSamplerParameterf == NULL);
  glewInfoFunc("glSamplerParameterfv", glSamplerParameterfv == NULL);
  glewInfoFunc("glSamplerParameteri", glSamplerParameteri == NULL);
  glewInfoFunc("glSamplerParameteriv", glSamplerParameteriv == NULL);
}

#endif /* GL_ARB_sampler_objects */

#ifdef GL_ARB_seamless_cube_map

static void _glewInfo_GL_ARB_seamless_cube_map (void)
{
  glewPrintExt("GL_ARB_seamless_cube_map", GLEW_ARB_seamless_cube_map, glewIsSupported("GL_ARB_seamless_cube_map"), glewGetExtension("GL_ARB_seamless_cube_map"));
}

#endif /* GL_ARB_seamless_cube_map */

#ifdef GL_ARB_seamless_cubemap_per_texture

static void _glewInfo_GL_ARB_seamless_cubemap_per_texture (void)
{
  glewPrintExt("GL_ARB_seamless_cubemap_per_texture", GLEW_ARB_seamless_cubemap_per_texture, glewIsSupported("GL_ARB_seamless_cubemap_per_texture"), glewGetExtension("GL_ARB_seamless_cubemap_per_texture"));
}

#endif /* GL_ARB_seamless_cubemap_per_texture */

#ifdef GL_ARB_separate_shader_objects

static void _glewInfo_GL_ARB_separate_shader_objects (void)
{
  glewPrintExt("GL_ARB_separate_shader_objects", GLEW_ARB_separate_shader_objects, glewIsSupported("GL_ARB_separate_shader_objects"), glewGetExtension("GL_ARB_separate_shader_objects"));

  glewInfoFunc("glActiveShaderProgram", glActiveShaderProgram == NULL);
  glewInfoFunc("glBindProgramPipeline", glBindProgramPipeline == NULL);
  glewInfoFunc("glCreateShaderProgramv", glCreateShaderProgramv == NULL);
  glewInfoFunc("glDeleteProgramPipelines", glDeleteProgramPipelines == NULL);
  glewInfoFunc("glGenProgramPipelines", glGenProgramPipelines == NULL);
  glewInfoFunc("glGetProgramPipelineInfoLog", glGetProgramPipelineInfoLog == NULL);
  glewInfoFunc("glGetProgramPipelineiv", glGetProgramPipelineiv == NULL);
  glewInfoFunc("glIsProgramPipeline", glIsProgramPipeline == NULL);
  glewInfoFunc("glProgramUniform1d", glProgramUniform1d == NULL);
  glewInfoFunc("glProgramUniform1dv", glProgramUniform1dv == NULL);
  glewInfoFunc("glProgramUniform1f", glProgramUniform1f == NULL);
  glewInfoFunc("glProgramUniform1fv", glProgramUniform1fv == NULL);
  glewInfoFunc("glProgramUniform1i", glProgramUniform1i == NULL);
  glewInfoFunc("glProgramUniform1iv", glProgramUniform1iv == NULL);
  glewInfoFunc("glProgramUniform1ui", glProgramUniform1ui == NULL);
  glewInfoFunc("glProgramUniform1uiv", glProgramUniform1uiv == NULL);
  glewInfoFunc("glProgramUniform2d", glProgramUniform2d == NULL);
  glewInfoFunc("glProgramUniform2dv", glProgramUniform2dv == NULL);
  glewInfoFunc("glProgramUniform2f", glProgramUniform2f == NULL);
  glewInfoFunc("glProgramUniform2fv", glProgramUniform2fv == NULL);
  glewInfoFunc("glProgramUniform2i", glProgramUniform2i == NULL);
  glewInfoFunc("glProgramUniform2iv", glProgramUniform2iv == NULL);
  glewInfoFunc("glProgramUniform2ui", glProgramUniform2ui == NULL);
  glewInfoFunc("glProgramUniform2uiv", glProgramUniform2uiv == NULL);
  glewInfoFunc("glProgramUniform3d", glProgramUniform3d == NULL);
  glewInfoFunc("glProgramUniform3dv", glProgramUniform3dv == NULL);
  glewInfoFunc("glProgramUniform3f", glProgramUniform3f == NULL);
  glewInfoFunc("glProgramUniform3fv", glProgramUniform3fv == NULL);
  glewInfoFunc("glProgramUniform3i", glProgramUniform3i == NULL);
  glewInfoFunc("glProgramUniform3iv", glProgramUniform3iv == NULL);
  glewInfoFunc("glProgramUniform3ui", glProgramUniform3ui == NULL);
  glewInfoFunc("glProgramUniform3uiv", glProgramUniform3uiv == NULL);
  glewInfoFunc("glProgramUniform4d", glProgramUniform4d == NULL);
  glewInfoFunc("glProgramUniform4dv", glProgramUniform4dv == NULL);
  glewInfoFunc("glProgramUniform4f", glProgramUniform4f == NULL);
  glewInfoFunc("glProgramUniform4fv", glProgramUniform4fv == NULL);
  glewInfoFunc("glProgramUniform4i", glProgramUniform4i == NULL);
  glewInfoFunc("glProgramUniform4iv", glProgramUniform4iv == NULL);
  glewInfoFunc("glProgramUniform4ui", glProgramUniform4ui == NULL);
  glewInfoFunc("glProgramUniform4uiv", glProgramUniform4uiv == NULL);
  glewInfoFunc("glProgramUniformMatrix2dv", glProgramUniformMatrix2dv == NULL);
  glewInfoFunc("glProgramUniformMatrix2fv", glProgramUniformMatrix2fv == NULL);
  glewInfoFunc("glProgramUniformMatrix2x3dv", glProgramUniformMatrix2x3dv == NULL);
  glewInfoFunc("glProgramUniformMatrix2x3fv", glProgramUniformMatrix2x3fv == NULL);
  glewInfoFunc("glProgramUniformMatrix2x4dv", glProgramUniformMatrix2x4dv == NULL);
  glewInfoFunc("glProgramUniformMatrix2x4fv", glProgramUniformMatrix2x4fv == NULL);
  glewInfoFunc("glProgramUniformMatrix3dv", glProgramUniformMatrix3dv == NULL);
  glewInfoFunc("glProgramUniformMatrix3fv", glProgramUniformMatrix3fv == NULL);
  glewInfoFunc("glProgramUniformMatrix3x2dv", glProgramUniformMatrix3x2dv == NULL);
  glewInfoFunc("glProgramUniformMatrix3x2fv", glProgramUniformMatrix3x2fv == NULL);
  glewInfoFunc("glProgramUniformMatrix3x4dv", glProgramUniformMatrix3x4dv == NULL);
  glewInfoFunc("glProgramUniformMatrix3x4fv", glProgramUniformMatrix3x4fv == NULL);
  glewInfoFunc("glProgramUniformMatrix4dv", glProgramUniformMatrix4dv == NULL);
  glewInfoFunc("glProgramUniformMatrix4fv", glProgramUniformMatrix4fv == NULL);
  glewInfoFunc("glProgramUniformMatrix4x2dv", glProgramUniformMatrix4x2dv == NULL);
  glewInfoFunc("glProgramUniformMatrix4x2fv", glProgramUniformMatrix4x2fv == NULL);
  glewInfoFunc("glProgramUniformMatrix4x3dv", glProgramUniformMatrix4x3dv == NULL);
  glewInfoFunc("glProgramUniformMatrix4x3fv", glProgramUniformMatrix4x3fv == NULL);
  glewInfoFunc("glUseProgramStages", glUseProgramStages == NULL);
  glewInfoFunc("glValidateProgramPipeline", glValidateProgramPipeline == NULL);
}

#endif /* GL_ARB_separate_shader_objects */

#ifdef GL_ARB_shader_atomic_counter_ops

static void _glewInfo_GL_ARB_shader_atomic_counter_ops (void)
{
  glewPrintExt("GL_ARB_shader_atomic_counter_ops", GLEW_ARB_shader_atomic_counter_ops, glewIsSupported("GL_ARB_shader_atomic_counter_ops"), glewGetExtension("GL_ARB_shader_atomic_counter_ops"));
}

#endif /* GL_ARB_shader_atomic_counter_ops */

#ifdef GL_ARB_shader_atomic_counters

static void _glewInfo_GL_ARB_shader_atomic_counters (void)
{
  glewPrintExt("GL_ARB_shader_atomic_counters", GLEW_ARB_shader_atomic_counters, glewIsSupported("GL_ARB_shader_atomic_counters"), glewGetExtension("GL_ARB_shader_atomic_counters"));

  glewInfoFunc("glGetActiveAtomicCounterBufferiv", glGetActiveAtomicCounterBufferiv == NULL);
}

#endif /* GL_ARB_shader_atomic_counters */

#ifdef GL_ARB_shader_ballot

static void _glewInfo_GL_ARB_shader_ballot (void)
{
  glewPrintExt("GL_ARB_shader_ballot", GLEW_ARB_shader_ballot, glewIsSupported("GL_ARB_shader_ballot"), glewGetExtension("GL_ARB_shader_ballot"));
}

#endif /* GL_ARB_shader_ballot */

#ifdef GL_ARB_shader_bit_encoding

static void _glewInfo_GL_ARB_shader_bit_encoding (void)
{
  glewPrintExt("GL_ARB_shader_bit_encoding", GLEW_ARB_shader_bit_encoding, glewIsSupported("GL_ARB_shader_bit_encoding"), glewGetExtension("GL_ARB_shader_bit_encoding"));
}

#endif /* GL_ARB_shader_bit_encoding */

#ifdef GL_ARB_shader_clock

static void _glewInfo_GL_ARB_shader_clock (void)
{
  glewPrintExt("GL_ARB_shader_clock", GLEW_ARB_shader_clock, glewIsSupported("GL_ARB_shader_clock"), glewGetExtension("GL_ARB_shader_clock"));
}

#endif /* GL_ARB_shader_clock */

#ifdef GL_ARB_shader_draw_parameters

static void _glewInfo_GL_ARB_shader_draw_parameters (void)
{
  glewPrintExt("GL_ARB_shader_draw_parameters", GLEW_ARB_shader_draw_parameters, glewIsSupported("GL_ARB_shader_draw_parameters"), glewGetExtension("GL_ARB_shader_draw_parameters"));
}

#endif /* GL_ARB_shader_draw_parameters */

#ifdef GL_ARB_shader_group_vote

static void _glewInfo_GL_ARB_shader_group_vote (void)
{
  glewPrintExt("GL_ARB_shader_group_vote", GLEW_ARB_shader_group_vote, glewIsSupported("GL_ARB_shader_group_vote"), glewGetExtension("GL_ARB_shader_group_vote"));
}

#endif /* GL_ARB_shader_group_vote */

#ifdef GL_ARB_shader_image_load_store

static void _glewInfo_GL_ARB_shader_image_load_store (void)
{
  glewPrintExt("GL_ARB_shader_image_load_store", GLEW_ARB_shader_image_load_store, glewIsSupported("GL_ARB_shader_image_load_store"), glewGetExtension("GL_ARB_shader_image_load_store"));

  glewInfoFunc("glBindImageTexture", glBindImageTexture == NULL);
  glewInfoFunc("glMemoryBarrier", glMemoryBarrier == NULL);
}

#endif /* GL_ARB_shader_image_load_store */

#ifdef GL_ARB_shader_image_size

static void _glewInfo_GL_ARB_shader_image_size (void)
{
  glewPrintExt("GL_ARB_shader_image_size", GLEW_ARB_shader_image_size, glewIsSupported("GL_ARB_shader_image_size"), glewGetExtension("GL_ARB_shader_image_size"));
}

#endif /* GL_ARB_shader_image_size */

#ifdef GL_ARB_shader_objects

static void _glewInfo_GL_ARB_shader_objects (void)
{
  glewPrintExt("GL_ARB_shader_objects", GLEW_ARB_shader_objects, glewIsSupported("GL_ARB_shader_objects"), glewGetExtension("GL_ARB_shader_objects"));

  glewInfoFunc("glAttachObjectARB", glAttachObjectARB == NULL);
  glewInfoFunc("glCompileShaderARB", glCompileShaderARB == NULL);
  glewInfoFunc("glCreateProgramObjectARB", glCreateProgramObjectARB == NULL);
  glewInfoFunc("glCreateShaderObjectARB", glCreateShaderObjectARB == NULL);
  glewInfoFunc("glDeleteObjectARB", glDeleteObjectARB == NULL);
  glewInfoFunc("glDetachObjectARB", glDetachObjectARB == NULL);
  glewInfoFunc("glGetActiveUniformARB", glGetActiveUniformARB == NULL);
  glewInfoFunc("glGetAttachedObjectsARB", glGetAttachedObjectsARB == NULL);
  glewInfoFunc("glGetHandleARB", glGetHandleARB == NULL);
  glewInfoFunc("glGetInfoLogARB", glGetInfoLogARB == NULL);
  glewInfoFunc("glGetObjectParameterfvARB", glGetObjectParameterfvARB == NULL);
  glewInfoFunc("glGetObjectParameterivARB", glGetObjectParameterivARB == NULL);
  glewInfoFunc("glGetShaderSourceARB", glGetShaderSourceARB == NULL);
  glewInfoFunc("glGetUniformLocationARB", glGetUniformLocationARB == NULL);
  glewInfoFunc("glGetUniformfvARB", glGetUniformfvARB == NULL);
  glewInfoFunc("glGetUniformivARB", glGetUniformivARB == NULL);
  glewInfoFunc("glLinkProgramARB", glLinkProgramARB == NULL);
  glewInfoFunc("glShaderSourceARB", glShaderSourceARB == NULL);
  glewInfoFunc("glUniform1fARB", glUniform1fARB == NULL);
  glewInfoFunc("glUniform1fvARB", glUniform1fvARB == NULL);
  glewInfoFunc("glUniform1iARB", glUniform1iARB == NULL);
  glewInfoFunc("glUniform1ivARB", glUniform1ivARB == NULL);
  glewInfoFunc("glUniform2fARB", glUniform2fARB == NULL);
  glewInfoFunc("glUniform2fvARB", glUniform2fvARB == NULL);
  glewInfoFunc("glUniform2iARB", glUniform2iARB == NULL);
  glewInfoFunc("glUniform2ivARB", glUniform2ivARB == NULL);
  glewInfoFunc("glUniform3fARB", glUniform3fARB == NULL);
  glewInfoFunc("glUniform3fvARB", glUniform3fvARB == NULL);
  glewInfoFunc("glUniform3iARB", glUniform3iARB == NULL);
  glewInfoFunc("glUniform3ivARB", glUniform3ivARB == NULL);
  glewInfoFunc("glUniform4fARB", glUniform4fARB == NULL);
  glewInfoFunc("glUniform4fvARB", glUniform4fvARB == NULL);
  glewInfoFunc("glUniform4iARB", glUniform4iARB == NULL);
  glewInfoFunc("glUniform4ivARB", glUniform4ivARB == NULL);
  glewInfoFunc("glUniformMatrix2fvARB", glUniformMatrix2fvARB == NULL);
  glewInfoFunc("glUniformMatrix3fvARB", glUniformMatrix3fvARB == NULL);
  glewInfoFunc("glUniformMatrix4fvARB", glUniformMatrix4fvARB == NULL);
  glewInfoFunc("glUseProgramObjectARB", glUseProgramObjectARB == NULL);
  glewInfoFunc("glValidateProgramARB", glValidateProgramARB == NULL);
}

#endif /* GL_ARB_shader_objects */

#ifdef GL_ARB_shader_precision

static void _glewInfo_GL_ARB_shader_precision (void)
{
  glewPrintExt("GL_ARB_shader_precision", GLEW_ARB_shader_precision, glewIsSupported("GL_ARB_shader_precision"), glewGetExtension("GL_ARB_shader_precision"));
}

#endif /* GL_ARB_shader_precision */

#ifdef GL_ARB_shader_stencil_export

static void _glewInfo_GL_ARB_shader_stencil_export (void)
{
  glewPrintExt("GL_ARB_shader_stencil_export", GLEW_ARB_shader_stencil_export, glewIsSupported("GL_ARB_shader_stencil_export"), glewGetExtension("GL_ARB_shader_stencil_export"));
}

#endif /* GL_ARB_shader_stencil_export */

#ifdef GL_ARB_shader_storage_buffer_object

static void _glewInfo_GL_ARB_shader_storage_buffer_object (void)
{
  glewPrintExt("GL_ARB_shader_storage_buffer_object", GLEW_ARB_shader_storage_buffer_object, glewIsSupported("GL_ARB_shader_storage_buffer_object"), glewGetExtension("GL_ARB_shader_storage_buffer_object"));

  glewInfoFunc("glShaderStorageBlockBinding", glShaderStorageBlockBinding == NULL);
}

#endif /* GL_ARB_shader_storage_buffer_object */

#ifdef GL_ARB_shader_subroutine

static void _glewInfo_GL_ARB_shader_subroutine (void)
{
  glewPrintExt("GL_ARB_shader_subroutine", GLEW_ARB_shader_subroutine, glewIsSupported("GL_ARB_shader_subroutine"), glewGetExtension("GL_ARB_shader_subroutine"));

  glewInfoFunc("glGetActiveSubroutineName", glGetActiveSubroutineName == NULL);
  glewInfoFunc("glGetActiveSubroutineUniformName", glGetActiveSubroutineUniformName == NULL);
  glewInfoFunc("glGetActiveSubroutineUniformiv", glGetActiveSubroutineUniformiv == NULL);
  glewInfoFunc("glGetProgramStageiv", glGetProgramStageiv == NULL);
  glewInfoFunc("glGetSubroutineIndex", glGetSubroutineIndex == NULL);
  glewInfoFunc("glGetSubroutineUniformLocation", glGetSubroutineUniformLocation == NULL);
  glewInfoFunc("glGetUniformSubroutineuiv", glGetUniformSubroutineuiv == NULL);
  glewInfoFunc("glUniformSubroutinesuiv", glUniformSubroutinesuiv == NULL);
}

#endif /* GL_ARB_shader_subroutine */

#ifdef GL_ARB_shader_texture_image_samples

static void _glewInfo_GL_ARB_shader_texture_image_samples (void)
{
  glewPrintExt("GL_ARB_shader_texture_image_samples", GLEW_ARB_shader_texture_image_samples, glewIsSupported("GL_ARB_shader_texture_image_samples"), glewGetExtension("GL_ARB_shader_texture_image_samples"));
}

#endif /* GL_ARB_shader_texture_image_samples */

#ifdef GL_ARB_shader_texture_lod

static void _glewInfo_GL_ARB_shader_texture_lod (void)
{
  glewPrintExt("GL_ARB_shader_texture_lod", GLEW_ARB_shader_texture_lod, glewIsSupported("GL_ARB_shader_texture_lod"), glewGetExtension("GL_ARB_shader_texture_lod"));
}

#endif /* GL_ARB_shader_texture_lod */

#ifdef GL_ARB_shader_viewport_layer_array

static void _glewInfo_GL_ARB_shader_viewport_layer_array (void)
{
  glewPrintExt("GL_ARB_shader_viewport_layer_array", GLEW_ARB_shader_viewport_layer_array, glewIsSupported("GL_ARB_shader_viewport_layer_array"), glewGetExtension("GL_ARB_shader_viewport_layer_array"));
}

#endif /* GL_ARB_shader_viewport_layer_array */

#ifdef GL_ARB_shading_language_100

static void _glewInfo_GL_ARB_shading_language_100 (void)
{
  glewPrintExt("GL_ARB_shading_language_100", GLEW_ARB_shading_language_100, glewIsSupported("GL_ARB_shading_language_100"), glewGetExtension("GL_ARB_shading_language_100"));
}

#endif /* GL_ARB_shading_language_100 */

#ifdef GL_ARB_shading_language_420pack

static void _glewInfo_GL_ARB_shading_language_420pack (void)
{
  glewPrintExt("GL_ARB_shading_language_420pack", GLEW_ARB_shading_language_420pack, glewIsSupported("GL_ARB_shading_language_420pack"), glewGetExtension("GL_ARB_shading_language_420pack"));
}

#endif /* GL_ARB_shading_language_420pack */

#ifdef GL_ARB_shading_language_include

static void _glewInfo_GL_ARB_shading_language_include (void)
{
  glewPrintExt("GL_ARB_shading_language_include", GLEW_ARB_shading_language_include, glewIsSupported("GL_ARB_shading_language_include"), glewGetExtension("GL_ARB_shading_language_include"));

  glewInfoFunc("glCompileShaderIncludeARB", glCompileShaderIncludeARB == NULL);
  glewInfoFunc("glDeleteNamedStringARB", glDeleteNamedStringARB == NULL);
  glewInfoFunc("glGetNamedStringARB", glGetNamedStringARB == NULL);
  glewInfoFunc("glGetNamedStringivARB", glGetNamedStringivARB == NULL);
  glewInfoFunc("glIsNamedStringARB", glIsNamedStringARB == NULL);
  glewInfoFunc("glNamedStringARB", glNamedStringARB == NULL);
}

#endif /* GL_ARB_shading_language_include */

#ifdef GL_ARB_shading_language_packing

static void _glewInfo_GL_ARB_shading_language_packing (void)
{
  glewPrintExt("GL_ARB_shading_language_packing", GLEW_ARB_shading_language_packing, glewIsSupported("GL_ARB_shading_language_packing"), glewGetExtension("GL_ARB_shading_language_packing"));
}

#endif /* GL_ARB_shading_language_packing */

#ifdef GL_ARB_shadow

static void _glewInfo_GL_ARB_shadow (void)
{
  glewPrintExt("GL_ARB_shadow", GLEW_ARB_shadow, glewIsSupported("GL_ARB_shadow"), glewGetExtension("GL_ARB_shadow"));
}

#endif /* GL_ARB_shadow */

#ifdef GL_ARB_shadow_ambient

static void _glewInfo_GL_ARB_shadow_ambient (void)
{
  glewPrintExt("GL_ARB_shadow_ambient", GLEW_ARB_shadow_ambient, glewIsSupported("GL_ARB_shadow_ambient"), glewGetExtension("GL_ARB_shadow_ambient"));
}

#endif /* GL_ARB_shadow_ambient */

#ifdef GL_ARB_sparse_buffer

static void _glewInfo_GL_ARB_sparse_buffer (void)
{
  glewPrintExt("GL_ARB_sparse_buffer", GLEW_ARB_sparse_buffer, glewIsSupported("GL_ARB_sparse_buffer"), glewGetExtension("GL_ARB_sparse_buffer"));

  glewInfoFunc("glBufferPageCommitmentARB", glBufferPageCommitmentARB == NULL);
}

#endif /* GL_ARB_sparse_buffer */

#ifdef GL_ARB_sparse_texture

static void _glewInfo_GL_ARB_sparse_texture (void)
{
  glewPrintExt("GL_ARB_sparse_texture", GLEW_ARB_sparse_texture, glewIsSupported("GL_ARB_sparse_texture"), glewGetExtension("GL_ARB_sparse_texture"));

  glewInfoFunc("glTexPageCommitmentARB", glTexPageCommitmentARB == NULL);
}

#endif /* GL_ARB_sparse_texture */

#ifdef GL_ARB_sparse_texture2

static void _glewInfo_GL_ARB_sparse_texture2 (void)
{
  glewPrintExt("GL_ARB_sparse_texture2", GLEW_ARB_sparse_texture2, glewIsSupported("GL_ARB_sparse_texture2"), glewGetExtension("GL_ARB_sparse_texture2"));
}

#endif /* GL_ARB_sparse_texture2 */

#ifdef GL_ARB_sparse_texture_clamp

static void _glewInfo_GL_ARB_sparse_texture_clamp (void)
{
  glewPrintExt("GL_ARB_sparse_texture_clamp", GLEW_ARB_sparse_texture_clamp, glewIsSupported("GL_ARB_sparse_texture_clamp"), glewGetExtension("GL_ARB_sparse_texture_clamp"));
}

#endif /* GL_ARB_sparse_texture_clamp */

#ifdef GL_ARB_spirv_extensions

static void _glewInfo_GL_ARB_spirv_extensions (void)
{
  glewPrintExt("GL_ARB_spirv_extensions", GLEW_ARB_spirv_extensions, glewIsSupported("GL_ARB_spirv_extensions"), glewGetExtension("GL_ARB_spirv_extensions"));
}

#endif /* GL_ARB_spirv_extensions */

#ifdef GL_ARB_stencil_texturing

static void _glewInfo_GL_ARB_stencil_texturing (void)
{
  glewPrintExt("GL_ARB_stencil_texturing", GLEW_ARB_stencil_texturing, glewIsSupported("GL_ARB_stencil_texturing"), glewGetExtension("GL_ARB_stencil_texturing"));
}

#endif /* GL_ARB_stencil_texturing */

#ifdef GL_ARB_sync

static void _glewInfo_GL_ARB_sync (void)
{
  glewPrintExt("GL_ARB_sync", GLEW_ARB_sync, glewIsSupported("GL_ARB_sync"), glewGetExtension("GL_ARB_sync"));

  glewInfoFunc("glClientWaitSync", glClientWaitSync == NULL);
  glewInfoFunc("glDeleteSync", glDeleteSync == NULL);
  glewInfoFunc("glFenceSync", glFenceSync == NULL);
  glewInfoFunc("glGetInteger64v", glGetInteger64v == NULL);
  glewInfoFunc("glGetSynciv", glGetSynciv == NULL);
  glewInfoFunc("glIsSync", glIsSync == NULL);
  glewInfoFunc("glWaitSync", glWaitSync == NULL);
}

#endif /* GL_ARB_sync */

#ifdef GL_ARB_tessellation_shader

static void _glewInfo_GL_ARB_tessellation_shader (void)
{
  glewPrintExt("GL_ARB_tessellation_shader", GLEW_ARB_tessellation_shader, glewIsSupported("GL_ARB_tessellation_shader"), glewGetExtension("GL_ARB_tessellation_shader"));

  glewInfoFunc("glPatchParameterfv", glPatchParameterfv == NULL);
  glewInfoFunc("glPatchParameteri", glPatchParameteri == NULL);
}

#endif /* GL_ARB_tessellation_shader */

#ifdef GL_ARB_texture_barrier

static void _glewInfo_GL_ARB_texture_barrier (void)
{
  glewPrintExt("GL_ARB_texture_barrier", GLEW_ARB_texture_barrier, glewIsSupported("GL_ARB_texture_barrier"), glewGetExtension("GL_ARB_texture_barrier"));

  glewInfoFunc("glTextureBarrier", glTextureBarrier == NULL);
}

#endif /* GL_ARB_texture_barrier */

#ifdef GL_ARB_texture_border_clamp

static void _glewInfo_GL_ARB_texture_border_clamp (void)
{
  glewPrintExt("GL_ARB_texture_border_clamp", GLEW_ARB_texture_border_clamp, glewIsSupported("GL_ARB_texture_border_clamp"), glewGetExtension("GL_ARB_texture_border_clamp"));
}

#endif /* GL_ARB_texture_border_clamp */

#ifdef GL_ARB_texture_buffer_object

static void _glewInfo_GL_ARB_texture_buffer_object (void)
{
  glewPrintExt("GL_ARB_texture_buffer_object", GLEW_ARB_texture_buffer_object, glewIsSupported("GL_ARB_texture_buffer_object"), glewGetExtension("GL_ARB_texture_buffer_object"));

  glewInfoFunc("glTexBufferARB", glTexBufferARB == NULL);
}

#endif /* GL_ARB_texture_buffer_object */

#ifdef GL_ARB_texture_buffer_object_rgb32

static void _glewInfo_GL_ARB_texture_buffer_object_rgb32 (void)
{
  glewPrintExt("GL_ARB_texture_buffer_object_rgb32", GLEW_ARB_texture_buffer_object_rgb32, glewIsSupported("GL_ARB_texture_buffer_object_rgb32"), glewGetExtension("GL_ARB_texture_buffer_object_rgb32"));
}

#endif /* GL_ARB_texture_buffer_object_rgb32 */

#ifdef GL_ARB_texture_buffer_range

static void _glewInfo_GL_ARB_texture_buffer_range (void)
{
  glewPrintExt("GL_ARB_texture_buffer_range", GLEW_ARB_texture_buffer_range, glewIsSupported("GL_ARB_texture_buffer_range"), glewGetExtension("GL_ARB_texture_buffer_range"));

  glewInfoFunc("glTexBufferRange", glTexBufferRange == NULL);
  glewInfoFunc("glTextureBufferRangeEXT", glTextureBufferRangeEXT == NULL);
}

#endif /* GL_ARB_texture_buffer_range */

#ifdef GL_ARB_texture_compression

static void _glewInfo_GL_ARB_texture_compression (void)
{
  glewPrintExt("GL_ARB_texture_compression", GLEW_ARB_texture_compression, glewIsSupported("GL_ARB_texture_compression"), glewGetExtension("GL_ARB_texture_compression"));

  glewInfoFunc("glCompressedTexImage1DARB", glCompressedTexImage1DARB == NULL);
  glewInfoFunc("glCompressedTexImage2DARB", glCompressedTexImage2DARB == NULL);
  glewInfoFunc("glCompressedTexImage3DARB", glCompressedTexImage3DARB == NULL);
  glewInfoFunc("glCompressedTexSubImage1DARB", glCompressedTexSubImage1DARB == NULL);
  glewInfoFunc("glCompressedTexSubImage2DARB", glCompressedTexSubImage2DARB == NULL);
  glewInfoFunc("glCompressedTexSubImage3DARB", glCompressedTexSubImage3DARB == NULL);
  glewInfoFunc("glGetCompressedTexImageARB", glGetCompressedTexImageARB == NULL);
}

#endif /* GL_ARB_texture_compression */

#ifdef GL_ARB_texture_compression_bptc

static void _glewInfo_GL_ARB_texture_compression_bptc (void)
{
  glewPrintExt("GL_ARB_texture_compression_bptc", GLEW_ARB_texture_compression_bptc, glewIsSupported("GL_ARB_texture_compression_bptc"), glewGetExtension("GL_ARB_texture_compression_bptc"));
}

#endif /* GL_ARB_texture_compression_bptc */

#ifdef GL_ARB_texture_compression_rgtc

static void _glewInfo_GL_ARB_texture_compression_rgtc (void)
{
  glewPrintExt("GL_ARB_texture_compression_rgtc", GLEW_ARB_texture_compression_rgtc, glewIsSupported("GL_ARB_texture_compression_rgtc"), glewGetExtension("GL_ARB_texture_compression_rgtc"));
}

#endif /* GL_ARB_texture_compression_rgtc */

#ifdef GL_ARB_texture_cube_map

static void _glewInfo_GL_ARB_texture_cube_map (void)
{
  glewPrintExt("GL_ARB_texture_cube_map", GLEW_ARB_texture_cube_map, glewIsSupported("GL_ARB_texture_cube_map"), glewGetExtension("GL_ARB_texture_cube_map"));
}

#endif /* GL_ARB_texture_cube_map */

#ifdef GL_ARB_texture_cube_map_array

static void _glewInfo_GL_ARB_texture_cube_map_array (void)
{
  glewPrintExt("GL_ARB_texture_cube_map_array", GLEW_ARB_texture_cube_map_array, glewIsSupported("GL_ARB_texture_cube_map_array"), glewGetExtension("GL_ARB_texture_cube_map_array"));
}

#endif /* GL_ARB_texture_cube_map_array */

#ifdef GL_ARB_texture_env_add

static void _glewInfo_GL_ARB_texture_env_add (void)
{
  glewPrintExt("GL_ARB_texture_env_add", GLEW_ARB_texture_env_add, glewIsSupported("GL_ARB_texture_env_add"), glewGetExtension("GL_ARB_texture_env_add"));
}

#endif /* GL_ARB_texture_env_add */

#ifdef GL_ARB_texture_env_combine

static void _glewInfo_GL_ARB_texture_env_combine (void)
{
  glewPrintExt("GL_ARB_texture_env_combine", GLEW_ARB_texture_env_combine, glewIsSupported("GL_ARB_texture_env_combine"), glewGetExtension("GL_ARB_texture_env_combine"));
}

#endif /* GL_ARB_texture_env_combine */

#ifdef GL_ARB_texture_env_crossbar

static void _glewInfo_GL_ARB_texture_env_crossbar (void)
{
  glewPrintExt("GL_ARB_texture_env_crossbar", GLEW_ARB_texture_env_crossbar, glewIsSupported("GL_ARB_texture_env_crossbar"), glewGetExtension("GL_ARB_texture_env_crossbar"));
}

#endif /* GL_ARB_texture_env_crossbar */

#ifdef GL_ARB_texture_env_dot3

static void _glewInfo_GL_ARB_texture_env_dot3 (void)
{
  glewPrintExt("GL_ARB_texture_env_dot3", GLEW_ARB_texture_env_dot3, glewIsSupported("GL_ARB_texture_env_dot3"), glewGetExtension("GL_ARB_texture_env_dot3"));
}

#endif /* GL_ARB_texture_env_dot3 */

#ifdef GL_ARB_texture_filter_anisotropic

static void _glewInfo_GL_ARB_texture_filter_anisotropic (void)
{
  glewPrintExt("GL_ARB_texture_filter_anisotropic", GLEW_ARB_texture_filter_anisotropic, glewIsSupported("GL_ARB_texture_filter_anisotropic"), glewGetExtension("GL_ARB_texture_filter_anisotropic"));
}

#endif /* GL_ARB_texture_filter_anisotropic */

#ifdef GL_ARB_texture_filter_minmax

static void _glewInfo_GL_ARB_texture_filter_minmax (void)
{
  glewPrintExt("GL_ARB_texture_filter_minmax", GLEW_ARB_texture_filter_minmax, glewIsSupported("GL_ARB_texture_filter_minmax"), glewGetExtension("GL_ARB_texture_filter_minmax"));
}

#endif /* GL_ARB_texture_filter_minmax */

#ifdef GL_ARB_texture_float

static void _glewInfo_GL_ARB_texture_float (void)
{
  glewPrintExt("GL_ARB_texture_float", GLEW_ARB_texture_float, glewIsSupported("GL_ARB_texture_float"), glewGetExtension("GL_ARB_texture_float"));
}

#endif /* GL_ARB_texture_float */

#ifdef GL_ARB_texture_gather

static void _glewInfo_GL_ARB_texture_gather (void)
{
  glewPrintExt("GL_ARB_texture_gather", GLEW_ARB_texture_gather, glewIsSupported("GL_ARB_texture_gather"), glewGetExtension("GL_ARB_texture_gather"));
}

#endif /* GL_ARB_texture_gather */

#ifdef GL_ARB_texture_mirror_clamp_to_edge

static void _glewInfo_GL_ARB_texture_mirror_clamp_to_edge (void)
{
  glewPrintExt("GL_ARB_texture_mirror_clamp_to_edge", GLEW_ARB_texture_mirror_clamp_to_edge, glewIsSupported("GL_ARB_texture_mirror_clamp_to_edge"), glewGetExtension("GL_ARB_texture_mirror_clamp_to_edge"));
}

#endif /* GL_ARB_texture_mirror_clamp_to_edge */

#ifdef GL_ARB_texture_mirrored_repeat

static void _glewInfo_GL_ARB_texture_mirrored_repeat (void)
{
  glewPrintExt("GL_ARB_texture_mirrored_repeat", GLEW_ARB_texture_mirrored_repeat, glewIsSupported("GL_ARB_texture_mirrored_repeat"), glewGetExtension("GL_ARB_texture_mirrored_repeat"));
}

#endif /* GL_ARB_texture_mirrored_repeat */

#ifdef GL_ARB_texture_multisample

static void _glewInfo_GL_ARB_texture_multisample (void)
{
  glewPrintExt("GL_ARB_texture_multisample", GLEW_ARB_texture_multisample, glewIsSupported("GL_ARB_texture_multisample"), glewGetExtension("GL_ARB_texture_multisample"));

  glewInfoFunc("glGetMultisamplefv", glGetMultisamplefv == NULL);
  glewInfoFunc("glSampleMaski", glSampleMaski == NULL);
  glewInfoFunc("glTexImage2DMultisample", glTexImage2DMultisample == NULL);
  glewInfoFunc("glTexImage3DMultisample", glTexImage3DMultisample == NULL);
}

#endif /* GL_ARB_texture_multisample */

#ifdef GL_ARB_texture_non_power_of_two

static void _glewInfo_GL_ARB_texture_non_power_of_two (void)
{
  glewPrintExt("GL_ARB_texture_non_power_of_two", GLEW_ARB_texture_non_power_of_two, glewIsSupported("GL_ARB_texture_non_power_of_two"), glewGetExtension("GL_ARB_texture_non_power_of_two"));
}

#endif /* GL_ARB_texture_non_power_of_two */

#ifdef GL_ARB_texture_query_levels

static void _glewInfo_GL_ARB_texture_query_levels (void)
{
  glewPrintExt("GL_ARB_texture_query_levels", GLEW_ARB_texture_query_levels, glewIsSupported("GL_ARB_texture_query_levels"), glewGetExtension("GL_ARB_texture_query_levels"));
}

#endif /* GL_ARB_texture_query_levels */

#ifdef GL_ARB_texture_query_lod

static void _glewInfo_GL_ARB_texture_query_lod (void)
{
  glewPrintExt("GL_ARB_texture_query_lod", GLEW_ARB_texture_query_lod, glewIsSupported("GL_ARB_texture_query_lod"), glewGetExtension("GL_ARB_texture_query_lod"));
}

#endif /* GL_ARB_texture_query_lod */

#ifdef GL_ARB_texture_rectangle

static void _glewInfo_GL_ARB_texture_rectangle (void)
{
  glewPrintExt("GL_ARB_texture_rectangle", GLEW_ARB_texture_rectangle, glewIsSupported("GL_ARB_texture_rectangle"), glewGetExtension("GL_ARB_texture_rectangle"));
}

#endif /* GL_ARB_texture_rectangle */

#ifdef GL_ARB_texture_rg

static void _glewInfo_GL_ARB_texture_rg (void)
{
  glewPrintExt("GL_ARB_texture_rg", GLEW_ARB_texture_rg, glewIsSupported("GL_ARB_texture_rg"), glewGetExtension("GL_ARB_texture_rg"));
}

#endif /* GL_ARB_texture_rg */

#ifdef GL_ARB_texture_rgb10_a2ui

static void _glewInfo_GL_ARB_texture_rgb10_a2ui (void)
{
  glewPrintExt("GL_ARB_texture_rgb10_a2ui", GLEW_ARB_texture_rgb10_a2ui, glewIsSupported("GL_ARB_texture_rgb10_a2ui"), glewGetExtension("GL_ARB_texture_rgb10_a2ui"));
}

#endif /* GL_ARB_texture_rgb10_a2ui */

#ifdef GL_ARB_texture_stencil8

static void _glewInfo_GL_ARB_texture_stencil8 (void)
{
  glewPrintExt("GL_ARB_texture_stencil8", GLEW_ARB_texture_stencil8, glewIsSupported("GL_ARB_texture_stencil8"), glewGetExtension("GL_ARB_texture_stencil8"));
}

#endif /* GL_ARB_texture_stencil8 */

#ifdef GL_ARB_texture_storage

static void _glewInfo_GL_ARB_texture_storage (void)
{
  glewPrintExt("GL_ARB_texture_storage", GLEW_ARB_texture_storage, glewIsSupported("GL_ARB_texture_storage"), glewGetExtension("GL_ARB_texture_storage"));

  glewInfoFunc("glTexStorage1D", glTexStorage1D == NULL);
  glewInfoFunc("glTexStorage2D", glTexStorage2D == NULL);
  glewInfoFunc("glTexStorage3D", glTexStorage3D == NULL);
}

#endif /* GL_ARB_texture_storage */

#ifdef GL_ARB_texture_storage_multisample

static void _glewInfo_GL_ARB_texture_storage_multisample (void)
{
  glewPrintExt("GL_ARB_texture_storage_multisample", GLEW_ARB_texture_storage_multisample, glewIsSupported("GL_ARB_texture_storage_multisample"), glewGetExtension("GL_ARB_texture_storage_multisample"));

  glewInfoFunc("glTexStorage2DMultisample", glTexStorage2DMultisample == NULL);
  glewInfoFunc("glTexStorage3DMultisample", glTexStorage3DMultisample == NULL);
  glewInfoFunc("glTextureStorage2DMultisampleEXT", glTextureStorage2DMultisampleEXT == NULL);
  glewInfoFunc("glTextureStorage3DMultisampleEXT", glTextureStorage3DMultisampleEXT == NULL);
}

#endif /* GL_ARB_texture_storage_multisample */

#ifdef GL_ARB_texture_swizzle

static void _glewInfo_GL_ARB_texture_swizzle (void)
{
  glewPrintExt("GL_ARB_texture_swizzle", GLEW_ARB_texture_swizzle, glewIsSupported("GL_ARB_texture_swizzle"), glewGetExtension("GL_ARB_texture_swizzle"));
}

#endif /* GL_ARB_texture_swizzle */

#ifdef GL_ARB_texture_view

static void _glewInfo_GL_ARB_texture_view (void)
{
  glewPrintExt("GL_ARB_texture_view", GLEW_ARB_texture_view, glewIsSupported("GL_ARB_texture_view"), glewGetExtension("GL_ARB_texture_view"));

  glewInfoFunc("glTextureView", glTextureView == NULL);
}

#endif /* GL_ARB_texture_view */

#ifdef GL_ARB_timer_query

static void _glewInfo_GL_ARB_timer_query (void)
{
  glewPrintExt("GL_ARB_timer_query", GLEW_ARB_timer_query, glewIsSupported("GL_ARB_timer_query"), glewGetExtension("GL_ARB_timer_query"));

  glewInfoFunc("glGetQueryObjecti64v", glGetQueryObjecti64v == NULL);
  glewInfoFunc("glGetQueryObjectui64v", glGetQueryObjectui64v == NULL);
  glewInfoFunc("glQueryCounter", glQueryCounter == NULL);
}

#endif /* GL_ARB_timer_query */

#ifdef GL_ARB_transform_feedback2

static void _glewInfo_GL_ARB_transform_feedback2 (void)
{
  glewPrintExt("GL_ARB_transform_feedback2", GLEW_ARB_transform_feedback2, glewIsSupported("GL_ARB_transform_feedback2"), glewGetExtension("GL_ARB_transform_feedback2"));

  glewInfoFunc("glBindTransformFeedback", glBindTransformFeedback == NULL);
  glewInfoFunc("glDeleteTransformFeedbacks", glDeleteTransformFeedbacks == NULL);
  glewInfoFunc("glDrawTransformFeedback", glDrawTransformFeedback == NULL);
  glewInfoFunc("glGenTransformFeedbacks", glGenTransformFeedbacks == NULL);
  glewInfoFunc("glIsTransformFeedback", glIsTransformFeedback == NULL);
  glewInfoFunc("glPauseTransformFeedback", glPauseTransformFeedback == NULL);
  glewInfoFunc("glResumeTransformFeedback", glResumeTransformFeedback == NULL);
}

#endif /* GL_ARB_transform_feedback2 */

#ifdef GL_ARB_transform_feedback3

static void _glewInfo_GL_ARB_transform_feedback3 (void)
{
  glewPrintExt("GL_ARB_transform_feedback3", GLEW_ARB_transform_feedback3, glewIsSupported("GL_ARB_transform_feedback3"), glewGetExtension("GL_ARB_transform_feedback3"));

  glewInfoFunc("glBeginQueryIndexed", glBeginQueryIndexed == NULL);
  glewInfoFunc("glDrawTransformFeedbackStream", glDrawTransformFeedbackStream == NULL);
  glewInfoFunc("glEndQueryIndexed", glEndQueryIndexed == NULL);
  glewInfoFunc("glGetQueryIndexediv", glGetQueryIndexediv == NULL);
}

#endif /* GL_ARB_transform_feedback3 */

#ifdef GL_ARB_transform_feedback_instanced

static void _glewInfo_GL_ARB_transform_feedback_instanced (void)
{
  glewPrintExt("GL_ARB_transform_feedback_instanced", GLEW_ARB_transform_feedback_instanced, glewIsSupported("GL_ARB_transform_feedback_instanced"), glewGetExtension("GL_ARB_transform_feedback_instanced"));

  glewInfoFunc("glDrawTransformFeedbackInstanced", glDrawTransformFeedbackInstanced == NULL);
  glewInfoFunc("glDrawTransformFeedbackStreamInstanced", glDrawTransformFeedbackStreamInstanced == NULL);
}

#endif /* GL_ARB_transform_feedback_instanced */

#ifdef GL_ARB_transform_feedback_overflow_query

static void _glewInfo_GL_ARB_transform_feedback_overflow_query (void)
{
  glewPrintExt("GL_ARB_transform_feedback_overflow_query", GLEW_ARB_transform_feedback_overflow_query, glewIsSupported("GL_ARB_transform_feedback_overflow_query"), glewGetExtension("GL_ARB_transform_feedback_overflow_query"));
}

#endif /* GL_ARB_transform_feedback_overflow_query */

#ifdef GL_ARB_transpose_matrix

static void _glewInfo_GL_ARB_transpose_matrix (void)
{
  glewPrintExt("GL_ARB_transpose_matrix", GLEW_ARB_transpose_matrix, glewIsSupported("GL_ARB_transpose_matrix"), glewGetExtension("GL_ARB_transpose_matrix"));

  glewInfoFunc("glLoadTransposeMatrixdARB", glLoadTransposeMatrixdARB == NULL);
  glewInfoFunc("glLoadTransposeMatrixfARB", glLoadTransposeMatrixfARB == NULL);
  glewInfoFunc("glMultTransposeMatrixdARB", glMultTransposeMatrixdARB == NULL);
  glewInfoFunc("glMultTransposeMatrixfARB", glMultTransposeMatrixfARB == NULL);
}

#endif /* GL_ARB_transpose_matrix */

#ifdef GL_ARB_uniform_buffer_object

static void _glewInfo_GL_ARB_uniform_buffer_object (void)
{
  glewPrintExt("GL_ARB_uniform_buffer_object", GLEW_ARB_uniform_buffer_object, glewIsSupported("GL_ARB_uniform_buffer_object"), glewGetExtension("GL_ARB_uniform_buffer_object"));

  glewInfoFunc("glBindBufferBase", glBindBufferBase == NULL);
  glewInfoFunc("glBindBufferRange", glBindBufferRange == NULL);
  glewInfoFunc("glGetActiveUniformBlockName", glGetActiveUniformBlockName == NULL);
  glewInfoFunc("glGetActiveUniformBlockiv", glGetActiveUniformBlockiv == NULL);
  glewInfoFunc("glGetActiveUniformName", glGetActiveUniformName == NULL);
  glewInfoFunc("glGetActiveUniformsiv", glGetActiveUniformsiv == NULL);
  glewInfoFunc("glGetIntegeri_v", glGetIntegeri_v == NULL);
  glewInfoFunc("glGetUniformBlockIndex", glGetUniformBlockIndex == NULL);
  glewInfoFunc("glGetUniformIndices", glGetUniformIndices == NULL);
  glewInfoFunc("glUniformBlockBinding", glUniformBlockBinding == NULL);
}

#endif /* GL_ARB_uniform_buffer_object */

#ifdef GL_ARB_vertex_array_bgra

static void _glewInfo_GL_ARB_vertex_array_bgra (void)
{
  glewPrintExt("GL_ARB_vertex_array_bgra", GLEW_ARB_vertex_array_bgra, glewIsSupported("GL_ARB_vertex_array_bgra"), glewGetExtension("GL_ARB_vertex_array_bgra"));
}

#endif /* GL_ARB_vertex_array_bgra */

#ifdef GL_ARB_vertex_array_object

static void _glewInfo_GL_ARB_vertex_array_object (void)
{
  glewPrintExt("GL_ARB_vertex_array_object", GLEW_ARB_vertex_array_object, glewIsSupported("GL_ARB_vertex_array_object"), glewGetExtension("GL_ARB_vertex_array_object"));

  glewInfoFunc("glBindVertexArray", glBindVertexArray == NULL);
  glewInfoFunc("glDeleteVertexArrays", glDeleteVertexArrays == NULL);
  glewInfoFunc("glGenVertexArrays", glGenVertexArrays == NULL);
  glewInfoFunc("glIsVertexArray", glIsVertexArray == NULL);
}

#endif /* GL_ARB_vertex_array_object */

#ifdef GL_ARB_vertex_attrib_64bit

static void _glewInfo_GL_ARB_vertex_attrib_64bit (void)
{
  glewPrintExt("GL_ARB_vertex_attrib_64bit", GLEW_ARB_vertex_attrib_64bit, glewIsSupported("GL_ARB_vertex_attrib_64bit"), glewGetExtension("GL_ARB_vertex_attrib_64bit"));

  glewInfoFunc("glGetVertexAttribLdv", glGetVertexAttribLdv == NULL);
  glewInfoFunc("glVertexAttribL1d", glVertexAttribL1d == NULL);
  glewInfoFunc("glVertexAttribL1dv", glVertexAttribL1dv == NULL);
  glewInfoFunc("glVertexAttribL2d", glVertexAttribL2d == NULL);
  glewInfoFunc("glVertexAttribL2dv", glVertexAttribL2dv == NULL);
  glewInfoFunc("glVertexAttribL3d", glVertexAttribL3d == NULL);
  glewInfoFunc("glVertexAttribL3dv", glVertexAttribL3dv == NULL);
  glewInfoFunc("glVertexAttribL4d", glVertexAttribL4d == NULL);
  glewInfoFunc("glVertexAttribL4dv", glVertexAttribL4dv == NULL);
  glewInfoFunc("glVertexAttribLPointer", glVertexAttribLPointer == NULL);
}

#endif /* GL_ARB_vertex_attrib_64bit */

#ifdef GL_ARB_vertex_attrib_binding

static void _glewInfo_GL_ARB_vertex_attrib_binding (void)
{
  glewPrintExt("GL_ARB_vertex_attrib_binding", GLEW_ARB_vertex_attrib_binding, glewIsSupported("GL_ARB_vertex_attrib_binding"), glewGetExtension("GL_ARB_vertex_attrib_binding"));

  glewInfoFunc("glBindVertexBuffer", glBindVertexBuffer == NULL);
  glewInfoFunc("glVertexArrayBindVertexBufferEXT", glVertexArrayBindVertexBufferEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribBindingEXT", glVertexArrayVertexAttribBindingEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribFormatEXT", glVertexArrayVertexAttribFormatEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribIFormatEXT", glVertexArrayVertexAttribIFormatEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribLFormatEXT", glVertexArrayVertexAttribLFormatEXT == NULL);
  glewInfoFunc("glVertexArrayVertexBindingDivisorEXT", glVertexArrayVertexBindingDivisorEXT == NULL);
  glewInfoFunc("glVertexAttribBinding", glVertexAttribBinding == NULL);
  glewInfoFunc("glVertexAttribFormat", glVertexAttribFormat == NULL);
  glewInfoFunc("glVertexAttribIFormat", glVertexAttribIFormat == NULL);
  glewInfoFunc("glVertexAttribLFormat", glVertexAttribLFormat == NULL);
  glewInfoFunc("glVertexBindingDivisor", glVertexBindingDivisor == NULL);
}

#endif /* GL_ARB_vertex_attrib_binding */

#ifdef GL_ARB_vertex_blend

static void _glewInfo_GL_ARB_vertex_blend (void)
{
  glewPrintExt("GL_ARB_vertex_blend", GLEW_ARB_vertex_blend, glewIsSupported("GL_ARB_vertex_blend"), glewGetExtension("GL_ARB_vertex_blend"));

  glewInfoFunc("glVertexBlendARB", glVertexBlendARB == NULL);
  glewInfoFunc("glWeightPointerARB", glWeightPointerARB == NULL);
  glewInfoFunc("glWeightbvARB", glWeightbvARB == NULL);
  glewInfoFunc("glWeightdvARB", glWeightdvARB == NULL);
  glewInfoFunc("glWeightfvARB", glWeightfvARB == NULL);
  glewInfoFunc("glWeightivARB", glWeightivARB == NULL);
  glewInfoFunc("glWeightsvARB", glWeightsvARB == NULL);
  glewInfoFunc("glWeightubvARB", glWeightubvARB == NULL);
  glewInfoFunc("glWeightuivARB", glWeightuivARB == NULL);
  glewInfoFunc("glWeightusvARB", glWeightusvARB == NULL);
}

#endif /* GL_ARB_vertex_blend */

#ifdef GL_ARB_vertex_buffer_object

static void _glewInfo_GL_ARB_vertex_buffer_object (void)
{
  glewPrintExt("GL_ARB_vertex_buffer_object", GLEW_ARB_vertex_buffer_object, glewIsSupported("GL_ARB_vertex_buffer_object"), glewGetExtension("GL_ARB_vertex_buffer_object"));

  glewInfoFunc("glBindBufferARB", glBindBufferARB == NULL);
  glewInfoFunc("glBufferDataARB", glBufferDataARB == NULL);
  glewInfoFunc("glBufferSubDataARB", glBufferSubDataARB == NULL);
  glewInfoFunc("glDeleteBuffersARB", glDeleteBuffersARB == NULL);
  glewInfoFunc("glGenBuffersARB", glGenBuffersARB == NULL);
  glewInfoFunc("glGetBufferParameterivARB", glGetBufferParameterivARB == NULL);
  glewInfoFunc("glGetBufferPointervARB", glGetBufferPointervARB == NULL);
  glewInfoFunc("glGetBufferSubDataARB", glGetBufferSubDataARB == NULL);
  glewInfoFunc("glIsBufferARB", glIsBufferARB == NULL);
  glewInfoFunc("glMapBufferARB", glMapBufferARB == NULL);
  glewInfoFunc("glUnmapBufferARB", glUnmapBufferARB == NULL);
}

#endif /* GL_ARB_vertex_buffer_object */

#ifdef GL_ARB_vertex_program

static void _glewInfo_GL_ARB_vertex_program (void)
{
  glewPrintExt("GL_ARB_vertex_program", GLEW_ARB_vertex_program, glewIsSupported("GL_ARB_vertex_program"), glewGetExtension("GL_ARB_vertex_program"));

  glewInfoFunc("glBindProgramARB", glBindProgramARB == NULL);
  glewInfoFunc("glDeleteProgramsARB", glDeleteProgramsARB == NULL);
  glewInfoFunc("glDisableVertexAttribArrayARB", glDisableVertexAttribArrayARB == NULL);
  glewInfoFunc("glEnableVertexAttribArrayARB", glEnableVertexAttribArrayARB == NULL);
  glewInfoFunc("glGenProgramsARB", glGenProgramsARB == NULL);
  glewInfoFunc("glGetProgramEnvParameterdvARB", glGetProgramEnvParameterdvARB == NULL);
  glewInfoFunc("glGetProgramEnvParameterfvARB", glGetProgramEnvParameterfvARB == NULL);
  glewInfoFunc("glGetProgramLocalParameterdvARB", glGetProgramLocalParameterdvARB == NULL);
  glewInfoFunc("glGetProgramLocalParameterfvARB", glGetProgramLocalParameterfvARB == NULL);
  glewInfoFunc("glGetProgramStringARB", glGetProgramStringARB == NULL);
  glewInfoFunc("glGetProgramivARB", glGetProgramivARB == NULL);
  glewInfoFunc("glGetVertexAttribPointervARB", glGetVertexAttribPointervARB == NULL);
  glewInfoFunc("glGetVertexAttribdvARB", glGetVertexAttribdvARB == NULL);
  glewInfoFunc("glGetVertexAttribfvARB", glGetVertexAttribfvARB == NULL);
  glewInfoFunc("glGetVertexAttribivARB", glGetVertexAttribivARB == NULL);
  glewInfoFunc("glIsProgramARB", glIsProgramARB == NULL);
  glewInfoFunc("glProgramEnvParameter4dARB", glProgramEnvParameter4dARB == NULL);
  glewInfoFunc("glProgramEnvParameter4dvARB", glProgramEnvParameter4dvARB == NULL);
  glewInfoFunc("glProgramEnvParameter4fARB", glProgramEnvParameter4fARB == NULL);
  glewInfoFunc("glProgramEnvParameter4fvARB", glProgramEnvParameter4fvARB == NULL);
  glewInfoFunc("glProgramLocalParameter4dARB", glProgramLocalParameter4dARB == NULL);
  glewInfoFunc("glProgramLocalParameter4dvARB", glProgramLocalParameter4dvARB == NULL);
  glewInfoFunc("glProgramLocalParameter4fARB", glProgramLocalParameter4fARB == NULL);
  glewInfoFunc("glProgramLocalParameter4fvARB", glProgramLocalParameter4fvARB == NULL);
  glewInfoFunc("glProgramStringARB", glProgramStringARB == NULL);
  glewInfoFunc("glVertexAttrib1dARB", glVertexAttrib1dARB == NULL);
  glewInfoFunc("glVertexAttrib1dvARB", glVertexAttrib1dvARB == NULL);
  glewInfoFunc("glVertexAttrib1fARB", glVertexAttrib1fARB == NULL);
  glewInfoFunc("glVertexAttrib1fvARB", glVertexAttrib1fvARB == NULL);
  glewInfoFunc("glVertexAttrib1sARB", glVertexAttrib1sARB == NULL);
  glewInfoFunc("glVertexAttrib1svARB", glVertexAttrib1svARB == NULL);
  glewInfoFunc("glVertexAttrib2dARB", glVertexAttrib2dARB == NULL);
  glewInfoFunc("glVertexAttrib2dvARB", glVertexAttrib2dvARB == NULL);
  glewInfoFunc("glVertexAttrib2fARB", glVertexAttrib2fARB == NULL);
  glewInfoFunc("glVertexAttrib2fvARB", glVertexAttrib2fvARB == NULL);
  glewInfoFunc("glVertexAttrib2sARB", glVertexAttrib2sARB == NULL);
  glewInfoFunc("glVertexAttrib2svARB", glVertexAttrib2svARB == NULL);
  glewInfoFunc("glVertexAttrib3dARB", glVertexAttrib3dARB == NULL);
  glewInfoFunc("glVertexAttrib3dvARB", glVertexAttrib3dvARB == NULL);
  glewInfoFunc("glVertexAttrib3fARB", glVertexAttrib3fARB == NULL);
  glewInfoFunc("glVertexAttrib3fvARB", glVertexAttrib3fvARB == NULL);
  glewInfoFunc("glVertexAttrib3sARB", glVertexAttrib3sARB == NULL);
  glewInfoFunc("glVertexAttrib3svARB", glVertexAttrib3svARB == NULL);
  glewInfoFunc("glVertexAttrib4NbvARB", glVertexAttrib4NbvARB == NULL);
  glewInfoFunc("glVertexAttrib4NivARB", glVertexAttrib4NivARB == NULL);
  glewInfoFunc("glVertexAttrib4NsvARB", glVertexAttrib4NsvARB == NULL);
  glewInfoFunc("glVertexAttrib4NubARB", glVertexAttrib4NubARB == NULL);
  glewInfoFunc("glVertexAttrib4NubvARB", glVertexAttrib4NubvARB == NULL);
  glewInfoFunc("glVertexAttrib4NuivARB", glVertexAttrib4NuivARB == NULL);
  glewInfoFunc("glVertexAttrib4NusvARB", glVertexAttrib4NusvARB == NULL);
  glewInfoFunc("glVertexAttrib4bvARB", glVertexAttrib4bvARB == NULL);
  glewInfoFunc("glVertexAttrib4dARB", glVertexAttrib4dARB == NULL);
  glewInfoFunc("glVertexAttrib4dvARB", glVertexAttrib4dvARB == NULL);
  glewInfoFunc("glVertexAttrib4fARB", glVertexAttrib4fARB == NULL);
  glewInfoFunc("glVertexAttrib4fvARB", glVertexAttrib4fvARB == NULL);
  glewInfoFunc("glVertexAttrib4ivARB", glVertexAttrib4ivARB == NULL);
  glewInfoFunc("glVertexAttrib4sARB", glVertexAttrib4sARB == NULL);
  glewInfoFunc("glVertexAttrib4svARB", glVertexAttrib4svARB == NULL);
  glewInfoFunc("glVertexAttrib4ubvARB", glVertexAttrib4ubvARB == NULL);
  glewInfoFunc("glVertexAttrib4uivARB", glVertexAttrib4uivARB == NULL);
  glewInfoFunc("glVertexAttrib4usvARB", glVertexAttrib4usvARB == NULL);
  glewInfoFunc("glVertexAttribPointerARB", glVertexAttribPointerARB == NULL);
}

#endif /* GL_ARB_vertex_program */

#ifdef GL_ARB_vertex_shader

static void _glewInfo_GL_ARB_vertex_shader (void)
{
  glewPrintExt("GL_ARB_vertex_shader", GLEW_ARB_vertex_shader, glewIsSupported("GL_ARB_vertex_shader"), glewGetExtension("GL_ARB_vertex_shader"));

  glewInfoFunc("glBindAttribLocationARB", glBindAttribLocationARB == NULL);
  glewInfoFunc("glGetActiveAttribARB", glGetActiveAttribARB == NULL);
  glewInfoFunc("glGetAttribLocationARB", glGetAttribLocationARB == NULL);
}

#endif /* GL_ARB_vertex_shader */

#ifdef GL_ARB_vertex_type_10f_11f_11f_rev

static void _glewInfo_GL_ARB_vertex_type_10f_11f_11f_rev (void)
{
  glewPrintExt("GL_ARB_vertex_type_10f_11f_11f_rev", GLEW_ARB_vertex_type_10f_11f_11f_rev, glewIsSupported("GL_ARB_vertex_type_10f_11f_11f_rev"), glewGetExtension("GL_ARB_vertex_type_10f_11f_11f_rev"));
}

#endif /* GL_ARB_vertex_type_10f_11f_11f_rev */

#ifdef GL_ARB_vertex_type_2_10_10_10_rev

static void _glewInfo_GL_ARB_vertex_type_2_10_10_10_rev (void)
{
  glewPrintExt("GL_ARB_vertex_type_2_10_10_10_rev", GLEW_ARB_vertex_type_2_10_10_10_rev, glewIsSupported("GL_ARB_vertex_type_2_10_10_10_rev"), glewGetExtension("GL_ARB_vertex_type_2_10_10_10_rev"));

  glewInfoFunc("glColorP3ui", glColorP3ui == NULL);
  glewInfoFunc("glColorP3uiv", glColorP3uiv == NULL);
  glewInfoFunc("glColorP4ui", glColorP4ui == NULL);
  glewInfoFunc("glColorP4uiv", glColorP4uiv == NULL);
  glewInfoFunc("glMultiTexCoordP1ui", glMultiTexCoordP1ui == NULL);
  glewInfoFunc("glMultiTexCoordP1uiv", glMultiTexCoordP1uiv == NULL);
  glewInfoFunc("glMultiTexCoordP2ui", glMultiTexCoordP2ui == NULL);
  glewInfoFunc("glMultiTexCoordP2uiv", glMultiTexCoordP2uiv == NULL);
  glewInfoFunc("glMultiTexCoordP3ui", glMultiTexCoordP3ui == NULL);
  glewInfoFunc("glMultiTexCoordP3uiv", glMultiTexCoordP3uiv == NULL);
  glewInfoFunc("glMultiTexCoordP4ui", glMultiTexCoordP4ui == NULL);
  glewInfoFunc("glMultiTexCoordP4uiv", glMultiTexCoordP4uiv == NULL);
  glewInfoFunc("glNormalP3ui", glNormalP3ui == NULL);
  glewInfoFunc("glNormalP3uiv", glNormalP3uiv == NULL);
  glewInfoFunc("glSecondaryColorP3ui", glSecondaryColorP3ui == NULL);
  glewInfoFunc("glSecondaryColorP3uiv", glSecondaryColorP3uiv == NULL);
  glewInfoFunc("glTexCoordP1ui", glTexCoordP1ui == NULL);
  glewInfoFunc("glTexCoordP1uiv", glTexCoordP1uiv == NULL);
  glewInfoFunc("glTexCoordP2ui", glTexCoordP2ui == NULL);
  glewInfoFunc("glTexCoordP2uiv", glTexCoordP2uiv == NULL);
  glewInfoFunc("glTexCoordP3ui", glTexCoordP3ui == NULL);
  glewInfoFunc("glTexCoordP3uiv", glTexCoordP3uiv == NULL);
  glewInfoFunc("glTexCoordP4ui", glTexCoordP4ui == NULL);
  glewInfoFunc("glTexCoordP4uiv", glTexCoordP4uiv == NULL);
  glewInfoFunc("glVertexAttribP1ui", glVertexAttribP1ui == NULL);
  glewInfoFunc("glVertexAttribP1uiv", glVertexAttribP1uiv == NULL);
  glewInfoFunc("glVertexAttribP2ui", glVertexAttribP2ui == NULL);
  glewInfoFunc("glVertexAttribP2uiv", glVertexAttribP2uiv == NULL);
  glewInfoFunc("glVertexAttribP3ui", glVertexAttribP3ui == NULL);
  glewInfoFunc("glVertexAttribP3uiv", glVertexAttribP3uiv == NULL);
  glewInfoFunc("glVertexAttribP4ui", glVertexAttribP4ui == NULL);
  glewInfoFunc("glVertexAttribP4uiv", glVertexAttribP4uiv == NULL);
  glewInfoFunc("glVertexP2ui", glVertexP2ui == NULL);
  glewInfoFunc("glVertexP2uiv", glVertexP2uiv == NULL);
  glewInfoFunc("glVertexP3ui", glVertexP3ui == NULL);
  glewInfoFunc("glVertexP3uiv", glVertexP3uiv == NULL);
  glewInfoFunc("glVertexP4ui", glVertexP4ui == NULL);
  glewInfoFunc("glVertexP4uiv", glVertexP4uiv == NULL);
}

#endif /* GL_ARB_vertex_type_2_10_10_10_rev */

#ifdef GL_ARB_viewport_array

static void _glewInfo_GL_ARB_viewport_array (void)
{
  glewPrintExt("GL_ARB_viewport_array", GLEW_ARB_viewport_array, glewIsSupported("GL_ARB_viewport_array"), glewGetExtension("GL_ARB_viewport_array"));

  glewInfoFunc("glDepthRangeArrayv", glDepthRangeArrayv == NULL);
  glewInfoFunc("glDepthRangeIndexed", glDepthRangeIndexed == NULL);
  glewInfoFunc("glGetDoublei_v", glGetDoublei_v == NULL);
  glewInfoFunc("glGetFloati_v", glGetFloati_v == NULL);
  glewInfoFunc("glScissorArrayv", glScissorArrayv == NULL);
  glewInfoFunc("glScissorIndexed", glScissorIndexed == NULL);
  glewInfoFunc("glScissorIndexedv", glScissorIndexedv == NULL);
  glewInfoFunc("glViewportArrayv", glViewportArrayv == NULL);
  glewInfoFunc("glViewportIndexedf", glViewportIndexedf == NULL);
  glewInfoFunc("glViewportIndexedfv", glViewportIndexedfv == NULL);
}

#endif /* GL_ARB_viewport_array */

#ifdef GL_ARB_window_pos

static void _glewInfo_GL_ARB_window_pos (void)
{
  glewPrintExt("GL_ARB_window_pos", GLEW_ARB_window_pos, glewIsSupported("GL_ARB_window_pos"), glewGetExtension("GL_ARB_window_pos"));

  glewInfoFunc("glWindowPos2dARB", glWindowPos2dARB == NULL);
  glewInfoFunc("glWindowPos2dvARB", glWindowPos2dvARB == NULL);
  glewInfoFunc("glWindowPos2fARB", glWindowPos2fARB == NULL);
  glewInfoFunc("glWindowPos2fvARB", glWindowPos2fvARB == NULL);
  glewInfoFunc("glWindowPos2iARB", glWindowPos2iARB == NULL);
  glewInfoFunc("glWindowPos2ivARB", glWindowPos2ivARB == NULL);
  glewInfoFunc("glWindowPos2sARB", glWindowPos2sARB == NULL);
  glewInfoFunc("glWindowPos2svARB", glWindowPos2svARB == NULL);
  glewInfoFunc("glWindowPos3dARB", glWindowPos3dARB == NULL);
  glewInfoFunc("glWindowPos3dvARB", glWindowPos3dvARB == NULL);
  glewInfoFunc("glWindowPos3fARB", glWindowPos3fARB == NULL);
  glewInfoFunc("glWindowPos3fvARB", glWindowPos3fvARB == NULL);
  glewInfoFunc("glWindowPos3iARB", glWindowPos3iARB == NULL);
  glewInfoFunc("glWindowPos3ivARB", glWindowPos3ivARB == NULL);
  glewInfoFunc("glWindowPos3sARB", glWindowPos3sARB == NULL);
  glewInfoFunc("glWindowPos3svARB", glWindowPos3svARB == NULL);
}

#endif /* GL_ARB_window_pos */

#ifdef GL_ARM_mali_program_binary

static void _glewInfo_GL_ARM_mali_program_binary (void)
{
  glewPrintExt("GL_ARM_mali_program_binary", GLEW_ARM_mali_program_binary, glewIsSupported("GL_ARM_mali_program_binary"), glewGetExtension("GL_ARM_mali_program_binary"));
}

#endif /* GL_ARM_mali_program_binary */

#ifdef GL_ARM_mali_shader_binary

static void _glewInfo_GL_ARM_mali_shader_binary (void)
{
  glewPrintExt("GL_ARM_mali_shader_binary", GLEW_ARM_mali_shader_binary, glewIsSupported("GL_ARM_mali_shader_binary"), glewGetExtension("GL_ARM_mali_shader_binary"));
}

#endif /* GL_ARM_mali_shader_binary */

#ifdef GL_ARM_rgba8

static void _glewInfo_GL_ARM_rgba8 (void)
{
  glewPrintExt("GL_ARM_rgba8", GLEW_ARM_rgba8, glewIsSupported("GL_ARM_rgba8"), glewGetExtension("GL_ARM_rgba8"));
}

#endif /* GL_ARM_rgba8 */

#ifdef GL_ARM_shader_framebuffer_fetch

static void _glewInfo_GL_ARM_shader_framebuffer_fetch (void)
{
  glewPrintExt("GL_ARM_shader_framebuffer_fetch", GLEW_ARM_shader_framebuffer_fetch, glewIsSupported("GL_ARM_shader_framebuffer_fetch"), glewGetExtension("GL_ARM_shader_framebuffer_fetch"));
}

#endif /* GL_ARM_shader_framebuffer_fetch */

#ifdef GL_ARM_shader_framebuffer_fetch_depth_stencil

static void _glewInfo_GL_ARM_shader_framebuffer_fetch_depth_stencil (void)
{
  glewPrintExt("GL_ARM_shader_framebuffer_fetch_depth_stencil", GLEW_ARM_shader_framebuffer_fetch_depth_stencil, glewIsSupported("GL_ARM_shader_framebuffer_fetch_depth_stencil"), glewGetExtension("GL_ARM_shader_framebuffer_fetch_depth_stencil"));
}

#endif /* GL_ARM_shader_framebuffer_fetch_depth_stencil */

#ifdef GL_ATIX_point_sprites

static void _glewInfo_GL_ATIX_point_sprites (void)
{
  glewPrintExt("GL_ATIX_point_sprites", GLEW_ATIX_point_sprites, glewIsSupported("GL_ATIX_point_sprites"), glewGetExtension("GL_ATIX_point_sprites"));
}

#endif /* GL_ATIX_point_sprites */

#ifdef GL_ATIX_texture_env_combine3

static void _glewInfo_GL_ATIX_texture_env_combine3 (void)
{
  glewPrintExt("GL_ATIX_texture_env_combine3", GLEW_ATIX_texture_env_combine3, glewIsSupported("GL_ATIX_texture_env_combine3"), glewGetExtension("GL_ATIX_texture_env_combine3"));
}

#endif /* GL_ATIX_texture_env_combine3 */

#ifdef GL_ATIX_texture_env_route

static void _glewInfo_GL_ATIX_texture_env_route (void)
{
  glewPrintExt("GL_ATIX_texture_env_route", GLEW_ATIX_texture_env_route, glewIsSupported("GL_ATIX_texture_env_route"), glewGetExtension("GL_ATIX_texture_env_route"));
}

#endif /* GL_ATIX_texture_env_route */

#ifdef GL_ATIX_vertex_shader_output_point_size

static void _glewInfo_GL_ATIX_vertex_shader_output_point_size (void)
{
  glewPrintExt("GL_ATIX_vertex_shader_output_point_size", GLEW_ATIX_vertex_shader_output_point_size, glewIsSupported("GL_ATIX_vertex_shader_output_point_size"), glewGetExtension("GL_ATIX_vertex_shader_output_point_size"));
}

#endif /* GL_ATIX_vertex_shader_output_point_size */

#ifdef GL_ATI_draw_buffers

static void _glewInfo_GL_ATI_draw_buffers (void)
{
  glewPrintExt("GL_ATI_draw_buffers", GLEW_ATI_draw_buffers, glewIsSupported("GL_ATI_draw_buffers"), glewGetExtension("GL_ATI_draw_buffers"));

  glewInfoFunc("glDrawBuffersATI", glDrawBuffersATI == NULL);
}

#endif /* GL_ATI_draw_buffers */

#ifdef GL_ATI_element_array

static void _glewInfo_GL_ATI_element_array (void)
{
  glewPrintExt("GL_ATI_element_array", GLEW_ATI_element_array, glewIsSupported("GL_ATI_element_array"), glewGetExtension("GL_ATI_element_array"));

  glewInfoFunc("glDrawElementArrayATI", glDrawElementArrayATI == NULL);
  glewInfoFunc("glDrawRangeElementArrayATI", glDrawRangeElementArrayATI == NULL);
  glewInfoFunc("glElementPointerATI", glElementPointerATI == NULL);
}

#endif /* GL_ATI_element_array */

#ifdef GL_ATI_envmap_bumpmap

static void _glewInfo_GL_ATI_envmap_bumpmap (void)
{
  glewPrintExt("GL_ATI_envmap_bumpmap", GLEW_ATI_envmap_bumpmap, glewIsSupported("GL_ATI_envmap_bumpmap"), glewGetExtension("GL_ATI_envmap_bumpmap"));

  glewInfoFunc("glGetTexBumpParameterfvATI", glGetTexBumpParameterfvATI == NULL);
  glewInfoFunc("glGetTexBumpParameterivATI", glGetTexBumpParameterivATI == NULL);
  glewInfoFunc("glTexBumpParameterfvATI", glTexBumpParameterfvATI == NULL);
  glewInfoFunc("glTexBumpParameterivATI", glTexBumpParameterivATI == NULL);
}

#endif /* GL_ATI_envmap_bumpmap */

#ifdef GL_ATI_fragment_shader

static void _glewInfo_GL_ATI_fragment_shader (void)
{
  glewPrintExt("GL_ATI_fragment_shader", GLEW_ATI_fragment_shader, glewIsSupported("GL_ATI_fragment_shader"), glewGetExtension("GL_ATI_fragment_shader"));

  glewInfoFunc("glAlphaFragmentOp1ATI", glAlphaFragmentOp1ATI == NULL);
  glewInfoFunc("glAlphaFragmentOp2ATI", glAlphaFragmentOp2ATI == NULL);
  glewInfoFunc("glAlphaFragmentOp3ATI", glAlphaFragmentOp3ATI == NULL);
  glewInfoFunc("glBeginFragmentShaderATI", glBeginFragmentShaderATI == NULL);
  glewInfoFunc("glBindFragmentShaderATI", glBindFragmentShaderATI == NULL);
  glewInfoFunc("glColorFragmentOp1ATI", glColorFragmentOp1ATI == NULL);
  glewInfoFunc("glColorFragmentOp2ATI", glColorFragmentOp2ATI == NULL);
  glewInfoFunc("glColorFragmentOp3ATI", glColorFragmentOp3ATI == NULL);
  glewInfoFunc("glDeleteFragmentShaderATI", glDeleteFragmentShaderATI == NULL);
  glewInfoFunc("glEndFragmentShaderATI", glEndFragmentShaderATI == NULL);
  glewInfoFunc("glGenFragmentShadersATI", glGenFragmentShadersATI == NULL);
  glewInfoFunc("glPassTexCoordATI", glPassTexCoordATI == NULL);
  glewInfoFunc("glSampleMapATI", glSampleMapATI == NULL);
  glewInfoFunc("glSetFragmentShaderConstantATI", glSetFragmentShaderConstantATI == NULL);
}

#endif /* GL_ATI_fragment_shader */

#ifdef GL_ATI_map_object_buffer

static void _glewInfo_GL_ATI_map_object_buffer (void)
{
  glewPrintExt("GL_ATI_map_object_buffer", GLEW_ATI_map_object_buffer, glewIsSupported("GL_ATI_map_object_buffer"), glewGetExtension("GL_ATI_map_object_buffer"));

  glewInfoFunc("glMapObjectBufferATI", glMapObjectBufferATI == NULL);
  glewInfoFunc("glUnmapObjectBufferATI", glUnmapObjectBufferATI == NULL);
}

#endif /* GL_ATI_map_object_buffer */

#ifdef GL_ATI_meminfo

static void _glewInfo_GL_ATI_meminfo (void)
{
  glewPrintExt("GL_ATI_meminfo", GLEW_ATI_meminfo, glewIsSupported("GL_ATI_meminfo"), glewGetExtension("GL_ATI_meminfo"));
}

#endif /* GL_ATI_meminfo */

#ifdef GL_ATI_pn_triangles

static void _glewInfo_GL_ATI_pn_triangles (void)
{
  glewPrintExt("GL_ATI_pn_triangles", GLEW_ATI_pn_triangles, glewIsSupported("GL_ATI_pn_triangles"), glewGetExtension("GL_ATI_pn_triangles"));

  glewInfoFunc("glPNTrianglesfATI", glPNTrianglesfATI == NULL);
  glewInfoFunc("glPNTrianglesiATI", glPNTrianglesiATI == NULL);
}

#endif /* GL_ATI_pn_triangles */

#ifdef GL_ATI_separate_stencil

static void _glewInfo_GL_ATI_separate_stencil (void)
{
  glewPrintExt("GL_ATI_separate_stencil", GLEW_ATI_separate_stencil, glewIsSupported("GL_ATI_separate_stencil"), glewGetExtension("GL_ATI_separate_stencil"));

  glewInfoFunc("glStencilFuncSeparateATI", glStencilFuncSeparateATI == NULL);
  glewInfoFunc("glStencilOpSeparateATI", glStencilOpSeparateATI == NULL);
}

#endif /* GL_ATI_separate_stencil */

#ifdef GL_ATI_shader_texture_lod

static void _glewInfo_GL_ATI_shader_texture_lod (void)
{
  glewPrintExt("GL_ATI_shader_texture_lod", GLEW_ATI_shader_texture_lod, glewIsSupported("GL_ATI_shader_texture_lod"), glewGetExtension("GL_ATI_shader_texture_lod"));
}

#endif /* GL_ATI_shader_texture_lod */

#ifdef GL_ATI_text_fragment_shader

static void _glewInfo_GL_ATI_text_fragment_shader (void)
{
  glewPrintExt("GL_ATI_text_fragment_shader", GLEW_ATI_text_fragment_shader, glewIsSupported("GL_ATI_text_fragment_shader"), glewGetExtension("GL_ATI_text_fragment_shader"));
}

#endif /* GL_ATI_text_fragment_shader */

#ifdef GL_ATI_texture_compression_3dc

static void _glewInfo_GL_ATI_texture_compression_3dc (void)
{
  glewPrintExt("GL_ATI_texture_compression_3dc", GLEW_ATI_texture_compression_3dc, glewIsSupported("GL_ATI_texture_compression_3dc"), glewGetExtension("GL_ATI_texture_compression_3dc"));
}

#endif /* GL_ATI_texture_compression_3dc */

#ifdef GL_ATI_texture_env_combine3

static void _glewInfo_GL_ATI_texture_env_combine3 (void)
{
  glewPrintExt("GL_ATI_texture_env_combine3", GLEW_ATI_texture_env_combine3, glewIsSupported("GL_ATI_texture_env_combine3"), glewGetExtension("GL_ATI_texture_env_combine3"));
}

#endif /* GL_ATI_texture_env_combine3 */

#ifdef GL_ATI_texture_float

static void _glewInfo_GL_ATI_texture_float (void)
{
  glewPrintExt("GL_ATI_texture_float", GLEW_ATI_texture_float, glewIsSupported("GL_ATI_texture_float"), glewGetExtension("GL_ATI_texture_float"));
}

#endif /* GL_ATI_texture_float */

#ifdef GL_ATI_texture_mirror_once

static void _glewInfo_GL_ATI_texture_mirror_once (void)
{
  glewPrintExt("GL_ATI_texture_mirror_once", GLEW_ATI_texture_mirror_once, glewIsSupported("GL_ATI_texture_mirror_once"), glewGetExtension("GL_ATI_texture_mirror_once"));
}

#endif /* GL_ATI_texture_mirror_once */

#ifdef GL_ATI_vertex_array_object

static void _glewInfo_GL_ATI_vertex_array_object (void)
{
  glewPrintExt("GL_ATI_vertex_array_object", GLEW_ATI_vertex_array_object, glewIsSupported("GL_ATI_vertex_array_object"), glewGetExtension("GL_ATI_vertex_array_object"));

  glewInfoFunc("glArrayObjectATI", glArrayObjectATI == NULL);
  glewInfoFunc("glFreeObjectBufferATI", glFreeObjectBufferATI == NULL);
  glewInfoFunc("glGetArrayObjectfvATI", glGetArrayObjectfvATI == NULL);
  glewInfoFunc("glGetArrayObjectivATI", glGetArrayObjectivATI == NULL);
  glewInfoFunc("glGetObjectBufferfvATI", glGetObjectBufferfvATI == NULL);
  glewInfoFunc("glGetObjectBufferivATI", glGetObjectBufferivATI == NULL);
  glewInfoFunc("glGetVariantArrayObjectfvATI", glGetVariantArrayObjectfvATI == NULL);
  glewInfoFunc("glGetVariantArrayObjectivATI", glGetVariantArrayObjectivATI == NULL);
  glewInfoFunc("glIsObjectBufferATI", glIsObjectBufferATI == NULL);
  glewInfoFunc("glNewObjectBufferATI", glNewObjectBufferATI == NULL);
  glewInfoFunc("glUpdateObjectBufferATI", glUpdateObjectBufferATI == NULL);
  glewInfoFunc("glVariantArrayObjectATI", glVariantArrayObjectATI == NULL);
}

#endif /* GL_ATI_vertex_array_object */

#ifdef GL_ATI_vertex_attrib_array_object

static void _glewInfo_GL_ATI_vertex_attrib_array_object (void)
{
  glewPrintExt("GL_ATI_vertex_attrib_array_object", GLEW_ATI_vertex_attrib_array_object, glewIsSupported("GL_ATI_vertex_attrib_array_object"), glewGetExtension("GL_ATI_vertex_attrib_array_object"));

  glewInfoFunc("glGetVertexAttribArrayObjectfvATI", glGetVertexAttribArrayObjectfvATI == NULL);
  glewInfoFunc("glGetVertexAttribArrayObjectivATI", glGetVertexAttribArrayObjectivATI == NULL);
  glewInfoFunc("glVertexAttribArrayObjectATI", glVertexAttribArrayObjectATI == NULL);
}

#endif /* GL_ATI_vertex_attrib_array_object */

#ifdef GL_ATI_vertex_streams

static void _glewInfo_GL_ATI_vertex_streams (void)
{
  glewPrintExt("GL_ATI_vertex_streams", GLEW_ATI_vertex_streams, glewIsSupported("GL_ATI_vertex_streams"), glewGetExtension("GL_ATI_vertex_streams"));

  glewInfoFunc("glClientActiveVertexStreamATI", glClientActiveVertexStreamATI == NULL);
  glewInfoFunc("glNormalStream3bATI", glNormalStream3bATI == NULL);
  glewInfoFunc("glNormalStream3bvATI", glNormalStream3bvATI == NULL);
  glewInfoFunc("glNormalStream3dATI", glNormalStream3dATI == NULL);
  glewInfoFunc("glNormalStream3dvATI", glNormalStream3dvATI == NULL);
  glewInfoFunc("glNormalStream3fATI", glNormalStream3fATI == NULL);
  glewInfoFunc("glNormalStream3fvATI", glNormalStream3fvATI == NULL);
  glewInfoFunc("glNormalStream3iATI", glNormalStream3iATI == NULL);
  glewInfoFunc("glNormalStream3ivATI", glNormalStream3ivATI == NULL);
  glewInfoFunc("glNormalStream3sATI", glNormalStream3sATI == NULL);
  glewInfoFunc("glNormalStream3svATI", glNormalStream3svATI == NULL);
  glewInfoFunc("glVertexBlendEnvfATI", glVertexBlendEnvfATI == NULL);
  glewInfoFunc("glVertexBlendEnviATI", glVertexBlendEnviATI == NULL);
  glewInfoFunc("glVertexStream1dATI", glVertexStream1dATI == NULL);
  glewInfoFunc("glVertexStream1dvATI", glVertexStream1dvATI == NULL);
  glewInfoFunc("glVertexStream1fATI", glVertexStream1fATI == NULL);
  glewInfoFunc("glVertexStream1fvATI", glVertexStream1fvATI == NULL);
  glewInfoFunc("glVertexStream1iATI", glVertexStream1iATI == NULL);
  glewInfoFunc("glVertexStream1ivATI", glVertexStream1ivATI == NULL);
  glewInfoFunc("glVertexStream1sATI", glVertexStream1sATI == NULL);
  glewInfoFunc("glVertexStream1svATI", glVertexStream1svATI == NULL);
  glewInfoFunc("glVertexStream2dATI", glVertexStream2dATI == NULL);
  glewInfoFunc("glVertexStream2dvATI", glVertexStream2dvATI == NULL);
  glewInfoFunc("glVertexStream2fATI", glVertexStream2fATI == NULL);
  glewInfoFunc("glVertexStream2fvATI", glVertexStream2fvATI == NULL);
  glewInfoFunc("glVertexStream2iATI", glVertexStream2iATI == NULL);
  glewInfoFunc("glVertexStream2ivATI", glVertexStream2ivATI == NULL);
  glewInfoFunc("glVertexStream2sATI", glVertexStream2sATI == NULL);
  glewInfoFunc("glVertexStream2svATI", glVertexStream2svATI == NULL);
  glewInfoFunc("glVertexStream3dATI", glVertexStream3dATI == NULL);
  glewInfoFunc("glVertexStream3dvATI", glVertexStream3dvATI == NULL);
  glewInfoFunc("glVertexStream3fATI", glVertexStream3fATI == NULL);
  glewInfoFunc("glVertexStream3fvATI", glVertexStream3fvATI == NULL);
  glewInfoFunc("glVertexStream3iATI", glVertexStream3iATI == NULL);
  glewInfoFunc("glVertexStream3ivATI", glVertexStream3ivATI == NULL);
  glewInfoFunc("glVertexStream3sATI", glVertexStream3sATI == NULL);
  glewInfoFunc("glVertexStream3svATI", glVertexStream3svATI == NULL);
  glewInfoFunc("glVertexStream4dATI", glVertexStream4dATI == NULL);
  glewInfoFunc("glVertexStream4dvATI", glVertexStream4dvATI == NULL);
  glewInfoFunc("glVertexStream4fATI", glVertexStream4fATI == NULL);
  glewInfoFunc("glVertexStream4fvATI", glVertexStream4fvATI == NULL);
  glewInfoFunc("glVertexStream4iATI", glVertexStream4iATI == NULL);
  glewInfoFunc("glVertexStream4ivATI", glVertexStream4ivATI == NULL);
  glewInfoFunc("glVertexStream4sATI", glVertexStream4sATI == NULL);
  glewInfoFunc("glVertexStream4svATI", glVertexStream4svATI == NULL);
}

#endif /* GL_ATI_vertex_streams */

#ifdef GL_EGL_KHR_context_flush_control

static void _glewInfo_GL_EGL_KHR_context_flush_control (void)
{
  glewPrintExt("GL_EGL_KHR_context_flush_control", GLEW_EGL_KHR_context_flush_control, glewIsSupported("GL_EGL_KHR_context_flush_control"), glewGetExtension("GL_EGL_KHR_context_flush_control"));
}

#endif /* GL_EGL_KHR_context_flush_control */

#ifdef GL_EGL_NV_robustness_video_memory_purge

static void _glewInfo_GL_EGL_NV_robustness_video_memory_purge (void)
{
  glewPrintExt("GL_EGL_NV_robustness_video_memory_purge", GLEW_EGL_NV_robustness_video_memory_purge, glewIsSupported("GL_EGL_NV_robustness_video_memory_purge"), glewGetExtension("GL_EGL_NV_robustness_video_memory_purge"));
}

#endif /* GL_EGL_NV_robustness_video_memory_purge */

#ifdef GL_EXT_422_pixels

static void _glewInfo_GL_EXT_422_pixels (void)
{
  glewPrintExt("GL_EXT_422_pixels", GLEW_EXT_422_pixels, glewIsSupported("GL_EXT_422_pixels"), glewGetExtension("GL_EXT_422_pixels"));
}

#endif /* GL_EXT_422_pixels */

#ifdef GL_EXT_Cg_shader

static void _glewInfo_GL_EXT_Cg_shader (void)
{
  glewPrintExt("GL_EXT_Cg_shader", GLEW_EXT_Cg_shader, glewIsSupported("GL_EXT_Cg_shader"), glewGetExtension("GL_EXT_Cg_shader"));
}

#endif /* GL_EXT_Cg_shader */

#ifdef GL_EXT_EGL_image_array

static void _glewInfo_GL_EXT_EGL_image_array (void)
{
  glewPrintExt("GL_EXT_EGL_image_array", GLEW_EXT_EGL_image_array, glewIsSupported("GL_EXT_EGL_image_array"), glewGetExtension("GL_EXT_EGL_image_array"));
}

#endif /* GL_EXT_EGL_image_array */

#ifdef GL_EXT_YUV_target

static void _glewInfo_GL_EXT_YUV_target (void)
{
  glewPrintExt("GL_EXT_YUV_target", GLEW_EXT_YUV_target, glewIsSupported("GL_EXT_YUV_target"), glewGetExtension("GL_EXT_YUV_target"));
}

#endif /* GL_EXT_YUV_target */

#ifdef GL_EXT_abgr

static void _glewInfo_GL_EXT_abgr (void)
{
  glewPrintExt("GL_EXT_abgr", GLEW_EXT_abgr, glewIsSupported("GL_EXT_abgr"), glewGetExtension("GL_EXT_abgr"));
}

#endif /* GL_EXT_abgr */

#ifdef GL_EXT_base_instance

static void _glewInfo_GL_EXT_base_instance (void)
{
  glewPrintExt("GL_EXT_base_instance", GLEW_EXT_base_instance, glewIsSupported("GL_EXT_base_instance"), glewGetExtension("GL_EXT_base_instance"));

  glewInfoFunc("glDrawArraysInstancedBaseInstanceEXT", glDrawArraysInstancedBaseInstanceEXT == NULL);
  glewInfoFunc("glDrawElementsInstancedBaseInstanceEXT", glDrawElementsInstancedBaseInstanceEXT == NULL);
  glewInfoFunc("glDrawElementsInstancedBaseVertexBaseInstanceEXT", glDrawElementsInstancedBaseVertexBaseInstanceEXT == NULL);
}

#endif /* GL_EXT_base_instance */

#ifdef GL_EXT_bgra

static void _glewInfo_GL_EXT_bgra (void)
{
  glewPrintExt("GL_EXT_bgra", GLEW_EXT_bgra, glewIsSupported("GL_EXT_bgra"), glewGetExtension("GL_EXT_bgra"));
}

#endif /* GL_EXT_bgra */

#ifdef GL_EXT_bindable_uniform

static void _glewInfo_GL_EXT_bindable_uniform (void)
{
  glewPrintExt("GL_EXT_bindable_uniform", GLEW_EXT_bindable_uniform, glewIsSupported("GL_EXT_bindable_uniform"), glewGetExtension("GL_EXT_bindable_uniform"));

  glewInfoFunc("glGetUniformBufferSizeEXT", glGetUniformBufferSizeEXT == NULL);
  glewInfoFunc("glGetUniformOffsetEXT", glGetUniformOffsetEXT == NULL);
  glewInfoFunc("glUniformBufferEXT", glUniformBufferEXT == NULL);
}

#endif /* GL_EXT_bindable_uniform */

#ifdef GL_EXT_blend_color

static void _glewInfo_GL_EXT_blend_color (void)
{
  glewPrintExt("GL_EXT_blend_color", GLEW_EXT_blend_color, glewIsSupported("GL_EXT_blend_color"), glewGetExtension("GL_EXT_blend_color"));

  glewInfoFunc("glBlendColorEXT", glBlendColorEXT == NULL);
}

#endif /* GL_EXT_blend_color */

#ifdef GL_EXT_blend_equation_separate

static void _glewInfo_GL_EXT_blend_equation_separate (void)
{
  glewPrintExt("GL_EXT_blend_equation_separate", GLEW_EXT_blend_equation_separate, glewIsSupported("GL_EXT_blend_equation_separate"), glewGetExtension("GL_EXT_blend_equation_separate"));

  glewInfoFunc("glBlendEquationSeparateEXT", glBlendEquationSeparateEXT == NULL);
}

#endif /* GL_EXT_blend_equation_separate */

#ifdef GL_EXT_blend_func_extended

static void _glewInfo_GL_EXT_blend_func_extended (void)
{
  glewPrintExt("GL_EXT_blend_func_extended", GLEW_EXT_blend_func_extended, glewIsSupported("GL_EXT_blend_func_extended"), glewGetExtension("GL_EXT_blend_func_extended"));

  glewInfoFunc("glBindFragDataLocationIndexedEXT", glBindFragDataLocationIndexedEXT == NULL);
  glewInfoFunc("glGetFragDataIndexEXT", glGetFragDataIndexEXT == NULL);
  glewInfoFunc("glGetProgramResourceLocationIndexEXT", glGetProgramResourceLocationIndexEXT == NULL);
}

#endif /* GL_EXT_blend_func_extended */

#ifdef GL_EXT_blend_func_separate

static void _glewInfo_GL_EXT_blend_func_separate (void)
{
  glewPrintExt("GL_EXT_blend_func_separate", GLEW_EXT_blend_func_separate, glewIsSupported("GL_EXT_blend_func_separate"), glewGetExtension("GL_EXT_blend_func_separate"));

  glewInfoFunc("glBlendFuncSeparateEXT", glBlendFuncSeparateEXT == NULL);
}

#endif /* GL_EXT_blend_func_separate */

#ifdef GL_EXT_blend_logic_op

static void _glewInfo_GL_EXT_blend_logic_op (void)
{
  glewPrintExt("GL_EXT_blend_logic_op", GLEW_EXT_blend_logic_op, glewIsSupported("GL_EXT_blend_logic_op"), glewGetExtension("GL_EXT_blend_logic_op"));
}

#endif /* GL_EXT_blend_logic_op */

#ifdef GL_EXT_blend_minmax

static void _glewInfo_GL_EXT_blend_minmax (void)
{
  glewPrintExt("GL_EXT_blend_minmax", GLEW_EXT_blend_minmax, glewIsSupported("GL_EXT_blend_minmax"), glewGetExtension("GL_EXT_blend_minmax"));

  glewInfoFunc("glBlendEquationEXT", glBlendEquationEXT == NULL);
}

#endif /* GL_EXT_blend_minmax */

#ifdef GL_EXT_blend_subtract

static void _glewInfo_GL_EXT_blend_subtract (void)
{
  glewPrintExt("GL_EXT_blend_subtract", GLEW_EXT_blend_subtract, glewIsSupported("GL_EXT_blend_subtract"), glewGetExtension("GL_EXT_blend_subtract"));
}

#endif /* GL_EXT_blend_subtract */

#ifdef GL_EXT_buffer_storage

static void _glewInfo_GL_EXT_buffer_storage (void)
{
  glewPrintExt("GL_EXT_buffer_storage", GLEW_EXT_buffer_storage, glewIsSupported("GL_EXT_buffer_storage"), glewGetExtension("GL_EXT_buffer_storage"));

  glewInfoFunc("glBufferStorageEXT", glBufferStorageEXT == NULL);
  glewInfoFunc("glNamedBufferStorageEXT", glNamedBufferStorageEXT == NULL);
}

#endif /* GL_EXT_buffer_storage */

#ifdef GL_EXT_clear_texture

static void _glewInfo_GL_EXT_clear_texture (void)
{
  glewPrintExt("GL_EXT_clear_texture", GLEW_EXT_clear_texture, glewIsSupported("GL_EXT_clear_texture"), glewGetExtension("GL_EXT_clear_texture"));

  glewInfoFunc("glClearTexImageEXT", glClearTexImageEXT == NULL);
  glewInfoFunc("glClearTexSubImageEXT", glClearTexSubImageEXT == NULL);
}

#endif /* GL_EXT_clear_texture */

#ifdef GL_EXT_clip_cull_distance

static void _glewInfo_GL_EXT_clip_cull_distance (void)
{
  glewPrintExt("GL_EXT_clip_cull_distance", GLEW_EXT_clip_cull_distance, glewIsSupported("GL_EXT_clip_cull_distance"), glewGetExtension("GL_EXT_clip_cull_distance"));
}

#endif /* GL_EXT_clip_cull_distance */

#ifdef GL_EXT_clip_volume_hint

static void _glewInfo_GL_EXT_clip_volume_hint (void)
{
  glewPrintExt("GL_EXT_clip_volume_hint", GLEW_EXT_clip_volume_hint, glewIsSupported("GL_EXT_clip_volume_hint"), glewGetExtension("GL_EXT_clip_volume_hint"));
}

#endif /* GL_EXT_clip_volume_hint */

#ifdef GL_EXT_cmyka

static void _glewInfo_GL_EXT_cmyka (void)
{
  glewPrintExt("GL_EXT_cmyka", GLEW_EXT_cmyka, glewIsSupported("GL_EXT_cmyka"), glewGetExtension("GL_EXT_cmyka"));
}

#endif /* GL_EXT_cmyka */

#ifdef GL_EXT_color_buffer_float

static void _glewInfo_GL_EXT_color_buffer_float (void)
{
  glewPrintExt("GL_EXT_color_buffer_float", GLEW_EXT_color_buffer_float, glewIsSupported("GL_EXT_color_buffer_float"), glewGetExtension("GL_EXT_color_buffer_float"));
}

#endif /* GL_EXT_color_buffer_float */

#ifdef GL_EXT_color_buffer_half_float

static void _glewInfo_GL_EXT_color_buffer_half_float (void)
{
  glewPrintExt("GL_EXT_color_buffer_half_float", GLEW_EXT_color_buffer_half_float, glewIsSupported("GL_EXT_color_buffer_half_float"), glewGetExtension("GL_EXT_color_buffer_half_float"));
}

#endif /* GL_EXT_color_buffer_half_float */

#ifdef GL_EXT_color_subtable

static void _glewInfo_GL_EXT_color_subtable (void)
{
  glewPrintExt("GL_EXT_color_subtable", GLEW_EXT_color_subtable, glewIsSupported("GL_EXT_color_subtable"), glewGetExtension("GL_EXT_color_subtable"));

  glewInfoFunc("glColorSubTableEXT", glColorSubTableEXT == NULL);
  glewInfoFunc("glCopyColorSubTableEXT", glCopyColorSubTableEXT == NULL);
}

#endif /* GL_EXT_color_subtable */

#ifdef GL_EXT_compiled_vertex_array

static void _glewInfo_GL_EXT_compiled_vertex_array (void)
{
  glewPrintExt("GL_EXT_compiled_vertex_array", GLEW_EXT_compiled_vertex_array, glewIsSupported("GL_EXT_compiled_vertex_array"), glewGetExtension("GL_EXT_compiled_vertex_array"));

  glewInfoFunc("glLockArraysEXT", glLockArraysEXT == NULL);
  glewInfoFunc("glUnlockArraysEXT", glUnlockArraysEXT == NULL);
}

#endif /* GL_EXT_compiled_vertex_array */

#ifdef GL_EXT_compressed_ETC1_RGB8_sub_texture

static void _glewInfo_GL_EXT_compressed_ETC1_RGB8_sub_texture (void)
{
  glewPrintExt("GL_EXT_compressed_ETC1_RGB8_sub_texture", GLEW_EXT_compressed_ETC1_RGB8_sub_texture, glewIsSupported("GL_EXT_compressed_ETC1_RGB8_sub_texture"), glewGetExtension("GL_EXT_compressed_ETC1_RGB8_sub_texture"));
}

#endif /* GL_EXT_compressed_ETC1_RGB8_sub_texture */

#ifdef GL_EXT_conservative_depth

static void _glewInfo_GL_EXT_conservative_depth (void)
{
  glewPrintExt("GL_EXT_conservative_depth", GLEW_EXT_conservative_depth, glewIsSupported("GL_EXT_conservative_depth"), glewGetExtension("GL_EXT_conservative_depth"));
}

#endif /* GL_EXT_conservative_depth */

#ifdef GL_EXT_convolution

static void _glewInfo_GL_EXT_convolution (void)
{
  glewPrintExt("GL_EXT_convolution", GLEW_EXT_convolution, glewIsSupported("GL_EXT_convolution"), glewGetExtension("GL_EXT_convolution"));

  glewInfoFunc("glConvolutionFilter1DEXT", glConvolutionFilter1DEXT == NULL);
  glewInfoFunc("glConvolutionFilter2DEXT", glConvolutionFilter2DEXT == NULL);
  glewInfoFunc("glConvolutionParameterfEXT", glConvolutionParameterfEXT == NULL);
  glewInfoFunc("glConvolutionParameterfvEXT", glConvolutionParameterfvEXT == NULL);
  glewInfoFunc("glConvolutionParameteriEXT", glConvolutionParameteriEXT == NULL);
  glewInfoFunc("glConvolutionParameterivEXT", glConvolutionParameterivEXT == NULL);
  glewInfoFunc("glCopyConvolutionFilter1DEXT", glCopyConvolutionFilter1DEXT == NULL);
  glewInfoFunc("glCopyConvolutionFilter2DEXT", glCopyConvolutionFilter2DEXT == NULL);
  glewInfoFunc("glGetConvolutionFilterEXT", glGetConvolutionFilterEXT == NULL);
  glewInfoFunc("glGetConvolutionParameterfvEXT", glGetConvolutionParameterfvEXT == NULL);
  glewInfoFunc("glGetConvolutionParameterivEXT", glGetConvolutionParameterivEXT == NULL);
  glewInfoFunc("glGetSeparableFilterEXT", glGetSeparableFilterEXT == NULL);
  glewInfoFunc("glSeparableFilter2DEXT", glSeparableFilter2DEXT == NULL);
}

#endif /* GL_EXT_convolution */

#ifdef GL_EXT_coordinate_frame

static void _glewInfo_GL_EXT_coordinate_frame (void)
{
  glewPrintExt("GL_EXT_coordinate_frame", GLEW_EXT_coordinate_frame, glewIsSupported("GL_EXT_coordinate_frame"), glewGetExtension("GL_EXT_coordinate_frame"));

  glewInfoFunc("glBinormalPointerEXT", glBinormalPointerEXT == NULL);
  glewInfoFunc("glTangentPointerEXT", glTangentPointerEXT == NULL);
}

#endif /* GL_EXT_coordinate_frame */

#ifdef GL_EXT_copy_image

static void _glewInfo_GL_EXT_copy_image (void)
{
  glewPrintExt("GL_EXT_copy_image", GLEW_EXT_copy_image, glewIsSupported("GL_EXT_copy_image"), glewGetExtension("GL_EXT_copy_image"));

  glewInfoFunc("glCopyImageSubDataEXT", glCopyImageSubDataEXT == NULL);
}

#endif /* GL_EXT_copy_image */

#ifdef GL_EXT_copy_texture

static void _glewInfo_GL_EXT_copy_texture (void)
{
  glewPrintExt("GL_EXT_copy_texture", GLEW_EXT_copy_texture, glewIsSupported("GL_EXT_copy_texture"), glewGetExtension("GL_EXT_copy_texture"));

  glewInfoFunc("glCopyTexImage1DEXT", glCopyTexImage1DEXT == NULL);
  glewInfoFunc("glCopyTexImage2DEXT", glCopyTexImage2DEXT == NULL);
  glewInfoFunc("glCopyTexSubImage1DEXT", glCopyTexSubImage1DEXT == NULL);
  glewInfoFunc("glCopyTexSubImage2DEXT", glCopyTexSubImage2DEXT == NULL);
  glewInfoFunc("glCopyTexSubImage3DEXT", glCopyTexSubImage3DEXT == NULL);
}

#endif /* GL_EXT_copy_texture */

#ifdef GL_EXT_cull_vertex

static void _glewInfo_GL_EXT_cull_vertex (void)
{
  glewPrintExt("GL_EXT_cull_vertex", GLEW_EXT_cull_vertex, glewIsSupported("GL_EXT_cull_vertex"), glewGetExtension("GL_EXT_cull_vertex"));

  glewInfoFunc("glCullParameterdvEXT", glCullParameterdvEXT == NULL);
  glewInfoFunc("glCullParameterfvEXT", glCullParameterfvEXT == NULL);
}

#endif /* GL_EXT_cull_vertex */

#ifdef GL_EXT_debug_label

static void _glewInfo_GL_EXT_debug_label (void)
{
  glewPrintExt("GL_EXT_debug_label", GLEW_EXT_debug_label, glewIsSupported("GL_EXT_debug_label"), glewGetExtension("GL_EXT_debug_label"));

  glewInfoFunc("glGetObjectLabelEXT", glGetObjectLabelEXT == NULL);
  glewInfoFunc("glLabelObjectEXT", glLabelObjectEXT == NULL);
}

#endif /* GL_EXT_debug_label */

#ifdef GL_EXT_debug_marker

static void _glewInfo_GL_EXT_debug_marker (void)
{
  glewPrintExt("GL_EXT_debug_marker", GLEW_EXT_debug_marker, glewIsSupported("GL_EXT_debug_marker"), glewGetExtension("GL_EXT_debug_marker"));

  glewInfoFunc("glInsertEventMarkerEXT", glInsertEventMarkerEXT == NULL);
  glewInfoFunc("glPopGroupMarkerEXT", glPopGroupMarkerEXT == NULL);
  glewInfoFunc("glPushGroupMarkerEXT", glPushGroupMarkerEXT == NULL);
}

#endif /* GL_EXT_debug_marker */

#ifdef GL_EXT_depth_bounds_test

static void _glewInfo_GL_EXT_depth_bounds_test (void)
{
  glewPrintExt("GL_EXT_depth_bounds_test", GLEW_EXT_depth_bounds_test, glewIsSupported("GL_EXT_depth_bounds_test"), glewGetExtension("GL_EXT_depth_bounds_test"));

  glewInfoFunc("glDepthBoundsEXT", glDepthBoundsEXT == NULL);
}

#endif /* GL_EXT_depth_bounds_test */

#ifdef GL_EXT_direct_state_access

static void _glewInfo_GL_EXT_direct_state_access (void)
{
  glewPrintExt("GL_EXT_direct_state_access", GLEW_EXT_direct_state_access, glewIsSupported("GL_EXT_direct_state_access"), glewGetExtension("GL_EXT_direct_state_access"));

  glewInfoFunc("glBindMultiTextureEXT", glBindMultiTextureEXT == NULL);
  glewInfoFunc("glCheckNamedFramebufferStatusEXT", glCheckNamedFramebufferStatusEXT == NULL);
  glewInfoFunc("glClientAttribDefaultEXT", glClientAttribDefaultEXT == NULL);
  glewInfoFunc("glCompressedMultiTexImage1DEXT", glCompressedMultiTexImage1DEXT == NULL);
  glewInfoFunc("glCompressedMultiTexImage2DEXT", glCompressedMultiTexImage2DEXT == NULL);
  glewInfoFunc("glCompressedMultiTexImage3DEXT", glCompressedMultiTexImage3DEXT == NULL);
  glewInfoFunc("glCompressedMultiTexSubImage1DEXT", glCompressedMultiTexSubImage1DEXT == NULL);
  glewInfoFunc("glCompressedMultiTexSubImage2DEXT", glCompressedMultiTexSubImage2DEXT == NULL);
  glewInfoFunc("glCompressedMultiTexSubImage3DEXT", glCompressedMultiTexSubImage3DEXT == NULL);
  glewInfoFunc("glCompressedTextureImage1DEXT", glCompressedTextureImage1DEXT == NULL);
  glewInfoFunc("glCompressedTextureImage2DEXT", glCompressedTextureImage2DEXT == NULL);
  glewInfoFunc("glCompressedTextureImage3DEXT", glCompressedTextureImage3DEXT == NULL);
  glewInfoFunc("glCompressedTextureSubImage1DEXT", glCompressedTextureSubImage1DEXT == NULL);
  glewInfoFunc("glCompressedTextureSubImage2DEXT", glCompressedTextureSubImage2DEXT == NULL);
  glewInfoFunc("glCompressedTextureSubImage3DEXT", glCompressedTextureSubImage3DEXT == NULL);
  glewInfoFunc("glCopyMultiTexImage1DEXT", glCopyMultiTexImage1DEXT == NULL);
  glewInfoFunc("glCopyMultiTexImage2DEXT", glCopyMultiTexImage2DEXT == NULL);
  glewInfoFunc("glCopyMultiTexSubImage1DEXT", glCopyMultiTexSubImage1DEXT == NULL);
  glewInfoFunc("glCopyMultiTexSubImage2DEXT", glCopyMultiTexSubImage2DEXT == NULL);
  glewInfoFunc("glCopyMultiTexSubImage3DEXT", glCopyMultiTexSubImage3DEXT == NULL);
  glewInfoFunc("glCopyTextureImage1DEXT", glCopyTextureImage1DEXT == NULL);
  glewInfoFunc("glCopyTextureImage2DEXT", glCopyTextureImage2DEXT == NULL);
  glewInfoFunc("glCopyTextureSubImage1DEXT", glCopyTextureSubImage1DEXT == NULL);
  glewInfoFunc("glCopyTextureSubImage2DEXT", glCopyTextureSubImage2DEXT == NULL);
  glewInfoFunc("glCopyTextureSubImage3DEXT", glCopyTextureSubImage3DEXT == NULL);
  glewInfoFunc("glDisableClientStateIndexedEXT", glDisableClientStateIndexedEXT == NULL);
  glewInfoFunc("glDisableClientStateiEXT", glDisableClientStateiEXT == NULL);
  glewInfoFunc("glDisableVertexArrayAttribEXT", glDisableVertexArrayAttribEXT == NULL);
  glewInfoFunc("glDisableVertexArrayEXT", glDisableVertexArrayEXT == NULL);
  glewInfoFunc("glEnableClientStateIndexedEXT", glEnableClientStateIndexedEXT == NULL);
  glewInfoFunc("glEnableClientStateiEXT", glEnableClientStateiEXT == NULL);
  glewInfoFunc("glEnableVertexArrayAttribEXT", glEnableVertexArrayAttribEXT == NULL);
  glewInfoFunc("glEnableVertexArrayEXT", glEnableVertexArrayEXT == NULL);
  glewInfoFunc("glFlushMappedNamedBufferRangeEXT", glFlushMappedNamedBufferRangeEXT == NULL);
  glewInfoFunc("glFramebufferDrawBufferEXT", glFramebufferDrawBufferEXT == NULL);
  glewInfoFunc("glFramebufferDrawBuffersEXT", glFramebufferDrawBuffersEXT == NULL);
  glewInfoFunc("glFramebufferReadBufferEXT", glFramebufferReadBufferEXT == NULL);
  glewInfoFunc("glGenerateMultiTexMipmapEXT", glGenerateMultiTexMipmapEXT == NULL);
  glewInfoFunc("glGenerateTextureMipmapEXT", glGenerateTextureMipmapEXT == NULL);
  glewInfoFunc("glGetCompressedMultiTexImageEXT", glGetCompressedMultiTexImageEXT == NULL);
  glewInfoFunc("glGetCompressedTextureImageEXT", glGetCompressedTextureImageEXT == NULL);
  glewInfoFunc("glGetDoubleIndexedvEXT", glGetDoubleIndexedvEXT == NULL);
  glewInfoFunc("glGetDoublei_vEXT", glGetDoublei_vEXT == NULL);
  glewInfoFunc("glGetFloatIndexedvEXT", glGetFloatIndexedvEXT == NULL);
  glewInfoFunc("glGetFloati_vEXT", glGetFloati_vEXT == NULL);
  glewInfoFunc("glGetFramebufferParameterivEXT", glGetFramebufferParameterivEXT == NULL);
  glewInfoFunc("glGetMultiTexEnvfvEXT", glGetMultiTexEnvfvEXT == NULL);
  glewInfoFunc("glGetMultiTexEnvivEXT", glGetMultiTexEnvivEXT == NULL);
  glewInfoFunc("glGetMultiTexGendvEXT", glGetMultiTexGendvEXT == NULL);
  glewInfoFunc("glGetMultiTexGenfvEXT", glGetMultiTexGenfvEXT == NULL);
  glewInfoFunc("glGetMultiTexGenivEXT", glGetMultiTexGenivEXT == NULL);
  glewInfoFunc("glGetMultiTexImageEXT", glGetMultiTexImageEXT == NULL);
  glewInfoFunc("glGetMultiTexLevelParameterfvEXT", glGetMultiTexLevelParameterfvEXT == NULL);
  glewInfoFunc("glGetMultiTexLevelParameterivEXT", glGetMultiTexLevelParameterivEXT == NULL);
  glewInfoFunc("glGetMultiTexParameterIivEXT", glGetMultiTexParameterIivEXT == NULL);
  glewInfoFunc("glGetMultiTexParameterIuivEXT", glGetMultiTexParameterIuivEXT == NULL);
  glewInfoFunc("glGetMultiTexParameterfvEXT", glGetMultiTexParameterfvEXT == NULL);
  glewInfoFunc("glGetMultiTexParameterivEXT", glGetMultiTexParameterivEXT == NULL);
  glewInfoFunc("glGetNamedBufferParameterivEXT", glGetNamedBufferParameterivEXT == NULL);
  glewInfoFunc("glGetNamedBufferPointervEXT", glGetNamedBufferPointervEXT == NULL);
  glewInfoFunc("glGetNamedBufferSubDataEXT", glGetNamedBufferSubDataEXT == NULL);
  glewInfoFunc("glGetNamedFramebufferAttachmentParameterivEXT", glGetNamedFramebufferAttachmentParameterivEXT == NULL);
  glewInfoFunc("glGetNamedProgramLocalParameterIivEXT", glGetNamedProgramLocalParameterIivEXT == NULL);
  glewInfoFunc("glGetNamedProgramLocalParameterIuivEXT", glGetNamedProgramLocalParameterIuivEXT == NULL);
  glewInfoFunc("glGetNamedProgramLocalParameterdvEXT", glGetNamedProgramLocalParameterdvEXT == NULL);
  glewInfoFunc("glGetNamedProgramLocalParameterfvEXT", glGetNamedProgramLocalParameterfvEXT == NULL);
  glewInfoFunc("glGetNamedProgramStringEXT", glGetNamedProgramStringEXT == NULL);
  glewInfoFunc("glGetNamedProgramivEXT", glGetNamedProgramivEXT == NULL);
  glewInfoFunc("glGetNamedRenderbufferParameterivEXT", glGetNamedRenderbufferParameterivEXT == NULL);
  glewInfoFunc("glGetPointerIndexedvEXT", glGetPointerIndexedvEXT == NULL);
  glewInfoFunc("glGetPointeri_vEXT", glGetPointeri_vEXT == NULL);
  glewInfoFunc("glGetTextureImageEXT", glGetTextureImageEXT == NULL);
  glewInfoFunc("glGetTextureLevelParameterfvEXT", glGetTextureLevelParameterfvEXT == NULL);
  glewInfoFunc("glGetTextureLevelParameterivEXT", glGetTextureLevelParameterivEXT == NULL);
  glewInfoFunc("glGetTextureParameterIivEXT", glGetTextureParameterIivEXT == NULL);
  glewInfoFunc("glGetTextureParameterIuivEXT", glGetTextureParameterIuivEXT == NULL);
  glewInfoFunc("glGetTextureParameterfvEXT", glGetTextureParameterfvEXT == NULL);
  glewInfoFunc("glGetTextureParameterivEXT", glGetTextureParameterivEXT == NULL);
  glewInfoFunc("glGetVertexArrayIntegeri_vEXT", glGetVertexArrayIntegeri_vEXT == NULL);
  glewInfoFunc("glGetVertexArrayIntegervEXT", glGetVertexArrayIntegervEXT == NULL);
  glewInfoFunc("glGetVertexArrayPointeri_vEXT", glGetVertexArrayPointeri_vEXT == NULL);
  glewInfoFunc("glGetVertexArrayPointervEXT", glGetVertexArrayPointervEXT == NULL);
  glewInfoFunc("glMapNamedBufferEXT", glMapNamedBufferEXT == NULL);
  glewInfoFunc("glMapNamedBufferRangeEXT", glMapNamedBufferRangeEXT == NULL);
  glewInfoFunc("glMatrixFrustumEXT", glMatrixFrustumEXT == NULL);
  glewInfoFunc("glMatrixLoadIdentityEXT", glMatrixLoadIdentityEXT == NULL);
  glewInfoFunc("glMatrixLoadTransposedEXT", glMatrixLoadTransposedEXT == NULL);
  glewInfoFunc("glMatrixLoadTransposefEXT", glMatrixLoadTransposefEXT == NULL);
  glewInfoFunc("glMatrixLoaddEXT", glMatrixLoaddEXT == NULL);
  glewInfoFunc("glMatrixLoadfEXT", glMatrixLoadfEXT == NULL);
  glewInfoFunc("glMatrixMultTransposedEXT", glMatrixMultTransposedEXT == NULL);
  glewInfoFunc("glMatrixMultTransposefEXT", glMatrixMultTransposefEXT == NULL);
  glewInfoFunc("glMatrixMultdEXT", glMatrixMultdEXT == NULL);
  glewInfoFunc("glMatrixMultfEXT", glMatrixMultfEXT == NULL);
  glewInfoFunc("glMatrixOrthoEXT", glMatrixOrthoEXT == NULL);
  glewInfoFunc("glMatrixPopEXT", glMatrixPopEXT == NULL);
  glewInfoFunc("glMatrixPushEXT", glMatrixPushEXT == NULL);
  glewInfoFunc("glMatrixRotatedEXT", glMatrixRotatedEXT == NULL);
  glewInfoFunc("glMatrixRotatefEXT", glMatrixRotatefEXT == NULL);
  glewInfoFunc("glMatrixScaledEXT", glMatrixScaledEXT == NULL);
  glewInfoFunc("glMatrixScalefEXT", glMatrixScalefEXT == NULL);
  glewInfoFunc("glMatrixTranslatedEXT", glMatrixTranslatedEXT == NULL);
  glewInfoFunc("glMatrixTranslatefEXT", glMatrixTranslatefEXT == NULL);
  glewInfoFunc("glMultiTexBufferEXT", glMultiTexBufferEXT == NULL);
  glewInfoFunc("glMultiTexCoordPointerEXT", glMultiTexCoordPointerEXT == NULL);
  glewInfoFunc("glMultiTexEnvfEXT", glMultiTexEnvfEXT == NULL);
  glewInfoFunc("glMultiTexEnvfvEXT", glMultiTexEnvfvEXT == NULL);
  glewInfoFunc("glMultiTexEnviEXT", glMultiTexEnviEXT == NULL);
  glewInfoFunc("glMultiTexEnvivEXT", glMultiTexEnvivEXT == NULL);
  glewInfoFunc("glMultiTexGendEXT", glMultiTexGendEXT == NULL);
  glewInfoFunc("glMultiTexGendvEXT", glMultiTexGendvEXT == NULL);
  glewInfoFunc("glMultiTexGenfEXT", glMultiTexGenfEXT == NULL);
  glewInfoFunc("glMultiTexGenfvEXT", glMultiTexGenfvEXT == NULL);
  glewInfoFunc("glMultiTexGeniEXT", glMultiTexGeniEXT == NULL);
  glewInfoFunc("glMultiTexGenivEXT", glMultiTexGenivEXT == NULL);
  glewInfoFunc("glMultiTexImage1DEXT", glMultiTexImage1DEXT == NULL);
  glewInfoFunc("glMultiTexImage2DEXT", glMultiTexImage2DEXT == NULL);
  glewInfoFunc("glMultiTexImage3DEXT", glMultiTexImage3DEXT == NULL);
  glewInfoFunc("glMultiTexParameterIivEXT", glMultiTexParameterIivEXT == NULL);
  glewInfoFunc("glMultiTexParameterIuivEXT", glMultiTexParameterIuivEXT == NULL);
  glewInfoFunc("glMultiTexParameterfEXT", glMultiTexParameterfEXT == NULL);
  glewInfoFunc("glMultiTexParameterfvEXT", glMultiTexParameterfvEXT == NULL);
  glewInfoFunc("glMultiTexParameteriEXT", glMultiTexParameteriEXT == NULL);
  glewInfoFunc("glMultiTexParameterivEXT", glMultiTexParameterivEXT == NULL);
  glewInfoFunc("glMultiTexRenderbufferEXT", glMultiTexRenderbufferEXT == NULL);
  glewInfoFunc("glMultiTexSubImage1DEXT", glMultiTexSubImage1DEXT == NULL);
  glewInfoFunc("glMultiTexSubImage2DEXT", glMultiTexSubImage2DEXT == NULL);
  glewInfoFunc("glMultiTexSubImage3DEXT", glMultiTexSubImage3DEXT == NULL);
  glewInfoFunc("glNamedBufferDataEXT", glNamedBufferDataEXT == NULL);
  glewInfoFunc("glNamedBufferSubDataEXT", glNamedBufferSubDataEXT == NULL);
  glewInfoFunc("glNamedCopyBufferSubDataEXT", glNamedCopyBufferSubDataEXT == NULL);
  glewInfoFunc("glNamedFramebufferRenderbufferEXT", glNamedFramebufferRenderbufferEXT == NULL);
  glewInfoFunc("glNamedFramebufferTexture1DEXT", glNamedFramebufferTexture1DEXT == NULL);
  glewInfoFunc("glNamedFramebufferTexture2DEXT", glNamedFramebufferTexture2DEXT == NULL);
  glewInfoFunc("glNamedFramebufferTexture3DEXT", glNamedFramebufferTexture3DEXT == NULL);
  glewInfoFunc("glNamedFramebufferTextureEXT", glNamedFramebufferTextureEXT == NULL);
  glewInfoFunc("glNamedFramebufferTextureFaceEXT", glNamedFramebufferTextureFaceEXT == NULL);
  glewInfoFunc("glNamedFramebufferTextureLayerEXT", glNamedFramebufferTextureLayerEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameter4dEXT", glNamedProgramLocalParameter4dEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameter4dvEXT", glNamedProgramLocalParameter4dvEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameter4fEXT", glNamedProgramLocalParameter4fEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameter4fvEXT", glNamedProgramLocalParameter4fvEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameterI4iEXT", glNamedProgramLocalParameterI4iEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameterI4ivEXT", glNamedProgramLocalParameterI4ivEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameterI4uiEXT", glNamedProgramLocalParameterI4uiEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameterI4uivEXT", glNamedProgramLocalParameterI4uivEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParameters4fvEXT", glNamedProgramLocalParameters4fvEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParametersI4ivEXT", glNamedProgramLocalParametersI4ivEXT == NULL);
  glewInfoFunc("glNamedProgramLocalParametersI4uivEXT", glNamedProgramLocalParametersI4uivEXT == NULL);
  glewInfoFunc("glNamedProgramStringEXT", glNamedProgramStringEXT == NULL);
  glewInfoFunc("glNamedRenderbufferStorageEXT", glNamedRenderbufferStorageEXT == NULL);
  glewInfoFunc("glNamedRenderbufferStorageMultisampleCoverageEXT", glNamedRenderbufferStorageMultisampleCoverageEXT == NULL);
  glewInfoFunc("glNamedRenderbufferStorageMultisampleEXT", glNamedRenderbufferStorageMultisampleEXT == NULL);
  glewInfoFunc("glProgramUniform1fEXT", glProgramUniform1fEXT == NULL);
  glewInfoFunc("glProgramUniform1fvEXT", glProgramUniform1fvEXT == NULL);
  glewInfoFunc("glProgramUniform1iEXT", glProgramUniform1iEXT == NULL);
  glewInfoFunc("glProgramUniform1ivEXT", glProgramUniform1ivEXT == NULL);
  glewInfoFunc("glProgramUniform1uiEXT", glProgramUniform1uiEXT == NULL);
  glewInfoFunc("glProgramUniform1uivEXT", glProgramUniform1uivEXT == NULL);
  glewInfoFunc("glProgramUniform2fEXT", glProgramUniform2fEXT == NULL);
  glewInfoFunc("glProgramUniform2fvEXT", glProgramUniform2fvEXT == NULL);
  glewInfoFunc("glProgramUniform2iEXT", glProgramUniform2iEXT == NULL);
  glewInfoFunc("glProgramUniform2ivEXT", glProgramUniform2ivEXT == NULL);
  glewInfoFunc("glProgramUniform2uiEXT", glProgramUniform2uiEXT == NULL);
  glewInfoFunc("glProgramUniform2uivEXT", glProgramUniform2uivEXT == NULL);
  glewInfoFunc("glProgramUniform3fEXT", glProgramUniform3fEXT == NULL);
  glewInfoFunc("glProgramUniform3fvEXT", glProgramUniform3fvEXT == NULL);
  glewInfoFunc("glProgramUniform3iEXT", glProgramUniform3iEXT == NULL);
  glewInfoFunc("glProgramUniform3ivEXT", glProgramUniform3ivEXT == NULL);
  glewInfoFunc("glProgramUniform3uiEXT", glProgramUniform3uiEXT == NULL);
  glewInfoFunc("glProgramUniform3uivEXT", glProgramUniform3uivEXT == NULL);
  glewInfoFunc("glProgramUniform4fEXT", glProgramUniform4fEXT == NULL);
  glewInfoFunc("glProgramUniform4fvEXT", glProgramUniform4fvEXT == NULL);
  glewInfoFunc("glProgramUniform4iEXT", glProgramUniform4iEXT == NULL);
  glewInfoFunc("glProgramUniform4ivEXT", glProgramUniform4ivEXT == NULL);
  glewInfoFunc("glProgramUniform4uiEXT", glProgramUniform4uiEXT == NULL);
  glewInfoFunc("glProgramUniform4uivEXT", glProgramUniform4uivEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix2fvEXT", glProgramUniformMatrix2fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix2x3fvEXT", glProgramUniformMatrix2x3fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix2x4fvEXT", glProgramUniformMatrix2x4fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix3fvEXT", glProgramUniformMatrix3fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix3x2fvEXT", glProgramUniformMatrix3x2fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix3x4fvEXT", glProgramUniformMatrix3x4fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix4fvEXT", glProgramUniformMatrix4fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix4x2fvEXT", glProgramUniformMatrix4x2fvEXT == NULL);
  glewInfoFunc("glProgramUniformMatrix4x3fvEXT", glProgramUniformMatrix4x3fvEXT == NULL);
  glewInfoFunc("glPushClientAttribDefaultEXT", glPushClientAttribDefaultEXT == NULL);
  glewInfoFunc("glTextureBufferEXT", glTextureBufferEXT == NULL);
  glewInfoFunc("glTextureImage1DEXT", glTextureImage1DEXT == NULL);
  glewInfoFunc("glTextureImage2DEXT", glTextureImage2DEXT == NULL);
  glewInfoFunc("glTextureImage3DEXT", glTextureImage3DEXT == NULL);
  glewInfoFunc("glTextureParameterIivEXT", glTextureParameterIivEXT == NULL);
  glewInfoFunc("glTextureParameterIuivEXT", glTextureParameterIuivEXT == NULL);
  glewInfoFunc("glTextureParameterfEXT", glTextureParameterfEXT == NULL);
  glewInfoFunc("glTextureParameterfvEXT", glTextureParameterfvEXT == NULL);
  glewInfoFunc("glTextureParameteriEXT", glTextureParameteriEXT == NULL);
  glewInfoFunc("glTextureParameterivEXT", glTextureParameterivEXT == NULL);
  glewInfoFunc("glTextureRenderbufferEXT", glTextureRenderbufferEXT == NULL);
  glewInfoFunc("glTextureSubImage1DEXT", glTextureSubImage1DEXT == NULL);
  glewInfoFunc("glTextureSubImage2DEXT", glTextureSubImage2DEXT == NULL);
  glewInfoFunc("glTextureSubImage3DEXT", glTextureSubImage3DEXT == NULL);
  glewInfoFunc("glUnmapNamedBufferEXT", glUnmapNamedBufferEXT == NULL);
  glewInfoFunc("glVertexArrayColorOffsetEXT", glVertexArrayColorOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayEdgeFlagOffsetEXT", glVertexArrayEdgeFlagOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayFogCoordOffsetEXT", glVertexArrayFogCoordOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayIndexOffsetEXT", glVertexArrayIndexOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayMultiTexCoordOffsetEXT", glVertexArrayMultiTexCoordOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayNormalOffsetEXT", glVertexArrayNormalOffsetEXT == NULL);
  glewInfoFunc("glVertexArraySecondaryColorOffsetEXT", glVertexArraySecondaryColorOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayTexCoordOffsetEXT", glVertexArrayTexCoordOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribDivisorEXT", glVertexArrayVertexAttribDivisorEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribIOffsetEXT", glVertexArrayVertexAttribIOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribOffsetEXT", glVertexArrayVertexAttribOffsetEXT == NULL);
  glewInfoFunc("glVertexArrayVertexOffsetEXT", glVertexArrayVertexOffsetEXT == NULL);
}

#endif /* GL_EXT_direct_state_access */

#ifdef GL_EXT_discard_framebuffer

static void _glewInfo_GL_EXT_discard_framebuffer (void)
{
  glewPrintExt("GL_EXT_discard_framebuffer", GLEW_EXT_discard_framebuffer, glewIsSupported("GL_EXT_discard_framebuffer"), glewGetExtension("GL_EXT_discard_framebuffer"));

  glewInfoFunc("glDiscardFramebufferEXT", glDiscardFramebufferEXT == NULL);
}

#endif /* GL_EXT_discard_framebuffer */

#ifdef GL_EXT_draw_buffers

static void _glewInfo_GL_EXT_draw_buffers (void)
{
  glewPrintExt("GL_EXT_draw_buffers", GLEW_EXT_draw_buffers, glewIsSupported("GL_EXT_draw_buffers"), glewGetExtension("GL_EXT_draw_buffers"));

  glewInfoFunc("glDrawBuffersEXT", glDrawBuffersEXT == NULL);
}

#endif /* GL_EXT_draw_buffers */

#ifdef GL_EXT_draw_buffers2

static void _glewInfo_GL_EXT_draw_buffers2 (void)
{
  glewPrintExt("GL_EXT_draw_buffers2", GLEW_EXT_draw_buffers2, glewIsSupported("GL_EXT_draw_buffers2"), glewGetExtension("GL_EXT_draw_buffers2"));

  glewInfoFunc("glColorMaskIndexedEXT", glColorMaskIndexedEXT == NULL);
  glewInfoFunc("glDisableIndexedEXT", glDisableIndexedEXT == NULL);
  glewInfoFunc("glEnableIndexedEXT", glEnableIndexedEXT == NULL);
  glewInfoFunc("glGetBooleanIndexedvEXT", glGetBooleanIndexedvEXT == NULL);
  glewInfoFunc("glGetIntegerIndexedvEXT", glGetIntegerIndexedvEXT == NULL);
  glewInfoFunc("glIsEnabledIndexedEXT", glIsEnabledIndexedEXT == NULL);
}

#endif /* GL_EXT_draw_buffers2 */

#ifdef GL_EXT_draw_buffers_indexed

static void _glewInfo_GL_EXT_draw_buffers_indexed (void)
{
  glewPrintExt("GL_EXT_draw_buffers_indexed", GLEW_EXT_draw_buffers_indexed, glewIsSupported("GL_EXT_draw_buffers_indexed"), glewGetExtension("GL_EXT_draw_buffers_indexed"));

  glewInfoFunc("glBlendEquationSeparateiEXT", glBlendEquationSeparateiEXT == NULL);
  glewInfoFunc("glBlendEquationiEXT", glBlendEquationiEXT == NULL);
  glewInfoFunc("glBlendFuncSeparateiEXT", glBlendFuncSeparateiEXT == NULL);
  glewInfoFunc("glBlendFunciEXT", glBlendFunciEXT == NULL);
  glewInfoFunc("glColorMaskiEXT", glColorMaskiEXT == NULL);
  glewInfoFunc("glDisableiEXT", glDisableiEXT == NULL);
  glewInfoFunc("glEnableiEXT", glEnableiEXT == NULL);
  glewInfoFunc("glIsEnablediEXT", glIsEnablediEXT == NULL);
}

#endif /* GL_EXT_draw_buffers_indexed */

#ifdef GL_EXT_draw_elements_base_vertex

static void _glewInfo_GL_EXT_draw_elements_base_vertex (void)
{
  glewPrintExt("GL_EXT_draw_elements_base_vertex", GLEW_EXT_draw_elements_base_vertex, glewIsSupported("GL_EXT_draw_elements_base_vertex"), glewGetExtension("GL_EXT_draw_elements_base_vertex"));

  glewInfoFunc("glDrawElementsBaseVertexEXT", glDrawElementsBaseVertexEXT == NULL);
  glewInfoFunc("glDrawElementsInstancedBaseVertexEXT", glDrawElementsInstancedBaseVertexEXT == NULL);
  glewInfoFunc("glDrawRangeElementsBaseVertexEXT", glDrawRangeElementsBaseVertexEXT == NULL);
  glewInfoFunc("glMultiDrawElementsBaseVertexEXT", glMultiDrawElementsBaseVertexEXT == NULL);
}

#endif /* GL_EXT_draw_elements_base_vertex */

#ifdef GL_EXT_draw_instanced

static void _glewInfo_GL_EXT_draw_instanced (void)
{
  glewPrintExt("GL_EXT_draw_instanced", GLEW_EXT_draw_instanced, glewIsSupported("GL_EXT_draw_instanced"), glewGetExtension("GL_EXT_draw_instanced"));

  glewInfoFunc("glDrawArraysInstancedEXT", glDrawArraysInstancedEXT == NULL);
  glewInfoFunc("glDrawElementsInstancedEXT", glDrawElementsInstancedEXT == NULL);
}

#endif /* GL_EXT_draw_instanced */

#ifdef GL_EXT_draw_range_elements

static void _glewInfo_GL_EXT_draw_range_elements (void)
{
  glewPrintExt("GL_EXT_draw_range_elements", GLEW_EXT_draw_range_elements, glewIsSupported("GL_EXT_draw_range_elements"), glewGetExtension("GL_EXT_draw_range_elements"));

  glewInfoFunc("glDrawRangeElementsEXT", glDrawRangeElementsEXT == NULL);
}

#endif /* GL_EXT_draw_range_elements */

#ifdef GL_EXT_external_buffer

static void _glewInfo_GL_EXT_external_buffer (void)
{
  glewPrintExt("GL_EXT_external_buffer", GLEW_EXT_external_buffer, glewIsSupported("GL_EXT_external_buffer"), glewGetExtension("GL_EXT_external_buffer"));

  glewInfoFunc("glBufferStorageExternalEXT", glBufferStorageExternalEXT == NULL);
  glewInfoFunc("glNamedBufferStorageExternalEXT", glNamedBufferStorageExternalEXT == NULL);
}

#endif /* GL_EXT_external_buffer */

#ifdef GL_EXT_float_blend

static void _glewInfo_GL_EXT_float_blend (void)
{
  glewPrintExt("GL_EXT_float_blend", GLEW_EXT_float_blend, glewIsSupported("GL_EXT_float_blend"), glewGetExtension("GL_EXT_float_blend"));
}

#endif /* GL_EXT_float_blend */

#ifdef GL_EXT_fog_coord

static void _glewInfo_GL_EXT_fog_coord (void)
{
  glewPrintExt("GL_EXT_fog_coord", GLEW_EXT_fog_coord, glewIsSupported("GL_EXT_fog_coord"), glewGetExtension("GL_EXT_fog_coord"));

  glewInfoFunc("glFogCoordPointerEXT", glFogCoordPointerEXT == NULL);
  glewInfoFunc("glFogCoorddEXT", glFogCoorddEXT == NULL);
  glewInfoFunc("glFogCoorddvEXT", glFogCoorddvEXT == NULL);
  glewInfoFunc("glFogCoordfEXT", glFogCoordfEXT == NULL);
  glewInfoFunc("glFogCoordfvEXT", glFogCoordfvEXT == NULL);
}

#endif /* GL_EXT_fog_coord */

#ifdef GL_EXT_frag_depth

static void _glewInfo_GL_EXT_frag_depth (void)
{
  glewPrintExt("GL_EXT_frag_depth", GLEW_EXT_frag_depth, glewIsSupported("GL_EXT_frag_depth"), glewGetExtension("GL_EXT_frag_depth"));
}

#endif /* GL_EXT_frag_depth */

#ifdef GL_EXT_fragment_lighting

static void _glewInfo_GL_EXT_fragment_lighting (void)
{
  glewPrintExt("GL_EXT_fragment_lighting", GLEW_EXT_fragment_lighting, glewIsSupported("GL_EXT_fragment_lighting"), glewGetExtension("GL_EXT_fragment_lighting"));

  glewInfoFunc("glFragmentColorMaterialEXT", glFragmentColorMaterialEXT == NULL);
  glewInfoFunc("glFragmentLightModelfEXT", glFragmentLightModelfEXT == NULL);
  glewInfoFunc("glFragmentLightModelfvEXT", glFragmentLightModelfvEXT == NULL);
  glewInfoFunc("glFragmentLightModeliEXT", glFragmentLightModeliEXT == NULL);
  glewInfoFunc("glFragmentLightModelivEXT", glFragmentLightModelivEXT == NULL);
  glewInfoFunc("glFragmentLightfEXT", glFragmentLightfEXT == NULL);
  glewInfoFunc("glFragmentLightfvEXT", glFragmentLightfvEXT == NULL);
  glewInfoFunc("glFragmentLightiEXT", glFragmentLightiEXT == NULL);
  glewInfoFunc("glFragmentLightivEXT", glFragmentLightivEXT == NULL);
  glewInfoFunc("glFragmentMaterialfEXT", glFragmentMaterialfEXT == NULL);
  glewInfoFunc("glFragmentMaterialfvEXT", glFragmentMaterialfvEXT == NULL);
  glewInfoFunc("glFragmentMaterialiEXT", glFragmentMaterialiEXT == NULL);
  glewInfoFunc("glFragmentMaterialivEXT", glFragmentMaterialivEXT == NULL);
  glewInfoFunc("glGetFragmentLightfvEXT", glGetFragmentLightfvEXT == NULL);
  glewInfoFunc("glGetFragmentLightivEXT", glGetFragmentLightivEXT == NULL);
  glewInfoFunc("glGetFragmentMaterialfvEXT", glGetFragmentMaterialfvEXT == NULL);
  glewInfoFunc("glGetFragmentMaterialivEXT", glGetFragmentMaterialivEXT == NULL);
  glewInfoFunc("glLightEnviEXT", glLightEnviEXT == NULL);
}

#endif /* GL_EXT_fragment_lighting */

#ifdef GL_EXT_framebuffer_blit

static void _glewInfo_GL_EXT_framebuffer_blit (void)
{
  glewPrintExt("GL_EXT_framebuffer_blit", GLEW_EXT_framebuffer_blit, glewIsSupported("GL_EXT_framebuffer_blit"), glewGetExtension("GL_EXT_framebuffer_blit"));

  glewInfoFunc("glBlitFramebufferEXT", glBlitFramebufferEXT == NULL);
}

#endif /* GL_EXT_framebuffer_blit */

#ifdef GL_EXT_framebuffer_multisample

static void _glewInfo_GL_EXT_framebuffer_multisample (void)
{
  glewPrintExt("GL_EXT_framebuffer_multisample", GLEW_EXT_framebuffer_multisample, glewIsSupported("GL_EXT_framebuffer_multisample"), glewGetExtension("GL_EXT_framebuffer_multisample"));

  glewInfoFunc("glRenderbufferStorageMultisampleEXT", glRenderbufferStorageMultisampleEXT == NULL);
}

#endif /* GL_EXT_framebuffer_multisample */

#ifdef GL_EXT_framebuffer_multisample_blit_scaled

static void _glewInfo_GL_EXT_framebuffer_multisample_blit_scaled (void)
{
  glewPrintExt("GL_EXT_framebuffer_multisample_blit_scaled", GLEW_EXT_framebuffer_multisample_blit_scaled, glewIsSupported("GL_EXT_framebuffer_multisample_blit_scaled"), glewGetExtension("GL_EXT_framebuffer_multisample_blit_scaled"));
}

#endif /* GL_EXT_framebuffer_multisample_blit_scaled */

#ifdef GL_EXT_framebuffer_object

static void _glewInfo_GL_EXT_framebuffer_object (void)
{
  glewPrintExt("GL_EXT_framebuffer_object", GLEW_EXT_framebuffer_object, glewIsSupported("GL_EXT_framebuffer_object"), glewGetExtension("GL_EXT_framebuffer_object"));

  glewInfoFunc("glBindFramebufferEXT", glBindFramebufferEXT == NULL);
  glewInfoFunc("glBindRenderbufferEXT", glBindRenderbufferEXT == NULL);
  glewInfoFunc("glCheckFramebufferStatusEXT", glCheckFramebufferStatusEXT == NULL);
  glewInfoFunc("glDeleteFramebuffersEXT", glDeleteFramebuffersEXT == NULL);
  glewInfoFunc("glDeleteRenderbuffersEXT", glDeleteRenderbuffersEXT == NULL);
  glewInfoFunc("glFramebufferRenderbufferEXT", glFramebufferRenderbufferEXT == NULL);
  glewInfoFunc("glFramebufferTexture1DEXT", glFramebufferTexture1DEXT == NULL);
  glewInfoFunc("glFramebufferTexture2DEXT", glFramebufferTexture2DEXT == NULL);
  glewInfoFunc("glFramebufferTexture3DEXT", glFramebufferTexture3DEXT == NULL);
  glewInfoFunc("glGenFramebuffersEXT", glGenFramebuffersEXT == NULL);
  glewInfoFunc("glGenRenderbuffersEXT", glGenRenderbuffersEXT == NULL);
  glewInfoFunc("glGenerateMipmapEXT", glGenerateMipmapEXT == NULL);
  glewInfoFunc("glGetFramebufferAttachmentParameterivEXT", glGetFramebufferAttachmentParameterivEXT == NULL);
  glewInfoFunc("glGetRenderbufferParameterivEXT", glGetRenderbufferParameterivEXT == NULL);
  glewInfoFunc("glIsFramebufferEXT", glIsFramebufferEXT == NULL);
  glewInfoFunc("glIsRenderbufferEXT", glIsRenderbufferEXT == NULL);
  glewInfoFunc("glRenderbufferStorageEXT", glRenderbufferStorageEXT == NULL);
}

#endif /* GL_EXT_framebuffer_object */

#ifdef GL_EXT_framebuffer_sRGB

static void _glewInfo_GL_EXT_framebuffer_sRGB (void)
{
  glewPrintExt("GL_EXT_framebuffer_sRGB", GLEW_EXT_framebuffer_sRGB, glewIsSupported("GL_EXT_framebuffer_sRGB"), glewGetExtension("GL_EXT_framebuffer_sRGB"));
}

#endif /* GL_EXT_framebuffer_sRGB */

#ifdef GL_EXT_geometry_point_size

static void _glewInfo_GL_EXT_geometry_point_size (void)
{
  glewPrintExt("GL_EXT_geometry_point_size", GLEW_EXT_geometry_point_size, glewIsSupported("GL_EXT_geometry_point_size"), glewGetExtension("GL_EXT_geometry_point_size"));
}

#endif /* GL_EXT_geometry_point_size */

#ifdef GL_EXT_geometry_shader

static void _glewInfo_GL_EXT_geometry_shader (void)
{
  glewPrintExt("GL_EXT_geometry_shader", GLEW_EXT_geometry_shader, glewIsSupported("GL_EXT_geometry_shader"), glewGetExtension("GL_EXT_geometry_shader"));
}

#endif /* GL_EXT_geometry_shader */

#ifdef GL_EXT_geometry_shader4

static void _glewInfo_GL_EXT_geometry_shader4 (void)
{
  glewPrintExt("GL_EXT_geometry_shader4", GLEW_EXT_geometry_shader4, glewIsSupported("GL_EXT_geometry_shader4"), glewGetExtension("GL_EXT_geometry_shader4"));

  glewInfoFunc("glFramebufferTextureEXT", glFramebufferTextureEXT == NULL);
  glewInfoFunc("glFramebufferTextureFaceEXT", glFramebufferTextureFaceEXT == NULL);
  glewInfoFunc("glProgramParameteriEXT", glProgramParameteriEXT == NULL);
}

#endif /* GL_EXT_geometry_shader4 */

#ifdef GL_EXT_gpu_program_parameters

static void _glewInfo_GL_EXT_gpu_program_parameters (void)
{
  glewPrintExt("GL_EXT_gpu_program_parameters", GLEW_EXT_gpu_program_parameters, glewIsSupported("GL_EXT_gpu_program_parameters"), glewGetExtension("GL_EXT_gpu_program_parameters"));

  glewInfoFunc("glProgramEnvParameters4fvEXT", glProgramEnvParameters4fvEXT == NULL);
  glewInfoFunc("glProgramLocalParameters4fvEXT", glProgramLocalParameters4fvEXT == NULL);
}

#endif /* GL_EXT_gpu_program_parameters */

#ifdef GL_EXT_gpu_shader4

static void _glewInfo_GL_EXT_gpu_shader4 (void)
{
  glewPrintExt("GL_EXT_gpu_shader4", GLEW_EXT_gpu_shader4, glewIsSupported("GL_EXT_gpu_shader4"), glewGetExtension("GL_EXT_gpu_shader4"));

  glewInfoFunc("glBindFragDataLocationEXT", glBindFragDataLocationEXT == NULL);
  glewInfoFunc("glGetFragDataLocationEXT", glGetFragDataLocationEXT == NULL);
  glewInfoFunc("glGetUniformuivEXT", glGetUniformuivEXT == NULL);
  glewInfoFunc("glGetVertexAttribIivEXT", glGetVertexAttribIivEXT == NULL);
  glewInfoFunc("glGetVertexAttribIuivEXT", glGetVertexAttribIuivEXT == NULL);
  glewInfoFunc("glUniform1uiEXT", glUniform1uiEXT == NULL);
  glewInfoFunc("glUniform1uivEXT", glUniform1uivEXT == NULL);
  glewInfoFunc("glUniform2uiEXT", glUniform2uiEXT == NULL);
  glewInfoFunc("glUniform2uivEXT", glUniform2uivEXT == NULL);
  glewInfoFunc("glUniform3uiEXT", glUniform3uiEXT == NULL);
  glewInfoFunc("glUniform3uivEXT", glUniform3uivEXT == NULL);
  glewInfoFunc("glUniform4uiEXT", glUniform4uiEXT == NULL);
  glewInfoFunc("glUniform4uivEXT", glUniform4uivEXT == NULL);
  glewInfoFunc("glVertexAttribI1iEXT", glVertexAttribI1iEXT == NULL);
  glewInfoFunc("glVertexAttribI1ivEXT", glVertexAttribI1ivEXT == NULL);
  glewInfoFunc("glVertexAttribI1uiEXT", glVertexAttribI1uiEXT == NULL);
  glewInfoFunc("glVertexAttribI1uivEXT", glVertexAttribI1uivEXT == NULL);
  glewInfoFunc("glVertexAttribI2iEXT", glVertexAttribI2iEXT == NULL);
  glewInfoFunc("glVertexAttribI2ivEXT", glVertexAttribI2ivEXT == NULL);
  glewInfoFunc("glVertexAttribI2uiEXT", glVertexAttribI2uiEXT == NULL);
  glewInfoFunc("glVertexAttribI2uivEXT", glVertexAttribI2uivEXT == NULL);
  glewInfoFunc("glVertexAttribI3iEXT", glVertexAttribI3iEXT == NULL);
  glewInfoFunc("glVertexAttribI3ivEXT", glVertexAttribI3ivEXT == NULL);
  glewInfoFunc("glVertexAttribI3uiEXT", glVertexAttribI3uiEXT == NULL);
  glewInfoFunc("glVertexAttribI3uivEXT", glVertexAttribI3uivEXT == NULL);
  glewInfoFunc("glVertexAttribI4bvEXT", glVertexAttribI4bvEXT == NULL);
  glewInfoFunc("glVertexAttribI4iEXT", glVertexAttribI4iEXT == NULL);
  glewInfoFunc("glVertexAttribI4ivEXT", glVertexAttribI4ivEXT == NULL);
  glewInfoFunc("glVertexAttribI4svEXT", glVertexAttribI4svEXT == NULL);
  glewInfoFunc("glVertexAttribI4ubvEXT", glVertexAttribI4ubvEXT == NULL);
  glewInfoFunc("glVertexAttribI4uiEXT", glVertexAttribI4uiEXT == NULL);
  glewInfoFunc("glVertexAttribI4uivEXT", glVertexAttribI4uivEXT == NULL);
  glewInfoFunc("glVertexAttribI4usvEXT", glVertexAttribI4usvEXT == NULL);
  glewInfoFunc("glVertexAttribIPointerEXT", glVertexAttribIPointerEXT == NULL);
}

#endif /* GL_EXT_gpu_shader4 */

#ifdef GL_EXT_gpu_shader5

static void _glewInfo_GL_EXT_gpu_shader5 (void)
{
  glewPrintExt("GL_EXT_gpu_shader5", GLEW_EXT_gpu_shader5, glewIsSupported("GL_EXT_gpu_shader5"), glewGetExtension("GL_EXT_gpu_shader5"));
}

#endif /* GL_EXT_gpu_shader5 */

#ifdef GL_EXT_histogram

static void _glewInfo_GL_EXT_histogram (void)
{
  glewPrintExt("GL_EXT_histogram", GLEW_EXT_histogram, glewIsSupported("GL_EXT_histogram"), glewGetExtension("GL_EXT_histogram"));

  glewInfoFunc("glGetHistogramEXT", glGetHistogramEXT == NULL);
  glewInfoFunc("glGetHistogramParameterfvEXT", glGetHistogramParameterfvEXT == NULL);
  glewInfoFunc("glGetHistogramParameterivEXT", glGetHistogramParameterivEXT == NULL);
  glewInfoFunc("glGetMinmaxEXT", glGetMinmaxEXT == NULL);
  glewInfoFunc("glGetMinmaxParameterfvEXT", glGetMinmaxParameterfvEXT == NULL);
  glewInfoFunc("glGetMinmaxParameterivEXT", glGetMinmaxParameterivEXT == NULL);
  glewInfoFunc("glHistogramEXT", glHistogramEXT == NULL);
  glewInfoFunc("glMinmaxEXT", glMinmaxEXT == NULL);
  glewInfoFunc("glResetHistogramEXT", glResetHistogramEXT == NULL);
  glewInfoFunc("glResetMinmaxEXT", glResetMinmaxEXT == NULL);
}

#endif /* GL_EXT_histogram */

#ifdef GL_EXT_index_array_formats

static void _glewInfo_GL_EXT_index_array_formats (void)
{
  glewPrintExt("GL_EXT_index_array_formats", GLEW_EXT_index_array_formats, glewIsSupported("GL_EXT_index_array_formats"), glewGetExtension("GL_EXT_index_array_formats"));
}

#endif /* GL_EXT_index_array_formats */

#ifdef GL_EXT_index_func

static void _glewInfo_GL_EXT_index_func (void)
{
  glewPrintExt("GL_EXT_index_func", GLEW_EXT_index_func, glewIsSupported("GL_EXT_index_func"), glewGetExtension("GL_EXT_index_func"));

  glewInfoFunc("glIndexFuncEXT", glIndexFuncEXT == NULL);
}

#endif /* GL_EXT_index_func */

#ifdef GL_EXT_index_material

static void _glewInfo_GL_EXT_index_material (void)
{
  glewPrintExt("GL_EXT_index_material", GLEW_EXT_index_material, glewIsSupported("GL_EXT_index_material"), glewGetExtension("GL_EXT_index_material"));

  glewInfoFunc("glIndexMaterialEXT", glIndexMaterialEXT == NULL);
}

#endif /* GL_EXT_index_material */

#ifdef GL_EXT_index_texture

static void _glewInfo_GL_EXT_index_texture (void)
{
  glewPrintExt("GL_EXT_index_texture", GLEW_EXT_index_texture, glewIsSupported("GL_EXT_index_texture"), glewGetExtension("GL_EXT_index_texture"));
}

#endif /* GL_EXT_index_texture */

#ifdef GL_EXT_instanced_arrays

static void _glewInfo_GL_EXT_instanced_arrays (void)
{
  glewPrintExt("GL_EXT_instanced_arrays", GLEW_EXT_instanced_arrays, glewIsSupported("GL_EXT_instanced_arrays"), glewGetExtension("GL_EXT_instanced_arrays"));

  glewInfoFunc("glVertexAttribDivisorEXT", glVertexAttribDivisorEXT == NULL);
}

#endif /* GL_EXT_instanced_arrays */

#ifdef GL_EXT_light_texture

static void _glewInfo_GL_EXT_light_texture (void)
{
  glewPrintExt("GL_EXT_light_texture", GLEW_EXT_light_texture, glewIsSupported("GL_EXT_light_texture"), glewGetExtension("GL_EXT_light_texture"));

  glewInfoFunc("glApplyTextureEXT", glApplyTextureEXT == NULL);
  glewInfoFunc("glTextureLightEXT", glTextureLightEXT == NULL);
  glewInfoFunc("glTextureMaterialEXT", glTextureMaterialEXT == NULL);
}

#endif /* GL_EXT_light_texture */

#ifdef GL_EXT_map_buffer_range

static void _glewInfo_GL_EXT_map_buffer_range (void)
{
  glewPrintExt("GL_EXT_map_buffer_range", GLEW_EXT_map_buffer_range, glewIsSupported("GL_EXT_map_buffer_range"), glewGetExtension("GL_EXT_map_buffer_range"));

  glewInfoFunc("glFlushMappedBufferRangeEXT", glFlushMappedBufferRangeEXT == NULL);
  glewInfoFunc("glMapBufferRangeEXT", glMapBufferRangeEXT == NULL);
}

#endif /* GL_EXT_map_buffer_range */

#ifdef GL_EXT_memory_object

static void _glewInfo_GL_EXT_memory_object (void)
{
  glewPrintExt("GL_EXT_memory_object", GLEW_EXT_memory_object, glewIsSupported("GL_EXT_memory_object"), glewGetExtension("GL_EXT_memory_object"));

  glewInfoFunc("glBufferStorageMemEXT", glBufferStorageMemEXT == NULL);
  glewInfoFunc("glCreateMemoryObjectsEXT", glCreateMemoryObjectsEXT == NULL);
  glewInfoFunc("glDeleteMemoryObjectsEXT", glDeleteMemoryObjectsEXT == NULL);
  glewInfoFunc("glGetMemoryObjectParameterivEXT", glGetMemoryObjectParameterivEXT == NULL);
  glewInfoFunc("glGetUnsignedBytei_vEXT", glGetUnsignedBytei_vEXT == NULL);
  glewInfoFunc("glGetUnsignedBytevEXT", glGetUnsignedBytevEXT == NULL);
  glewInfoFunc("glIsMemoryObjectEXT", glIsMemoryObjectEXT == NULL);
  glewInfoFunc("glMemoryObjectParameterivEXT", glMemoryObjectParameterivEXT == NULL);
  glewInfoFunc("glNamedBufferStorageMemEXT", glNamedBufferStorageMemEXT == NULL);
  glewInfoFunc("glTexStorageMem1DEXT", glTexStorageMem1DEXT == NULL);
  glewInfoFunc("glTexStorageMem2DEXT", glTexStorageMem2DEXT == NULL);
  glewInfoFunc("glTexStorageMem2DMultisampleEXT", glTexStorageMem2DMultisampleEXT == NULL);
  glewInfoFunc("glTexStorageMem3DEXT", glTexStorageMem3DEXT == NULL);
  glewInfoFunc("glTexStorageMem3DMultisampleEXT", glTexStorageMem3DMultisampleEXT == NULL);
  glewInfoFunc("glTextureStorageMem1DEXT", glTextureStorageMem1DEXT == NULL);
  glewInfoFunc("glTextureStorageMem2DEXT", glTextureStorageMem2DEXT == NULL);
  glewInfoFunc("glTextureStorageMem2DMultisampleEXT", glTextureStorageMem2DMultisampleEXT == NULL);
  glewInfoFunc("glTextureStorageMem3DEXT", glTextureStorageMem3DEXT == NULL);
  glewInfoFunc("glTextureStorageMem3DMultisampleEXT", glTextureStorageMem3DMultisampleEXT == NULL);
}

#endif /* GL_EXT_memory_object */

#ifdef GL_EXT_memory_object_fd

static void _glewInfo_GL_EXT_memory_object_fd (void)
{
  glewPrintExt("GL_EXT_memory_object_fd", GLEW_EXT_memory_object_fd, glewIsSupported("GL_EXT_memory_object_fd"), glewGetExtension("GL_EXT_memory_object_fd"));

  glewInfoFunc("glImportMemoryFdEXT", glImportMemoryFdEXT == NULL);
}

#endif /* GL_EXT_memory_object_fd */

#ifdef GL_EXT_memory_object_win32

static void _glewInfo_GL_EXT_memory_object_win32 (void)
{
  glewPrintExt("GL_EXT_memory_object_win32", GLEW_EXT_memory_object_win32, glewIsSupported("GL_EXT_memory_object_win32"), glewGetExtension("GL_EXT_memory_object_win32"));

  glewInfoFunc("glImportMemoryWin32HandleEXT", glImportMemoryWin32HandleEXT == NULL);
  glewInfoFunc("glImportMemoryWin32NameEXT", glImportMemoryWin32NameEXT == NULL);
}

#endif /* GL_EXT_memory_object_win32 */

#ifdef GL_EXT_misc_attribute

static void _glewInfo_GL_EXT_misc_attribute (void)
{
  glewPrintExt("GL_EXT_misc_attribute", GLEW_EXT_misc_attribute, glewIsSupported("GL_EXT_misc_attribute"), glewGetExtension("GL_EXT_misc_attribute"));
}

#endif /* GL_EXT_misc_attribute */

#ifdef GL_EXT_multi_draw_arrays

static void _glewInfo_GL_EXT_multi_draw_arrays (void)
{
  glewPrintExt("GL_EXT_multi_draw_arrays", GLEW_EXT_multi_draw_arrays, glewIsSupported("GL_EXT_multi_draw_arrays"), glewGetExtension("GL_EXT_multi_draw_arrays"));

  glewInfoFunc("glMultiDrawArraysEXT", glMultiDrawArraysEXT == NULL);
  glewInfoFunc("glMultiDrawElementsEXT", glMultiDrawElementsEXT == NULL);
}

#endif /* GL_EXT_multi_draw_arrays */

#ifdef GL_EXT_multi_draw_indirect

static void _glewInfo_GL_EXT_multi_draw_indirect (void)
{
  glewPrintExt("GL_EXT_multi_draw_indirect", GLEW_EXT_multi_draw_indirect, glewIsSupported("GL_EXT_multi_draw_indirect"), glewGetExtension("GL_EXT_multi_draw_indirect"));

  glewInfoFunc("glMultiDrawArraysIndirectEXT", glMultiDrawArraysIndirectEXT == NULL);
  glewInfoFunc("glMultiDrawElementsIndirectEXT", glMultiDrawElementsIndirectEXT == NULL);
}

#endif /* GL_EXT_multi_draw_indirect */

#ifdef GL_EXT_multiple_textures

static void _glewInfo_GL_EXT_multiple_textures (void)
{
  glewPrintExt("GL_EXT_multiple_textures", GLEW_EXT_multiple_textures, glewIsSupported("GL_EXT_multiple_textures"), glewGetExtension("GL_EXT_multiple_textures"));
}

#endif /* GL_EXT_multiple_textures */

#ifdef GL_EXT_multisample

static void _glewInfo_GL_EXT_multisample (void)
{
  glewPrintExt("GL_EXT_multisample", GLEW_EXT_multisample, glewIsSupported("GL_EXT_multisample"), glewGetExtension("GL_EXT_multisample"));

  glewInfoFunc("glSampleMaskEXT", glSampleMaskEXT == NULL);
  glewInfoFunc("glSamplePatternEXT", glSamplePatternEXT == NULL);
}

#endif /* GL_EXT_multisample */

#ifdef GL_EXT_multisample_compatibility

static void _glewInfo_GL_EXT_multisample_compatibility (void)
{
  glewPrintExt("GL_EXT_multisample_compatibility", GLEW_EXT_multisample_compatibility, glewIsSupported("GL_EXT_multisample_compatibility"), glewGetExtension("GL_EXT_multisample_compatibility"));
}

#endif /* GL_EXT_multisample_compatibility */

#ifdef GL_EXT_multisampled_render_to_texture

static void _glewInfo_GL_EXT_multisampled_render_to_texture (void)
{
  glewPrintExt("GL_EXT_multisampled_render_to_texture", GLEW_EXT_multisampled_render_to_texture, glewIsSupported("GL_EXT_multisampled_render_to_texture"), glewGetExtension("GL_EXT_multisampled_render_to_texture"));

  glewInfoFunc("glFramebufferTexture2DMultisampleEXT", glFramebufferTexture2DMultisampleEXT == NULL);
}

#endif /* GL_EXT_multisampled_render_to_texture */

#ifdef GL_EXT_multisampled_render_to_texture2

static void _glewInfo_GL_EXT_multisampled_render_to_texture2 (void)
{
  glewPrintExt("GL_EXT_multisampled_render_to_texture2", GLEW_EXT_multisampled_render_to_texture2, glewIsSupported("GL_EXT_multisampled_render_to_texture2"), glewGetExtension("GL_EXT_multisampled_render_to_texture2"));
}

#endif /* GL_EXT_multisampled_render_to_texture2 */

#ifdef GL_EXT_multiview_draw_buffers

static void _glewInfo_GL_EXT_multiview_draw_buffers (void)
{
  glewPrintExt("GL_EXT_multiview_draw_buffers", GLEW_EXT_multiview_draw_buffers, glewIsSupported("GL_EXT_multiview_draw_buffers"), glewGetExtension("GL_EXT_multiview_draw_buffers"));

  glewInfoFunc("glDrawBuffersIndexedEXT", glDrawBuffersIndexedEXT == NULL);
  glewInfoFunc("glGetIntegeri_vEXT", glGetIntegeri_vEXT == NULL);
  glewInfoFunc("glReadBufferIndexedEXT", glReadBufferIndexedEXT == NULL);
}

#endif /* GL_EXT_multiview_draw_buffers */

#ifdef GL_EXT_packed_depth_stencil

static void _glewInfo_GL_EXT_packed_depth_stencil (void)
{
  glewPrintExt("GL_EXT_packed_depth_stencil", GLEW_EXT_packed_depth_stencil, glewIsSupported("GL_EXT_packed_depth_stencil"), glewGetExtension("GL_EXT_packed_depth_stencil"));
}

#endif /* GL_EXT_packed_depth_stencil */

#ifdef GL_EXT_packed_float

static void _glewInfo_GL_EXT_packed_float (void)
{
  glewPrintExt("GL_EXT_packed_float", GLEW_EXT_packed_float, glewIsSupported("GL_EXT_packed_float"), glewGetExtension("GL_EXT_packed_float"));
}

#endif /* GL_EXT_packed_float */

#ifdef GL_EXT_packed_pixels

static void _glewInfo_GL_EXT_packed_pixels (void)
{
  glewPrintExt("GL_EXT_packed_pixels", GLEW_EXT_packed_pixels, glewIsSupported("GL_EXT_packed_pixels"), glewGetExtension("GL_EXT_packed_pixels"));
}

#endif /* GL_EXT_packed_pixels */

#ifdef GL_EXT_paletted_texture

static void _glewInfo_GL_EXT_paletted_texture (void)
{
  glewPrintExt("GL_EXT_paletted_texture", GLEW_EXT_paletted_texture, glewIsSupported("GL_EXT_paletted_texture"), glewGetExtension("GL_EXT_paletted_texture"));

  glewInfoFunc("glColorTableEXT", glColorTableEXT == NULL);
  glewInfoFunc("glGetColorTableEXT", glGetColorTableEXT == NULL);
  glewInfoFunc("glGetColorTableParameterfvEXT", glGetColorTableParameterfvEXT == NULL);
  glewInfoFunc("glGetColorTableParameterivEXT", glGetColorTableParameterivEXT == NULL);
}

#endif /* GL_EXT_paletted_texture */

#ifdef GL_EXT_pixel_buffer_object

static void _glewInfo_GL_EXT_pixel_buffer_object (void)
{
  glewPrintExt("GL_EXT_pixel_buffer_object", GLEW_EXT_pixel_buffer_object, glewIsSupported("GL_EXT_pixel_buffer_object"), glewGetExtension("GL_EXT_pixel_buffer_object"));
}

#endif /* GL_EXT_pixel_buffer_object */

#ifdef GL_EXT_pixel_transform

static void _glewInfo_GL_EXT_pixel_transform (void)
{
  glewPrintExt("GL_EXT_pixel_transform", GLEW_EXT_pixel_transform, glewIsSupported("GL_EXT_pixel_transform"), glewGetExtension("GL_EXT_pixel_transform"));

  glewInfoFunc("glGetPixelTransformParameterfvEXT", glGetPixelTransformParameterfvEXT == NULL);
  glewInfoFunc("glGetPixelTransformParameterivEXT", glGetPixelTransformParameterivEXT == NULL);
  glewInfoFunc("glPixelTransformParameterfEXT", glPixelTransformParameterfEXT == NULL);
  glewInfoFunc("glPixelTransformParameterfvEXT", glPixelTransformParameterfvEXT == NULL);
  glewInfoFunc("glPixelTransformParameteriEXT", glPixelTransformParameteriEXT == NULL);
  glewInfoFunc("glPixelTransformParameterivEXT", glPixelTransformParameterivEXT == NULL);
}

#endif /* GL_EXT_pixel_transform */

#ifdef GL_EXT_pixel_transform_color_table

static void _glewInfo_GL_EXT_pixel_transform_color_table (void)
{
  glewPrintExt("GL_EXT_pixel_transform_color_table", GLEW_EXT_pixel_transform_color_table, glewIsSupported("GL_EXT_pixel_transform_color_table"), glewGetExtension("GL_EXT_pixel_transform_color_table"));
}

#endif /* GL_EXT_pixel_transform_color_table */

#ifdef GL_EXT_point_parameters

static void _glewInfo_GL_EXT_point_parameters (void)
{
  glewPrintExt("GL_EXT_point_parameters", GLEW_EXT_point_parameters, glewIsSupported("GL_EXT_point_parameters"), glewGetExtension("GL_EXT_point_parameters"));

  glewInfoFunc("glPointParameterfEXT", glPointParameterfEXT == NULL);
  glewInfoFunc("glPointParameterfvEXT", glPointParameterfvEXT == NULL);
}

#endif /* GL_EXT_point_parameters */

#ifdef GL_EXT_polygon_offset

static void _glewInfo_GL_EXT_polygon_offset (void)
{
  glewPrintExt("GL_EXT_polygon_offset", GLEW_EXT_polygon_offset, glewIsSupported("GL_EXT_polygon_offset"), glewGetExtension("GL_EXT_polygon_offset"));

  glewInfoFunc("glPolygonOffsetEXT", glPolygonOffsetEXT == NULL);
}

#endif /* GL_EXT_polygon_offset */

#ifdef GL_EXT_polygon_offset_clamp

static void _glewInfo_GL_EXT_polygon_offset_clamp (void)
{
  glewPrintExt("GL_EXT_polygon_offset_clamp", GLEW_EXT_polygon_offset_clamp, glewIsSupported("GL_EXT_polygon_offset_clamp"), glewGetExtension("GL_EXT_polygon_offset_clamp"));

  glewInfoFunc("glPolygonOffsetClampEXT", glPolygonOffsetClampEXT == NULL);
}

#endif /* GL_EXT_polygon_offset_clamp */

#ifdef GL_EXT_post_depth_coverage

static void _glewInfo_GL_EXT_post_depth_coverage (void)
{
  glewPrintExt("GL_EXT_post_depth_coverage", GLEW_EXT_post_depth_coverage, glewIsSupported("GL_EXT_post_depth_coverage"), glewGetExtension("GL_EXT_post_depth_coverage"));
}

#endif /* GL_EXT_post_depth_coverage */

#ifdef GL_EXT_provoking_vertex

static void _glewInfo_GL_EXT_provoking_vertex (void)
{
  glewPrintExt("GL_EXT_provoking_vertex", GLEW_EXT_provoking_vertex, glewIsSupported("GL_EXT_provoking_vertex"), glewGetExtension("GL_EXT_provoking_vertex"));

  glewInfoFunc("glProvokingVertexEXT", glProvokingVertexEXT == NULL);
}

#endif /* GL_EXT_provoking_vertex */

#ifdef GL_EXT_pvrtc_sRGB

static void _glewInfo_GL_EXT_pvrtc_sRGB (void)
{
  glewPrintExt("GL_EXT_pvrtc_sRGB", GLEW_EXT_pvrtc_sRGB, glewIsSupported("GL_EXT_pvrtc_sRGB"), glewGetExtension("GL_EXT_pvrtc_sRGB"));
}

#endif /* GL_EXT_pvrtc_sRGB */

#ifdef GL_EXT_raster_multisample

static void _glewInfo_GL_EXT_raster_multisample (void)
{
  glewPrintExt("GL_EXT_raster_multisample", GLEW_EXT_raster_multisample, glewIsSupported("GL_EXT_raster_multisample"), glewGetExtension("GL_EXT_raster_multisample"));

  glewInfoFunc("glCoverageModulationNV", glCoverageModulationNV == NULL);
  glewInfoFunc("glCoverageModulationTableNV", glCoverageModulationTableNV == NULL);
  glewInfoFunc("glGetCoverageModulationTableNV", glGetCoverageModulationTableNV == NULL);
  glewInfoFunc("glRasterSamplesEXT", glRasterSamplesEXT == NULL);
}

#endif /* GL_EXT_raster_multisample */

#ifdef GL_EXT_read_format_bgra

static void _glewInfo_GL_EXT_read_format_bgra (void)
{
  glewPrintExt("GL_EXT_read_format_bgra", GLEW_EXT_read_format_bgra, glewIsSupported("GL_EXT_read_format_bgra"), glewGetExtension("GL_EXT_read_format_bgra"));
}

#endif /* GL_EXT_read_format_bgra */

#ifdef GL_EXT_render_snorm

static void _glewInfo_GL_EXT_render_snorm (void)
{
  glewPrintExt("GL_EXT_render_snorm", GLEW_EXT_render_snorm, glewIsSupported("GL_EXT_render_snorm"), glewGetExtension("GL_EXT_render_snorm"));
}

#endif /* GL_EXT_render_snorm */

#ifdef GL_EXT_rescale_normal

static void _glewInfo_GL_EXT_rescale_normal (void)
{
  glewPrintExt("GL_EXT_rescale_normal", GLEW_EXT_rescale_normal, glewIsSupported("GL_EXT_rescale_normal"), glewGetExtension("GL_EXT_rescale_normal"));
}

#endif /* GL_EXT_rescale_normal */

#ifdef GL_EXT_sRGB

static void _glewInfo_GL_EXT_sRGB (void)
{
  glewPrintExt("GL_EXT_sRGB", GLEW_EXT_sRGB, glewIsSupported("GL_EXT_sRGB"), glewGetExtension("GL_EXT_sRGB"));
}

#endif /* GL_EXT_sRGB */

#ifdef GL_EXT_sRGB_write_control

static void _glewInfo_GL_EXT_sRGB_write_control (void)
{
  glewPrintExt("GL_EXT_sRGB_write_control", GLEW_EXT_sRGB_write_control, glewIsSupported("GL_EXT_sRGB_write_control"), glewGetExtension("GL_EXT_sRGB_write_control"));
}

#endif /* GL_EXT_sRGB_write_control */

#ifdef GL_EXT_scene_marker

static void _glewInfo_GL_EXT_scene_marker (void)
{
  glewPrintExt("GL_EXT_scene_marker", GLEW_EXT_scene_marker, glewIsSupported("GL_EXT_scene_marker"), glewGetExtension("GL_EXT_scene_marker"));

  glewInfoFunc("glBeginSceneEXT", glBeginSceneEXT == NULL);
  glewInfoFunc("glEndSceneEXT", glEndSceneEXT == NULL);
}

#endif /* GL_EXT_scene_marker */

#ifdef GL_EXT_secondary_color

static void _glewInfo_GL_EXT_secondary_color (void)
{
  glewPrintExt("GL_EXT_secondary_color", GLEW_EXT_secondary_color, glewIsSupported("GL_EXT_secondary_color"), glewGetExtension("GL_EXT_secondary_color"));

  glewInfoFunc("glSecondaryColor3bEXT", glSecondaryColor3bEXT == NULL);
  glewInfoFunc("glSecondaryColor3bvEXT", glSecondaryColor3bvEXT == NULL);
  glewInfoFunc("glSecondaryColor3dEXT", glSecondaryColor3dEXT == NULL);
  glewInfoFunc("glSecondaryColor3dvEXT", glSecondaryColor3dvEXT == NULL);
  glewInfoFunc("glSecondaryColor3fEXT", glSecondaryColor3fEXT == NULL);
  glewInfoFunc("glSecondaryColor3fvEXT", glSecondaryColor3fvEXT == NULL);
  glewInfoFunc("glSecondaryColor3iEXT", glSecondaryColor3iEXT == NULL);
  glewInfoFunc("glSecondaryColor3ivEXT", glSecondaryColor3ivEXT == NULL);
  glewInfoFunc("glSecondaryColor3sEXT", glSecondaryColor3sEXT == NULL);
  glewInfoFunc("glSecondaryColor3svEXT", glSecondaryColor3svEXT == NULL);
  glewInfoFunc("glSecondaryColor3ubEXT", glSecondaryColor3ubEXT == NULL);
  glewInfoFunc("glSecondaryColor3ubvEXT", glSecondaryColor3ubvEXT == NULL);
  glewInfoFunc("glSecondaryColor3uiEXT", glSecondaryColor3uiEXT == NULL);
  glewInfoFunc("glSecondaryColor3uivEXT", glSecondaryColor3uivEXT == NULL);
  glewInfoFunc("glSecondaryColor3usEXT", glSecondaryColor3usEXT == NULL);
  glewInfoFunc("glSecondaryColor3usvEXT", glSecondaryColor3usvEXT == NULL);
  glewInfoFunc("glSecondaryColorPointerEXT", glSecondaryColorPointerEXT == NULL);
}

#endif /* GL_EXT_secondary_color */

#ifdef GL_EXT_semaphore

static void _glewInfo_GL_EXT_semaphore (void)
{
  glewPrintExt("GL_EXT_semaphore", GLEW_EXT_semaphore, glewIsSupported("GL_EXT_semaphore"), glewGetExtension("GL_EXT_semaphore"));

  glewInfoFunc("glDeleteSemaphoresEXT", glDeleteSemaphoresEXT == NULL);
  glewInfoFunc("glGenSemaphoresEXT", glGenSemaphoresEXT == NULL);
  glewInfoFunc("glGetSemaphoreParameterui64vEXT", glGetSemaphoreParameterui64vEXT == NULL);
  glewInfoFunc("glIsSemaphoreEXT", glIsSemaphoreEXT == NULL);
  glewInfoFunc("glSemaphoreParameterui64vEXT", glSemaphoreParameterui64vEXT == NULL);
  glewInfoFunc("glSignalSemaphoreEXT", glSignalSemaphoreEXT == NULL);
  glewInfoFunc("glWaitSemaphoreEXT", glWaitSemaphoreEXT == NULL);
}

#endif /* GL_EXT_semaphore */

#ifdef GL_EXT_semaphore_fd

static void _glewInfo_GL_EXT_semaphore_fd (void)
{
  glewPrintExt("GL_EXT_semaphore_fd", GLEW_EXT_semaphore_fd, glewIsSupported("GL_EXT_semaphore_fd"), glewGetExtension("GL_EXT_semaphore_fd"));

  glewInfoFunc("glImportSemaphoreFdEXT", glImportSemaphoreFdEXT == NULL);
}

#endif /* GL_EXT_semaphore_fd */

#ifdef GL_EXT_semaphore_win32

static void _glewInfo_GL_EXT_semaphore_win32 (void)
{
  glewPrintExt("GL_EXT_semaphore_win32", GLEW_EXT_semaphore_win32, glewIsSupported("GL_EXT_semaphore_win32"), glewGetExtension("GL_EXT_semaphore_win32"));

  glewInfoFunc("glImportSemaphoreWin32HandleEXT", glImportSemaphoreWin32HandleEXT == NULL);
  glewInfoFunc("glImportSemaphoreWin32NameEXT", glImportSemaphoreWin32NameEXT == NULL);
}

#endif /* GL_EXT_semaphore_win32 */

#ifdef GL_EXT_separate_shader_objects

static void _glewInfo_GL_EXT_separate_shader_objects (void)
{
  glewPrintExt("GL_EXT_separate_shader_objects", GLEW_EXT_separate_shader_objects, glewIsSupported("GL_EXT_separate_shader_objects"), glewGetExtension("GL_EXT_separate_shader_objects"));

  glewInfoFunc("glActiveProgramEXT", glActiveProgramEXT == NULL);
  glewInfoFunc("glCreateShaderProgramEXT", glCreateShaderProgramEXT == NULL);
  glewInfoFunc("glUseShaderProgramEXT", glUseShaderProgramEXT == NULL);
}

#endif /* GL_EXT_separate_shader_objects */

#ifdef GL_EXT_separate_specular_color

static void _glewInfo_GL_EXT_separate_specular_color (void)
{
  glewPrintExt("GL_EXT_separate_specular_color", GLEW_EXT_separate_specular_color, glewIsSupported("GL_EXT_separate_specular_color"), glewGetExtension("GL_EXT_separate_specular_color"));
}

#endif /* GL_EXT_separate_specular_color */

#ifdef GL_EXT_shader_framebuffer_fetch

static void _glewInfo_GL_EXT_shader_framebuffer_fetch (void)
{
  glewPrintExt("GL_EXT_shader_framebuffer_fetch", GLEW_EXT_shader_framebuffer_fetch, glewIsSupported("GL_EXT_shader_framebuffer_fetch"), glewGetExtension("GL_EXT_shader_framebuffer_fetch"));
}

#endif /* GL_EXT_shader_framebuffer_fetch */

#ifdef GL_EXT_shader_group_vote

static void _glewInfo_GL_EXT_shader_group_vote (void)
{
  glewPrintExt("GL_EXT_shader_group_vote", GLEW_EXT_shader_group_vote, glewIsSupported("GL_EXT_shader_group_vote"), glewGetExtension("GL_EXT_shader_group_vote"));
}

#endif /* GL_EXT_shader_group_vote */

#ifdef GL_EXT_shader_image_load_formatted

static void _glewInfo_GL_EXT_shader_image_load_formatted (void)
{
  glewPrintExt("GL_EXT_shader_image_load_formatted", GLEW_EXT_shader_image_load_formatted, glewIsSupported("GL_EXT_shader_image_load_formatted"), glewGetExtension("GL_EXT_shader_image_load_formatted"));
}

#endif /* GL_EXT_shader_image_load_formatted */

#ifdef GL_EXT_shader_image_load_store

static void _glewInfo_GL_EXT_shader_image_load_store (void)
{
  glewPrintExt("GL_EXT_shader_image_load_store", GLEW_EXT_shader_image_load_store, glewIsSupported("GL_EXT_shader_image_load_store"), glewGetExtension("GL_EXT_shader_image_load_store"));

  glewInfoFunc("glBindImageTextureEXT", glBindImageTextureEXT == NULL);
  glewInfoFunc("glMemoryBarrierEXT", glMemoryBarrierEXT == NULL);
}

#endif /* GL_EXT_shader_image_load_store */

#ifdef GL_EXT_shader_implicit_conversions

static void _glewInfo_GL_EXT_shader_implicit_conversions (void)
{
  glewPrintExt("GL_EXT_shader_implicit_conversions", GLEW_EXT_shader_implicit_conversions, glewIsSupported("GL_EXT_shader_implicit_conversions"), glewGetExtension("GL_EXT_shader_implicit_conversions"));
}

#endif /* GL_EXT_shader_implicit_conversions */

#ifdef GL_EXT_shader_integer_mix

static void _glewInfo_GL_EXT_shader_integer_mix (void)
{
  glewPrintExt("GL_EXT_shader_integer_mix", GLEW_EXT_shader_integer_mix, glewIsSupported("GL_EXT_shader_integer_mix"), glewGetExtension("GL_EXT_shader_integer_mix"));
}

#endif /* GL_EXT_shader_integer_mix */

#ifdef GL_EXT_shader_io_blocks

static void _glewInfo_GL_EXT_shader_io_blocks (void)
{
  glewPrintExt("GL_EXT_shader_io_blocks", GLEW_EXT_shader_io_blocks, glewIsSupported("GL_EXT_shader_io_blocks"), glewGetExtension("GL_EXT_shader_io_blocks"));
}

#endif /* GL_EXT_shader_io_blocks */

#ifdef GL_EXT_shader_non_constant_global_initializers

static void _glewInfo_GL_EXT_shader_non_constant_global_initializers (void)
{
  glewPrintExt("GL_EXT_shader_non_constant_global_initializers", GLEW_EXT_shader_non_constant_global_initializers, glewIsSupported("GL_EXT_shader_non_constant_global_initializers"), glewGetExtension("GL_EXT_shader_non_constant_global_initializers"));
}

#endif /* GL_EXT_shader_non_constant_global_initializers */

#ifdef GL_EXT_shader_pixel_local_storage

static void _glewInfo_GL_EXT_shader_pixel_local_storage (void)
{
  glewPrintExt("GL_EXT_shader_pixel_local_storage", GLEW_EXT_shader_pixel_local_storage, glewIsSupported("GL_EXT_shader_pixel_local_storage"), glewGetExtension("GL_EXT_shader_pixel_local_storage"));
}

#endif /* GL_EXT_shader_pixel_local_storage */

#ifdef GL_EXT_shader_pixel_local_storage2

static void _glewInfo_GL_EXT_shader_pixel_local_storage2 (void)
{
  glewPrintExt("GL_EXT_shader_pixel_local_storage2", GLEW_EXT_shader_pixel_local_storage2, glewIsSupported("GL_EXT_shader_pixel_local_storage2"), glewGetExtension("GL_EXT_shader_pixel_local_storage2"));

  glewInfoFunc("glClearPixelLocalStorageuiEXT", glClearPixelLocalStorageuiEXT == NULL);
  glewInfoFunc("glFramebufferPixelLocalStorageSizeEXT", glFramebufferPixelLocalStorageSizeEXT == NULL);
  glewInfoFunc("glGetFramebufferPixelLocalStorageSizeEXT", glGetFramebufferPixelLocalStorageSizeEXT == NULL);
}

#endif /* GL_EXT_shader_pixel_local_storage2 */

#ifdef GL_EXT_shader_texture_lod

static void _glewInfo_GL_EXT_shader_texture_lod (void)
{
  glewPrintExt("GL_EXT_shader_texture_lod", GLEW_EXT_shader_texture_lod, glewIsSupported("GL_EXT_shader_texture_lod"), glewGetExtension("GL_EXT_shader_texture_lod"));
}

#endif /* GL_EXT_shader_texture_lod */

#ifdef GL_EXT_shadow_funcs

static void _glewInfo_GL_EXT_shadow_funcs (void)
{
  glewPrintExt("GL_EXT_shadow_funcs", GLEW_EXT_shadow_funcs, glewIsSupported("GL_EXT_shadow_funcs"), glewGetExtension("GL_EXT_shadow_funcs"));
}

#endif /* GL_EXT_shadow_funcs */

#ifdef GL_EXT_shadow_samplers

static void _glewInfo_GL_EXT_shadow_samplers (void)
{
  glewPrintExt("GL_EXT_shadow_samplers", GLEW_EXT_shadow_samplers, glewIsSupported("GL_EXT_shadow_samplers"), glewGetExtension("GL_EXT_shadow_samplers"));
}

#endif /* GL_EXT_shadow_samplers */

#ifdef GL_EXT_shared_texture_palette

static void _glewInfo_GL_EXT_shared_texture_palette (void)
{
  glewPrintExt("GL_EXT_shared_texture_palette", GLEW_EXT_shared_texture_palette, glewIsSupported("GL_EXT_shared_texture_palette"), glewGetExtension("GL_EXT_shared_texture_palette"));
}

#endif /* GL_EXT_shared_texture_palette */

#ifdef GL_EXT_sparse_texture

static void _glewInfo_GL_EXT_sparse_texture (void)
{
  glewPrintExt("GL_EXT_sparse_texture", GLEW_EXT_sparse_texture, glewIsSupported("GL_EXT_sparse_texture"), glewGetExtension("GL_EXT_sparse_texture"));

  glewInfoFunc("glTexPageCommitmentEXT", glTexPageCommitmentEXT == NULL);
  glewInfoFunc("glTexturePageCommitmentEXT", glTexturePageCommitmentEXT == NULL);
}

#endif /* GL_EXT_sparse_texture */

#ifdef GL_EXT_sparse_texture2

static void _glewInfo_GL_EXT_sparse_texture2 (void)
{
  glewPrintExt("GL_EXT_sparse_texture2", GLEW_EXT_sparse_texture2, glewIsSupported("GL_EXT_sparse_texture2"), glewGetExtension("GL_EXT_sparse_texture2"));
}

#endif /* GL_EXT_sparse_texture2 */

#ifdef GL_EXT_stencil_clear_tag

static void _glewInfo_GL_EXT_stencil_clear_tag (void)
{
  glewPrintExt("GL_EXT_stencil_clear_tag", GLEW_EXT_stencil_clear_tag, glewIsSupported("GL_EXT_stencil_clear_tag"), glewGetExtension("GL_EXT_stencil_clear_tag"));
}

#endif /* GL_EXT_stencil_clear_tag */

#ifdef GL_EXT_stencil_two_side

static void _glewInfo_GL_EXT_stencil_two_side (void)
{
  glewPrintExt("GL_EXT_stencil_two_side", GLEW_EXT_stencil_two_side, glewIsSupported("GL_EXT_stencil_two_side"), glewGetExtension("GL_EXT_stencil_two_side"));

  glewInfoFunc("glActiveStencilFaceEXT", glActiveStencilFaceEXT == NULL);
}

#endif /* GL_EXT_stencil_two_side */

#ifdef GL_EXT_stencil_wrap

static void _glewInfo_GL_EXT_stencil_wrap (void)
{
  glewPrintExt("GL_EXT_stencil_wrap", GLEW_EXT_stencil_wrap, glewIsSupported("GL_EXT_stencil_wrap"), glewGetExtension("GL_EXT_stencil_wrap"));
}

#endif /* GL_EXT_stencil_wrap */

#ifdef GL_EXT_subtexture

static void _glewInfo_GL_EXT_subtexture (void)
{
  glewPrintExt("GL_EXT_subtexture", GLEW_EXT_subtexture, glewIsSupported("GL_EXT_subtexture"), glewGetExtension("GL_EXT_subtexture"));

  glewInfoFunc("glTexSubImage1DEXT", glTexSubImage1DEXT == NULL);
  glewInfoFunc("glTexSubImage2DEXT", glTexSubImage2DEXT == NULL);
  glewInfoFunc("glTexSubImage3DEXT", glTexSubImage3DEXT == NULL);
}

#endif /* GL_EXT_subtexture */

#ifdef GL_EXT_texture

static void _glewInfo_GL_EXT_texture (void)
{
  glewPrintExt("GL_EXT_texture", GLEW_EXT_texture, glewIsSupported("GL_EXT_texture"), glewGetExtension("GL_EXT_texture"));
}

#endif /* GL_EXT_texture */

#ifdef GL_EXT_texture3D

static void _glewInfo_GL_EXT_texture3D (void)
{
  glewPrintExt("GL_EXT_texture3D", GLEW_EXT_texture3D, glewIsSupported("GL_EXT_texture3D"), glewGetExtension("GL_EXT_texture3D"));

  glewInfoFunc("glTexImage3DEXT", glTexImage3DEXT == NULL);
}

#endif /* GL_EXT_texture3D */

#ifdef GL_EXT_texture_array

static void _glewInfo_GL_EXT_texture_array (void)
{
  glewPrintExt("GL_EXT_texture_array", GLEW_EXT_texture_array, glewIsSupported("GL_EXT_texture_array"), glewGetExtension("GL_EXT_texture_array"));

  glewInfoFunc("glFramebufferTextureLayerEXT", glFramebufferTextureLayerEXT == NULL);
}

#endif /* GL_EXT_texture_array */

#ifdef GL_EXT_texture_buffer_object

static void _glewInfo_GL_EXT_texture_buffer_object (void)
{
  glewPrintExt("GL_EXT_texture_buffer_object", GLEW_EXT_texture_buffer_object, glewIsSupported("GL_EXT_texture_buffer_object"), glewGetExtension("GL_EXT_texture_buffer_object"));

  glewInfoFunc("glTexBufferEXT", glTexBufferEXT == NULL);
}

#endif /* GL_EXT_texture_buffer_object */

#ifdef GL_EXT_texture_compression_astc_decode_mode

static void _glewInfo_GL_EXT_texture_compression_astc_decode_mode (void)
{
  glewPrintExt("GL_EXT_texture_compression_astc_decode_mode", GLEW_EXT_texture_compression_astc_decode_mode, glewIsSupported("GL_EXT_texture_compression_astc_decode_mode"), glewGetExtension("GL_EXT_texture_compression_astc_decode_mode"));
}

#endif /* GL_EXT_texture_compression_astc_decode_mode */

#ifdef GL_EXT_texture_compression_astc_decode_mode_rgb9e5

static void _glewInfo_GL_EXT_texture_compression_astc_decode_mode_rgb9e5 (void)
{
  glewPrintExt("GL_EXT_texture_compression_astc_decode_mode_rgb9e5", GLEW_EXT_texture_compression_astc_decode_mode_rgb9e5, glewIsSupported("GL_EXT_texture_compression_astc_decode_mode_rgb9e5"), glewGetExtension("GL_EXT_texture_compression_astc_decode_mode_rgb9e5"));
}

#endif /* GL_EXT_texture_compression_astc_decode_mode_rgb9e5 */

#ifdef GL_EXT_texture_compression_bptc

static void _glewInfo_GL_EXT_texture_compression_bptc (void)
{
  glewPrintExt("GL_EXT_texture_compression_bptc", GLEW_EXT_texture_compression_bptc, glewIsSupported("GL_EXT_texture_compression_bptc"), glewGetExtension("GL_EXT_texture_compression_bptc"));
}

#endif /* GL_EXT_texture_compression_bptc */

#ifdef GL_EXT_texture_compression_dxt1

static void _glewInfo_GL_EXT_texture_compression_dxt1 (void)
{
  glewPrintExt("GL_EXT_texture_compression_dxt1", GLEW_EXT_texture_compression_dxt1, glewIsSupported("GL_EXT_texture_compression_dxt1"), glewGetExtension("GL_EXT_texture_compression_dxt1"));
}

#endif /* GL_EXT_texture_compression_dxt1 */

#ifdef GL_EXT_texture_compression_latc

static void _glewInfo_GL_EXT_texture_compression_latc (void)
{
  glewPrintExt("GL_EXT_texture_compression_latc", GLEW_EXT_texture_compression_latc, glewIsSupported("GL_EXT_texture_compression_latc"), glewGetExtension("GL_EXT_texture_compression_latc"));
}

#endif /* GL_EXT_texture_compression_latc */

#ifdef GL_EXT_texture_compression_rgtc

static void _glewInfo_GL_EXT_texture_compression_rgtc (void)
{
  glewPrintExt("GL_EXT_texture_compression_rgtc", GLEW_EXT_texture_compression_rgtc, glewIsSupported("GL_EXT_texture_compression_rgtc"), glewGetExtension("GL_EXT_texture_compression_rgtc"));
}

#endif /* GL_EXT_texture_compression_rgtc */

#ifdef GL_EXT_texture_compression_s3tc

static void _glewInfo_GL_EXT_texture_compression_s3tc (void)
{
  glewPrintExt("GL_EXT_texture_compression_s3tc", GLEW_EXT_texture_compression_s3tc, glewIsSupported("GL_EXT_texture_compression_s3tc"), glewGetExtension("GL_EXT_texture_compression_s3tc"));
}

#endif /* GL_EXT_texture_compression_s3tc */

#ifdef GL_EXT_texture_cube_map

static void _glewInfo_GL_EXT_texture_cube_map (void)
{
  glewPrintExt("GL_EXT_texture_cube_map", GLEW_EXT_texture_cube_map, glewIsSupported("GL_EXT_texture_cube_map"), glewGetExtension("GL_EXT_texture_cube_map"));
}

#endif /* GL_EXT_texture_cube_map */

#ifdef GL_EXT_texture_cube_map_array

static void _glewInfo_GL_EXT_texture_cube_map_array (void)
{
  glewPrintExt("GL_EXT_texture_cube_map_array", GLEW_EXT_texture_cube_map_array, glewIsSupported("GL_EXT_texture_cube_map_array"), glewGetExtension("GL_EXT_texture_cube_map_array"));
}

#endif /* GL_EXT_texture_cube_map_array */

#ifdef GL_EXT_texture_edge_clamp

static void _glewInfo_GL_EXT_texture_edge_clamp (void)
{
  glewPrintExt("GL_EXT_texture_edge_clamp", GLEW_EXT_texture_edge_clamp, glewIsSupported("GL_EXT_texture_edge_clamp"), glewGetExtension("GL_EXT_texture_edge_clamp"));
}

#endif /* GL_EXT_texture_edge_clamp */

#ifdef GL_EXT_texture_env

static void _glewInfo_GL_EXT_texture_env (void)
{
  glewPrintExt("GL_EXT_texture_env", GLEW_EXT_texture_env, glewIsSupported("GL_EXT_texture_env"), glewGetExtension("GL_EXT_texture_env"));
}

#endif /* GL_EXT_texture_env */

#ifdef GL_EXT_texture_env_add

static void _glewInfo_GL_EXT_texture_env_add (void)
{
  glewPrintExt("GL_EXT_texture_env_add", GLEW_EXT_texture_env_add, glewIsSupported("GL_EXT_texture_env_add"), glewGetExtension("GL_EXT_texture_env_add"));
}

#endif /* GL_EXT_texture_env_add */

#ifdef GL_EXT_texture_env_combine

static void _glewInfo_GL_EXT_texture_env_combine (void)
{
  glewPrintExt("GL_EXT_texture_env_combine", GLEW_EXT_texture_env_combine, glewIsSupported("GL_EXT_texture_env_combine"), glewGetExtension("GL_EXT_texture_env_combine"));
}

#endif /* GL_EXT_texture_env_combine */

#ifdef GL_EXT_texture_env_dot3

static void _glewInfo_GL_EXT_texture_env_dot3 (void)
{
  glewPrintExt("GL_EXT_texture_env_dot3", GLEW_EXT_texture_env_dot3, glewIsSupported("GL_EXT_texture_env_dot3"), glewGetExtension("GL_EXT_texture_env_dot3"));
}

#endif /* GL_EXT_texture_env_dot3 */

#ifdef GL_EXT_texture_filter_anisotropic

static void _glewInfo_GL_EXT_texture_filter_anisotropic (void)
{
  glewPrintExt("GL_EXT_texture_filter_anisotropic", GLEW_EXT_texture_filter_anisotropic, glewIsSupported("GL_EXT_texture_filter_anisotropic"), glewGetExtension("GL_EXT_texture_filter_anisotropic"));
}

#endif /* GL_EXT_texture_filter_anisotropic */

#ifdef GL_EXT_texture_filter_minmax

static void _glewInfo_GL_EXT_texture_filter_minmax (void)
{
  glewPrintExt("GL_EXT_texture_filter_minmax", GLEW_EXT_texture_filter_minmax, glewIsSupported("GL_EXT_texture_filter_minmax"), glewGetExtension("GL_EXT_texture_filter_minmax"));
}

#endif /* GL_EXT_texture_filter_minmax */

#ifdef GL_EXT_texture_format_BGRA8888

static void _glewInfo_GL_EXT_texture_format_BGRA8888 (void)
{
  glewPrintExt("GL_EXT_texture_format_BGRA8888", GLEW_EXT_texture_format_BGRA8888, glewIsSupported("GL_EXT_texture_format_BGRA8888"), glewGetExtension("GL_EXT_texture_format_BGRA8888"));
}

#endif /* GL_EXT_texture_format_BGRA8888 */

#ifdef GL_EXT_texture_integer

static void _glewInfo_GL_EXT_texture_integer (void)
{
  glewPrintExt("GL_EXT_texture_integer", GLEW_EXT_texture_integer, glewIsSupported("GL_EXT_texture_integer"), glewGetExtension("GL_EXT_texture_integer"));

  glewInfoFunc("glClearColorIiEXT", glClearColorIiEXT == NULL);
  glewInfoFunc("glClearColorIuiEXT", glClearColorIuiEXT == NULL);
  glewInfoFunc("glGetTexParameterIivEXT", glGetTexParameterIivEXT == NULL);
  glewInfoFunc("glGetTexParameterIuivEXT", glGetTexParameterIuivEXT == NULL);
  glewInfoFunc("glTexParameterIivEXT", glTexParameterIivEXT == NULL);
  glewInfoFunc("glTexParameterIuivEXT", glTexParameterIuivEXT == NULL);
}

#endif /* GL_EXT_texture_integer */

#ifdef GL_EXT_texture_lod_bias

static void _glewInfo_GL_EXT_texture_lod_bias (void)
{
  glewPrintExt("GL_EXT_texture_lod_bias", GLEW_EXT_texture_lod_bias, glewIsSupported("GL_EXT_texture_lod_bias"), glewGetExtension("GL_EXT_texture_lod_bias"));
}

#endif /* GL_EXT_texture_lod_bias */

#ifdef GL_EXT_texture_mirror_clamp

static void _glewInfo_GL_EXT_texture_mirror_clamp (void)
{
  glewPrintExt("GL_EXT_texture_mirror_clamp", GLEW_EXT_texture_mirror_clamp, glewIsSupported("GL_EXT_texture_mirror_clamp"), glewGetExtension("GL_EXT_texture_mirror_clamp"));
}

#endif /* GL_EXT_texture_mirror_clamp */

#ifdef GL_EXT_texture_norm16

static void _glewInfo_GL_EXT_texture_norm16 (void)
{
  glewPrintExt("GL_EXT_texture_norm16", GLEW_EXT_texture_norm16, glewIsSupported("GL_EXT_texture_norm16"), glewGetExtension("GL_EXT_texture_norm16"));
}

#endif /* GL_EXT_texture_norm16 */

#ifdef GL_EXT_texture_object

static void _glewInfo_GL_EXT_texture_object (void)
{
  glewPrintExt("GL_EXT_texture_object", GLEW_EXT_texture_object, glewIsSupported("GL_EXT_texture_object"), glewGetExtension("GL_EXT_texture_object"));

  glewInfoFunc("glAreTexturesResidentEXT", glAreTexturesResidentEXT == NULL);
  glewInfoFunc("glBindTextureEXT", glBindTextureEXT == NULL);
  glewInfoFunc("glDeleteTexturesEXT", glDeleteTexturesEXT == NULL);
  glewInfoFunc("glGenTexturesEXT", glGenTexturesEXT == NULL);
  glewInfoFunc("glIsTextureEXT", glIsTextureEXT == NULL);
  glewInfoFunc("glPrioritizeTexturesEXT", glPrioritizeTexturesEXT == NULL);
}

#endif /* GL_EXT_texture_object */

#ifdef GL_EXT_texture_perturb_normal

static void _glewInfo_GL_EXT_texture_perturb_normal (void)
{
  glewPrintExt("GL_EXT_texture_perturb_normal", GLEW_EXT_texture_perturb_normal, glewIsSupported("GL_EXT_texture_perturb_normal"), glewGetExtension("GL_EXT_texture_perturb_normal"));

  glewInfoFunc("glTextureNormalEXT", glTextureNormalEXT == NULL);
}

#endif /* GL_EXT_texture_perturb_normal */

#ifdef GL_EXT_texture_rectangle

static void _glewInfo_GL_EXT_texture_rectangle (void)
{
  glewPrintExt("GL_EXT_texture_rectangle", GLEW_EXT_texture_rectangle, glewIsSupported("GL_EXT_texture_rectangle"), glewGetExtension("GL_EXT_texture_rectangle"));
}

#endif /* GL_EXT_texture_rectangle */

#ifdef GL_EXT_texture_rg

static void _glewInfo_GL_EXT_texture_rg (void)
{
  glewPrintExt("GL_EXT_texture_rg", GLEW_EXT_texture_rg, glewIsSupported("GL_EXT_texture_rg"), glewGetExtension("GL_EXT_texture_rg"));
}

#endif /* GL_EXT_texture_rg */

#ifdef GL_EXT_texture_sRGB

static void _glewInfo_GL_EXT_texture_sRGB (void)
{
  glewPrintExt("GL_EXT_texture_sRGB", GLEW_EXT_texture_sRGB, glewIsSupported("GL_EXT_texture_sRGB"), glewGetExtension("GL_EXT_texture_sRGB"));
}

#endif /* GL_EXT_texture_sRGB */

#ifdef GL_EXT_texture_sRGB_R8

static void _glewInfo_GL_EXT_texture_sRGB_R8 (void)
{
  glewPrintExt("GL_EXT_texture_sRGB_R8", GLEW_EXT_texture_sRGB_R8, glewIsSupported("GL_EXT_texture_sRGB_R8"), glewGetExtension("GL_EXT_texture_sRGB_R8"));
}

#endif /* GL_EXT_texture_sRGB_R8 */

#ifdef GL_EXT_texture_sRGB_RG8

static void _glewInfo_GL_EXT_texture_sRGB_RG8 (void)
{
  glewPrintExt("GL_EXT_texture_sRGB_RG8", GLEW_EXT_texture_sRGB_RG8, glewIsSupported("GL_EXT_texture_sRGB_RG8"), glewGetExtension("GL_EXT_texture_sRGB_RG8"));
}

#endif /* GL_EXT_texture_sRGB_RG8 */

#ifdef GL_EXT_texture_sRGB_decode

static void _glewInfo_GL_EXT_texture_sRGB_decode (void)
{
  glewPrintExt("GL_EXT_texture_sRGB_decode", GLEW_EXT_texture_sRGB_decode, glewIsSupported("GL_EXT_texture_sRGB_decode"), glewGetExtension("GL_EXT_texture_sRGB_decode"));
}

#endif /* GL_EXT_texture_sRGB_decode */

#ifdef GL_EXT_texture_shared_exponent

static void _glewInfo_GL_EXT_texture_shared_exponent (void)
{
  glewPrintExt("GL_EXT_texture_shared_exponent", GLEW_EXT_texture_shared_exponent, glewIsSupported("GL_EXT_texture_shared_exponent"), glewGetExtension("GL_EXT_texture_shared_exponent"));
}

#endif /* GL_EXT_texture_shared_exponent */

#ifdef GL_EXT_texture_snorm

static void _glewInfo_GL_EXT_texture_snorm (void)
{
  glewPrintExt("GL_EXT_texture_snorm", GLEW_EXT_texture_snorm, glewIsSupported("GL_EXT_texture_snorm"), glewGetExtension("GL_EXT_texture_snorm"));
}

#endif /* GL_EXT_texture_snorm */

#ifdef GL_EXT_texture_storage

static void _glewInfo_GL_EXT_texture_storage (void)
{
  glewPrintExt("GL_EXT_texture_storage", GLEW_EXT_texture_storage, glewIsSupported("GL_EXT_texture_storage"), glewGetExtension("GL_EXT_texture_storage"));

  glewInfoFunc("glTexStorage1DEXT", glTexStorage1DEXT == NULL);
  glewInfoFunc("glTexStorage2DEXT", glTexStorage2DEXT == NULL);
  glewInfoFunc("glTexStorage3DEXT", glTexStorage3DEXT == NULL);
  glewInfoFunc("glTextureStorage1DEXT", glTextureStorage1DEXT == NULL);
  glewInfoFunc("glTextureStorage2DEXT", glTextureStorage2DEXT == NULL);
  glewInfoFunc("glTextureStorage3DEXT", glTextureStorage3DEXT == NULL);
}

#endif /* GL_EXT_texture_storage */

#ifdef GL_EXT_texture_swizzle

static void _glewInfo_GL_EXT_texture_swizzle (void)
{
  glewPrintExt("GL_EXT_texture_swizzle", GLEW_EXT_texture_swizzle, glewIsSupported("GL_EXT_texture_swizzle"), glewGetExtension("GL_EXT_texture_swizzle"));
}

#endif /* GL_EXT_texture_swizzle */

#ifdef GL_EXT_texture_type_2_10_10_10_REV

static void _glewInfo_GL_EXT_texture_type_2_10_10_10_REV (void)
{
  glewPrintExt("GL_EXT_texture_type_2_10_10_10_REV", GLEW_EXT_texture_type_2_10_10_10_REV, glewIsSupported("GL_EXT_texture_type_2_10_10_10_REV"), glewGetExtension("GL_EXT_texture_type_2_10_10_10_REV"));
}

#endif /* GL_EXT_texture_type_2_10_10_10_REV */

#ifdef GL_EXT_texture_view

static void _glewInfo_GL_EXT_texture_view (void)
{
  glewPrintExt("GL_EXT_texture_view", GLEW_EXT_texture_view, glewIsSupported("GL_EXT_texture_view"), glewGetExtension("GL_EXT_texture_view"));

  glewInfoFunc("glTextureViewEXT", glTextureViewEXT == NULL);
}

#endif /* GL_EXT_texture_view */

#ifdef GL_EXT_timer_query

static void _glewInfo_GL_EXT_timer_query (void)
{
  glewPrintExt("GL_EXT_timer_query", GLEW_EXT_timer_query, glewIsSupported("GL_EXT_timer_query"), glewGetExtension("GL_EXT_timer_query"));

  glewInfoFunc("glGetQueryObjecti64vEXT", glGetQueryObjecti64vEXT == NULL);
  glewInfoFunc("glGetQueryObjectui64vEXT", glGetQueryObjectui64vEXT == NULL);
}

#endif /* GL_EXT_timer_query */

#ifdef GL_EXT_transform_feedback

static void _glewInfo_GL_EXT_transform_feedback (void)
{
  glewPrintExt("GL_EXT_transform_feedback", GLEW_EXT_transform_feedback, glewIsSupported("GL_EXT_transform_feedback"), glewGetExtension("GL_EXT_transform_feedback"));

  glewInfoFunc("glBeginTransformFeedbackEXT", glBeginTransformFeedbackEXT == NULL);
  glewInfoFunc("glBindBufferBaseEXT", glBindBufferBaseEXT == NULL);
  glewInfoFunc("glBindBufferOffsetEXT", glBindBufferOffsetEXT == NULL);
  glewInfoFunc("glBindBufferRangeEXT", glBindBufferRangeEXT == NULL);
  glewInfoFunc("glEndTransformFeedbackEXT", glEndTransformFeedbackEXT == NULL);
  glewInfoFunc("glGetTransformFeedbackVaryingEXT", glGetTransformFeedbackVaryingEXT == NULL);
  glewInfoFunc("glTransformFeedbackVaryingsEXT", glTransformFeedbackVaryingsEXT == NULL);
}

#endif /* GL_EXT_transform_feedback */

#ifdef GL_EXT_unpack_subimage

static void _glewInfo_GL_EXT_unpack_subimage (void)
{
  glewPrintExt("GL_EXT_unpack_subimage", GLEW_EXT_unpack_subimage, glewIsSupported("GL_EXT_unpack_subimage"), glewGetExtension("GL_EXT_unpack_subimage"));
}

#endif /* GL_EXT_unpack_subimage */

#ifdef GL_EXT_vertex_array

static void _glewInfo_GL_EXT_vertex_array (void)
{
  glewPrintExt("GL_EXT_vertex_array", GLEW_EXT_vertex_array, glewIsSupported("GL_EXT_vertex_array"), glewGetExtension("GL_EXT_vertex_array"));

  glewInfoFunc("glArrayElementEXT", glArrayElementEXT == NULL);
  glewInfoFunc("glColorPointerEXT", glColorPointerEXT == NULL);
  glewInfoFunc("glDrawArraysEXT", glDrawArraysEXT == NULL);
  glewInfoFunc("glEdgeFlagPointerEXT", glEdgeFlagPointerEXT == NULL);
  glewInfoFunc("glIndexPointerEXT", glIndexPointerEXT == NULL);
  glewInfoFunc("glNormalPointerEXT", glNormalPointerEXT == NULL);
  glewInfoFunc("glTexCoordPointerEXT", glTexCoordPointerEXT == NULL);
  glewInfoFunc("glVertexPointerEXT", glVertexPointerEXT == NULL);
}

#endif /* GL_EXT_vertex_array */

#ifdef GL_EXT_vertex_array_bgra

static void _glewInfo_GL_EXT_vertex_array_bgra (void)
{
  glewPrintExt("GL_EXT_vertex_array_bgra", GLEW_EXT_vertex_array_bgra, glewIsSupported("GL_EXT_vertex_array_bgra"), glewGetExtension("GL_EXT_vertex_array_bgra"));
}

#endif /* GL_EXT_vertex_array_bgra */

#ifdef GL_EXT_vertex_array_setXXX

static void _glewInfo_GL_EXT_vertex_array_setXXX (void)
{
  glewPrintExt("GL_EXT_vertex_array_setXXX", GLEW_EXT_vertex_array_setXXX, glewIsSupported("GL_EXT_vertex_array_setXXX"), glewGetExtension("GL_EXT_vertex_array_setXXX"));

  glewInfoFunc("glBindArraySetEXT", glBindArraySetEXT == NULL);
  glewInfoFunc("glCreateArraySetExt", glCreateArraySetExt == NULL);
  glewInfoFunc("glDeleteArraySetsEXT", glDeleteArraySetsEXT == NULL);
}

#endif /* GL_EXT_vertex_array_setXXX */

#ifdef GL_EXT_vertex_attrib_64bit

static void _glewInfo_GL_EXT_vertex_attrib_64bit (void)
{
  glewPrintExt("GL_EXT_vertex_attrib_64bit", GLEW_EXT_vertex_attrib_64bit, glewIsSupported("GL_EXT_vertex_attrib_64bit"), glewGetExtension("GL_EXT_vertex_attrib_64bit"));

  glewInfoFunc("glGetVertexAttribLdvEXT", glGetVertexAttribLdvEXT == NULL);
  glewInfoFunc("glVertexArrayVertexAttribLOffsetEXT", glVertexArrayVertexAttribLOffsetEXT == NULL);
  glewInfoFunc("glVertexAttribL1dEXT", glVertexAttribL1dEXT == NULL);
  glewInfoFunc("glVertexAttribL1dvEXT", glVertexAttribL1dvEXT == NULL);
  glewInfoFunc("glVertexAttribL2dEXT", glVertexAttribL2dEXT == NULL);
  glewInfoFunc("glVertexAttribL2dvEXT", glVertexAttribL2dvEXT == NULL);
  glewInfoFunc("glVertexAttribL3dEXT", glVertexAttribL3dEXT == NULL);
  glewInfoFunc("glVertexAttribL3dvEXT", glVertexAttribL3dvEXT == NULL);
  glewInfoFunc("glVertexAttribL4dEXT", glVertexAttribL4dEXT == NULL);
  glewInfoFunc("glVertexAttribL4dvEXT", glVertexAttribL4dvEXT == NULL);
  glewInfoFunc("glVertexAttribLPointerEXT", glVertexAttribLPointerEXT == NULL);
}

#endif /* GL_EXT_vertex_attrib_64bit */

#ifdef GL_EXT_vertex_shader

static void _glewInfo_GL_EXT_vertex_shader (void)
{
  glewPrintExt("GL_EXT_vertex_shader", GLEW_EXT_vertex_shader, glewIsSupported("GL_EXT_vertex_shader"), glewGetExtension("GL_EXT_vertex_shader"));

  glewInfoFunc("glBeginVertexShaderEXT", glBeginVertexShaderEXT == NULL);
  glewInfoFunc("glBindLightParameterEXT", glBindLightParameterEXT == NULL);
  glewInfoFunc("glBindMaterialParameterEXT", glBindMaterialParameterEXT == NULL);
  glewInfoFunc("glBindParameterEXT", glBindParameterEXT == NULL);
  glewInfoFunc("glBindTexGenParameterEXT", glBindTexGenParameterEXT == NULL);
  glewInfoFunc("glBindTextureUnitParameterEXT", glBindTextureUnitParameterEXT == NULL);
  glewInfoFunc("glBindVertexShaderEXT", glBindVertexShaderEXT == NULL);
  glewInfoFunc("glDeleteVertexShaderEXT", glDeleteVertexShaderEXT == NULL);
  glewInfoFunc("glDisableVariantClientStateEXT", glDisableVariantClientStateEXT == NULL);
  glewInfoFunc("glEnableVariantClientStateEXT", glEnableVariantClientStateEXT == NULL);
  glewInfoFunc("glEndVertexShaderEXT", glEndVertexShaderEXT == NULL);
  glewInfoFunc("glExtractComponentEXT", glExtractComponentEXT == NULL);
  glewInfoFunc("glGenSymbolsEXT", glGenSymbolsEXT == NULL);
  glewInfoFunc("glGenVertexShadersEXT", glGenVertexShadersEXT == NULL);
  glewInfoFunc("glGetInvariantBooleanvEXT", glGetInvariantBooleanvEXT == NULL);
  glewInfoFunc("glGetInvariantFloatvEXT", glGetInvariantFloatvEXT == NULL);
  glewInfoFunc("glGetInvariantIntegervEXT", glGetInvariantIntegervEXT == NULL);
  glewInfoFunc("glGetLocalConstantBooleanvEXT", glGetLocalConstantBooleanvEXT == NULL);
  glewInfoFunc("glGetLocalConstantFloatvEXT", glGetLocalConstantFloatvEXT == NULL);
  glewInfoFunc("glGetLocalConstantIntegervEXT", glGetLocalConstantIntegervEXT == NULL);
  glewInfoFunc("glGetVariantBooleanvEXT", glGetVariantBooleanvEXT == NULL);
  glewInfoFunc("glGetVariantFloatvEXT", glGetVariantFloatvEXT == NULL);
  glewInfoFunc("glGetVariantIntegervEXT", glGetVariantIntegervEXT == NULL);
  glewInfoFunc("glGetVariantPointervEXT", glGetVariantPointervEXT == NULL);
  glewInfoFunc("glInsertComponentEXT", glInsertComponentEXT == NULL);
  glewInfoFunc("glIsVariantEnabledEXT", glIsVariantEnabledEXT == NULL);
  glewInfoFunc("glSetInvariantEXT", glSetInvariantEXT == NULL);
  glewInfoFunc("glSetLocalConstantEXT", glSetLocalConstantEXT == NULL);
  glewInfoFunc("glShaderOp1EXT", glShaderOp1EXT == NULL);
  glewInfoFunc("glShaderOp2EXT", glShaderOp2EXT == NULL);
  glewInfoFunc("glShaderOp3EXT", glShaderOp3EXT == NULL);
  glewInfoFunc("glSwizzleEXT", glSwizzleEXT == NULL);
  glewInfoFunc("glVariantPointerEXT", glVariantPointerEXT == NULL);
  glewInfoFunc("glVariantbvEXT", glVariantbvEXT == NULL);
  glewInfoFunc("glVariantdvEXT", glVariantdvEXT == NULL);
  glewInfoFunc("glVariantfvEXT", glVariantfvEXT == NULL);
  glewInfoFunc("glVariantivEXT", glVariantivEXT == NULL);
  glewInfoFunc("glVariantsvEXT", glVariantsvEXT == NULL);
  glewInfoFunc("glVariantubvEXT", glVariantubvEXT == NULL);
  glewInfoFunc("glVariantuivEXT", glVariantuivEXT == NULL);
  glewInfoFunc("glVariantusvEXT", glVariantusvEXT == NULL);
  glewInfoFunc("glWriteMaskEXT", glWriteMaskEXT == NULL);
}

#endif /* GL_EXT_vertex_shader */

#ifdef GL_EXT_vertex_weighting

static void _glewInfo_GL_EXT_vertex_weighting (void)
{
  glewPrintExt("GL_EXT_vertex_weighting", GLEW_EXT_vertex_weighting, glewIsSupported("GL_EXT_vertex_weighting"), glewGetExtension("GL_EXT_vertex_weighting"));

  glewInfoFunc("glVertexWeightPointerEXT", glVertexWeightPointerEXT == NULL);
  glewInfoFunc("glVertexWeightfEXT", glVertexWeightfEXT == NULL);
  glewInfoFunc("glVertexWeightfvEXT", glVertexWeightfvEXT == NULL);
}

#endif /* GL_EXT_vertex_weighting */

#ifdef GL_EXT_win32_keyed_mutex

static void _glewInfo_GL_EXT_win32_keyed_mutex (void)
{
  glewPrintExt("GL_EXT_win32_keyed_mutex", GLEW_EXT_win32_keyed_mutex, glewIsSupported("GL_EXT_win32_keyed_mutex"), glewGetExtension("GL_EXT_win32_keyed_mutex"));

  glewInfoFunc("glAcquireKeyedMutexWin32EXT", glAcquireKeyedMutexWin32EXT == NULL);
  glewInfoFunc("glReleaseKeyedMutexWin32EXT", glReleaseKeyedMutexWin32EXT == NULL);
}

#endif /* GL_EXT_win32_keyed_mutex */

#ifdef GL_EXT_window_rectangles

static void _glewInfo_GL_EXT_window_rectangles (void)
{
  glewPrintExt("GL_EXT_window_rectangles", GLEW_EXT_window_rectangles, glewIsSupported("GL_EXT_window_rectangles"), glewGetExtension("GL_EXT_window_rectangles"));

  glewInfoFunc("glWindowRectanglesEXT", glWindowRectanglesEXT == NULL);
}

#endif /* GL_EXT_window_rectangles */

#ifdef GL_EXT_x11_sync_object

static void _glewInfo_GL_EXT_x11_sync_object (void)
{
  glewPrintExt("GL_EXT_x11_sync_object", GLEW_EXT_x11_sync_object, glewIsSupported("GL_EXT_x11_sync_object"), glewGetExtension("GL_EXT_x11_sync_object"));

  glewInfoFunc("glImportSyncEXT", glImportSyncEXT == NULL);
}

#endif /* GL_EXT_x11_sync_object */

#ifdef GL_GREMEDY_frame_terminator

static void _glewInfo_GL_GREMEDY_frame_terminator (void)
{
  glewPrintExt("GL_GREMEDY_frame_terminator", GLEW_GREMEDY_frame_terminator, glewIsSupported("GL_GREMEDY_frame_terminator"), glewGetExtension("GL_GREMEDY_frame_terminator"));

  glewInfoFunc("glFrameTerminatorGREMEDY", glFrameTerminatorGREMEDY == NULL);
}

#endif /* GL_GREMEDY_frame_terminator */

#ifdef GL_GREMEDY_string_marker

static void _glewInfo_GL_GREMEDY_string_marker (void)
{
  glewPrintExt("GL_GREMEDY_string_marker", GLEW_GREMEDY_string_marker, glewIsSupported("GL_GREMEDY_string_marker"), glewGetExtension("GL_GREMEDY_string_marker"));

  glewInfoFunc("glStringMarkerGREMEDY", glStringMarkerGREMEDY == NULL);
}

#endif /* GL_GREMEDY_string_marker */

#ifdef GL_HP_convolution_border_modes

static void _glewInfo_GL_HP_convolution_border_modes (void)
{
  glewPrintExt("GL_HP_convolution_border_modes", GLEW_HP_convolution_border_modes, glewIsSupported("GL_HP_convolution_border_modes"), glewGetExtension("GL_HP_convolution_border_modes"));
}

#endif /* GL_HP_convolution_border_modes */

#ifdef GL_HP_image_transform

static void _glewInfo_GL_HP_image_transform (void)
{
  glewPrintExt("GL_HP_image_transform", GLEW_HP_image_transform, glewIsSupported("GL_HP_image_transform"), glewGetExtension("GL_HP_image_transform"));

  glewInfoFunc("glGetImageTransformParameterfvHP", glGetImageTransformParameterfvHP == NULL);
  glewInfoFunc("glGetImageTransformParameterivHP", glGetImageTransformParameterivHP == NULL);
  glewInfoFunc("glImageTransformParameterfHP", glImageTransformParameterfHP == NULL);
  glewInfoFunc("glImageTransformParameterfvHP", glImageTransformParameterfvHP == NULL);
  glewInfoFunc("glImageTransformParameteriHP", glImageTransformParameteriHP == NULL);
  glewInfoFunc("glImageTransformParameterivHP", glImageTransformParameterivHP == NULL);
}

#endif /* GL_HP_image_transform */

#ifdef GL_HP_occlusion_test

static void _glewInfo_GL_HP_occlusion_test (void)
{
  glewPrintExt("GL_HP_occlusion_test", GLEW_HP_occlusion_test, glewIsSupported("GL_HP_occlusion_test"), glewGetExtension("GL_HP_occlusion_test"));
}

#endif /* GL_HP_occlusion_test */

#ifdef GL_HP_texture_lighting

static void _glewInfo_GL_HP_texture_lighting (void)
{
  glewPrintExt("GL_HP_texture_lighting", GLEW_HP_texture_lighting, glewIsSupported("GL_HP_texture_lighting"), glewGetExtension("GL_HP_texture_lighting"));
}

#endif /* GL_HP_texture_lighting */

#ifdef GL_IBM_cull_vertex

static void _glewInfo_GL_IBM_cull_vertex (void)
{
  glewPrintExt("GL_IBM_cull_vertex", GLEW_IBM_cull_vertex, glewIsSupported("GL_IBM_cull_vertex"), glewGetExtension("GL_IBM_cull_vertex"));
}

#endif /* GL_IBM_cull_vertex */

#ifdef GL_IBM_multimode_draw_arrays

static void _glewInfo_GL_IBM_multimode_draw_arrays (void)
{
  glewPrintExt("GL_IBM_multimode_draw_arrays", GLEW_IBM_multimode_draw_arrays, glewIsSupported("GL_IBM_multimode_draw_arrays"), glewGetExtension("GL_IBM_multimode_draw_arrays"));

  glewInfoFunc("glMultiModeDrawArraysIBM", glMultiModeDrawArraysIBM == NULL);
  glewInfoFunc("glMultiModeDrawElementsIBM", glMultiModeDrawElementsIBM == NULL);
}

#endif /* GL_IBM_multimode_draw_arrays */

#ifdef GL_IBM_rasterpos_clip

static void _glewInfo_GL_IBM_rasterpos_clip (void)
{
  glewPrintExt("GL_IBM_rasterpos_clip", GLEW_IBM_rasterpos_clip, glewIsSupported("GL_IBM_rasterpos_clip"), glewGetExtension("GL_IBM_rasterpos_clip"));
}

#endif /* GL_IBM_rasterpos_clip */

#ifdef GL_IBM_static_data

static void _glewInfo_GL_IBM_static_data (void)
{
  glewPrintExt("GL_IBM_static_data", GLEW_IBM_static_data, glewIsSupported("GL_IBM_static_data"), glewGetExtension("GL_IBM_static_data"));
}

#endif /* GL_IBM_static_data */

#ifdef GL_IBM_texture_mirrored_repeat

static void _glewInfo_GL_IBM_texture_mirrored_repeat (void)
{
  glewPrintExt("GL_IBM_texture_mirrored_repeat", GLEW_IBM_texture_mirrored_repeat, glewIsSupported("GL_IBM_texture_mirrored_repeat"), glewGetExtension("GL_IBM_texture_mirrored_repeat"));
}

#endif /* GL_IBM_texture_mirrored_repeat */

#ifdef GL_IBM_vertex_array_lists

static void _glewInfo_GL_IBM_vertex_array_lists (void)
{
  glewPrintExt("GL_IBM_vertex_array_lists", GLEW_IBM_vertex_array_lists, glewIsSupported("GL_IBM_vertex_array_lists"), glewGetExtension("GL_IBM_vertex_array_lists"));

  glewInfoFunc("glColorPointerListIBM", glColorPointerListIBM == NULL);
  glewInfoFunc("glEdgeFlagPointerListIBM", glEdgeFlagPointerListIBM == NULL);
  glewInfoFunc("glFogCoordPointerListIBM", glFogCoordPointerListIBM == NULL);
  glewInfoFunc("glIndexPointerListIBM", glIndexPointerListIBM == NULL);
  glewInfoFunc("glNormalPointerListIBM", glNormalPointerListIBM == NULL);
  glewInfoFunc("glSecondaryColorPointerListIBM", glSecondaryColorPointerListIBM == NULL);
  glewInfoFunc("glTexCoordPointerListIBM", glTexCoordPointerListIBM == NULL);
  glewInfoFunc("glVertexPointerListIBM", glVertexPointerListIBM == NULL);
}

#endif /* GL_IBM_vertex_array_lists */

#ifdef GL_INGR_color_clamp

static void _glewInfo_GL_INGR_color_clamp (void)
{
  glewPrintExt("GL_INGR_color_clamp", GLEW_INGR_color_clamp, glewIsSupported("GL_INGR_color_clamp"), glewGetExtension("GL_INGR_color_clamp"));
}

#endif /* GL_INGR_color_clamp */

#ifdef GL_INGR_interlace_read

static void _glewInfo_GL_INGR_interlace_read (void)
{
  glewPrintExt("GL_INGR_interlace_read", GLEW_INGR_interlace_read, glewIsSupported("GL_INGR_interlace_read"), glewGetExtension("GL_INGR_interlace_read"));
}

#endif /* GL_INGR_interlace_read */

#ifdef GL_INTEL_conservative_rasterization

static void _glewInfo_GL_INTEL_conservative_rasterization (void)
{
  glewPrintExt("GL_INTEL_conservative_rasterization", GLEW_INTEL_conservative_rasterization, glewIsSupported("GL_INTEL_conservative_rasterization"), glewGetExtension("GL_INTEL_conservative_rasterization"));
}

#endif /* GL_INTEL_conservative_rasterization */

#ifdef GL_INTEL_fragment_shader_ordering

static void _glewInfo_GL_INTEL_fragment_shader_ordering (void)
{
  glewPrintExt("GL_INTEL_fragment_shader_ordering", GLEW_INTEL_fragment_shader_ordering, glewIsSupported("GL_INTEL_fragment_shader_ordering"), glewGetExtension("GL_INTEL_fragment_shader_ordering"));
}

#endif /* GL_INTEL_fragment_shader_ordering */

#ifdef GL_INTEL_framebuffer_CMAA

static void _glewInfo_GL_INTEL_framebuffer_CMAA (void)
{
  glewPrintExt("GL_INTEL_framebuffer_CMAA", GLEW_INTEL_framebuffer_CMAA, glewIsSupported("GL_INTEL_framebuffer_CMAA"), glewGetExtension("GL_INTEL_framebuffer_CMAA"));
}

#endif /* GL_INTEL_framebuffer_CMAA */

#ifdef GL_INTEL_map_texture

static void _glewInfo_GL_INTEL_map_texture (void)
{
  glewPrintExt("GL_INTEL_map_texture", GLEW_INTEL_map_texture, glewIsSupported("GL_INTEL_map_texture"), glewGetExtension("GL_INTEL_map_texture"));

  glewInfoFunc("glMapTexture2DINTEL", glMapTexture2DINTEL == NULL);
  glewInfoFunc("glSyncTextureINTEL", glSyncTextureINTEL == NULL);
  glewInfoFunc("glUnmapTexture2DINTEL", glUnmapTexture2DINTEL == NULL);
}

#endif /* GL_INTEL_map_texture */

#ifdef GL_INTEL_parallel_arrays

static void _glewInfo_GL_INTEL_parallel_arrays (void)
{
  glewPrintExt("GL_INTEL_parallel_arrays", GLEW_INTEL_parallel_arrays, glewIsSupported("GL_INTEL_parallel_arrays"), glewGetExtension("GL_INTEL_parallel_arrays"));

  glewInfoFunc("glColorPointervINTEL", glColorPointervINTEL == NULL);
  glewInfoFunc("glNormalPointervINTEL", glNormalPointervINTEL == NULL);
  glewInfoFunc("glTexCoordPointervINTEL", glTexCoordPointervINTEL == NULL);
  glewInfoFunc("glVertexPointervINTEL", glVertexPointervINTEL == NULL);
}

#endif /* GL_INTEL_parallel_arrays */

#ifdef GL_INTEL_performance_query

static void _glewInfo_GL_INTEL_performance_query (void)
{
  glewPrintExt("GL_INTEL_performance_query", GLEW_INTEL_performance_query, glewIsSupported("GL_INTEL_performance_query"), glewGetExtension("GL_INTEL_performance_query"));

  glewInfoFunc("glBeginPerfQueryINTEL", glBeginPerfQueryINTEL == NULL);
  glewInfoFunc("glCreatePerfQueryINTEL", glCreatePerfQueryINTEL == NULL);
  glewInfoFunc("glDeletePerfQueryINTEL", glDeletePerfQueryINTEL == NULL);
  glewInfoFunc("glEndPerfQueryINTEL", glEndPerfQueryINTEL == NULL);
  glewInfoFunc("glGetFirstPerfQueryIdINTEL", glGetFirstPerfQueryIdINTEL == NULL);
  glewInfoFunc("glGetNextPerfQueryIdINTEL", glGetNextPerfQueryIdINTEL == NULL);
  glewInfoFunc("glGetPerfCounterInfoINTEL", glGetPerfCounterInfoINTEL == NULL);
  glewInfoFunc("glGetPerfQueryDataINTEL", glGetPerfQueryDataINTEL == NULL);
  glewInfoFunc("glGetPerfQueryIdByNameINTEL", glGetPerfQueryIdByNameINTEL == NULL);
  glewInfoFunc("glGetPerfQueryInfoINTEL", glGetPerfQueryInfoINTEL == NULL);
}

#endif /* GL_INTEL_performance_query */

#ifdef GL_INTEL_texture_scissor

static void _glewInfo_GL_INTEL_texture_scissor (void)
{
  glewPrintExt("GL_INTEL_texture_scissor", GLEW_INTEL_texture_scissor, glewIsSupported("GL_INTEL_texture_scissor"), glewGetExtension("GL_INTEL_texture_scissor"));

  glewInfoFunc("glTexScissorFuncINTEL", glTexScissorFuncINTEL == NULL);
  glewInfoFunc("glTexScissorINTEL", glTexScissorINTEL == NULL);
}

#endif /* GL_INTEL_texture_scissor */

#ifdef GL_KHR_blend_equation_advanced

static void _glewInfo_GL_KHR_blend_equation_advanced (void)
{
  glewPrintExt("GL_KHR_blend_equation_advanced", GLEW_KHR_blend_equation_advanced, glewIsSupported("GL_KHR_blend_equation_advanced"), glewGetExtension("GL_KHR_blend_equation_advanced"));

  glewInfoFunc("glBlendBarrierKHR", glBlendBarrierKHR == NULL);
}

#endif /* GL_KHR_blend_equation_advanced */

#ifdef GL_KHR_blend_equation_advanced_coherent

static void _glewInfo_GL_KHR_blend_equation_advanced_coherent (void)
{
  glewPrintExt("GL_KHR_blend_equation_advanced_coherent", GLEW_KHR_blend_equation_advanced_coherent, glewIsSupported("GL_KHR_blend_equation_advanced_coherent"), glewGetExtension("GL_KHR_blend_equation_advanced_coherent"));
}

#endif /* GL_KHR_blend_equation_advanced_coherent */

#ifdef GL_KHR_context_flush_control

static void _glewInfo_GL_KHR_context_flush_control (void)
{
  glewPrintExt("GL_KHR_context_flush_control", GLEW_KHR_context_flush_control, glewIsSupported("GL_KHR_context_flush_control"), glewGetExtension("GL_KHR_context_flush_control"));
}

#endif /* GL_KHR_context_flush_control */

#ifdef GL_KHR_debug

static void _glewInfo_GL_KHR_debug (void)
{
  glewPrintExt("GL_KHR_debug", GLEW_KHR_debug, glewIsSupported("GL_KHR_debug"), glewGetExtension("GL_KHR_debug"));

  glewInfoFunc("glDebugMessageCallback", glDebugMessageCallback == NULL);
  glewInfoFunc("glDebugMessageControl", glDebugMessageControl == NULL);
  glewInfoFunc("glDebugMessageInsert", glDebugMessageInsert == NULL);
  glewInfoFunc("glGetDebugMessageLog", glGetDebugMessageLog == NULL);
  glewInfoFunc("glGetObjectLabel", glGetObjectLabel == NULL);
  glewInfoFunc("glGetObjectPtrLabel", glGetObjectPtrLabel == NULL);
  glewInfoFunc("glObjectLabel", glObjectLabel == NULL);
  glewInfoFunc("glObjectPtrLabel", glObjectPtrLabel == NULL);
  glewInfoFunc("glPopDebugGroup", glPopDebugGroup == NULL);
  glewInfoFunc("glPushDebugGroup", glPushDebugGroup == NULL);
}

#endif /* GL_KHR_debug */

#ifdef GL_KHR_no_error

static void _glewInfo_GL_KHR_no_error (void)
{
  glewPrintExt("GL_KHR_no_error", GLEW_KHR_no_error, glewIsSupported("GL_KHR_no_error"), glewGetExtension("GL_KHR_no_error"));
}

#endif /* GL_KHR_no_error */

#ifdef GL_KHR_parallel_shader_compile

static void _glewInfo_GL_KHR_parallel_shader_compile (void)
{
  glewPrintExt("GL_KHR_parallel_shader_compile", GLEW_KHR_parallel_shader_compile, glewIsSupported("GL_KHR_parallel_shader_compile"), glewGetExtension("GL_KHR_parallel_shader_compile"));

  glewInfoFunc("glMaxShaderCompilerThreadsKHR", glMaxShaderCompilerThreadsKHR == NULL);
}

#endif /* GL_KHR_parallel_shader_compile */

#ifdef GL_KHR_robust_buffer_access_behavior

static void _glewInfo_GL_KHR_robust_buffer_access_behavior (void)
{
  glewPrintExt("GL_KHR_robust_buffer_access_behavior", GLEW_KHR_robust_buffer_access_behavior, glewIsSupported("GL_KHR_robust_buffer_access_behavior"), glewGetExtension("GL_KHR_robust_buffer_access_behavior"));
}

#endif /* GL_KHR_robust_buffer_access_behavior */

#ifdef GL_KHR_robustness

static void _glewInfo_GL_KHR_robustness (void)
{
  glewPrintExt("GL_KHR_robustness", GLEW_KHR_robustness, glewIsSupported("GL_KHR_robustness"), glewGetExtension("GL_KHR_robustness"));

  glewInfoFunc("glGetnUniformfv", glGetnUniformfv == NULL);
  glewInfoFunc("glGetnUniformiv", glGetnUniformiv == NULL);
  glewInfoFunc("glGetnUniformuiv", glGetnUniformuiv == NULL);
  glewInfoFunc("glReadnPixels", glReadnPixels == NULL);
}

#endif /* GL_KHR_robustness */

#ifdef GL_KHR_texture_compression_astc_hdr

static void _glewInfo_GL_KHR_texture_compression_astc_hdr (void)
{
  glewPrintExt("GL_KHR_texture_compression_astc_hdr", GLEW_KHR_texture_compression_astc_hdr, glewIsSupported("GL_KHR_texture_compression_astc_hdr"), glewGetExtension("GL_KHR_texture_compression_astc_hdr"));
}

#endif /* GL_KHR_texture_compression_astc_hdr */

#ifdef GL_KHR_texture_compression_astc_ldr

static void _glewInfo_GL_KHR_texture_compression_astc_ldr (void)
{
  glewPrintExt("GL_KHR_texture_compression_astc_ldr", GLEW_KHR_texture_compression_astc_ldr, glewIsSupported("GL_KHR_texture_compression_astc_ldr"), glewGetExtension("GL_KHR_texture_compression_astc_ldr"));
}

#endif /* GL_KHR_texture_compression_astc_ldr */

#ifdef GL_KHR_texture_compression_astc_sliced_3d

static void _glewInfo_GL_KHR_texture_compression_astc_sliced_3d (void)
{
  glewPrintExt("GL_KHR_texture_compression_astc_sliced_3d", GLEW_KHR_texture_compression_astc_sliced_3d, glewIsSupported("GL_KHR_texture_compression_astc_sliced_3d"), glewGetExtension("GL_KHR_texture_compression_astc_sliced_3d"));
}

#endif /* GL_KHR_texture_compression_astc_sliced_3d */

#ifdef GL_KTX_buffer_region

static void _glewInfo_GL_KTX_buffer_region (void)
{
  glewPrintExt("GL_KTX_buffer_region", GLEW_KTX_buffer_region, glewIsSupported("GL_KTX_buffer_region"), glewGetExtension("GL_KTX_buffer_region"));

  glewInfoFunc("glBufferRegionEnabled", glBufferRegionEnabled == NULL);
  glewInfoFunc("glDeleteBufferRegion", glDeleteBufferRegion == NULL);
  glewInfoFunc("glDrawBufferRegion", glDrawBufferRegion == NULL);
  glewInfoFunc("glNewBufferRegion", glNewBufferRegion == NULL);
  glewInfoFunc("glReadBufferRegion", glReadBufferRegion == NULL);
}

#endif /* GL_KTX_buffer_region */

#ifdef GL_MESAX_texture_stack

static void _glewInfo_GL_MESAX_texture_stack (void)
{
  glewPrintExt("GL_MESAX_texture_stack", GLEW_MESAX_texture_stack, glewIsSupported("GL_MESAX_texture_stack"), glewGetExtension("GL_MESAX_texture_stack"));
}

#endif /* GL_MESAX_texture_stack */

#ifdef GL_MESA_pack_invert

static void _glewInfo_GL_MESA_pack_invert (void)
{
  glewPrintExt("GL_MESA_pack_invert", GLEW_MESA_pack_invert, glewIsSupported("GL_MESA_pack_invert"), glewGetExtension("GL_MESA_pack_invert"));
}

#endif /* GL_MESA_pack_invert */

#ifdef GL_MESA_resize_buffers

static void _glewInfo_GL_MESA_resize_buffers (void)
{
  glewPrintExt("GL_MESA_resize_buffers", GLEW_MESA_resize_buffers, glewIsSupported("GL_MESA_resize_buffers"), glewGetExtension("GL_MESA_resize_buffers"));

  glewInfoFunc("glResizeBuffersMESA", glResizeBuffersMESA == NULL);
}

#endif /* GL_MESA_resize_buffers */

#ifdef GL_MESA_shader_integer_functions

static void _glewInfo_GL_MESA_shader_integer_functions (void)
{
  glewPrintExt("GL_MESA_shader_integer_functions", GLEW_MESA_shader_integer_functions, glewIsSupported("GL_MESA_shader_integer_functions"), glewGetExtension("GL_MESA_shader_integer_functions"));
}

#endif /* GL_MESA_shader_integer_functions */

#ifdef GL_MESA_window_pos

static void _glewInfo_GL_MESA_window_pos (void)
{
  glewPrintExt("GL_MESA_window_pos", GLEW_MESA_window_pos, glewIsSupported("GL_MESA_window_pos"), glewGetExtension("GL_MESA_window_pos"));

  glewInfoFunc("glWindowPos2dMESA", glWindowPos2dMESA == NULL);
  glewInfoFunc("glWindowPos2dvMESA", glWindowPos2dvMESA == NULL);
  glewInfoFunc("glWindowPos2fMESA", glWindowPos2fMESA == NULL);
  glewInfoFunc("glWindowPos2fvMESA", glWindowPos2fvMESA == NULL);
  glewInfoFunc("glWindowPos2iMESA", glWindowPos2iMESA == NULL);
  glewInfoFunc("glWindowPos2ivMESA", glWindowPos2ivMESA == NULL);
  glewInfoFunc("glWindowPos2sMESA", glWindowPos2sMESA == NULL);
  glewInfoFunc("glWindowPos2svMESA", glWindowPos2svMESA == NULL);
  glewInfoFunc("glWindowPos3dMESA", glWindowPos3dMESA == NULL);
  glewInfoFunc("glWindowPos3dvMESA", glWindowPos3dvMESA == NULL);
  glewInfoFunc("glWindowPos3fMESA", glWindowPos3fMESA == NULL);
  glewInfoFunc("glWindowPos3fvMESA", glWindowPos3fvMESA == NULL);
  glewInfoFunc("glWindowPos3iMESA", glWindowPos3iMESA == NULL);
  glewInfoFunc("glWindowPos3ivMESA", glWindowPos3ivMESA == NULL);
  glewInfoFunc("glWindowPos3sMESA", glWindowPos3sMESA == NULL);
  glewInfoFunc("glWindowPos3svMESA", glWindowPos3svMESA == NULL);
  glewInfoFunc("glWindowPos4dMESA", glWindowPos4dMESA == NULL);
  glewInfoFunc("glWindowPos4dvMESA", glWindowPos4dvMESA == NULL);
  glewInfoFunc("glWindowPos4fMESA", glWindowPos4fMESA == NULL);
  glewInfoFunc("glWindowPos4fvMESA", glWindowPos4fvMESA == NULL);
  glewInfoFunc("glWindowPos4iMESA", glWindowPos4iMESA == NULL);
  glewInfoFunc("glWindowPos4ivMESA", glWindowPos4ivMESA == NULL);
  glewInfoFunc("glWindowPos4sMESA", glWindowPos4sMESA == NULL);
  glewInfoFunc("glWindowPos4svMESA", glWindowPos4svMESA == NULL);
}

#endif /* GL_MESA_window_pos */

#ifdef GL_MESA_ycbcr_texture

static void _glewInfo_GL_MESA_ycbcr_texture (void)
{
  glewPrintExt("GL_MESA_ycbcr_texture", GLEW_MESA_ycbcr_texture, glewIsSupported("GL_MESA_ycbcr_texture"), glewGetExtension("GL_MESA_ycbcr_texture"));
}

#endif /* GL_MESA_ycbcr_texture */

#ifdef GL_NVX_blend_equation_advanced_multi_draw_buffers

static void _glewInfo_GL_NVX_blend_equation_advanced_multi_draw_buffers (void)
{
  glewPrintExt("GL_NVX_blend_equation_advanced_multi_draw_buffers", GLEW_NVX_blend_equation_advanced_multi_draw_buffers, glewIsSupported("GL_NVX_blend_equation_advanced_multi_draw_buffers"), glewGetExtension("GL_NVX_blend_equation_advanced_multi_draw_buffers"));
}

#endif /* GL_NVX_blend_equation_advanced_multi_draw_buffers */

#ifdef GL_NVX_conditional_render

static void _glewInfo_GL_NVX_conditional_render (void)
{
  glewPrintExt("GL_NVX_conditional_render", GLEW_NVX_conditional_render, glewIsSupported("GL_NVX_conditional_render"), glewGetExtension("GL_NVX_conditional_render"));

  glewInfoFunc("glBeginConditionalRenderNVX", glBeginConditionalRenderNVX == NULL);
  glewInfoFunc("glEndConditionalRenderNVX", glEndConditionalRenderNVX == NULL);
}

#endif /* GL_NVX_conditional_render */

#ifdef GL_NVX_gpu_memory_info

static void _glewInfo_GL_NVX_gpu_memory_info (void)
{
  glewPrintExt("GL_NVX_gpu_memory_info", GLEW_NVX_gpu_memory_info, glewIsSupported("GL_NVX_gpu_memory_info"), glewGetExtension("GL_NVX_gpu_memory_info"));
}

#endif /* GL_NVX_gpu_memory_info */

#ifdef GL_NVX_linked_gpu_multicast

static void _glewInfo_GL_NVX_linked_gpu_multicast (void)
{
  glewPrintExt("GL_NVX_linked_gpu_multicast", GLEW_NVX_linked_gpu_multicast, glewIsSupported("GL_NVX_linked_gpu_multicast"), glewGetExtension("GL_NVX_linked_gpu_multicast"));

  glewInfoFunc("glLGPUCopyImageSubDataNVX", glLGPUCopyImageSubDataNVX == NULL);
  glewInfoFunc("glLGPUInterlockNVX", glLGPUInterlockNVX == NULL);
  glewInfoFunc("glLGPUNamedBufferSubDataNVX", glLGPUNamedBufferSubDataNVX == NULL);
}

#endif /* GL_NVX_linked_gpu_multicast */

#ifdef GL_NV_3dvision_settings

static void _glewInfo_GL_NV_3dvision_settings (void)
{
  glewPrintExt("GL_NV_3dvision_settings", GLEW_NV_3dvision_settings, glewIsSupported("GL_NV_3dvision_settings"), glewGetExtension("GL_NV_3dvision_settings"));

  glewInfoFunc("glStereoParameterfNV", glStereoParameterfNV == NULL);
  glewInfoFunc("glStereoParameteriNV", glStereoParameteriNV == NULL);
}

#endif /* GL_NV_3dvision_settings */

#ifdef GL_NV_EGL_stream_consumer_external

static void _glewInfo_GL_NV_EGL_stream_consumer_external (void)
{
  glewPrintExt("GL_NV_EGL_stream_consumer_external", GLEW_NV_EGL_stream_consumer_external, glewIsSupported("GL_NV_EGL_stream_consumer_external"), glewGetExtension("GL_NV_EGL_stream_consumer_external"));
}

#endif /* GL_NV_EGL_stream_consumer_external */

#ifdef GL_NV_alpha_to_coverage_dither_control

static void _glewInfo_GL_NV_alpha_to_coverage_dither_control (void)
{
  glewPrintExt("GL_NV_alpha_to_coverage_dither_control", GLEW_NV_alpha_to_coverage_dither_control, glewIsSupported("GL_NV_alpha_to_coverage_dither_control"), glewGetExtension("GL_NV_alpha_to_coverage_dither_control"));
}

#endif /* GL_NV_alpha_to_coverage_dither_control */

#ifdef GL_NV_bgr

static void _glewInfo_GL_NV_bgr (void)
{
  glewPrintExt("GL_NV_bgr", GLEW_NV_bgr, glewIsSupported("GL_NV_bgr"), glewGetExtension("GL_NV_bgr"));
}

#endif /* GL_NV_bgr */

#ifdef GL_NV_bindless_multi_draw_indirect

static void _glewInfo_GL_NV_bindless_multi_draw_indirect (void)
{
  glewPrintExt("GL_NV_bindless_multi_draw_indirect", GLEW_NV_bindless_multi_draw_indirect, glewIsSupported("GL_NV_bindless_multi_draw_indirect"), glewGetExtension("GL_NV_bindless_multi_draw_indirect"));

  glewInfoFunc("glMultiDrawArraysIndirectBindlessNV", glMultiDrawArraysIndirectBindlessNV == NULL);
  glewInfoFunc("glMultiDrawElementsIndirectBindlessNV", glMultiDrawElementsIndirectBindlessNV == NULL);
}

#endif /* GL_NV_bindless_multi_draw_indirect */

#ifdef GL_NV_bindless_multi_draw_indirect_count

static void _glewInfo_GL_NV_bindless_multi_draw_indirect_count (void)
{
  glewPrintExt("GL_NV_bindless_multi_draw_indirect_count", GLEW_NV_bindless_multi_draw_indirect_count, glewIsSupported("GL_NV_bindless_multi_draw_indirect_count"), glewGetExtension("GL_NV_bindless_multi_draw_indirect_count"));

  glewInfoFunc("glMultiDrawArraysIndirectBindlessCountNV", glMultiDrawArraysIndirectBindlessCountNV == NULL);
  glewInfoFunc("glMultiDrawElementsIndirectBindlessCountNV", glMultiDrawElementsIndirectBindlessCountNV == NULL);
}

#endif /* GL_NV_bindless_multi_draw_indirect_count */

#ifdef GL_NV_bindless_texture

static void _glewInfo_GL_NV_bindless_texture (void)
{
  glewPrintExt("GL_NV_bindless_texture", GLEW_NV_bindless_texture, glewIsSupported("GL_NV_bindless_texture"), glewGetExtension("GL_NV_bindless_texture"));

  glewInfoFunc("glGetImageHandleNV", glGetImageHandleNV == NULL);
  glewInfoFunc("glGetTextureHandleNV", glGetTextureHandleNV == NULL);
  glewInfoFunc("glGetTextureSamplerHandleNV", glGetTextureSamplerHandleNV == NULL);
  glewInfoFunc("glIsImageHandleResidentNV", glIsImageHandleResidentNV == NULL);
  glewInfoFunc("glIsTextureHandleResidentNV", glIsTextureHandleResidentNV == NULL);
  glewInfoFunc("glMakeImageHandleNonResidentNV", glMakeImageHandleNonResidentNV == NULL);
  glewInfoFunc("glMakeImageHandleResidentNV", glMakeImageHandleResidentNV == NULL);
  glewInfoFunc("glMakeTextureHandleNonResidentNV", glMakeTextureHandleNonResidentNV == NULL);
  glewInfoFunc("glMakeTextureHandleResidentNV", glMakeTextureHandleResidentNV == NULL);
  glewInfoFunc("glProgramUniformHandleui64NV", glProgramUniformHandleui64NV == NULL);
  glewInfoFunc("glProgramUniformHandleui64vNV", glProgramUniformHandleui64vNV == NULL);
  glewInfoFunc("glUniformHandleui64NV", glUniformHandleui64NV == NULL);
  glewInfoFunc("glUniformHandleui64vNV", glUniformHandleui64vNV == NULL);
}

#endif /* GL_NV_bindless_texture */

#ifdef GL_NV_blend_equation_advanced

static void _glewInfo_GL_NV_blend_equation_advanced (void)
{
  glewPrintExt("GL_NV_blend_equation_advanced", GLEW_NV_blend_equation_advanced, glewIsSupported("GL_NV_blend_equation_advanced"), glewGetExtension("GL_NV_blend_equation_advanced"));

  glewInfoFunc("glBlendBarrierNV", glBlendBarrierNV == NULL);
  glewInfoFunc("glBlendParameteriNV", glBlendParameteriNV == NULL);
}

#endif /* GL_NV_blend_equation_advanced */

#ifdef GL_NV_blend_equation_advanced_coherent

static void _glewInfo_GL_NV_blend_equation_advanced_coherent (void)
{
  glewPrintExt("GL_NV_blend_equation_advanced_coherent", GLEW_NV_blend_equation_advanced_coherent, glewIsSupported("GL_NV_blend_equation_advanced_coherent"), glewGetExtension("GL_NV_blend_equation_advanced_coherent"));
}

#endif /* GL_NV_blend_equation_advanced_coherent */

#ifdef GL_NV_blend_minmax_factor

static void _glewInfo_GL_NV_blend_minmax_factor (void)
{
  glewPrintExt("GL_NV_blend_minmax_factor", GLEW_NV_blend_minmax_factor, glewIsSupported("GL_NV_blend_minmax_factor"), glewGetExtension("GL_NV_blend_minmax_factor"));
}

#endif /* GL_NV_blend_minmax_factor */

#ifdef GL_NV_blend_square

static void _glewInfo_GL_NV_blend_square (void)
{
  glewPrintExt("GL_NV_blend_square", GLEW_NV_blend_square, glewIsSupported("GL_NV_blend_square"), glewGetExtension("GL_NV_blend_square"));
}

#endif /* GL_NV_blend_square */

#ifdef GL_NV_clip_space_w_scaling

static void _glewInfo_GL_NV_clip_space_w_scaling (void)
{
  glewPrintExt("GL_NV_clip_space_w_scaling", GLEW_NV_clip_space_w_scaling, glewIsSupported("GL_NV_clip_space_w_scaling"), glewGetExtension("GL_NV_clip_space_w_scaling"));

  glewInfoFunc("glViewportPositionWScaleNV", glViewportPositionWScaleNV == NULL);
}

#endif /* GL_NV_clip_space_w_scaling */

#ifdef GL_NV_command_list

static void _glewInfo_GL_NV_command_list (void)
{
  glewPrintExt("GL_NV_command_list", GLEW_NV_command_list, glewIsSupported("GL_NV_command_list"), glewGetExtension("GL_NV_command_list"));

  glewInfoFunc("glCallCommandListNV", glCallCommandListNV == NULL);
  glewInfoFunc("glCommandListSegmentsNV", glCommandListSegmentsNV == NULL);
  glewInfoFunc("glCompileCommandListNV", glCompileCommandListNV == NULL);
  glewInfoFunc("glCreateCommandListsNV", glCreateCommandListsNV == NULL);
  glewInfoFunc("glCreateStatesNV", glCreateStatesNV == NULL);
  glewInfoFunc("glDeleteCommandListsNV", glDeleteCommandListsNV == NULL);
  glewInfoFunc("glDeleteStatesNV", glDeleteStatesNV == NULL);
  glewInfoFunc("glDrawCommandsAddressNV", glDrawCommandsAddressNV == NULL);
  glewInfoFunc("glDrawCommandsNV", glDrawCommandsNV == NULL);
  glewInfoFunc("glDrawCommandsStatesAddressNV", glDrawCommandsStatesAddressNV == NULL);
  glewInfoFunc("glDrawCommandsStatesNV", glDrawCommandsStatesNV == NULL);
  glewInfoFunc("glGetCommandHeaderNV", glGetCommandHeaderNV == NULL);
  glewInfoFunc("glGetStageIndexNV", glGetStageIndexNV == NULL);
  glewInfoFunc("glIsCommandListNV", glIsCommandListNV == NULL);
  glewInfoFunc("glIsStateNV", glIsStateNV == NULL);
  glewInfoFunc("glListDrawCommandsStatesClientNV", glListDrawCommandsStatesClientNV == NULL);
  glewInfoFunc("glStateCaptureNV", glStateCaptureNV == NULL);
}

#endif /* GL_NV_command_list */

#ifdef GL_NV_compute_program5

static void _glewInfo_GL_NV_compute_program5 (void)
{
  glewPrintExt("GL_NV_compute_program5", GLEW_NV_compute_program5, glewIsSupported("GL_NV_compute_program5"), glewGetExtension("GL_NV_compute_program5"));
}

#endif /* GL_NV_compute_program5 */

#ifdef GL_NV_conditional_render

static void _glewInfo_GL_NV_conditional_render (void)
{
  glewPrintExt("GL_NV_conditional_render", GLEW_NV_conditional_render, glewIsSupported("GL_NV_conditional_render"), glewGetExtension("GL_NV_conditional_render"));

  glewInfoFunc("glBeginConditionalRenderNV", glBeginConditionalRenderNV == NULL);
  glewInfoFunc("glEndConditionalRenderNV", glEndConditionalRenderNV == NULL);
}

#endif /* GL_NV_conditional_render */

#ifdef GL_NV_conservative_raster

static void _glewInfo_GL_NV_conservative_raster (void)
{
  glewPrintExt("GL_NV_conservative_raster", GLEW_NV_conservative_raster, glewIsSupported("GL_NV_conservative_raster"), glewGetExtension("GL_NV_conservative_raster"));

  glewInfoFunc("glSubpixelPrecisionBiasNV", glSubpixelPrecisionBiasNV == NULL);
}

#endif /* GL_NV_conservative_raster */

#ifdef GL_NV_conservative_raster_dilate

static void _glewInfo_GL_NV_conservative_raster_dilate (void)
{
  glewPrintExt("GL_NV_conservative_raster_dilate", GLEW_NV_conservative_raster_dilate, glewIsSupported("GL_NV_conservative_raster_dilate"), glewGetExtension("GL_NV_conservative_raster_dilate"));

  glewInfoFunc("glConservativeRasterParameterfNV", glConservativeRasterParameterfNV == NULL);
}

#endif /* GL_NV_conservative_raster_dilate */

#ifdef GL_NV_conservative_raster_pre_snap_triangles

static void _glewInfo_GL_NV_conservative_raster_pre_snap_triangles (void)
{
  glewPrintExt("GL_NV_conservative_raster_pre_snap_triangles", GLEW_NV_conservative_raster_pre_snap_triangles, glewIsSupported("GL_NV_conservative_raster_pre_snap_triangles"), glewGetExtension("GL_NV_conservative_raster_pre_snap_triangles"));

  glewInfoFunc("glConservativeRasterParameteriNV", glConservativeRasterParameteriNV == NULL);
}

#endif /* GL_NV_conservative_raster_pre_snap_triangles */

#ifdef GL_NV_copy_buffer

static void _glewInfo_GL_NV_copy_buffer (void)
{
  glewPrintExt("GL_NV_copy_buffer", GLEW_NV_copy_buffer, glewIsSupported("GL_NV_copy_buffer"), glewGetExtension("GL_NV_copy_buffer"));

  glewInfoFunc("glCopyBufferSubDataNV", glCopyBufferSubDataNV == NULL);
}

#endif /* GL_NV_copy_buffer */

#ifdef GL_NV_copy_depth_to_color

static void _glewInfo_GL_NV_copy_depth_to_color (void)
{
  glewPrintExt("GL_NV_copy_depth_to_color", GLEW_NV_copy_depth_to_color, glewIsSupported("GL_NV_copy_depth_to_color"), glewGetExtension("GL_NV_copy_depth_to_color"));
}

#endif /* GL_NV_copy_depth_to_color */

#ifdef GL_NV_copy_image

static void _glewInfo_GL_NV_copy_image (void)
{
  glewPrintExt("GL_NV_copy_image", GLEW_NV_copy_image, glewIsSupported("GL_NV_copy_image"), glewGetExtension("GL_NV_copy_image"));

  glewInfoFunc("glCopyImageSubDataNV", glCopyImageSubDataNV == NULL);
}

#endif /* GL_NV_copy_image */

#ifdef GL_NV_deep_texture3D

static void _glewInfo_GL_NV_deep_texture3D (void)
{
  glewPrintExt("GL_NV_deep_texture3D", GLEW_NV_deep_texture3D, glewIsSupported("GL_NV_deep_texture3D"), glewGetExtension("GL_NV_deep_texture3D"));
}

#endif /* GL_NV_deep_texture3D */

#ifdef GL_NV_depth_buffer_float

static void _glewInfo_GL_NV_depth_buffer_float (void)
{
  glewPrintExt("GL_NV_depth_buffer_float", GLEW_NV_depth_buffer_float, glewIsSupported("GL_NV_depth_buffer_float"), glewGetExtension("GL_NV_depth_buffer_float"));

  glewInfoFunc("glClearDepthdNV", glClearDepthdNV == NULL);
  glewInfoFunc("glDepthBoundsdNV", glDepthBoundsdNV == NULL);
  glewInfoFunc("glDepthRangedNV", glDepthRangedNV == NULL);
}

#endif /* GL_NV_depth_buffer_float */

#ifdef GL_NV_depth_clamp

static void _glewInfo_GL_NV_depth_clamp (void)
{
  glewPrintExt("GL_NV_depth_clamp", GLEW_NV_depth_clamp, glewIsSupported("GL_NV_depth_clamp"), glewGetExtension("GL_NV_depth_clamp"));
}

#endif /* GL_NV_depth_clamp */

#ifdef GL_NV_depth_range_unclamped

static void _glewInfo_GL_NV_depth_range_unclamped (void)
{
  glewPrintExt("GL_NV_depth_range_unclamped", GLEW_NV_depth_range_unclamped, glewIsSupported("GL_NV_depth_range_unclamped"), glewGetExtension("GL_NV_depth_range_unclamped"));
}

#endif /* GL_NV_depth_range_unclamped */

#ifdef GL_NV_draw_buffers

static void _glewInfo_GL_NV_draw_buffers (void)
{
  glewPrintExt("GL_NV_draw_buffers", GLEW_NV_draw_buffers, glewIsSupported("GL_NV_draw_buffers"), glewGetExtension("GL_NV_draw_buffers"));

  glewInfoFunc("glDrawBuffersNV", glDrawBuffersNV == NULL);
}

#endif /* GL_NV_draw_buffers */

#ifdef GL_NV_draw_instanced

static void _glewInfo_GL_NV_draw_instanced (void)
{
  glewPrintExt("GL_NV_draw_instanced", GLEW_NV_draw_instanced, glewIsSupported("GL_NV_draw_instanced"), glewGetExtension("GL_NV_draw_instanced"));

  glewInfoFunc("glDrawArraysInstancedNV", glDrawArraysInstancedNV == NULL);
  glewInfoFunc("glDrawElementsInstancedNV", glDrawElementsInstancedNV == NULL);
}

#endif /* GL_NV_draw_instanced */

#ifdef GL_NV_draw_texture

static void _glewInfo_GL_NV_draw_texture (void)
{
  glewPrintExt("GL_NV_draw_texture", GLEW_NV_draw_texture, glewIsSupported("GL_NV_draw_texture"), glewGetExtension("GL_NV_draw_texture"));

  glewInfoFunc("glDrawTextureNV", glDrawTextureNV == NULL);
}

#endif /* GL_NV_draw_texture */

#ifdef GL_NV_draw_vulkan_image

static void _glewInfo_GL_NV_draw_vulkan_image (void)
{
  glewPrintExt("GL_NV_draw_vulkan_image", GLEW_NV_draw_vulkan_image, glewIsSupported("GL_NV_draw_vulkan_image"), glewGetExtension("GL_NV_draw_vulkan_image"));

  glewInfoFunc("glDrawVkImageNV", glDrawVkImageNV == NULL);
  glewInfoFunc("glGetVkProcAddrNV", glGetVkProcAddrNV == NULL);
  glewInfoFunc("glSignalVkFenceNV", glSignalVkFenceNV == NULL);
  glewInfoFunc("glSignalVkSemaphoreNV", glSignalVkSemaphoreNV == NULL);
  glewInfoFunc("glWaitVkSemaphoreNV", glWaitVkSemaphoreNV == NULL);
}

#endif /* GL_NV_draw_vulkan_image */

#ifdef GL_NV_evaluators

static void _glewInfo_GL_NV_evaluators (void)
{
  glewPrintExt("GL_NV_evaluators", GLEW_NV_evaluators, glewIsSupported("GL_NV_evaluators"), glewGetExtension("GL_NV_evaluators"));

  glewInfoFunc("glEvalMapsNV", glEvalMapsNV == NULL);
  glewInfoFunc("glGetMapAttribParameterfvNV", glGetMapAttribParameterfvNV == NULL);
  glewInfoFunc("glGetMapAttribParameterivNV", glGetMapAttribParameterivNV == NULL);
  glewInfoFunc("glGetMapControlPointsNV", glGetMapControlPointsNV == NULL);
  glewInfoFunc("glGetMapParameterfvNV", glGetMapParameterfvNV == NULL);
  glewInfoFunc("glGetMapParameterivNV", glGetMapParameterivNV == NULL);
  glewInfoFunc("glMapControlPointsNV", glMapControlPointsNV == NULL);
  glewInfoFunc("glMapParameterfvNV", glMapParameterfvNV == NULL);
  glewInfoFunc("glMapParameterivNV", glMapParameterivNV == NULL);
}

#endif /* GL_NV_evaluators */

#ifdef GL_NV_explicit_attrib_location

static void _glewInfo_GL_NV_explicit_attrib_location (void)
{
  glewPrintExt("GL_NV_explicit_attrib_location", GLEW_NV_explicit_attrib_location, glewIsSupported("GL_NV_explicit_attrib_location"), glewGetExtension("GL_NV_explicit_attrib_location"));
}

#endif /* GL_NV_explicit_attrib_location */

#ifdef GL_NV_explicit_multisample

static void _glewInfo_GL_NV_explicit_multisample (void)
{
  glewPrintExt("GL_NV_explicit_multisample", GLEW_NV_explicit_multisample, glewIsSupported("GL_NV_explicit_multisample"), glewGetExtension("GL_NV_explicit_multisample"));

  glewInfoFunc("glGetMultisamplefvNV", glGetMultisamplefvNV == NULL);
  glewInfoFunc("glSampleMaskIndexedNV", glSampleMaskIndexedNV == NULL);
  glewInfoFunc("glTexRenderbufferNV", glTexRenderbufferNV == NULL);
}

#endif /* GL_NV_explicit_multisample */

#ifdef GL_NV_fbo_color_attachments

static void _glewInfo_GL_NV_fbo_color_attachments (void)
{
  glewPrintExt("GL_NV_fbo_color_attachments", GLEW_NV_fbo_color_attachments, glewIsSupported("GL_NV_fbo_color_attachments"), glewGetExtension("GL_NV_fbo_color_attachments"));
}

#endif /* GL_NV_fbo_color_attachments */

#ifdef GL_NV_fence

static void _glewInfo_GL_NV_fence (void)
{
  glewPrintExt("GL_NV_fence", GLEW_NV_fence, glewIsSupported("GL_NV_fence"), glewGetExtension("GL_NV_fence"));

  glewInfoFunc("glDeleteFencesNV", glDeleteFencesNV == NULL);
  glewInfoFunc("glFinishFenceNV", glFinishFenceNV == NULL);
  glewInfoFunc("glGenFencesNV", glGenFencesNV == NULL);
  glewInfoFunc("glGetFenceivNV", glGetFenceivNV == NULL);
  glewInfoFunc("glIsFenceNV", glIsFenceNV == NULL);
  glewInfoFunc("glSetFenceNV", glSetFenceNV == NULL);
  glewInfoFunc("glTestFenceNV", glTestFenceNV == NULL);
}

#endif /* GL_NV_fence */

#ifdef GL_NV_fill_rectangle

static void _glewInfo_GL_NV_fill_rectangle (void)
{
  glewPrintExt("GL_NV_fill_rectangle", GLEW_NV_fill_rectangle, glewIsSupported("GL_NV_fill_rectangle"), glewGetExtension("GL_NV_fill_rectangle"));
}

#endif /* GL_NV_fill_rectangle */

#ifdef GL_NV_float_buffer

static void _glewInfo_GL_NV_float_buffer (void)
{
  glewPrintExt("GL_NV_float_buffer", GLEW_NV_float_buffer, glewIsSupported("GL_NV_float_buffer"), glewGetExtension("GL_NV_float_buffer"));
}

#endif /* GL_NV_float_buffer */

#ifdef GL_NV_fog_distance

static void _glewInfo_GL_NV_fog_distance (void)
{
  glewPrintExt("GL_NV_fog_distance", GLEW_NV_fog_distance, glewIsSupported("GL_NV_fog_distance"), glewGetExtension("GL_NV_fog_distance"));
}

#endif /* GL_NV_fog_distance */

#ifdef GL_NV_fragment_coverage_to_color

static void _glewInfo_GL_NV_fragment_coverage_to_color (void)
{
  glewPrintExt("GL_NV_fragment_coverage_to_color", GLEW_NV_fragment_coverage_to_color, glewIsSupported("GL_NV_fragment_coverage_to_color"), glewGetExtension("GL_NV_fragment_coverage_to_color"));

  glewInfoFunc("glFragmentCoverageColorNV", glFragmentCoverageColorNV == NULL);
}

#endif /* GL_NV_fragment_coverage_to_color */

#ifdef GL_NV_fragment_program

static void _glewInfo_GL_NV_fragment_program (void)
{
  glewPrintExt("GL_NV_fragment_program", GLEW_NV_fragment_program, glewIsSupported("GL_NV_fragment_program"), glewGetExtension("GL_NV_fragment_program"));

  glewInfoFunc("glGetProgramNamedParameterdvNV", glGetProgramNamedParameterdvNV == NULL);
  glewInfoFunc("glGetProgramNamedParameterfvNV", glGetProgramNamedParameterfvNV == NULL);
  glewInfoFunc("glProgramNamedParameter4dNV", glProgramNamedParameter4dNV == NULL);
  glewInfoFunc("glProgramNamedParameter4dvNV", glProgramNamedParameter4dvNV == NULL);
  glewInfoFunc("glProgramNamedParameter4fNV", glProgramNamedParameter4fNV == NULL);
  glewInfoFunc("glProgramNamedParameter4fvNV", glProgramNamedParameter4fvNV == NULL);
}

#endif /* GL_NV_fragment_program */

#ifdef GL_NV_fragment_program2

static void _glewInfo_GL_NV_fragment_program2 (void)
{
  glewPrintExt("GL_NV_fragment_program2", GLEW_NV_fragment_program2, glewIsSupported("GL_NV_fragment_program2"), glewGetExtension("GL_NV_fragment_program2"));
}

#endif /* GL_NV_fragment_program2 */

#ifdef GL_NV_fragment_program4

static void _glewInfo_GL_NV_fragment_program4 (void)
{
  glewPrintExt("GL_NV_fragment_program4", GLEW_NV_fragment_program4, glewIsSupported("GL_NV_fragment_program4"), glewGetExtension("GL_NV_gpu_program4"));
}

#endif /* GL_NV_fragment_program4 */

#ifdef GL_NV_fragment_program_option

static void _glewInfo_GL_NV_fragment_program_option (void)
{
  glewPrintExt("GL_NV_fragment_program_option", GLEW_NV_fragment_program_option, glewIsSupported("GL_NV_fragment_program_option"), glewGetExtension("GL_NV_fragment_program_option"));
}

#endif /* GL_NV_fragment_program_option */

#ifdef GL_NV_fragment_shader_interlock

static void _glewInfo_GL_NV_fragment_shader_interlock (void)
{
  glewPrintExt("GL_NV_fragment_shader_interlock", GLEW_NV_fragment_shader_interlock, glewIsSupported("GL_NV_fragment_shader_interlock"), glewGetExtension("GL_NV_fragment_shader_interlock"));
}

#endif /* GL_NV_fragment_shader_interlock */

#ifdef GL_NV_framebuffer_blit

static void _glewInfo_GL_NV_framebuffer_blit (void)
{
  glewPrintExt("GL_NV_framebuffer_blit", GLEW_NV_framebuffer_blit, glewIsSupported("GL_NV_framebuffer_blit"), glewGetExtension("GL_NV_framebuffer_blit"));

  glewInfoFunc("glBlitFramebufferNV", glBlitFramebufferNV == NULL);
}

#endif /* GL_NV_framebuffer_blit */

#ifdef GL_NV_framebuffer_mixed_samples

static void _glewInfo_GL_NV_framebuffer_mixed_samples (void)
{
  glewPrintExt("GL_NV_framebuffer_mixed_samples", GLEW_NV_framebuffer_mixed_samples, glewIsSupported("GL_NV_framebuffer_mixed_samples"), glewGetExtension("GL_NV_framebuffer_mixed_samples"));
}

#endif /* GL_NV_framebuffer_mixed_samples */

#ifdef GL_NV_framebuffer_multisample

static void _glewInfo_GL_NV_framebuffer_multisample (void)
{
  glewPrintExt("GL_NV_framebuffer_multisample", GLEW_NV_framebuffer_multisample, glewIsSupported("GL_NV_framebuffer_multisample"), glewGetExtension("GL_NV_framebuffer_multisample"));

  glewInfoFunc("glRenderbufferStorageMultisampleNV", glRenderbufferStorageMultisampleNV == NULL);
}

#endif /* GL_NV_framebuffer_multisample */

#ifdef GL_NV_framebuffer_multisample_coverage

static void _glewInfo_GL_NV_framebuffer_multisample_coverage (void)
{
  glewPrintExt("GL_NV_framebuffer_multisample_coverage", GLEW_NV_framebuffer_multisample_coverage, glewIsSupported("GL_NV_framebuffer_multisample_coverage"), glewGetExtension("GL_NV_framebuffer_multisample_coverage"));

  glewInfoFunc("glRenderbufferStorageMultisampleCoverageNV", glRenderbufferStorageMultisampleCoverageNV == NULL);
}

#endif /* GL_NV_framebuffer_multisample_coverage */

#ifdef GL_NV_generate_mipmap_sRGB

static void _glewInfo_GL_NV_generate_mipmap_sRGB (void)
{
  glewPrintExt("GL_NV_generate_mipmap_sRGB", GLEW_NV_generate_mipmap_sRGB, glewIsSupported("GL_NV_generate_mipmap_sRGB"), glewGetExtension("GL_NV_generate_mipmap_sRGB"));
}

#endif /* GL_NV_generate_mipmap_sRGB */

#ifdef GL_NV_geometry_program4

static void _glewInfo_GL_NV_geometry_program4 (void)
{
  glewPrintExt("GL_NV_geometry_program4", GLEW_NV_geometry_program4, glewIsSupported("GL_NV_geometry_program4"), glewGetExtension("GL_NV_gpu_program4"));

  glewInfoFunc("glProgramVertexLimitNV", glProgramVertexLimitNV == NULL);
}

#endif /* GL_NV_geometry_program4 */

#ifdef GL_NV_geometry_shader4

static void _glewInfo_GL_NV_geometry_shader4 (void)
{
  glewPrintExt("GL_NV_geometry_shader4", GLEW_NV_geometry_shader4, glewIsSupported("GL_NV_geometry_shader4"), glewGetExtension("GL_NV_geometry_shader4"));
}

#endif /* GL_NV_geometry_shader4 */

#ifdef GL_NV_geometry_shader_passthrough

static void _glewInfo_GL_NV_geometry_shader_passthrough (void)
{
  glewPrintExt("GL_NV_geometry_shader_passthrough", GLEW_NV_geometry_shader_passthrough, glewIsSupported("GL_NV_geometry_shader_passthrough"), glewGetExtension("GL_NV_geometry_shader_passthrough"));
}

#endif /* GL_NV_geometry_shader_passthrough */

#ifdef GL_NV_gpu_multicast

static void _glewInfo_GL_NV_gpu_multicast (void)
{
  glewPrintExt("GL_NV_gpu_multicast", GLEW_NV_gpu_multicast, glewIsSupported("GL_NV_gpu_multicast"), glewGetExtension("GL_NV_gpu_multicast"));

  glewInfoFunc("glMulticastBarrierNV", glMulticastBarrierNV == NULL);
  glewInfoFunc("glMulticastBlitFramebufferNV", glMulticastBlitFramebufferNV == NULL);
  glewInfoFunc("glMulticastBufferSubDataNV", glMulticastBufferSubDataNV == NULL);
  glewInfoFunc("glMulticastCopyBufferSubDataNV", glMulticastCopyBufferSubDataNV == NULL);
  glewInfoFunc("glMulticastCopyImageSubDataNV", glMulticastCopyImageSubDataNV == NULL);
  glewInfoFunc("glMulticastFramebufferSampleLocationsfvNV", glMulticastFramebufferSampleLocationsfvNV == NULL);
  glewInfoFunc("glMulticastGetQueryObjecti64vNV", glMulticastGetQueryObjecti64vNV == NULL);
  glewInfoFunc("glMulticastGetQueryObjectivNV", glMulticastGetQueryObjectivNV == NULL);
  glewInfoFunc("glMulticastGetQueryObjectui64vNV", glMulticastGetQueryObjectui64vNV == NULL);
  glewInfoFunc("glMulticastGetQueryObjectuivNV", glMulticastGetQueryObjectuivNV == NULL);
  glewInfoFunc("glMulticastWaitSyncNV", glMulticastWaitSyncNV == NULL);
  glewInfoFunc("glRenderGpuMaskNV", glRenderGpuMaskNV == NULL);
}

#endif /* GL_NV_gpu_multicast */

#ifdef GL_NV_gpu_program4

static void _glewInfo_GL_NV_gpu_program4 (void)
{
  glewPrintExt("GL_NV_gpu_program4", GLEW_NV_gpu_program4, glewIsSupported("GL_NV_gpu_program4"), glewGetExtension("GL_NV_gpu_program4"));

  glewInfoFunc("glProgramEnvParameterI4iNV", glProgramEnvParameterI4iNV == NULL);
  glewInfoFunc("glProgramEnvParameterI4ivNV", glProgramEnvParameterI4ivNV == NULL);
  glewInfoFunc("glProgramEnvParameterI4uiNV", glProgramEnvParameterI4uiNV == NULL);
  glewInfoFunc("glProgramEnvParameterI4uivNV", glProgramEnvParameterI4uivNV == NULL);
  glewInfoFunc("glProgramEnvParametersI4ivNV", glProgramEnvParametersI4ivNV == NULL);
  glewInfoFunc("glProgramEnvParametersI4uivNV", glProgramEnvParametersI4uivNV == NULL);
  glewInfoFunc("glProgramLocalParameterI4iNV", glProgramLocalParameterI4iNV == NULL);
  glewInfoFunc("glProgramLocalParameterI4ivNV", glProgramLocalParameterI4ivNV == NULL);
  glewInfoFunc("glProgramLocalParameterI4uiNV", glProgramLocalParameterI4uiNV == NULL);
  glewInfoFunc("glProgramLocalParameterI4uivNV", glProgramLocalParameterI4uivNV == NULL);
  glewInfoFunc("glProgramLocalParametersI4ivNV", glProgramLocalParametersI4ivNV == NULL);
  glewInfoFunc("glProgramLocalParametersI4uivNV", glProgramLocalParametersI4uivNV == NULL);
}

#endif /* GL_NV_gpu_program4 */

#ifdef GL_NV_gpu_program5

static void _glewInfo_GL_NV_gpu_program5 (void)
{
  glewPrintExt("GL_NV_gpu_program5", GLEW_NV_gpu_program5, glewIsSupported("GL_NV_gpu_program5"), glewGetExtension("GL_NV_gpu_program5"));
}

#endif /* GL_NV_gpu_program5 */

#ifdef GL_NV_gpu_program5_mem_extended

static void _glewInfo_GL_NV_gpu_program5_mem_extended (void)
{
  glewPrintExt("GL_NV_gpu_program5_mem_extended", GLEW_NV_gpu_program5_mem_extended, glewIsSupported("GL_NV_gpu_program5_mem_extended"), glewGetExtension("GL_NV_gpu_program5_mem_extended"));
}

#endif /* GL_NV_gpu_program5_mem_extended */

#ifdef GL_NV_gpu_program_fp64

static void _glewInfo_GL_NV_gpu_program_fp64 (void)
{
  glewPrintExt("GL_NV_gpu_program_fp64", GLEW_NV_gpu_program_fp64, glewIsSupported("GL_NV_gpu_program_fp64"), glewGetExtension("GL_NV_gpu_program_fp64"));
}

#endif /* GL_NV_gpu_program_fp64 */

#ifdef GL_NV_gpu_shader5

static void _glewInfo_GL_NV_gpu_shader5 (void)
{
  glewPrintExt("GL_NV_gpu_shader5", GLEW_NV_gpu_shader5, glewIsSupported("GL_NV_gpu_shader5"), glewGetExtension("GL_NV_gpu_shader5"));

  glewInfoFunc("glGetUniformi64vNV", glGetUniformi64vNV == NULL);
  glewInfoFunc("glGetUniformui64vNV", glGetUniformui64vNV == NULL);
  glewInfoFunc("glProgramUniform1i64NV", glProgramUniform1i64NV == NULL);
  glewInfoFunc("glProgramUniform1i64vNV", glProgramUniform1i64vNV == NULL);
  glewInfoFunc("glProgramUniform1ui64NV", glProgramUniform1ui64NV == NULL);
  glewInfoFunc("glProgramUniform1ui64vNV", glProgramUniform1ui64vNV == NULL);
  glewInfoFunc("glProgramUniform2i64NV", glProgramUniform2i64NV == NULL);
  glewInfoFunc("glProgramUniform2i64vNV", glProgramUniform2i64vNV == NULL);
  glewInfoFunc("glProgramUniform2ui64NV", glProgramUniform2ui64NV == NULL);
  glewInfoFunc("glProgramUniform2ui64vNV", glProgramUniform2ui64vNV == NULL);
  glewInfoFunc("glProgramUniform3i64NV", glProgramUniform3i64NV == NULL);
  glewInfoFunc("glProgramUniform3i64vNV", glProgramUniform3i64vNV == NULL);
  glewInfoFunc("glProgramUniform3ui64NV", glProgramUniform3ui64NV == NULL);
  glewInfoFunc("glProgramUniform3ui64vNV", glProgramUniform3ui64vNV == NULL);
  glewInfoFunc("glProgramUniform4i64NV", glProgramUniform4i64NV == NULL);
  glewInfoFunc("glProgramUniform4i64vNV", glProgramUniform4i64vNV == NULL);
  glewInfoFunc("glProgramUniform4ui64NV", glProgramUniform4ui64NV == NULL);
  glewInfoFunc("glProgramUniform4ui64vNV", glProgramUniform4ui64vNV == NULL);
  glewInfoFunc("glUniform1i64NV", glUniform1i64NV == NULL);
  glewInfoFunc("glUniform1i64vNV", glUniform1i64vNV == NULL);
  glewInfoFunc("glUniform1ui64NV", glUniform1ui64NV == NULL);
  glewInfoFunc("glUniform1ui64vNV", glUniform1ui64vNV == NULL);
  glewInfoFunc("glUniform2i64NV", glUniform2i64NV == NULL);
  glewInfoFunc("glUniform2i64vNV", glUniform2i64vNV == NULL);
  glewInfoFunc("glUniform2ui64NV", glUniform2ui64NV == NULL);
  glewInfoFunc("glUniform2ui64vNV", glUniform2ui64vNV == NULL);
  glewInfoFunc("glUniform3i64NV", glUniform3i64NV == NULL);
  glewInfoFunc("glUniform3i64vNV", glUniform3i64vNV == NULL);
  glewInfoFunc("glUniform3ui64NV", glUniform3ui64NV == NULL);
  glewInfoFunc("glUniform3ui64vNV", glUniform3ui64vNV == NULL);
  glewInfoFunc("glUniform4i64NV", glUniform4i64NV == NULL);
  glewInfoFunc("glUniform4i64vNV", glUniform4i64vNV == NULL);
  glewInfoFunc("glUniform4ui64NV", glUniform4ui64NV == NULL);
  glewInfoFunc("glUniform4ui64vNV", glUniform4ui64vNV == NULL);
}

#endif /* GL_NV_gpu_shader5 */

#ifdef GL_NV_half_float

static void _glewInfo_GL_NV_half_float (void)
{
  glewPrintExt("GL_NV_half_float", GLEW_NV_half_float, glewIsSupported("GL_NV_half_float"), glewGetExtension("GL_NV_half_float"));

  glewInfoFunc("glColor3hNV", glColor3hNV == NULL);
  glewInfoFunc("glColor3hvNV", glColor3hvNV == NULL);
  glewInfoFunc("glColor4hNV", glColor4hNV == NULL);
  glewInfoFunc("glColor4hvNV", glColor4hvNV == NULL);
  glewInfoFunc("glFogCoordhNV", glFogCoordhNV == NULL);
  glewInfoFunc("glFogCoordhvNV", glFogCoordhvNV == NULL);
  glewInfoFunc("glMultiTexCoord1hNV", glMultiTexCoord1hNV == NULL);
  glewInfoFunc("glMultiTexCoord1hvNV", glMultiTexCoord1hvNV == NULL);
  glewInfoFunc("glMultiTexCoord2hNV", glMultiTexCoord2hNV == NULL);
  glewInfoFunc("glMultiTexCoord2hvNV", glMultiTexCoord2hvNV == NULL);
  glewInfoFunc("glMultiTexCoord3hNV", glMultiTexCoord3hNV == NULL);
  glewInfoFunc("glMultiTexCoord3hvNV", glMultiTexCoord3hvNV == NULL);
  glewInfoFunc("glMultiTexCoord4hNV", glMultiTexCoord4hNV == NULL);
  glewInfoFunc("glMultiTexCoord4hvNV", glMultiTexCoord4hvNV == NULL);
  glewInfoFunc("glNormal3hNV", glNormal3hNV == NULL);
  glewInfoFunc("glNormal3hvNV", glNormal3hvNV == NULL);
  glewInfoFunc("glSecondaryColor3hNV", glSecondaryColor3hNV == NULL);
  glewInfoFunc("glSecondaryColor3hvNV", glSecondaryColor3hvNV == NULL);
  glewInfoFunc("glTexCoord1hNV", glTexCoord1hNV == NULL);
  glewInfoFunc("glTexCoord1hvNV", glTexCoord1hvNV == NULL);
  glewInfoFunc("glTexCoord2hNV", glTexCoord2hNV == NULL);
  glewInfoFunc("glTexCoord2hvNV", glTexCoord2hvNV == NULL);
  glewInfoFunc("glTexCoord3hNV", glTexCoord3hNV == NULL);
  glewInfoFunc("glTexCoord3hvNV", glTexCoord3hvNV == NULL);
  glewInfoFunc("glTexCoord4hNV", glTexCoord4hNV == NULL);
  glewInfoFunc("glTexCoord4hvNV", glTexCoord4hvNV == NULL);
  glewInfoFunc("glVertex2hNV", glVertex2hNV == NULL);
  glewInfoFunc("glVertex2hvNV", glVertex2hvNV == NULL);
  glewInfoFunc("glVertex3hNV", glVertex3hNV == NULL);
  glewInfoFunc("glVertex3hvNV", glVertex3hvNV == NULL);
  glewInfoFunc("glVertex4hNV", glVertex4hNV == NULL);
  glewInfoFunc("glVertex4hvNV", glVertex4hvNV == NULL);
  glewInfoFunc("glVertexAttrib1hNV", glVertexAttrib1hNV == NULL);
  glewInfoFunc("glVertexAttrib1hvNV", glVertexAttrib1hvNV == NULL);
  glewInfoFunc("glVertexAttrib2hNV", glVertexAttrib2hNV == NULL);
  glewInfoFunc("glVertexAttrib2hvNV", glVertexAttrib2hvNV == NULL);
  glewInfoFunc("glVertexAttrib3hNV", glVertexAttrib3hNV == NULL);
  glewInfoFunc("glVertexAttrib3hvNV", glVertexAttrib3hvNV == NULL);
  glewInfoFunc("glVertexAttrib4hNV", glVertexAttrib4hNV == NULL);
  glewInfoFunc("glVertexAttrib4hvNV", glVertexAttrib4hvNV == NULL);
  glewInfoFunc("glVertexAttribs1hvNV", glVertexAttribs1hvNV == NULL);
  glewInfoFunc("glVertexAttribs2hvNV", glVertexAttribs2hvNV == NULL);
  glewInfoFunc("glVertexAttribs3hvNV", glVertexAttribs3hvNV == NULL);
  glewInfoFunc("glVertexAttribs4hvNV", glVertexAttribs4hvNV == NULL);
  glewInfoFunc("glVertexWeighthNV", glVertexWeighthNV == NULL);
  glewInfoFunc("glVertexWeighthvNV", glVertexWeighthvNV == NULL);
}

#endif /* GL_NV_half_float */

#ifdef GL_NV_image_formats

static void _glewInfo_GL_NV_image_formats (void)
{
  glewPrintExt("GL_NV_image_formats", GLEW_NV_image_formats, glewIsSupported("GL_NV_image_formats"), glewGetExtension("GL_NV_image_formats"));
}

#endif /* GL_NV_image_formats */

#ifdef GL_NV_instanced_arrays

static void _glewInfo_GL_NV_instanced_arrays (void)
{
  glewPrintExt("GL_NV_instanced_arrays", GLEW_NV_instanced_arrays, glewIsSupported("GL_NV_instanced_arrays"), glewGetExtension("GL_NV_instanced_arrays"));

  glewInfoFunc("glVertexAttribDivisorNV", glVertexAttribDivisorNV == NULL);
}

#endif /* GL_NV_instanced_arrays */

#ifdef GL_NV_internalformat_sample_query

static void _glewInfo_GL_NV_internalformat_sample_query (void)
{
  glewPrintExt("GL_NV_internalformat_sample_query", GLEW_NV_internalformat_sample_query, glewIsSupported("GL_NV_internalformat_sample_query"), glewGetExtension("GL_NV_internalformat_sample_query"));

  glewInfoFunc("glGetInternalformatSampleivNV", glGetInternalformatSampleivNV == NULL);
}

#endif /* GL_NV_internalformat_sample_query */

#ifdef GL_NV_light_max_exponent

static void _glewInfo_GL_NV_light_max_exponent (void)
{
  glewPrintExt("GL_NV_light_max_exponent", GLEW_NV_light_max_exponent, glewIsSupported("GL_NV_light_max_exponent"), glewGetExtension("GL_NV_light_max_exponent"));
}

#endif /* GL_NV_light_max_exponent */

#ifdef GL_NV_multisample_coverage

static void _glewInfo_GL_NV_multisample_coverage (void)
{
  glewPrintExt("GL_NV_multisample_coverage", GLEW_NV_multisample_coverage, glewIsSupported("GL_NV_multisample_coverage"), glewGetExtension("GL_NV_multisample_coverage"));
}

#endif /* GL_NV_multisample_coverage */

#ifdef GL_NV_multisample_filter_hint

static void _glewInfo_GL_NV_multisample_filter_hint (void)
{
  glewPrintExt("GL_NV_multisample_filter_hint", GLEW_NV_multisample_filter_hint, glewIsSupported("GL_NV_multisample_filter_hint"), glewGetExtension("GL_NV_multisample_filter_hint"));
}

#endif /* GL_NV_multisample_filter_hint */

#ifdef GL_NV_non_square_matrices

static void _glewInfo_GL_NV_non_square_matrices (void)
{
  glewPrintExt("GL_NV_non_square_matrices", GLEW_NV_non_square_matrices, glewIsSupported("GL_NV_non_square_matrices"), glewGetExtension("GL_NV_non_square_matrices"));

  glewInfoFunc("glUniformMatrix2x3fvNV", glUniformMatrix2x3fvNV == NULL);
  glewInfoFunc("glUniformMatrix2x4fvNV", glUniformMatrix2x4fvNV == NULL);
  glewInfoFunc("glUniformMatrix3x2fvNV", glUniformMatrix3x2fvNV == NULL);
  glewInfoFunc("glUniformMatrix3x4fvNV", glUniformMatrix3x4fvNV == NULL);
  glewInfoFunc("glUniformMatrix4x2fvNV", glUniformMatrix4x2fvNV == NULL);
  glewInfoFunc("glUniformMatrix4x3fvNV", glUniformMatrix4x3fvNV == NULL);
}

#endif /* GL_NV_non_square_matrices */

#ifdef GL_NV_occlusion_query

static void _glewInfo_GL_NV_occlusion_query (void)
{
  glewPrintExt("GL_NV_occlusion_query", GLEW_NV_occlusion_query, glewIsSupported("GL_NV_occlusion_query"), glewGetExtension("GL_NV_occlusion_query"));

  glewInfoFunc("glBeginOcclusionQueryNV", glBeginOcclusionQueryNV == NULL);
  glewInfoFunc("glDeleteOcclusionQueriesNV", glDeleteOcclusionQueriesNV == NULL);
  glewInfoFunc("glEndOcclusionQueryNV", glEndOcclusionQueryNV == NULL);
  glewInfoFunc("glGenOcclusionQueriesNV", glGenOcclusionQueriesNV == NULL);
  glewInfoFunc("glGetOcclusionQueryivNV", glGetOcclusionQueryivNV == NULL);
  glewInfoFunc("glGetOcclusionQueryuivNV", glGetOcclusionQueryuivNV == NULL);
  glewInfoFunc("glIsOcclusionQueryNV", glIsOcclusionQueryNV == NULL);
}

#endif /* GL_NV_occlusion_query */

#ifdef GL_NV_pack_subimage

static void _glewInfo_GL_NV_pack_subimage (void)
{
  glewPrintExt("GL_NV_pack_subimage", GLEW_NV_pack_subimage, glewIsSupported("GL_NV_pack_subimage"), glewGetExtension("GL_NV_pack_subimage"));
}

#endif /* GL_NV_pack_subimage */

#ifdef GL_NV_packed_depth_stencil

static void _glewInfo_GL_NV_packed_depth_stencil (void)
{
  glewPrintExt("GL_NV_packed_depth_stencil", GLEW_NV_packed_depth_stencil, glewIsSupported("GL_NV_packed_depth_stencil"), glewGetExtension("GL_NV_packed_depth_stencil"));
}

#endif /* GL_NV_packed_depth_stencil */

#ifdef GL_NV_packed_float

static void _glewInfo_GL_NV_packed_float (void)
{
  glewPrintExt("GL_NV_packed_float", GLEW_NV_packed_float, glewIsSupported("GL_NV_packed_float"), glewGetExtension("GL_NV_packed_float"));
}

#endif /* GL_NV_packed_float */

#ifdef GL_NV_packed_float_linear

static void _glewInfo_GL_NV_packed_float_linear (void)
{
  glewPrintExt("GL_NV_packed_float_linear", GLEW_NV_packed_float_linear, glewIsSupported("GL_NV_packed_float_linear"), glewGetExtension("GL_NV_packed_float_linear"));
}

#endif /* GL_NV_packed_float_linear */

#ifdef GL_NV_parameter_buffer_object

static void _glewInfo_GL_NV_parameter_buffer_object (void)
{
  glewPrintExt("GL_NV_parameter_buffer_object", GLEW_NV_parameter_buffer_object, glewIsSupported("GL_NV_parameter_buffer_object"), glewGetExtension("GL_NV_parameter_buffer_object"));

  glewInfoFunc("glProgramBufferParametersIivNV", glProgramBufferParametersIivNV == NULL);
  glewInfoFunc("glProgramBufferParametersIuivNV", glProgramBufferParametersIuivNV == NULL);
  glewInfoFunc("glProgramBufferParametersfvNV", glProgramBufferParametersfvNV == NULL);
}

#endif /* GL_NV_parameter_buffer_object */

#ifdef GL_NV_parameter_buffer_object2

static void _glewInfo_GL_NV_parameter_buffer_object2 (void)
{
  glewPrintExt("GL_NV_parameter_buffer_object2", GLEW_NV_parameter_buffer_object2, glewIsSupported("GL_NV_parameter_buffer_object2"), glewGetExtension("GL_NV_parameter_buffer_object2"));
}

#endif /* GL_NV_parameter_buffer_object2 */

#ifdef GL_NV_path_rendering

static void _glewInfo_GL_NV_path_rendering (void)
{
  glewPrintExt("GL_NV_path_rendering", GLEW_NV_path_rendering, glewIsSupported("GL_NV_path_rendering"), glewGetExtension("GL_NV_path_rendering"));

  glewInfoFunc("glCopyPathNV", glCopyPathNV == NULL);
  glewInfoFunc("glCoverFillPathInstancedNV", glCoverFillPathInstancedNV == NULL);
  glewInfoFunc("glCoverFillPathNV", glCoverFillPathNV == NULL);
  glewInfoFunc("glCoverStrokePathInstancedNV", glCoverStrokePathInstancedNV == NULL);
  glewInfoFunc("glCoverStrokePathNV", glCoverStrokePathNV == NULL);
  glewInfoFunc("glDeletePathsNV", glDeletePathsNV == NULL);
  glewInfoFunc("glGenPathsNV", glGenPathsNV == NULL);
  glewInfoFunc("glGetPathColorGenfvNV", glGetPathColorGenfvNV == NULL);
  glewInfoFunc("glGetPathColorGenivNV", glGetPathColorGenivNV == NULL);
  glewInfoFunc("glGetPathCommandsNV", glGetPathCommandsNV == NULL);
  glewInfoFunc("glGetPathCoordsNV", glGetPathCoordsNV == NULL);
  glewInfoFunc("glGetPathDashArrayNV", glGetPathDashArrayNV == NULL);
  glewInfoFunc("glGetPathLengthNV", glGetPathLengthNV == NULL);
  glewInfoFunc("glGetPathMetricRangeNV", glGetPathMetricRangeNV == NULL);
  glewInfoFunc("glGetPathMetricsNV", glGetPathMetricsNV == NULL);
  glewInfoFunc("glGetPathParameterfvNV", glGetPathParameterfvNV == NULL);
  glewInfoFunc("glGetPathParameterivNV", glGetPathParameterivNV == NULL);
  glewInfoFunc("glGetPathSpacingNV", glGetPathSpacingNV == NULL);
  glewInfoFunc("glGetPathTexGenfvNV", glGetPathTexGenfvNV == NULL);
  glewInfoFunc("glGetPathTexGenivNV", glGetPathTexGenivNV == NULL);
  glewInfoFunc("glGetProgramResourcefvNV", glGetProgramResourcefvNV == NULL);
  glewInfoFunc("glInterpolatePathsNV", glInterpolatePathsNV == NULL);
  glewInfoFunc("glIsPathNV", glIsPathNV == NULL);
  glewInfoFunc("glIsPointInFillPathNV", glIsPointInFillPathNV == NULL);
  glewInfoFunc("glIsPointInStrokePathNV", glIsPointInStrokePathNV == NULL);
  glewInfoFunc("glMatrixLoad3x2fNV", glMatrixLoad3x2fNV == NULL);
  glewInfoFunc("glMatrixLoad3x3fNV", glMatrixLoad3x3fNV == NULL);
  glewInfoFunc("glMatrixLoadTranspose3x3fNV", glMatrixLoadTranspose3x3fNV == NULL);
  glewInfoFunc("glMatrixMult3x2fNV", glMatrixMult3x2fNV == NULL);
  glewInfoFunc("glMatrixMult3x3fNV", glMatrixMult3x3fNV == NULL);
  glewInfoFunc("glMatrixMultTranspose3x3fNV", glMatrixMultTranspose3x3fNV == NULL);
  glewInfoFunc("glPathColorGenNV", glPathColorGenNV == NULL);
  glewInfoFunc("glPathCommandsNV", glPathCommandsNV == NULL);
  glewInfoFunc("glPathCoordsNV", glPathCoordsNV == NULL);
  glewInfoFunc("glPathCoverDepthFuncNV", glPathCoverDepthFuncNV == NULL);
  glewInfoFunc("glPathDashArrayNV", glPathDashArrayNV == NULL);
  glewInfoFunc("glPathFogGenNV", glPathFogGenNV == NULL);
  glewInfoFunc("glPathGlyphIndexArrayNV", glPathGlyphIndexArrayNV == NULL);
  glewInfoFunc("glPathGlyphIndexRangeNV", glPathGlyphIndexRangeNV == NULL);
  glewInfoFunc("glPathGlyphRangeNV", glPathGlyphRangeNV == NULL);
  glewInfoFunc("glPathGlyphsNV", glPathGlyphsNV == NULL);
  glewInfoFunc("glPathMemoryGlyphIndexArrayNV", glPathMemoryGlyphIndexArrayNV == NULL);
  glewInfoFunc("glPathParameterfNV", glPathParameterfNV == NULL);
  glewInfoFunc("glPathParameterfvNV", glPathParameterfvNV == NULL);
  glewInfoFunc("glPathParameteriNV", glPathParameteriNV == NULL);
  glewInfoFunc("glPathParameterivNV", glPathParameterivNV == NULL);
  glewInfoFunc("glPathStencilDepthOffsetNV", glPathStencilDepthOffsetNV == NULL);
  glewInfoFunc("glPathStencilFuncNV", glPathStencilFuncNV == NULL);
  glewInfoFunc("glPathStringNV", glPathStringNV == NULL);
  glewInfoFunc("glPathSubCommandsNV", glPathSubCommandsNV == NULL);
  glewInfoFunc("glPathSubCoordsNV", glPathSubCoordsNV == NULL);
  glewInfoFunc("glPathTexGenNV", glPathTexGenNV == NULL);
  glewInfoFunc("glPointAlongPathNV", glPointAlongPathNV == NULL);
  glewInfoFunc("glProgramPathFragmentInputGenNV", glProgramPathFragmentInputGenNV == NULL);
  glewInfoFunc("glStencilFillPathInstancedNV", glStencilFillPathInstancedNV == NULL);
  glewInfoFunc("glStencilFillPathNV", glStencilFillPathNV == NULL);
  glewInfoFunc("glStencilStrokePathInstancedNV", glStencilStrokePathInstancedNV == NULL);
  glewInfoFunc("glStencilStrokePathNV", glStencilStrokePathNV == NULL);
  glewInfoFunc("glStencilThenCoverFillPathInstancedNV", glStencilThenCoverFillPathInstancedNV == NULL);
  glewInfoFunc("glStencilThenCoverFillPathNV", glStencilThenCoverFillPathNV == NULL);
  glewInfoFunc("glStencilThenCoverStrokePathInstancedNV", glStencilThenCoverStrokePathInstancedNV == NULL);
  glewInfoFunc("glStencilThenCoverStrokePathNV", glStencilThenCoverStrokePathNV == NULL);
  glewInfoFunc("glTransformPathNV", glTransformPathNV == NULL);
  glewInfoFunc("glWeightPathsNV", glWeightPathsNV == NULL);
}

#endif /* GL_NV_path_rendering */

#ifdef GL_NV_path_rendering_shared_edge

static void _glewInfo_GL_NV_path_rendering_shared_edge (void)
{
  glewPrintExt("GL_NV_path_rendering_shared_edge", GLEW_NV_path_rendering_shared_edge, glewIsSupported("GL_NV_path_rendering_shared_edge"), glewGetExtension("GL_NV_path_rendering_shared_edge"));
}

#endif /* GL_NV_path_rendering_shared_edge */

#ifdef GL_NV_pixel_buffer_object

static void _glewInfo_GL_NV_pixel_buffer_object (void)
{
  glewPrintExt("GL_NV_pixel_buffer_object", GLEW_NV_pixel_buffer_object, glewIsSupported("GL_NV_pixel_buffer_object"), glewGetExtension("GL_NV_pixel_buffer_object"));
}

#endif /* GL_NV_pixel_buffer_object */

#ifdef GL_NV_pixel_data_range

static void _glewInfo_GL_NV_pixel_data_range (void)
{
  glewPrintExt("GL_NV_pixel_data_range", GLEW_NV_pixel_data_range, glewIsSupported("GL_NV_pixel_data_range"), glewGetExtension("GL_NV_pixel_data_range"));

  glewInfoFunc("glFlushPixelDataRangeNV", glFlushPixelDataRangeNV == NULL);
  glewInfoFunc("glPixelDataRangeNV", glPixelDataRangeNV == NULL);
}

#endif /* GL_NV_pixel_data_range */

#ifdef GL_NV_platform_binary

static void _glewInfo_GL_NV_platform_binary (void)
{
  glewPrintExt("GL_NV_platform_binary", GLEW_NV_platform_binary, glewIsSupported("GL_NV_platform_binary"), glewGetExtension("GL_NV_platform_binary"));
}

#endif /* GL_NV_platform_binary */

#ifdef GL_NV_point_sprite

static void _glewInfo_GL_NV_point_sprite (void)
{
  glewPrintExt("GL_NV_point_sprite", GLEW_NV_point_sprite, glewIsSupported("GL_NV_point_sprite"), glewGetExtension("GL_NV_point_sprite"));

  glewInfoFunc("glPointParameteriNV", glPointParameteriNV == NULL);
  glewInfoFunc("glPointParameterivNV", glPointParameterivNV == NULL);
}

#endif /* GL_NV_point_sprite */

#ifdef GL_NV_polygon_mode

static void _glewInfo_GL_NV_polygon_mode (void)
{
  glewPrintExt("GL_NV_polygon_mode", GLEW_NV_polygon_mode, glewIsSupported("GL_NV_polygon_mode"), glewGetExtension("GL_NV_polygon_mode"));

  glewInfoFunc("glPolygonModeNV", glPolygonModeNV == NULL);
}

#endif /* GL_NV_polygon_mode */

#ifdef GL_NV_present_video

static void _glewInfo_GL_NV_present_video (void)
{
  glewPrintExt("GL_NV_present_video", GLEW_NV_present_video, glewIsSupported("GL_NV_present_video"), glewGetExtension("GL_NV_present_video"));

  glewInfoFunc("glGetVideoi64vNV", glGetVideoi64vNV == NULL);
  glewInfoFunc("glGetVideoivNV", glGetVideoivNV == NULL);
  glewInfoFunc("glGetVideoui64vNV", glGetVideoui64vNV == NULL);
  glewInfoFunc("glGetVideouivNV", glGetVideouivNV == NULL);
  glewInfoFunc("glPresentFrameDualFillNV", glPresentFrameDualFillNV == NULL);
  glewInfoFunc("glPresentFrameKeyedNV", glPresentFrameKeyedNV == NULL);
}

#endif /* GL_NV_present_video */

#ifdef GL_NV_primitive_restart

static void _glewInfo_GL_NV_primitive_restart (void)
{
  glewPrintExt("GL_NV_primitive_restart", GLEW_NV_primitive_restart, glewIsSupported("GL_NV_primitive_restart"), glewGetExtension("GL_NV_primitive_restart"));

  glewInfoFunc("glPrimitiveRestartIndexNV", glPrimitiveRestartIndexNV == NULL);
  glewInfoFunc("glPrimitiveRestartNV", glPrimitiveRestartNV == NULL);
}

#endif /* GL_NV_primitive_restart */

#ifdef GL_NV_read_depth

static void _glewInfo_GL_NV_read_depth (void)
{
  glewPrintExt("GL_NV_read_depth", GLEW_NV_read_depth, glewIsSupported("GL_NV_read_depth"), glewGetExtension("GL_NV_read_depth"));
}

#endif /* GL_NV_read_depth */

#ifdef GL_NV_read_depth_stencil

static void _glewInfo_GL_NV_read_depth_stencil (void)
{
  glewPrintExt("GL_NV_read_depth_stencil", GLEW_NV_read_depth_stencil, glewIsSupported("GL_NV_read_depth_stencil"), glewGetExtension("GL_NV_read_depth_stencil"));
}

#endif /* GL_NV_read_depth_stencil */

#ifdef GL_NV_read_stencil

static void _glewInfo_GL_NV_read_stencil (void)
{
  glewPrintExt("GL_NV_read_stencil", GLEW_NV_read_stencil, glewIsSupported("GL_NV_read_stencil"), glewGetExtension("GL_NV_read_stencil"));
}

#endif /* GL_NV_read_stencil */

#ifdef GL_NV_register_combiners

static void _glewInfo_GL_NV_register_combiners (void)
{
  glewPrintExt("GL_NV_register_combiners", GLEW_NV_register_combiners, glewIsSupported("GL_NV_register_combiners"), glewGetExtension("GL_NV_register_combiners"));

  glewInfoFunc("glCombinerInputNV", glCombinerInputNV == NULL);
  glewInfoFunc("glCombinerOutputNV", glCombinerOutputNV == NULL);
  glewInfoFunc("glCombinerParameterfNV", glCombinerParameterfNV == NULL);
  glewInfoFunc("glCombinerParameterfvNV", glCombinerParameterfvNV == NULL);
  glewInfoFunc("glCombinerParameteriNV", glCombinerParameteriNV == NULL);
  glewInfoFunc("glCombinerParameterivNV", glCombinerParameterivNV == NULL);
  glewInfoFunc("glFinalCombinerInputNV", glFinalCombinerInputNV == NULL);
  glewInfoFunc("glGetCombinerInputParameterfvNV", glGetCombinerInputParameterfvNV == NULL);
  glewInfoFunc("glGetCombinerInputParameterivNV", glGetCombinerInputParameterivNV == NULL);
  glewInfoFunc("glGetCombinerOutputParameterfvNV", glGetCombinerOutputParameterfvNV == NULL);
  glewInfoFunc("glGetCombinerOutputParameterivNV", glGetCombinerOutputParameterivNV == NULL);
  glewInfoFunc("glGetFinalCombinerInputParameterfvNV", glGetFinalCombinerInputParameterfvNV == NULL);
  glewInfoFunc("glGetFinalCombinerInputParameterivNV", glGetFinalCombinerInputParameterivNV == NULL);
}

#endif /* GL_NV_register_combiners */

#ifdef GL_NV_register_combiners2

static void _glewInfo_GL_NV_register_combiners2 (void)
{
  glewPrintExt("GL_NV_register_combiners2", GLEW_NV_register_combiners2, glewIsSupported("GL_NV_register_combiners2"), glewGetExtension("GL_NV_register_combiners2"));

  glewInfoFunc("glCombinerStageParameterfvNV", glCombinerStageParameterfvNV == NULL);
  glewInfoFunc("glGetCombinerStageParameterfvNV", glGetCombinerStageParameterfvNV == NULL);
}

#endif /* GL_NV_register_combiners2 */

#ifdef GL_NV_robustness_video_memory_purge

static void _glewInfo_GL_NV_robustness_video_memory_purge (void)
{
  glewPrintExt("GL_NV_robustness_video_memory_purge", GLEW_NV_robustness_video_memory_purge, glewIsSupported("GL_NV_robustness_video_memory_purge"), glewGetExtension("GL_NV_robustness_video_memory_purge"));
}

#endif /* GL_NV_robustness_video_memory_purge */

#ifdef GL_NV_sRGB_formats

static void _glewInfo_GL_NV_sRGB_formats (void)
{
  glewPrintExt("GL_NV_sRGB_formats", GLEW_NV_sRGB_formats, glewIsSupported("GL_NV_sRGB_formats"), glewGetExtension("GL_NV_sRGB_formats"));
}

#endif /* GL_NV_sRGB_formats */

#ifdef GL_NV_sample_locations

static void _glewInfo_GL_NV_sample_locations (void)
{
  glewPrintExt("GL_NV_sample_locations", GLEW_NV_sample_locations, glewIsSupported("GL_NV_sample_locations"), glewGetExtension("GL_NV_sample_locations"));

  glewInfoFunc("glFramebufferSampleLocationsfvNV", glFramebufferSampleLocationsfvNV == NULL);
  glewInfoFunc("glNamedFramebufferSampleLocationsfvNV", glNamedFramebufferSampleLocationsfvNV == NULL);
}

#endif /* GL_NV_sample_locations */

#ifdef GL_NV_sample_mask_override_coverage

static void _glewInfo_GL_NV_sample_mask_override_coverage (void)
{
  glewPrintExt("GL_NV_sample_mask_override_coverage", GLEW_NV_sample_mask_override_coverage, glewIsSupported("GL_NV_sample_mask_override_coverage"), glewGetExtension("GL_NV_sample_mask_override_coverage"));
}

#endif /* GL_NV_sample_mask_override_coverage */

#ifdef GL_NV_shader_atomic_counters

static void _glewInfo_GL_NV_shader_atomic_counters (void)
{
  glewPrintExt("GL_NV_shader_atomic_counters", GLEW_NV_shader_atomic_counters, glewIsSupported("GL_NV_shader_atomic_counters"), glewGetExtension("GL_NV_shader_atomic_counters"));
}

#endif /* GL_NV_shader_atomic_counters */

#ifdef GL_NV_shader_atomic_float

static void _glewInfo_GL_NV_shader_atomic_float (void)
{
  glewPrintExt("GL_NV_shader_atomic_float", GLEW_NV_shader_atomic_float, glewIsSupported("GL_NV_shader_atomic_float"), glewGetExtension("GL_NV_shader_atomic_float"));
}

#endif /* GL_NV_shader_atomic_float */

#ifdef GL_NV_shader_atomic_float64

static void _glewInfo_GL_NV_shader_atomic_float64 (void)
{
  glewPrintExt("GL_NV_shader_atomic_float64", GLEW_NV_shader_atomic_float64, glewIsSupported("GL_NV_shader_atomic_float64"), glewGetExtension("GL_NV_shader_atomic_float64"));
}

#endif /* GL_NV_shader_atomic_float64 */

#ifdef GL_NV_shader_atomic_fp16_vector

static void _glewInfo_GL_NV_shader_atomic_fp16_vector (void)
{
  glewPrintExt("GL_NV_shader_atomic_fp16_vector", GLEW_NV_shader_atomic_fp16_vector, glewIsSupported("GL_NV_shader_atomic_fp16_vector"), glewGetExtension("GL_NV_shader_atomic_fp16_vector"));
}

#endif /* GL_NV_shader_atomic_fp16_vector */

#ifdef GL_NV_shader_atomic_int64

static void _glewInfo_GL_NV_shader_atomic_int64 (void)
{
  glewPrintExt("GL_NV_shader_atomic_int64", GLEW_NV_shader_atomic_int64, glewIsSupported("GL_NV_shader_atomic_int64"), glewGetExtension("GL_NV_shader_atomic_int64"));
}

#endif /* GL_NV_shader_atomic_int64 */

#ifdef GL_NV_shader_buffer_load

static void _glewInfo_GL_NV_shader_buffer_load (void)
{
  glewPrintExt("GL_NV_shader_buffer_load", GLEW_NV_shader_buffer_load, glewIsSupported("GL_NV_shader_buffer_load"), glewGetExtension("GL_NV_shader_buffer_load"));

  glewInfoFunc("glGetBufferParameterui64vNV", glGetBufferParameterui64vNV == NULL);
  glewInfoFunc("glGetIntegerui64vNV", glGetIntegerui64vNV == NULL);
  glewInfoFunc("glGetNamedBufferParameterui64vNV", glGetNamedBufferParameterui64vNV == NULL);
  glewInfoFunc("glIsBufferResidentNV", glIsBufferResidentNV == NULL);
  glewInfoFunc("glIsNamedBufferResidentNV", glIsNamedBufferResidentNV == NULL);
  glewInfoFunc("glMakeBufferNonResidentNV", glMakeBufferNonResidentNV == NULL);
  glewInfoFunc("glMakeBufferResidentNV", glMakeBufferResidentNV == NULL);
  glewInfoFunc("glMakeNamedBufferNonResidentNV", glMakeNamedBufferNonResidentNV == NULL);
  glewInfoFunc("glMakeNamedBufferResidentNV", glMakeNamedBufferResidentNV == NULL);
  glewInfoFunc("glProgramUniformui64NV", glProgramUniformui64NV == NULL);
  glewInfoFunc("glProgramUniformui64vNV", glProgramUniformui64vNV == NULL);
  glewInfoFunc("glUniformui64NV", glUniformui64NV == NULL);
  glewInfoFunc("glUniformui64vNV", glUniformui64vNV == NULL);
}

#endif /* GL_NV_shader_buffer_load */

#ifdef GL_NV_shader_noperspective_interpolation

static void _glewInfo_GL_NV_shader_noperspective_interpolation (void)
{
  glewPrintExt("GL_NV_shader_noperspective_interpolation", GLEW_NV_shader_noperspective_interpolation, glewIsSupported("GL_NV_shader_noperspective_interpolation"), glewGetExtension("GL_NV_shader_noperspective_interpolation"));
}

#endif /* GL_NV_shader_noperspective_interpolation */

#ifdef GL_NV_shader_storage_buffer_object

static void _glewInfo_GL_NV_shader_storage_buffer_object (void)
{
  glewPrintExt("GL_NV_shader_storage_buffer_object", GLEW_NV_shader_storage_buffer_object, glewIsSupported("GL_NV_shader_storage_buffer_object"), glewGetExtension("GL_NV_shader_storage_buffer_object"));
}

#endif /* GL_NV_shader_storage_buffer_object */

#ifdef GL_NV_shader_thread_group

static void _glewInfo_GL_NV_shader_thread_group (void)
{
  glewPrintExt("GL_NV_shader_thread_group", GLEW_NV_shader_thread_group, glewIsSupported("GL_NV_shader_thread_group"), glewGetExtension("GL_NV_shader_thread_group"));
}

#endif /* GL_NV_shader_thread_group */

#ifdef GL_NV_shader_thread_shuffle

static void _glewInfo_GL_NV_shader_thread_shuffle (void)
{
  glewPrintExt("GL_NV_shader_thread_shuffle", GLEW_NV_shader_thread_shuffle, glewIsSupported("GL_NV_shader_thread_shuffle"), glewGetExtension("GL_NV_shader_thread_shuffle"));
}

#endif /* GL_NV_shader_thread_shuffle */

#ifdef GL_NV_shadow_samplers_array

static void _glewInfo_GL_NV_shadow_samplers_array (void)
{
  glewPrintExt("GL_NV_shadow_samplers_array", GLEW_NV_shadow_samplers_array, glewIsSupported("GL_NV_shadow_samplers_array"), glewGetExtension("GL_NV_shadow_samplers_array"));
}

#endif /* GL_NV_shadow_samplers_array */

#ifdef GL_NV_shadow_samplers_cube

static void _glewInfo_GL_NV_shadow_samplers_cube (void)
{
  glewPrintExt("GL_NV_shadow_samplers_cube", GLEW_NV_shadow_samplers_cube, glewIsSupported("GL_NV_shadow_samplers_cube"), glewGetExtension("GL_NV_shadow_samplers_cube"));
}

#endif /* GL_NV_shadow_samplers_cube */

#ifdef GL_NV_stereo_view_rendering

static void _glewInfo_GL_NV_stereo_view_rendering (void)
{
  glewPrintExt("GL_NV_stereo_view_rendering", GLEW_NV_stereo_view_rendering, glewIsSupported("GL_NV_stereo_view_rendering"), glewGetExtension("GL_NV_stereo_view_rendering"));
}

#endif /* GL_NV_stereo_view_rendering */

#ifdef GL_NV_tessellation_program5

static void _glewInfo_GL_NV_tessellation_program5 (void)
{
  glewPrintExt("GL_NV_tessellation_program5", GLEW_NV_tessellation_program5, glewIsSupported("GL_NV_tessellation_program5"), glewGetExtension("GL_NV_gpu_program5"));
}

#endif /* GL_NV_tessellation_program5 */

#ifdef GL_NV_texgen_emboss

static void _glewInfo_GL_NV_texgen_emboss (void)
{
  glewPrintExt("GL_NV_texgen_emboss", GLEW_NV_texgen_emboss, glewIsSupported("GL_NV_texgen_emboss"), glewGetExtension("GL_NV_texgen_emboss"));
}

#endif /* GL_NV_texgen_emboss */

#ifdef GL_NV_texgen_reflection

static void _glewInfo_GL_NV_texgen_reflection (void)
{
  glewPrintExt("GL_NV_texgen_reflection", GLEW_NV_texgen_reflection, glewIsSupported("GL_NV_texgen_reflection"), glewGetExtension("GL_NV_texgen_reflection"));
}

#endif /* GL_NV_texgen_reflection */

#ifdef GL_NV_texture_array

static void _glewInfo_GL_NV_texture_array (void)
{
  glewPrintExt("GL_NV_texture_array", GLEW_NV_texture_array, glewIsSupported("GL_NV_texture_array"), glewGetExtension("GL_NV_texture_array"));

  glewInfoFunc("glCompressedTexImage3DNV", glCompressedTexImage3DNV == NULL);
  glewInfoFunc("glCompressedTexSubImage3DNV", glCompressedTexSubImage3DNV == NULL);
  glewInfoFunc("glCopyTexSubImage3DNV", glCopyTexSubImage3DNV == NULL);
  glewInfoFunc("glFramebufferTextureLayerNV", glFramebufferTextureLayerNV == NULL);
  glewInfoFunc("glTexImage3DNV", glTexImage3DNV == NULL);
  glewInfoFunc("glTexSubImage3DNV", glTexSubImage3DNV == NULL);
}

#endif /* GL_NV_texture_array */

#ifdef GL_NV_texture_barrier

static void _glewInfo_GL_NV_texture_barrier (void)
{
  glewPrintExt("GL_NV_texture_barrier", GLEW_NV_texture_barrier, glewIsSupported("GL_NV_texture_barrier"), glewGetExtension("GL_NV_texture_barrier"));

  glewInfoFunc("glTextureBarrierNV", glTextureBarrierNV == NULL);
}

#endif /* GL_NV_texture_barrier */

#ifdef GL_NV_texture_border_clamp

static void _glewInfo_GL_NV_texture_border_clamp (void)
{
  glewPrintExt("GL_NV_texture_border_clamp", GLEW_NV_texture_border_clamp, glewIsSupported("GL_NV_texture_border_clamp"), glewGetExtension("GL_NV_texture_border_clamp"));
}

#endif /* GL_NV_texture_border_clamp */

#ifdef GL_NV_texture_compression_latc

static void _glewInfo_GL_NV_texture_compression_latc (void)
{
  glewPrintExt("GL_NV_texture_compression_latc", GLEW_NV_texture_compression_latc, glewIsSupported("GL_NV_texture_compression_latc"), glewGetExtension("GL_NV_texture_compression_latc"));
}

#endif /* GL_NV_texture_compression_latc */

#ifdef GL_NV_texture_compression_s3tc

static void _glewInfo_GL_NV_texture_compression_s3tc (void)
{
  glewPrintExt("GL_NV_texture_compression_s3tc", GLEW_NV_texture_compression_s3tc, glewIsSupported("GL_NV_texture_compression_s3tc"), glewGetExtension("GL_NV_texture_compression_s3tc"));
}

#endif /* GL_NV_texture_compression_s3tc */

#ifdef GL_NV_texture_compression_s3tc_update

static void _glewInfo_GL_NV_texture_compression_s3tc_update (void)
{
  glewPrintExt("GL_NV_texture_compression_s3tc_update", GLEW_NV_texture_compression_s3tc_update, glewIsSupported("GL_NV_texture_compression_s3tc_update"), glewGetExtension("GL_NV_texture_compression_s3tc_update"));
}

#endif /* GL_NV_texture_compression_s3tc_update */

#ifdef GL_NV_texture_compression_vtc

static void _glewInfo_GL_NV_texture_compression_vtc (void)
{
  glewPrintExt("GL_NV_texture_compression_vtc", GLEW_NV_texture_compression_vtc, glewIsSupported("GL_NV_texture_compression_vtc"), glewGetExtension("GL_NV_texture_compression_vtc"));
}

#endif /* GL_NV_texture_compression_vtc */

#ifdef GL_NV_texture_env_combine4

static void _glewInfo_GL_NV_texture_env_combine4 (void)
{
  glewPrintExt("GL_NV_texture_env_combine4", GLEW_NV_texture_env_combine4, glewIsSupported("GL_NV_texture_env_combine4"), glewGetExtension("GL_NV_texture_env_combine4"));
}

#endif /* GL_NV_texture_env_combine4 */

#ifdef GL_NV_texture_expand_normal

static void _glewInfo_GL_NV_texture_expand_normal (void)
{
  glewPrintExt("GL_NV_texture_expand_normal", GLEW_NV_texture_expand_normal, glewIsSupported("GL_NV_texture_expand_normal"), glewGetExtension("GL_NV_texture_expand_normal"));
}

#endif /* GL_NV_texture_expand_normal */

#ifdef GL_NV_texture_multisample

static void _glewInfo_GL_NV_texture_multisample (void)
{
  glewPrintExt("GL_NV_texture_multisample", GLEW_NV_texture_multisample, glewIsSupported("GL_NV_texture_multisample"), glewGetExtension("GL_NV_texture_multisample"));

  glewInfoFunc("glTexImage2DMultisampleCoverageNV", glTexImage2DMultisampleCoverageNV == NULL);
  glewInfoFunc("glTexImage3DMultisampleCoverageNV", glTexImage3DMultisampleCoverageNV == NULL);
  glewInfoFunc("glTextureImage2DMultisampleCoverageNV", glTextureImage2DMultisampleCoverageNV == NULL);
  glewInfoFunc("glTextureImage2DMultisampleNV", glTextureImage2DMultisampleNV == NULL);
  glewInfoFunc("glTextureImage3DMultisampleCoverageNV", glTextureImage3DMultisampleCoverageNV == NULL);
  glewInfoFunc("glTextureImage3DMultisampleNV", glTextureImage3DMultisampleNV == NULL);
}

#endif /* GL_NV_texture_multisample */

#ifdef GL_NV_texture_npot_2D_mipmap

static void _glewInfo_GL_NV_texture_npot_2D_mipmap (void)
{
  glewPrintExt("GL_NV_texture_npot_2D_mipmap", GLEW_NV_texture_npot_2D_mipmap, glewIsSupported("GL_NV_texture_npot_2D_mipmap"), glewGetExtension("GL_NV_texture_npot_2D_mipmap"));
}

#endif /* GL_NV_texture_npot_2D_mipmap */

#ifdef GL_NV_texture_rectangle

static void _glewInfo_GL_NV_texture_rectangle (void)
{
  glewPrintExt("GL_NV_texture_rectangle", GLEW_NV_texture_rectangle, glewIsSupported("GL_NV_texture_rectangle"), glewGetExtension("GL_NV_texture_rectangle"));
}

#endif /* GL_NV_texture_rectangle */

#ifdef GL_NV_texture_rectangle_compressed

static void _glewInfo_GL_NV_texture_rectangle_compressed (void)
{
  glewPrintExt("GL_NV_texture_rectangle_compressed", GLEW_NV_texture_rectangle_compressed, glewIsSupported("GL_NV_texture_rectangle_compressed"), glewGetExtension("GL_NV_texture_rectangle_compressed"));
}

#endif /* GL_NV_texture_rectangle_compressed */

#ifdef GL_NV_texture_shader

static void _glewInfo_GL_NV_texture_shader (void)
{
  glewPrintExt("GL_NV_texture_shader", GLEW_NV_texture_shader, glewIsSupported("GL_NV_texture_shader"), glewGetExtension("GL_NV_texture_shader"));
}

#endif /* GL_NV_texture_shader */

#ifdef GL_NV_texture_shader2

static void _glewInfo_GL_NV_texture_shader2 (void)
{
  glewPrintExt("GL_NV_texture_shader2", GLEW_NV_texture_shader2, glewIsSupported("GL_NV_texture_shader2"), glewGetExtension("GL_NV_texture_shader2"));
}

#endif /* GL_NV_texture_shader2 */

#ifdef GL_NV_texture_shader3

static void _glewInfo_GL_NV_texture_shader3 (void)
{
  glewPrintExt("GL_NV_texture_shader3", GLEW_NV_texture_shader3, glewIsSupported("GL_NV_texture_shader3"), glewGetExtension("GL_NV_texture_shader3"));
}

#endif /* GL_NV_texture_shader3 */

#ifdef GL_NV_transform_feedback

static void _glewInfo_GL_NV_transform_feedback (void)
{
  glewPrintExt("GL_NV_transform_feedback", GLEW_NV_transform_feedback, glewIsSupported("GL_NV_transform_feedback"), glewGetExtension("GL_NV_transform_feedback"));

  glewInfoFunc("glActiveVaryingNV", glActiveVaryingNV == NULL);
  glewInfoFunc("glBeginTransformFeedbackNV", glBeginTransformFeedbackNV == NULL);
  glewInfoFunc("glBindBufferBaseNV", glBindBufferBaseNV == NULL);
  glewInfoFunc("glBindBufferOffsetNV", glBindBufferOffsetNV == NULL);
  glewInfoFunc("glBindBufferRangeNV", glBindBufferRangeNV == NULL);
  glewInfoFunc("glEndTransformFeedbackNV", glEndTransformFeedbackNV == NULL);
  glewInfoFunc("glGetActiveVaryingNV", glGetActiveVaryingNV == NULL);
  glewInfoFunc("glGetTransformFeedbackVaryingNV", glGetTransformFeedbackVaryingNV == NULL);
  glewInfoFunc("glGetVaryingLocationNV", glGetVaryingLocationNV == NULL);
  glewInfoFunc("glTransformFeedbackAttribsNV", glTransformFeedbackAttribsNV == NULL);
  glewInfoFunc("glTransformFeedbackVaryingsNV", glTransformFeedbackVaryingsNV == NULL);
}

#endif /* GL_NV_transform_feedback */

#ifdef GL_NV_transform_feedback2

static void _glewInfo_GL_NV_transform_feedback2 (void)
{
  glewPrintExt("GL_NV_transform_feedback2", GLEW_NV_transform_feedback2, glewIsSupported("GL_NV_transform_feedback2"), glewGetExtension("GL_NV_transform_feedback2"));

  glewInfoFunc("glBindTransformFeedbackNV", glBindTransformFeedbackNV == NULL);
  glewInfoFunc("glDeleteTransformFeedbacksNV", glDeleteTransformFeedbacksNV == NULL);
  glewInfoFunc("glDrawTransformFeedbackNV", glDrawTransformFeedbackNV == NULL);
  glewInfoFunc("glGenTransformFeedbacksNV", glGenTransformFeedbacksNV == NULL);
  glewInfoFunc("glIsTransformFeedbackNV", glIsTransformFeedbackNV == NULL);
  glewInfoFunc("glPauseTransformFeedbackNV", glPauseTransformFeedbackNV == NULL);
  glewInfoFunc("glResumeTransformFeedbackNV", glResumeTransformFeedbackNV == NULL);
}

#endif /* GL_NV_transform_feedback2 */

#ifdef GL_NV_uniform_buffer_unified_memory

static void _glewInfo_GL_NV_uniform_buffer_unified_memory (void)
{
  glewPrintExt("GL_NV_uniform_buffer_unified_memory", GLEW_NV_uniform_buffer_unified_memory, glewIsSupported("GL_NV_uniform_buffer_unified_memory"), glewGetExtension("GL_NV_uniform_buffer_unified_memory"));
}

#endif /* GL_NV_uniform_buffer_unified_memory */

#ifdef GL_NV_vdpau_interop

static void _glewInfo_GL_NV_vdpau_interop (void)
{
  glewPrintExt("GL_NV_vdpau_interop", GLEW_NV_vdpau_interop, glewIsSupported("GL_NV_vdpau_interop"), glewGetExtension("GL_NV_vdpau_interop"));

  glewInfoFunc("glVDPAUFiniNV", glVDPAUFiniNV == NULL);
  glewInfoFunc("glVDPAUGetSurfaceivNV", glVDPAUGetSurfaceivNV == NULL);
  glewInfoFunc("glVDPAUInitNV", glVDPAUInitNV == NULL);
  glewInfoFunc("glVDPAUIsSurfaceNV", glVDPAUIsSurfaceNV == NULL);
  glewInfoFunc("glVDPAUMapSurfacesNV", glVDPAUMapSurfacesNV == NULL);
  glewInfoFunc("glVDPAURegisterOutputSurfaceNV", glVDPAURegisterOutputSurfaceNV == NULL);
  glewInfoFunc("glVDPAURegisterVideoSurfaceNV", glVDPAURegisterVideoSurfaceNV == NULL);
  glewInfoFunc("glVDPAUSurfaceAccessNV", glVDPAUSurfaceAccessNV == NULL);
  glewInfoFunc("glVDPAUUnmapSurfacesNV", glVDPAUUnmapSurfacesNV == NULL);
  glewInfoFunc("glVDPAUUnregisterSurfaceNV", glVDPAUUnregisterSurfaceNV == NULL);
}

#endif /* GL_NV_vdpau_interop */

#ifdef GL_NV_vertex_array_range

static void _glewInfo_GL_NV_vertex_array_range (void)
{
  glewPrintExt("GL_NV_vertex_array_range", GLEW_NV_vertex_array_range, glewIsSupported("GL_NV_vertex_array_range"), glewGetExtension("GL_NV_vertex_array_range"));

  glewInfoFunc("glFlushVertexArrayRangeNV", glFlushVertexArrayRangeNV == NULL);
  glewInfoFunc("glVertexArrayRangeNV", glVertexArrayRangeNV == NULL);
}

#endif /* GL_NV_vertex_array_range */

#ifdef GL_NV_vertex_array_range2

static void _glewInfo_GL_NV_vertex_array_range2 (void)
{
  glewPrintExt("GL_NV_vertex_array_range2", GLEW_NV_vertex_array_range2, glewIsSupported("GL_NV_vertex_array_range2"), glewGetExtension("GL_NV_vertex_array_range2"));
}

#endif /* GL_NV_vertex_array_range2 */

#ifdef GL_NV_vertex_attrib_integer_64bit

static void _glewInfo_GL_NV_vertex_attrib_integer_64bit (void)
{
  glewPrintExt("GL_NV_vertex_attrib_integer_64bit", GLEW_NV_vertex_attrib_integer_64bit, glewIsSupported("GL_NV_vertex_attrib_integer_64bit"), glewGetExtension("GL_NV_vertex_attrib_integer_64bit"));

  glewInfoFunc("glGetVertexAttribLi64vNV", glGetVertexAttribLi64vNV == NULL);
  glewInfoFunc("glGetVertexAttribLui64vNV", glGetVertexAttribLui64vNV == NULL);
  glewInfoFunc("glVertexAttribL1i64NV", glVertexAttribL1i64NV == NULL);
  glewInfoFunc("glVertexAttribL1i64vNV", glVertexAttribL1i64vNV == NULL);
  glewInfoFunc("glVertexAttribL1ui64NV", glVertexAttribL1ui64NV == NULL);
  glewInfoFunc("glVertexAttribL1ui64vNV", glVertexAttribL1ui64vNV == NULL);
  glewInfoFunc("glVertexAttribL2i64NV", glVertexAttribL2i64NV == NULL);
  glewInfoFunc("glVertexAttribL2i64vNV", glVertexAttribL2i64vNV == NULL);
  glewInfoFunc("glVertexAttribL2ui64NV", glVertexAttribL2ui64NV == NULL);
  glewInfoFunc("glVertexAttribL2ui64vNV", glVertexAttribL2ui64vNV == NULL);
  glewInfoFunc("glVertexAttribL3i64NV", glVertexAttribL3i64NV == NULL);
  glewInfoFunc("glVertexAttribL3i64vNV", glVertexAttribL3i64vNV == NULL);
  glewInfoFunc("glVertexAttribL3ui64NV", glVertexAttribL3ui64NV == NULL);
  glewInfoFunc("glVertexAttribL3ui64vNV", glVertexAttribL3ui64vNV == NULL);
  glewInfoFunc("glVertexAttribL4i64NV", glVertexAttribL4i64NV == NULL);
  glewInfoFunc("glVertexAttribL4i64vNV", glVertexAttribL4i64vNV == NULL);
  glewInfoFunc("glVertexAttribL4ui64NV", glVertexAttribL4ui64NV == NULL);
  glewInfoFunc("glVertexAttribL4ui64vNV", glVertexAttribL4ui64vNV == NULL);
  glewInfoFunc("glVertexAttribLFormatNV", glVertexAttribLFormatNV == NULL);
}

#endif /* GL_NV_vertex_attrib_integer_64bit */

#ifdef GL_NV_vertex_buffer_unified_memory

static void _glewInfo_GL_NV_vertex_buffer_unified_memory (void)
{
  glewPrintExt("GL_NV_vertex_buffer_unified_memory", GLEW_NV_vertex_buffer_unified_memory, glewIsSupported("GL_NV_vertex_buffer_unified_memory"), glewGetExtension("GL_NV_vertex_buffer_unified_memory"));

  glewInfoFunc("glBufferAddressRangeNV", glBufferAddressRangeNV == NULL);
  glewInfoFunc("glColorFormatNV", glColorFormatNV == NULL);
  glewInfoFunc("glEdgeFlagFormatNV", glEdgeFlagFormatNV == NULL);
  glewInfoFunc("glFogCoordFormatNV", glFogCoordFormatNV == NULL);
  glewInfoFunc("glGetIntegerui64i_vNV", glGetIntegerui64i_vNV == NULL);
  glewInfoFunc("glIndexFormatNV", glIndexFormatNV == NULL);
  glewInfoFunc("glNormalFormatNV", glNormalFormatNV == NULL);
  glewInfoFunc("glSecondaryColorFormatNV", glSecondaryColorFormatNV == NULL);
  glewInfoFunc("glTexCoordFormatNV", glTexCoordFormatNV == NULL);
  glewInfoFunc("glVertexAttribFormatNV", glVertexAttribFormatNV == NULL);
  glewInfoFunc("glVertexAttribIFormatNV", glVertexAttribIFormatNV == NULL);
  glewInfoFunc("glVertexFormatNV", glVertexFormatNV == NULL);
}

#endif /* GL_NV_vertex_buffer_unified_memory */

#ifdef GL_NV_vertex_program

static void _glewInfo_GL_NV_vertex_program (void)
{
  glewPrintExt("GL_NV_vertex_program", GLEW_NV_vertex_program, glewIsSupported("GL_NV_vertex_program"), glewGetExtension("GL_NV_vertex_program"));

  glewInfoFunc("glAreProgramsResidentNV", glAreProgramsResidentNV == NULL);
  glewInfoFunc("glBindProgramNV", glBindProgramNV == NULL);
  glewInfoFunc("glDeleteProgramsNV", glDeleteProgramsNV == NULL);
  glewInfoFunc("glExecuteProgramNV", glExecuteProgramNV == NULL);
  glewInfoFunc("glGenProgramsNV", glGenProgramsNV == NULL);
  glewInfoFunc("glGetProgramParameterdvNV", glGetProgramParameterdvNV == NULL);
  glewInfoFunc("glGetProgramParameterfvNV", glGetProgramParameterfvNV == NULL);
  glewInfoFunc("glGetProgramStringNV", glGetProgramStringNV == NULL);
  glewInfoFunc("glGetProgramivNV", glGetProgramivNV == NULL);
  glewInfoFunc("glGetTrackMatrixivNV", glGetTrackMatrixivNV == NULL);
  glewInfoFunc("glGetVertexAttribPointervNV", glGetVertexAttribPointervNV == NULL);
  glewInfoFunc("glGetVertexAttribdvNV", glGetVertexAttribdvNV == NULL);
  glewInfoFunc("glGetVertexAttribfvNV", glGetVertexAttribfvNV == NULL);
  glewInfoFunc("glGetVertexAttribivNV", glGetVertexAttribivNV == NULL);
  glewInfoFunc("glIsProgramNV", glIsProgramNV == NULL);
  glewInfoFunc("glLoadProgramNV", glLoadProgramNV == NULL);
  glewInfoFunc("glProgramParameter4dNV", glProgramParameter4dNV == NULL);
  glewInfoFunc("glProgramParameter4dvNV", glProgramParameter4dvNV == NULL);
  glewInfoFunc("glProgramParameter4fNV", glProgramParameter4fNV == NULL);
  glewInfoFunc("glProgramParameter4fvNV", glProgramParameter4fvNV == NULL);
  glewInfoFunc("glProgramParameters4dvNV", glProgramParameters4dvNV == NULL);
  glewInfoFunc("glProgramParameters4fvNV", glProgramParameters4fvNV == NULL);
  glewInfoFunc("glRequestResidentProgramsNV", glRequestResidentProgramsNV == NULL);
  glewInfoFunc("glTrackMatrixNV", glTrackMatrixNV == NULL);
  glewInfoFunc("glVertexAttrib1dNV", glVertexAttrib1dNV == NULL);
  glewInfoFunc("glVertexAttrib1dvNV", glVertexAttrib1dvNV == NULL);
  glewInfoFunc("glVertexAttrib1fNV", glVertexAttrib1fNV == NULL);
  glewInfoFunc("glVertexAttrib1fvNV", glVertexAttrib1fvNV == NULL);
  glewInfoFunc("glVertexAttrib1sNV", glVertexAttrib1sNV == NULL);
  glewInfoFunc("glVertexAttrib1svNV", glVertexAttrib1svNV == NULL);
  glewInfoFunc("glVertexAttrib2dNV", glVertexAttrib2dNV == NULL);
  glewInfoFunc("glVertexAttrib2dvNV", glVertexAttrib2dvNV == NULL);
  glewInfoFunc("glVertexAttrib2fNV", glVertexAttrib2fNV == NULL);
  glewInfoFunc("glVertexAttrib2fvNV", glVertexAttrib2fvNV == NULL);
  glewInfoFunc("glVertexAttrib2sNV", glVertexAttrib2sNV == NULL);
  glewInfoFunc("glVertexAttrib2svNV", glVertexAttrib2svNV == NULL);
  glewInfoFunc("glVertexAttrib3dNV", glVertexAttrib3dNV == NULL);
  glewInfoFunc("glVertexAttrib3dvNV", glVertexAttrib3dvNV == NULL);
  glewInfoFunc("glVertexAttrib3fNV", glVertexAttrib3fNV == NULL);
  glewInfoFunc("glVertexAttrib3fvNV", glVertexAttrib3fvNV == NULL);
  glewInfoFunc("glVertexAttrib3sNV", glVertexAttrib3sNV == NULL);
  glewInfoFunc("glVertexAttrib3svNV", glVertexAttrib3svNV == NULL);
  glewInfoFunc("glVertexAttrib4dNV", glVertexAttrib4dNV == NULL);
  glewInfoFunc("glVertexAttrib4dvNV", glVertexAttrib4dvNV == NULL);
  glewInfoFunc("glVertexAttrib4fNV", glVertexAttrib4fNV == NULL);
  glewInfoFunc("glVertexAttrib4fvNV", glVertexAttrib4fvNV == NULL);
  glewInfoFunc("glVertexAttrib4sNV", glVertexAttrib4sNV == NULL);
  glewInfoFunc("glVertexAttrib4svNV", glVertexAttrib4svNV == NULL);
  glewInfoFunc("glVertexAttrib4ubNV", glVertexAttrib4ubNV == NULL);
  glewInfoFunc("glVertexAttrib4ubvNV", glVertexAttrib4ubvNV == NULL);
  glewInfoFunc("glVertexAttribPointerNV", glVertexAttribPointerNV == NULL);
  glewInfoFunc("glVertexAttribs1dvNV", glVertexAttribs1dvNV == NULL);
  glewInfoFunc("glVertexAttribs1fvNV", glVertexAttribs1fvNV == NULL);
  glewInfoFunc("glVertexAttribs1svNV", glVertexAttribs1svNV == NULL);
  glewInfoFunc("glVertexAttribs2dvNV", glVertexAttribs2dvNV == NULL);
  glewInfoFunc("glVertexAttribs2fvNV", glVertexAttribs2fvNV == NULL);
  glewInfoFunc("glVertexAttribs2svNV", glVertexAttribs2svNV == NULL);
  glewInfoFunc("glVertexAttribs3dvNV", glVertexAttribs3dvNV == NULL);
  glewInfoFunc("glVertexAttribs3fvNV", glVertexAttribs3fvNV == NULL);
  glewInfoFunc("glVertexAttribs3svNV", glVertexAttribs3svNV == NULL);
  glewInfoFunc("glVertexAttribs4dvNV", glVertexAttribs4dvNV == NULL);
  glewInfoFunc("glVertexAttribs4fvNV", glVertexAttribs4fvNV == NULL);
  glewInfoFunc("glVertexAttribs4svNV", glVertexAttribs4svNV == NULL);
  glewInfoFunc("glVertexAttribs4ubvNV", glVertexAttribs4ubvNV == NULL);
}

#endif /* GL_NV_vertex_program */

#ifdef GL_NV_vertex_program1_1

static void _glewInfo_GL_NV_vertex_program1_1 (void)
{
  glewPrintExt("GL_NV_vertex_program1_1", GLEW_NV_vertex_program1_1, glewIsSupported("GL_NV_vertex_program1_1"), glewGetExtension("GL_NV_vertex_program1_1"));
}

#endif /* GL_NV_vertex_program1_1 */

#ifdef GL_NV_vertex_program2

static void _glewInfo_GL_NV_vertex_program2 (void)
{
  glewPrintExt("GL_NV_vertex_program2", GLEW_NV_vertex_program2, glewIsSupported("GL_NV_vertex_program2"), glewGetExtension("GL_NV_vertex_program2"));
}

#endif /* GL_NV_vertex_program2 */

#ifdef GL_NV_vertex_program2_option

static void _glewInfo_GL_NV_vertex_program2_option (void)
{
  glewPrintExt("GL_NV_vertex_program2_option", GLEW_NV_vertex_program2_option, glewIsSupported("GL_NV_vertex_program2_option"), glewGetExtension("GL_NV_vertex_program2_option"));
}

#endif /* GL_NV_vertex_program2_option */

#ifdef GL_NV_vertex_program3

static void _glewInfo_GL_NV_vertex_program3 (void)
{
  glewPrintExt("GL_NV_vertex_program3", GLEW_NV_vertex_program3, glewIsSupported("GL_NV_vertex_program3"), glewGetExtension("GL_NV_vertex_program3"));
}

#endif /* GL_NV_vertex_program3 */

#ifdef GL_NV_vertex_program4

static void _glewInfo_GL_NV_vertex_program4 (void)
{
  glewPrintExt("GL_NV_vertex_program4", GLEW_NV_vertex_program4, glewIsSupported("GL_NV_vertex_program4"), glewGetExtension("GL_NV_gpu_program4"));
}

#endif /* GL_NV_vertex_program4 */

#ifdef GL_NV_video_capture

static void _glewInfo_GL_NV_video_capture (void)
{
  glewPrintExt("GL_NV_video_capture", GLEW_NV_video_capture, glewIsSupported("GL_NV_video_capture"), glewGetExtension("GL_NV_video_capture"));

  glewInfoFunc("glBeginVideoCaptureNV", glBeginVideoCaptureNV == NULL);
  glewInfoFunc("glBindVideoCaptureStreamBufferNV", glBindVideoCaptureStreamBufferNV == NULL);
  glewInfoFunc("glBindVideoCaptureStreamTextureNV", glBindVideoCaptureStreamTextureNV == NULL);
  glewInfoFunc("glEndVideoCaptureNV", glEndVideoCaptureNV == NULL);
  glewInfoFunc("glGetVideoCaptureStreamdvNV", glGetVideoCaptureStreamdvNV == NULL);
  glewInfoFunc("glGetVideoCaptureStreamfvNV", glGetVideoCaptureStreamfvNV == NULL);
  glewInfoFunc("glGetVideoCaptureStreamivNV", glGetVideoCaptureStreamivNV == NULL);
  glewInfoFunc("glGetVideoCaptureivNV", glGetVideoCaptureivNV == NULL);
  glewInfoFunc("glVideoCaptureNV", glVideoCaptureNV == NULL);
  glewInfoFunc("glVideoCaptureStreamParameterdvNV", glVideoCaptureStreamParameterdvNV == NULL);
  glewInfoFunc("glVideoCaptureStreamParameterfvNV", glVideoCaptureStreamParameterfvNV == NULL);
  glewInfoFunc("glVideoCaptureStreamParameterivNV", glVideoCaptureStreamParameterivNV == NULL);
}

#endif /* GL_NV_video_capture */

#ifdef GL_NV_viewport_array

static void _glewInfo_GL_NV_viewport_array (void)
{
  glewPrintExt("GL_NV_viewport_array", GLEW_NV_viewport_array, glewIsSupported("GL_NV_viewport_array"), glewGetExtension("GL_NV_viewport_array"));

  glewInfoFunc("glDepthRangeArrayfvNV", glDepthRangeArrayfvNV == NULL);
  glewInfoFunc("glDepthRangeIndexedfNV", glDepthRangeIndexedfNV == NULL);
  glewInfoFunc("glDisableiNV", glDisableiNV == NULL);
  glewInfoFunc("glEnableiNV", glEnableiNV == NULL);
  glewInfoFunc("glGetFloati_vNV", glGetFloati_vNV == NULL);
  glewInfoFunc("glIsEnablediNV", glIsEnablediNV == NULL);
  glewInfoFunc("glScissorArrayvNV", glScissorArrayvNV == NULL);
  glewInfoFunc("glScissorIndexedNV", glScissorIndexedNV == NULL);
  glewInfoFunc("glScissorIndexedvNV", glScissorIndexedvNV == NULL);
  glewInfoFunc("glViewportArrayvNV", glViewportArrayvNV == NULL);
  glewInfoFunc("glViewportIndexedfNV", glViewportIndexedfNV == NULL);
  glewInfoFunc("glViewportIndexedfvNV", glViewportIndexedfvNV == NULL);
}

#endif /* GL_NV_viewport_array */

#ifdef GL_NV_viewport_array2

static void _glewInfo_GL_NV_viewport_array2 (void)
{
  glewPrintExt("GL_NV_viewport_array2", GLEW_NV_viewport_array2, glewIsSupported("GL_NV_viewport_array2"), glewGetExtension("GL_NV_viewport_array2"));
}

#endif /* GL_NV_viewport_array2 */

#ifdef GL_NV_viewport_swizzle

static void _glewInfo_GL_NV_viewport_swizzle (void)
{
  glewPrintExt("GL_NV_viewport_swizzle", GLEW_NV_viewport_swizzle, glewIsSupported("GL_NV_viewport_swizzle"), glewGetExtension("GL_NV_viewport_swizzle"));

  glewInfoFunc("glViewportSwizzleNV", glViewportSwizzleNV == NULL);
}

#endif /* GL_NV_viewport_swizzle */

#ifdef GL_OES_byte_coordinates

static void _glewInfo_GL_OES_byte_coordinates (void)
{
  glewPrintExt("GL_OES_byte_coordinates", GLEW_OES_byte_coordinates, glewIsSupported("GL_OES_byte_coordinates"), glewGetExtension("GL_OES_byte_coordinates"));
}

#endif /* GL_OES_byte_coordinates */

#ifdef GL_OML_interlace

static void _glewInfo_GL_OML_interlace (void)
{
  glewPrintExt("GL_OML_interlace", GLEW_OML_interlace, glewIsSupported("GL_OML_interlace"), glewGetExtension("GL_OML_interlace"));
}

#endif /* GL_OML_interlace */

#ifdef GL_OML_resample

static void _glewInfo_GL_OML_resample (void)
{
  glewPrintExt("GL_OML_resample", GLEW_OML_resample, glewIsSupported("GL_OML_resample"), glewGetExtension("GL_OML_resample"));
}

#endif /* GL_OML_resample */

#ifdef GL_OML_subsample

static void _glewInfo_GL_OML_subsample (void)
{
  glewPrintExt("GL_OML_subsample", GLEW_OML_subsample, glewIsSupported("GL_OML_subsample"), glewGetExtension("GL_OML_subsample"));
}

#endif /* GL_OML_subsample */

#ifdef GL_OVR_multiview

static void _glewInfo_GL_OVR_multiview (void)
{
  glewPrintExt("GL_OVR_multiview", GLEW_OVR_multiview, glewIsSupported("GL_OVR_multiview"), glewGetExtension("GL_OVR_multiview"));

  glewInfoFunc("glFramebufferTextureMultiviewOVR", glFramebufferTextureMultiviewOVR == NULL);
}

#endif /* GL_OVR_multiview */

#ifdef GL_OVR_multiview2

static void _glewInfo_GL_OVR_multiview2 (void)
{
  glewPrintExt("GL_OVR_multiview2", GLEW_OVR_multiview2, glewIsSupported("GL_OVR_multiview2"), glewGetExtension("GL_OVR_multiview2"));
}

#endif /* GL_OVR_multiview2 */

#ifdef GL_OVR_multiview_multisampled_render_to_texture

static void _glewInfo_GL_OVR_multiview_multisampled_render_to_texture (void)
{
  glewPrintExt("GL_OVR_multiview_multisampled_render_to_texture", GLEW_OVR_multiview_multisampled_render_to_texture, glewIsSupported("GL_OVR_multiview_multisampled_render_to_texture"), glewGetExtension("GL_OVR_multiview_multisampled_render_to_texture"));

  glewInfoFunc("glFramebufferTextureMultisampleMultiviewOVR", glFramebufferTextureMultisampleMultiviewOVR == NULL);
}

#endif /* GL_OVR_multiview_multisampled_render_to_texture */

#ifdef GL_PGI_misc_hints

static void _glewInfo_GL_PGI_misc_hints (void)
{
  glewPrintExt("GL_PGI_misc_hints", GLEW_PGI_misc_hints, glewIsSupported("GL_PGI_misc_hints"), glewGetExtension("GL_PGI_misc_hints"));
}

#endif /* GL_PGI_misc_hints */

#ifdef GL_PGI_vertex_hints

static void _glewInfo_GL_PGI_vertex_hints (void)
{
  glewPrintExt("GL_PGI_vertex_hints", GLEW_PGI_vertex_hints, glewIsSupported("GL_PGI_vertex_hints"), glewGetExtension("GL_PGI_vertex_hints"));
}

#endif /* GL_PGI_vertex_hints */

#ifdef GL_QCOM_alpha_test

static void _glewInfo_GL_QCOM_alpha_test (void)
{
  glewPrintExt("GL_QCOM_alpha_test", GLEW_QCOM_alpha_test, glewIsSupported("GL_QCOM_alpha_test"), glewGetExtension("GL_QCOM_alpha_test"));

  glewInfoFunc("glAlphaFuncQCOM", glAlphaFuncQCOM == NULL);
}

#endif /* GL_QCOM_alpha_test */

#ifdef GL_QCOM_binning_control

static void _glewInfo_GL_QCOM_binning_control (void)
{
  glewPrintExt("GL_QCOM_binning_control", GLEW_QCOM_binning_control, glewIsSupported("GL_QCOM_binning_control"), glewGetExtension("GL_QCOM_binning_control"));
}

#endif /* GL_QCOM_binning_control */

#ifdef GL_QCOM_driver_control

static void _glewInfo_GL_QCOM_driver_control (void)
{
  glewPrintExt("GL_QCOM_driver_control", GLEW_QCOM_driver_control, glewIsSupported("GL_QCOM_driver_control"), glewGetExtension("GL_QCOM_driver_control"));

  glewInfoFunc("glDisableDriverControlQCOM", glDisableDriverControlQCOM == NULL);
  glewInfoFunc("glEnableDriverControlQCOM", glEnableDriverControlQCOM == NULL);
  glewInfoFunc("glGetDriverControlStringQCOM", glGetDriverControlStringQCOM == NULL);
  glewInfoFunc("glGetDriverControlsQCOM", glGetDriverControlsQCOM == NULL);
}

#endif /* GL_QCOM_driver_control */

#ifdef GL_QCOM_extended_get

static void _glewInfo_GL_QCOM_extended_get (void)
{
  glewPrintExt("GL_QCOM_extended_get", GLEW_QCOM_extended_get, glewIsSupported("GL_QCOM_extended_get"), glewGetExtension("GL_QCOM_extended_get"));

  glewInfoFunc("glExtGetBufferPointervQCOM", glExtGetBufferPointervQCOM == NULL);
  glewInfoFunc("glExtGetBuffersQCOM", glExtGetBuffersQCOM == NULL);
  glewInfoFunc("glExtGetFramebuffersQCOM", glExtGetFramebuffersQCOM == NULL);
  glewInfoFunc("glExtGetRenderbuffersQCOM", glExtGetRenderbuffersQCOM == NULL);
  glewInfoFunc("glExtGetTexLevelParameterivQCOM", glExtGetTexLevelParameterivQCOM == NULL);
  glewInfoFunc("glExtGetTexSubImageQCOM", glExtGetTexSubImageQCOM == NULL);
  glewInfoFunc("glExtGetTexturesQCOM", glExtGetTexturesQCOM == NULL);
  glewInfoFunc("glExtTexObjectStateOverrideiQCOM", glExtTexObjectStateOverrideiQCOM == NULL);
}

#endif /* GL_QCOM_extended_get */

#ifdef GL_QCOM_extended_get2

static void _glewInfo_GL_QCOM_extended_get2 (void)
{
  glewPrintExt("GL_QCOM_extended_get2", GLEW_QCOM_extended_get2, glewIsSupported("GL_QCOM_extended_get2"), glewGetExtension("GL_QCOM_extended_get2"));

  glewInfoFunc("glExtGetProgramBinarySourceQCOM", glExtGetProgramBinarySourceQCOM == NULL);
  glewInfoFunc("glExtGetProgramsQCOM", glExtGetProgramsQCOM == NULL);
  glewInfoFunc("glExtGetShadersQCOM", glExtGetShadersQCOM == NULL);
  glewInfoFunc("glExtIsProgramBinaryQCOM", glExtIsProgramBinaryQCOM == NULL);
}

#endif /* GL_QCOM_extended_get2 */

#ifdef GL_QCOM_framebuffer_foveated

static void _glewInfo_GL_QCOM_framebuffer_foveated (void)
{
  glewPrintExt("GL_QCOM_framebuffer_foveated", GLEW_QCOM_framebuffer_foveated, glewIsSupported("GL_QCOM_framebuffer_foveated"), glewGetExtension("GL_QCOM_framebuffer_foveated"));

  glewInfoFunc("glFramebufferFoveationConfigQCOM", glFramebufferFoveationConfigQCOM == NULL);
  glewInfoFunc("glFramebufferFoveationParametersQCOM", glFramebufferFoveationParametersQCOM == NULL);
}

#endif /* GL_QCOM_framebuffer_foveated */

#ifdef GL_QCOM_perfmon_global_mode

static void _glewInfo_GL_QCOM_perfmon_global_mode (void)
{
  glewPrintExt("GL_QCOM_perfmon_global_mode", GLEW_QCOM_perfmon_global_mode, glewIsSupported("GL_QCOM_perfmon_global_mode"), glewGetExtension("GL_QCOM_perfmon_global_mode"));
}

#endif /* GL_QCOM_perfmon_global_mode */

#ifdef GL_QCOM_shader_framebuffer_fetch_noncoherent

static void _glewInfo_GL_QCOM_shader_framebuffer_fetch_noncoherent (void)
{
  glewPrintExt("GL_QCOM_shader_framebuffer_fetch_noncoherent", GLEW_QCOM_shader_framebuffer_fetch_noncoherent, glewIsSupported("GL_QCOM_shader_framebuffer_fetch_noncoherent"), glewGetExtension("GL_QCOM_shader_framebuffer_fetch_noncoherent"));

  glewInfoFunc("glFramebufferFetchBarrierQCOM", glFramebufferFetchBarrierQCOM == NULL);
}

#endif /* GL_QCOM_shader_framebuffer_fetch_noncoherent */

#ifdef GL_QCOM_tiled_rendering

static void _glewInfo_GL_QCOM_tiled_rendering (void)
{
  glewPrintExt("GL_QCOM_tiled_rendering", GLEW_QCOM_tiled_rendering, glewIsSupported("GL_QCOM_tiled_rendering"), glewGetExtension("GL_QCOM_tiled_rendering"));

  glewInfoFunc("glEndTilingQCOM", glEndTilingQCOM == NULL);
  glewInfoFunc("glStartTilingQCOM", glStartTilingQCOM == NULL);
}

#endif /* GL_QCOM_tiled_rendering */

#ifdef GL_QCOM_writeonly_rendering

static void _glewInfo_GL_QCOM_writeonly_rendering (void)
{
  glewPrintExt("GL_QCOM_writeonly_rendering", GLEW_QCOM_writeonly_rendering, glewIsSupported("GL_QCOM_writeonly_rendering"), glewGetExtension("GL_QCOM_writeonly_rendering"));
}

#endif /* GL_QCOM_writeonly_rendering */

#ifdef GL_REGAL_ES1_0_compatibility

static void _glewInfo_GL_REGAL_ES1_0_compatibility (void)
{
  glewPrintExt("GL_REGAL_ES1_0_compatibility", GLEW_REGAL_ES1_0_compatibility, glewIsSupported("GL_REGAL_ES1_0_compatibility"), glewGetExtension("GL_REGAL_ES1_0_compatibility"));

  glewInfoFunc("glAlphaFuncx", glAlphaFuncx == NULL);
  glewInfoFunc("glClearColorx", glClearColorx == NULL);
  glewInfoFunc("glClearDepthx", glClearDepthx == NULL);
  glewInfoFunc("glColor4x", glColor4x == NULL);
  glewInfoFunc("glDepthRangex", glDepthRangex == NULL);
  glewInfoFunc("glFogx", glFogx == NULL);
  glewInfoFunc("glFogxv", glFogxv == NULL);
  glewInfoFunc("glFrustumf", glFrustumf == NULL);
  glewInfoFunc("glFrustumx", glFrustumx == NULL);
  glewInfoFunc("glLightModelx", glLightModelx == NULL);
  glewInfoFunc("glLightModelxv", glLightModelxv == NULL);
  glewInfoFunc("glLightx", glLightx == NULL);
  glewInfoFunc("glLightxv", glLightxv == NULL);
  glewInfoFunc("glLineWidthx", glLineWidthx == NULL);
  glewInfoFunc("glLoadMatrixx", glLoadMatrixx == NULL);
  glewInfoFunc("glMaterialx", glMaterialx == NULL);
  glewInfoFunc("glMaterialxv", glMaterialxv == NULL);
  glewInfoFunc("glMultMatrixx", glMultMatrixx == NULL);
  glewInfoFunc("glMultiTexCoord4x", glMultiTexCoord4x == NULL);
  glewInfoFunc("glNormal3x", glNormal3x == NULL);
  glewInfoFunc("glOrthof", glOrthof == NULL);
  glewInfoFunc("glOrthox", glOrthox == NULL);
  glewInfoFunc("glPointSizex", glPointSizex == NULL);
  glewInfoFunc("glPolygonOffsetx", glPolygonOffsetx == NULL);
  glewInfoFunc("glRotatex", glRotatex == NULL);
  glewInfoFunc("glSampleCoveragex", glSampleCoveragex == NULL);
  glewInfoFunc("glScalex", glScalex == NULL);
  glewInfoFunc("glTexEnvx", glTexEnvx == NULL);
  glewInfoFunc("glTexEnvxv", glTexEnvxv == NULL);
  glewInfoFunc("glTexParameterx", glTexParameterx == NULL);
  glewInfoFunc("glTranslatex", glTranslatex == NULL);
}

#endif /* GL_REGAL_ES1_0_compatibility */

#ifdef GL_REGAL_ES1_1_compatibility

static void _glewInfo_GL_REGAL_ES1_1_compatibility (void)
{
  glewPrintExt("GL_REGAL_ES1_1_compatibility", GLEW_REGAL_ES1_1_compatibility, glewIsSupported("GL_REGAL_ES1_1_compatibility"), glewGetExtension("GL_REGAL_ES1_1_compatibility"));

  glewInfoFunc("glClipPlanef", glClipPlanef == NULL);
  glewInfoFunc("glClipPlanex", glClipPlanex == NULL);
  glewInfoFunc("glGetClipPlanef", glGetClipPlanef == NULL);
  glewInfoFunc("glGetClipPlanex", glGetClipPlanex == NULL);
  glewInfoFunc("glGetFixedv", glGetFixedv == NULL);
  glewInfoFunc("glGetLightxv", glGetLightxv == NULL);
  glewInfoFunc("glGetMaterialxv", glGetMaterialxv == NULL);
  glewInfoFunc("glGetTexEnvxv", glGetTexEnvxv == NULL);
  glewInfoFunc("glGetTexParameterxv", glGetTexParameterxv == NULL);
  glewInfoFunc("glPointParameterx", glPointParameterx == NULL);
  glewInfoFunc("glPointParameterxv", glPointParameterxv == NULL);
  glewInfoFunc("glPointSizePointerOES", glPointSizePointerOES == NULL);
  glewInfoFunc("glTexParameterxv", glTexParameterxv == NULL);
}

#endif /* GL_REGAL_ES1_1_compatibility */

#ifdef GL_REGAL_enable

static void _glewInfo_GL_REGAL_enable (void)
{
  glewPrintExt("GL_REGAL_enable", GLEW_REGAL_enable, glewIsSupported("GL_REGAL_enable"), glewGetExtension("GL_REGAL_enable"));
}

#endif /* GL_REGAL_enable */

#ifdef GL_REGAL_error_string

static void _glewInfo_GL_REGAL_error_string (void)
{
  glewPrintExt("GL_REGAL_error_string", GLEW_REGAL_error_string, glewIsSupported("GL_REGAL_error_string"), glewGetExtension("GL_REGAL_error_string"));

  glewInfoFunc("glErrorStringREGAL", glErrorStringREGAL == NULL);
}

#endif /* GL_REGAL_error_string */

#ifdef GL_REGAL_extension_query

static void _glewInfo_GL_REGAL_extension_query (void)
{
  glewPrintExt("GL_REGAL_extension_query", GLEW_REGAL_extension_query, glewIsSupported("GL_REGAL_extension_query"), glewGetExtension("GL_REGAL_extension_query"));

  glewInfoFunc("glGetExtensionREGAL", glGetExtensionREGAL == NULL);
  glewInfoFunc("glIsSupportedREGAL", glIsSupportedREGAL == NULL);
}

#endif /* GL_REGAL_extension_query */

#ifdef GL_REGAL_log

static void _glewInfo_GL_REGAL_log (void)
{
  glewPrintExt("GL_REGAL_log", GLEW_REGAL_log, glewIsSupported("GL_REGAL_log"), glewGetExtension("GL_REGAL_log"));

  glewInfoFunc("glLogMessageCallbackREGAL", glLogMessageCallbackREGAL == NULL);
}

#endif /* GL_REGAL_log */

#ifdef GL_REGAL_proc_address

static void _glewInfo_GL_REGAL_proc_address (void)
{
  glewPrintExt("GL_REGAL_proc_address", GLEW_REGAL_proc_address, glewIsSupported("GL_REGAL_proc_address"), glewGetExtension("GL_REGAL_proc_address"));

  glewInfoFunc("glGetProcAddressREGAL", glGetProcAddressREGAL == NULL);
}

#endif /* GL_REGAL_proc_address */

#ifdef GL_REND_screen_coordinates

static void _glewInfo_GL_REND_screen_coordinates (void)
{
  glewPrintExt("GL_REND_screen_coordinates", GLEW_REND_screen_coordinates, glewIsSupported("GL_REND_screen_coordinates"), glewGetExtension("GL_REND_screen_coordinates"));
}

#endif /* GL_REND_screen_coordinates */

#ifdef GL_S3_s3tc

static void _glewInfo_GL_S3_s3tc (void)
{
  glewPrintExt("GL_S3_s3tc", GLEW_S3_s3tc, glewIsSupported("GL_S3_s3tc"), glewGetExtension("GL_S3_s3tc"));
}

#endif /* GL_S3_s3tc */

#ifdef GL_SGIS_clip_band_hint

static void _glewInfo_GL_SGIS_clip_band_hint (void)
{
  glewPrintExt("GL_SGIS_clip_band_hint", GLEW_SGIS_clip_band_hint, glewIsSupported("GL_SGIS_clip_band_hint"), glewGetExtension("GL_SGIS_clip_band_hint"));
}

#endif /* GL_SGIS_clip_band_hint */

#ifdef GL_SGIS_color_range

static void _glewInfo_GL_SGIS_color_range (void)
{
  glewPrintExt("GL_SGIS_color_range", GLEW_SGIS_color_range, glewIsSupported("GL_SGIS_color_range"), glewGetExtension("GL_SGIS_color_range"));
}

#endif /* GL_SGIS_color_range */

#ifdef GL_SGIS_detail_texture

static void _glewInfo_GL_SGIS_detail_texture (void)
{
  glewPrintExt("GL_SGIS_detail_texture", GLEW_SGIS_detail_texture, glewIsSupported("GL_SGIS_detail_texture"), glewGetExtension("GL_SGIS_detail_texture"));

  glewInfoFunc("glDetailTexFuncSGIS", glDetailTexFuncSGIS == NULL);
  glewInfoFunc("glGetDetailTexFuncSGIS", glGetDetailTexFuncSGIS == NULL);
}

#endif /* GL_SGIS_detail_texture */

#ifdef GL_SGIS_fog_function

static void _glewInfo_GL_SGIS_fog_function (void)
{
  glewPrintExt("GL_SGIS_fog_function", GLEW_SGIS_fog_function, glewIsSupported("GL_SGIS_fog_function"), glewGetExtension("GL_SGIS_fog_function"));

  glewInfoFunc("glFogFuncSGIS", glFogFuncSGIS == NULL);
  glewInfoFunc("glGetFogFuncSGIS", glGetFogFuncSGIS == NULL);
}

#endif /* GL_SGIS_fog_function */

#ifdef GL_SGIS_generate_mipmap

static void _glewInfo_GL_SGIS_generate_mipmap (void)
{
  glewPrintExt("GL_SGIS_generate_mipmap", GLEW_SGIS_generate_mipmap, glewIsSupported("GL_SGIS_generate_mipmap"), glewGetExtension("GL_SGIS_generate_mipmap"));
}

#endif /* GL_SGIS_generate_mipmap */

#ifdef GL_SGIS_line_texgen

static void _glewInfo_GL_SGIS_line_texgen (void)
{
  glewPrintExt("GL_SGIS_line_texgen", GLEW_SGIS_line_texgen, glewIsSupported("GL_SGIS_line_texgen"), glewGetExtension("GL_SGIS_line_texgen"));
}

#endif /* GL_SGIS_line_texgen */

#ifdef GL_SGIS_multisample

static void _glewInfo_GL_SGIS_multisample (void)
{
  glewPrintExt("GL_SGIS_multisample", GLEW_SGIS_multisample, glewIsSupported("GL_SGIS_multisample"), glewGetExtension("GL_SGIS_multisample"));

  glewInfoFunc("glSampleMaskSGIS", glSampleMaskSGIS == NULL);
  glewInfoFunc("glSamplePatternSGIS", glSamplePatternSGIS == NULL);
}

#endif /* GL_SGIS_multisample */

#ifdef GL_SGIS_multitexture

static void _glewInfo_GL_SGIS_multitexture (void)
{
  glewPrintExt("GL_SGIS_multitexture", GLEW_SGIS_multitexture, glewIsSupported("GL_SGIS_multitexture"), glewGetExtension("GL_SGIS_multitexture"));

  glewInfoFunc("glInterleavedTextureCoordSetsSGIS", glInterleavedTextureCoordSetsSGIS == NULL);
  glewInfoFunc("glSelectTextureCoordSetSGIS", glSelectTextureCoordSetSGIS == NULL);
  glewInfoFunc("glSelectTextureSGIS", glSelectTextureSGIS == NULL);
  glewInfoFunc("glSelectTextureTransformSGIS", glSelectTextureTransformSGIS == NULL);
}

#endif /* GL_SGIS_multitexture */

#ifdef GL_SGIS_pixel_texture

static void _glewInfo_GL_SGIS_pixel_texture (void)
{
  glewPrintExt("GL_SGIS_pixel_texture", GLEW_SGIS_pixel_texture, glewIsSupported("GL_SGIS_pixel_texture"), glewGetExtension("GL_SGIS_pixel_texture"));
}

#endif /* GL_SGIS_pixel_texture */

#ifdef GL_SGIS_point_line_texgen

static void _glewInfo_GL_SGIS_point_line_texgen (void)
{
  glewPrintExt("GL_SGIS_point_line_texgen", GLEW_SGIS_point_line_texgen, glewIsSupported("GL_SGIS_point_line_texgen"), glewGetExtension("GL_SGIS_point_line_texgen"));
}

#endif /* GL_SGIS_point_line_texgen */

#ifdef GL_SGIS_shared_multisample

static void _glewInfo_GL_SGIS_shared_multisample (void)
{
  glewPrintExt("GL_SGIS_shared_multisample", GLEW_SGIS_shared_multisample, glewIsSupported("GL_SGIS_shared_multisample"), glewGetExtension("GL_SGIS_shared_multisample"));

  glewInfoFunc("glMultisampleSubRectPosSGIS", glMultisampleSubRectPosSGIS == NULL);
}

#endif /* GL_SGIS_shared_multisample */

#ifdef GL_SGIS_sharpen_texture

static void _glewInfo_GL_SGIS_sharpen_texture (void)
{
  glewPrintExt("GL_SGIS_sharpen_texture", GLEW_SGIS_sharpen_texture, glewIsSupported("GL_SGIS_sharpen_texture"), glewGetExtension("GL_SGIS_sharpen_texture"));

  glewInfoFunc("glGetSharpenTexFuncSGIS", glGetSharpenTexFuncSGIS == NULL);
  glewInfoFunc("glSharpenTexFuncSGIS", glSharpenTexFuncSGIS == NULL);
}

#endif /* GL_SGIS_sharpen_texture */

#ifdef GL_SGIS_texture4D

static void _glewInfo_GL_SGIS_texture4D (void)
{
  glewPrintExt("GL_SGIS_texture4D", GLEW_SGIS_texture4D, glewIsSupported("GL_SGIS_texture4D"), glewGetExtension("GL_SGIS_texture4D"));

  glewInfoFunc("glTexImage4DSGIS", glTexImage4DSGIS == NULL);
  glewInfoFunc("glTexSubImage4DSGIS", glTexSubImage4DSGIS == NULL);
}

#endif /* GL_SGIS_texture4D */

#ifdef GL_SGIS_texture_border_clamp

static void _glewInfo_GL_SGIS_texture_border_clamp (void)
{
  glewPrintExt("GL_SGIS_texture_border_clamp", GLEW_SGIS_texture_border_clamp, glewIsSupported("GL_SGIS_texture_border_clamp"), glewGetExtension("GL_SGIS_texture_border_clamp"));
}

#endif /* GL_SGIS_texture_border_clamp */

#ifdef GL_SGIS_texture_edge_clamp

static void _glewInfo_GL_SGIS_texture_edge_clamp (void)
{
  glewPrintExt("GL_SGIS_texture_edge_clamp", GLEW_SGIS_texture_edge_clamp, glewIsSupported("GL_SGIS_texture_edge_clamp"), glewGetExtension("GL_SGIS_texture_edge_clamp"));
}

#endif /* GL_SGIS_texture_edge_clamp */

#ifdef GL_SGIS_texture_filter4

static void _glewInfo_GL_SGIS_texture_filter4 (void)
{
  glewPrintExt("GL_SGIS_texture_filter4", GLEW_SGIS_texture_filter4, glewIsSupported("GL_SGIS_texture_filter4"), glewGetExtension("GL_SGIS_texture_filter4"));

  glewInfoFunc("glGetTexFilterFuncSGIS", glGetTexFilterFuncSGIS == NULL);
  glewInfoFunc("glTexFilterFuncSGIS", glTexFilterFuncSGIS == NULL);
}

#endif /* GL_SGIS_texture_filter4 */

#ifdef GL_SGIS_texture_lod

static void _glewInfo_GL_SGIS_texture_lod (void)
{
  glewPrintExt("GL_SGIS_texture_lod", GLEW_SGIS_texture_lod, glewIsSupported("GL_SGIS_texture_lod"), glewGetExtension("GL_SGIS_texture_lod"));
}

#endif /* GL_SGIS_texture_lod */

#ifdef GL_SGIS_texture_select

static void _glewInfo_GL_SGIS_texture_select (void)
{
  glewPrintExt("GL_SGIS_texture_select", GLEW_SGIS_texture_select, glewIsSupported("GL_SGIS_texture_select"), glewGetExtension("GL_SGIS_texture_select"));
}

#endif /* GL_SGIS_texture_select */

#ifdef GL_SGIX_async

static void _glewInfo_GL_SGIX_async (void)
{
  glewPrintExt("GL_SGIX_async", GLEW_SGIX_async, glewIsSupported("GL_SGIX_async"), glewGetExtension("GL_SGIX_async"));

  glewInfoFunc("glAsyncMarkerSGIX", glAsyncMarkerSGIX == NULL);
  glewInfoFunc("glDeleteAsyncMarkersSGIX", glDeleteAsyncMarkersSGIX == NULL);
  glewInfoFunc("glFinishAsyncSGIX", glFinishAsyncSGIX == NULL);
  glewInfoFunc("glGenAsyncMarkersSGIX", glGenAsyncMarkersSGIX == NULL);
  glewInfoFunc("glIsAsyncMarkerSGIX", glIsAsyncMarkerSGIX == NULL);
  glewInfoFunc("glPollAsyncSGIX", glPollAsyncSGIX == NULL);
}

#endif /* GL_SGIX_async */

#ifdef GL_SGIX_async_histogram

static void _glewInfo_GL_SGIX_async_histogram (void)
{
  glewPrintExt("GL_SGIX_async_histogram", GLEW_SGIX_async_histogram, glewIsSupported("GL_SGIX_async_histogram"), glewGetExtension("GL_SGIX_async_histogram"));
}

#endif /* GL_SGIX_async_histogram */

#ifdef GL_SGIX_async_pixel

static void _glewInfo_GL_SGIX_async_pixel (void)
{
  glewPrintExt("GL_SGIX_async_pixel", GLEW_SGIX_async_pixel, glewIsSupported("GL_SGIX_async_pixel"), glewGetExtension("GL_SGIX_async_pixel"));
}

#endif /* GL_SGIX_async_pixel */

#ifdef GL_SGIX_bali_g_instruments

static void _glewInfo_GL_SGIX_bali_g_instruments (void)
{
  glewPrintExt("GL_SGIX_bali_g_instruments", GLEW_SGIX_bali_g_instruments, glewIsSupported("GL_SGIX_bali_g_instruments"), glewGetExtension("GL_SGIX_bali_g_instruments"));
}

#endif /* GL_SGIX_bali_g_instruments */

#ifdef GL_SGIX_bali_r_instruments

static void _glewInfo_GL_SGIX_bali_r_instruments (void)
{
  glewPrintExt("GL_SGIX_bali_r_instruments", GLEW_SGIX_bali_r_instruments, glewIsSupported("GL_SGIX_bali_r_instruments"), glewGetExtension("GL_SGIX_bali_r_instruments"));
}

#endif /* GL_SGIX_bali_r_instruments */

#ifdef GL_SGIX_bali_timer_instruments

static void _glewInfo_GL_SGIX_bali_timer_instruments (void)
{
  glewPrintExt("GL_SGIX_bali_timer_instruments", GLEW_SGIX_bali_timer_instruments, glewIsSupported("GL_SGIX_bali_timer_instruments"), glewGetExtension("GL_SGIX_bali_timer_instruments"));
}

#endif /* GL_SGIX_bali_timer_instruments */

#ifdef GL_SGIX_blend_alpha_minmax

static void _glewInfo_GL_SGIX_blend_alpha_minmax (void)
{
  glewPrintExt("GL_SGIX_blend_alpha_minmax", GLEW_SGIX_blend_alpha_minmax, glewIsSupported("GL_SGIX_blend_alpha_minmax"), glewGetExtension("GL_SGIX_blend_alpha_minmax"));
}

#endif /* GL_SGIX_blend_alpha_minmax */

#ifdef GL_SGIX_blend_cadd

static void _glewInfo_GL_SGIX_blend_cadd (void)
{
  glewPrintExt("GL_SGIX_blend_cadd", GLEW_SGIX_blend_cadd, glewIsSupported("GL_SGIX_blend_cadd"), glewGetExtension("GL_SGIX_blend_cadd"));
}

#endif /* GL_SGIX_blend_cadd */

#ifdef GL_SGIX_blend_cmultiply

static void _glewInfo_GL_SGIX_blend_cmultiply (void)
{
  glewPrintExt("GL_SGIX_blend_cmultiply", GLEW_SGIX_blend_cmultiply, glewIsSupported("GL_SGIX_blend_cmultiply"), glewGetExtension("GL_SGIX_blend_cmultiply"));
}

#endif /* GL_SGIX_blend_cmultiply */

#ifdef GL_SGIX_calligraphic_fragment

static void _glewInfo_GL_SGIX_calligraphic_fragment (void)
{
  glewPrintExt("GL_SGIX_calligraphic_fragment", GLEW_SGIX_calligraphic_fragment, glewIsSupported("GL_SGIX_calligraphic_fragment"), glewGetExtension("GL_SGIX_calligraphic_fragment"));
}

#endif /* GL_SGIX_calligraphic_fragment */

#ifdef GL_SGIX_clipmap

static void _glewInfo_GL_SGIX_clipmap (void)
{
  glewPrintExt("GL_SGIX_clipmap", GLEW_SGIX_clipmap, glewIsSupported("GL_SGIX_clipmap"), glewGetExtension("GL_SGIX_clipmap"));
}

#endif /* GL_SGIX_clipmap */

#ifdef GL_SGIX_color_matrix_accuracy

static void _glewInfo_GL_SGIX_color_matrix_accuracy (void)
{
  glewPrintExt("GL_SGIX_color_matrix_accuracy", GLEW_SGIX_color_matrix_accuracy, glewIsSupported("GL_SGIX_color_matrix_accuracy"), glewGetExtension("GL_SGIX_color_matrix_accuracy"));
}

#endif /* GL_SGIX_color_matrix_accuracy */

#ifdef GL_SGIX_color_table_index_mode

static void _glewInfo_GL_SGIX_color_table_index_mode (void)
{
  glewPrintExt("GL_SGIX_color_table_index_mode", GLEW_SGIX_color_table_index_mode, glewIsSupported("GL_SGIX_color_table_index_mode"), glewGetExtension("GL_SGIX_color_table_index_mode"));
}

#endif /* GL_SGIX_color_table_index_mode */

#ifdef GL_SGIX_complex_polar

static void _glewInfo_GL_SGIX_complex_polar (void)
{
  glewPrintExt("GL_SGIX_complex_polar", GLEW_SGIX_complex_polar, glewIsSupported("GL_SGIX_complex_polar"), glewGetExtension("GL_SGIX_complex_polar"));
}

#endif /* GL_SGIX_complex_polar */

#ifdef GL_SGIX_convolution_accuracy

static void _glewInfo_GL_SGIX_convolution_accuracy (void)
{
  glewPrintExt("GL_SGIX_convolution_accuracy", GLEW_SGIX_convolution_accuracy, glewIsSupported("GL_SGIX_convolution_accuracy"), glewGetExtension("GL_SGIX_convolution_accuracy"));
}

#endif /* GL_SGIX_convolution_accuracy */

#ifdef GL_SGIX_cube_map

static void _glewInfo_GL_SGIX_cube_map (void)
{
  glewPrintExt("GL_SGIX_cube_map", GLEW_SGIX_cube_map, glewIsSupported("GL_SGIX_cube_map"), glewGetExtension("GL_SGIX_cube_map"));
}

#endif /* GL_SGIX_cube_map */

#ifdef GL_SGIX_cylinder_texgen

static void _glewInfo_GL_SGIX_cylinder_texgen (void)
{
  glewPrintExt("GL_SGIX_cylinder_texgen", GLEW_SGIX_cylinder_texgen, glewIsSupported("GL_SGIX_cylinder_texgen"), glewGetExtension("GL_SGIX_cylinder_texgen"));
}

#endif /* GL_SGIX_cylinder_texgen */

#ifdef GL_SGIX_datapipe

static void _glewInfo_GL_SGIX_datapipe (void)
{
  glewPrintExt("GL_SGIX_datapipe", GLEW_SGIX_datapipe, glewIsSupported("GL_SGIX_datapipe"), glewGetExtension("GL_SGIX_datapipe"));

  glewInfoFunc("glAddressSpace", glAddressSpace == NULL);
  glewInfoFunc("glDataPipe", glDataPipe == NULL);
}

#endif /* GL_SGIX_datapipe */

#ifdef GL_SGIX_decimation

static void _glewInfo_GL_SGIX_decimation (void)
{
  glewPrintExt("GL_SGIX_decimation", GLEW_SGIX_decimation, glewIsSupported("GL_SGIX_decimation"), glewGetExtension("GL_SGIX_decimation"));
}

#endif /* GL_SGIX_decimation */

#ifdef GL_SGIX_depth_pass_instrument

static void _glewInfo_GL_SGIX_depth_pass_instrument (void)
{
  glewPrintExt("GL_SGIX_depth_pass_instrument", GLEW_SGIX_depth_pass_instrument, glewIsSupported("GL_SGIX_depth_pass_instrument"), glewGetExtension("GL_SGIX_depth_pass_instrument"));
}

#endif /* GL_SGIX_depth_pass_instrument */

#ifdef GL_SGIX_depth_texture

static void _glewInfo_GL_SGIX_depth_texture (void)
{
  glewPrintExt("GL_SGIX_depth_texture", GLEW_SGIX_depth_texture, glewIsSupported("GL_SGIX_depth_texture"), glewGetExtension("GL_SGIX_depth_texture"));
}

#endif /* GL_SGIX_depth_texture */

#ifdef GL_SGIX_dvc

static void _glewInfo_GL_SGIX_dvc (void)
{
  glewPrintExt("GL_SGIX_dvc", GLEW_SGIX_dvc, glewIsSupported("GL_SGIX_dvc"), glewGetExtension("GL_SGIX_dvc"));
}

#endif /* GL_SGIX_dvc */

#ifdef GL_SGIX_flush_raster

static void _glewInfo_GL_SGIX_flush_raster (void)
{
  glewPrintExt("GL_SGIX_flush_raster", GLEW_SGIX_flush_raster, glewIsSupported("GL_SGIX_flush_raster"), glewGetExtension("GL_SGIX_flush_raster"));

  glewInfoFunc("glFlushRasterSGIX", glFlushRasterSGIX == NULL);
}

#endif /* GL_SGIX_flush_raster */

#ifdef GL_SGIX_fog_blend

static void _glewInfo_GL_SGIX_fog_blend (void)
{
  glewPrintExt("GL_SGIX_fog_blend", GLEW_SGIX_fog_blend, glewIsSupported("GL_SGIX_fog_blend"), glewGetExtension("GL_SGIX_fog_blend"));
}

#endif /* GL_SGIX_fog_blend */

#ifdef GL_SGIX_fog_factor_to_alpha

static void _glewInfo_GL_SGIX_fog_factor_to_alpha (void)
{
  glewPrintExt("GL_SGIX_fog_factor_to_alpha", GLEW_SGIX_fog_factor_to_alpha, glewIsSupported("GL_SGIX_fog_factor_to_alpha"), glewGetExtension("GL_SGIX_fog_factor_to_alpha"));
}

#endif /* GL_SGIX_fog_factor_to_alpha */

#ifdef GL_SGIX_fog_layers

static void _glewInfo_GL_SGIX_fog_layers (void)
{
  glewPrintExt("GL_SGIX_fog_layers", GLEW_SGIX_fog_layers, glewIsSupported("GL_SGIX_fog_layers"), glewGetExtension("GL_SGIX_fog_layers"));

  glewInfoFunc("glFogLayersSGIX", glFogLayersSGIX == NULL);
  glewInfoFunc("glGetFogLayersSGIX", glGetFogLayersSGIX == NULL);
}

#endif /* GL_SGIX_fog_layers */

#ifdef GL_SGIX_fog_offset

static void _glewInfo_GL_SGIX_fog_offset (void)
{
  glewPrintExt("GL_SGIX_fog_offset", GLEW_SGIX_fog_offset, glewIsSupported("GL_SGIX_fog_offset"), glewGetExtension("GL_SGIX_fog_offset"));
}

#endif /* GL_SGIX_fog_offset */

#ifdef GL_SGIX_fog_patchy

static void _glewInfo_GL_SGIX_fog_patchy (void)
{
  glewPrintExt("GL_SGIX_fog_patchy", GLEW_SGIX_fog_patchy, glewIsSupported("GL_SGIX_fog_patchy"), glewGetExtension("GL_SGIX_fog_patchy"));
}

#endif /* GL_SGIX_fog_patchy */

#ifdef GL_SGIX_fog_scale

static void _glewInfo_GL_SGIX_fog_scale (void)
{
  glewPrintExt("GL_SGIX_fog_scale", GLEW_SGIX_fog_scale, glewIsSupported("GL_SGIX_fog_scale"), glewGetExtension("GL_SGIX_fog_scale"));
}

#endif /* GL_SGIX_fog_scale */

#ifdef GL_SGIX_fog_texture

static void _glewInfo_GL_SGIX_fog_texture (void)
{
  glewPrintExt("GL_SGIX_fog_texture", GLEW_SGIX_fog_texture, glewIsSupported("GL_SGIX_fog_texture"), glewGetExtension("GL_SGIX_fog_texture"));

  glewInfoFunc("glTextureFogSGIX", glTextureFogSGIX == NULL);
}

#endif /* GL_SGIX_fog_texture */

#ifdef GL_SGIX_fragment_lighting_space

static void _glewInfo_GL_SGIX_fragment_lighting_space (void)
{
  glewPrintExt("GL_SGIX_fragment_lighting_space", GLEW_SGIX_fragment_lighting_space, glewIsSupported("GL_SGIX_fragment_lighting_space"), glewGetExtension("GL_SGIX_fragment_lighting_space"));
}

#endif /* GL_SGIX_fragment_lighting_space */

#ifdef GL_SGIX_fragment_specular_lighting

static void _glewInfo_GL_SGIX_fragment_specular_lighting (void)
{
  glewPrintExt("GL_SGIX_fragment_specular_lighting", GLEW_SGIX_fragment_specular_lighting, glewIsSupported("GL_SGIX_fragment_specular_lighting"), glewGetExtension("GL_SGIX_fragment_specular_lighting"));

  glewInfoFunc("glFragmentColorMaterialSGIX", glFragmentColorMaterialSGIX == NULL);
  glewInfoFunc("glFragmentLightModelfSGIX", glFragmentLightModelfSGIX == NULL);
  glewInfoFunc("glFragmentLightModelfvSGIX", glFragmentLightModelfvSGIX == NULL);
  glewInfoFunc("glFragmentLightModeliSGIX", glFragmentLightModeliSGIX == NULL);
  glewInfoFunc("glFragmentLightModelivSGIX", glFragmentLightModelivSGIX == NULL);
  glewInfoFunc("glFragmentLightfSGIX", glFragmentLightfSGIX == NULL);
  glewInfoFunc("glFragmentLightfvSGIX", glFragmentLightfvSGIX == NULL);
  glewInfoFunc("glFragmentLightiSGIX", glFragmentLightiSGIX == NULL);
  glewInfoFunc("glFragmentLightivSGIX", glFragmentLightivSGIX == NULL);
  glewInfoFunc("glFragmentMaterialfSGIX", glFragmentMaterialfSGIX == NULL);
  glewInfoFunc("glFragmentMaterialfvSGIX", glFragmentMaterialfvSGIX == NULL);
  glewInfoFunc("glFragmentMaterialiSGIX", glFragmentMaterialiSGIX == NULL);
  glewInfoFunc("glFragmentMaterialivSGIX", glFragmentMaterialivSGIX == NULL);
  glewInfoFunc("glGetFragmentLightfvSGIX", glGetFragmentLightfvSGIX == NULL);
  glewInfoFunc("glGetFragmentLightivSGIX", glGetFragmentLightivSGIX == NULL);
  glewInfoFunc("glGetFragmentMaterialfvSGIX", glGetFragmentMaterialfvSGIX == NULL);
  glewInfoFunc("glGetFragmentMaterialivSGIX", glGetFragmentMaterialivSGIX == NULL);
}

#endif /* GL_SGIX_fragment_specular_lighting */

#ifdef GL_SGIX_fragments_instrument

static void _glewInfo_GL_SGIX_fragments_instrument (void)
{
  glewPrintExt("GL_SGIX_fragments_instrument", GLEW_SGIX_fragments_instrument, glewIsSupported("GL_SGIX_fragments_instrument"), glewGetExtension("GL_SGIX_fragments_instrument"));
}

#endif /* GL_SGIX_fragments_instrument */

#ifdef GL_SGIX_framezoom

static void _glewInfo_GL_SGIX_framezoom (void)
{
  glewPrintExt("GL_SGIX_framezoom", GLEW_SGIX_framezoom, glewIsSupported("GL_SGIX_framezoom"), glewGetExtension("GL_SGIX_framezoom"));

  glewInfoFunc("glFrameZoomSGIX", glFrameZoomSGIX == NULL);
}

#endif /* GL_SGIX_framezoom */

#ifdef GL_SGIX_icc_texture

static void _glewInfo_GL_SGIX_icc_texture (void)
{
  glewPrintExt("GL_SGIX_icc_texture", GLEW_SGIX_icc_texture, glewIsSupported("GL_SGIX_icc_texture"), glewGetExtension("GL_SGIX_icc_texture"));
}

#endif /* GL_SGIX_icc_texture */

#ifdef GL_SGIX_igloo_interface

static void _glewInfo_GL_SGIX_igloo_interface (void)
{
  glewPrintExt("GL_SGIX_igloo_interface", GLEW_SGIX_igloo_interface, glewIsSupported("GL_SGIX_igloo_interface"), glewGetExtension("GL_SGIX_igloo_interface"));

  glewInfoFunc("glIglooInterfaceSGIX", glIglooInterfaceSGIX == NULL);
}

#endif /* GL_SGIX_igloo_interface */

#ifdef GL_SGIX_image_compression

static void _glewInfo_GL_SGIX_image_compression (void)
{
  glewPrintExt("GL_SGIX_image_compression", GLEW_SGIX_image_compression, glewIsSupported("GL_SGIX_image_compression"), glewGetExtension("GL_SGIX_image_compression"));
}

#endif /* GL_SGIX_image_compression */

#ifdef GL_SGIX_impact_pixel_texture

static void _glewInfo_GL_SGIX_impact_pixel_texture (void)
{
  glewPrintExt("GL_SGIX_impact_pixel_texture", GLEW_SGIX_impact_pixel_texture, glewIsSupported("GL_SGIX_impact_pixel_texture"), glewGetExtension("GL_SGIX_impact_pixel_texture"));
}

#endif /* GL_SGIX_impact_pixel_texture */

#ifdef GL_SGIX_instrument_error

static void _glewInfo_GL_SGIX_instrument_error (void)
{
  glewPrintExt("GL_SGIX_instrument_error", GLEW_SGIX_instrument_error, glewIsSupported("GL_SGIX_instrument_error"), glewGetExtension("GL_SGIX_instrument_error"));
}

#endif /* GL_SGIX_instrument_error */

#ifdef GL_SGIX_interlace

static void _glewInfo_GL_SGIX_interlace (void)
{
  glewPrintExt("GL_SGIX_interlace", GLEW_SGIX_interlace, glewIsSupported("GL_SGIX_interlace"), glewGetExtension("GL_SGIX_interlace"));
}

#endif /* GL_SGIX_interlace */

#ifdef GL_SGIX_ir_instrument1

static void _glewInfo_GL_SGIX_ir_instrument1 (void)
{
  glewPrintExt("GL_SGIX_ir_instrument1", GLEW_SGIX_ir_instrument1, glewIsSupported("GL_SGIX_ir_instrument1"), glewGetExtension("GL_SGIX_ir_instrument1"));
}

#endif /* GL_SGIX_ir_instrument1 */

#ifdef GL_SGIX_line_quality_hint

static void _glewInfo_GL_SGIX_line_quality_hint (void)
{
  glewPrintExt("GL_SGIX_line_quality_hint", GLEW_SGIX_line_quality_hint, glewIsSupported("GL_SGIX_line_quality_hint"), glewGetExtension("GL_SGIX_line_quality_hint"));
}

#endif /* GL_SGIX_line_quality_hint */

#ifdef GL_SGIX_list_priority

static void _glewInfo_GL_SGIX_list_priority (void)
{
  glewPrintExt("GL_SGIX_list_priority", GLEW_SGIX_list_priority, glewIsSupported("GL_SGIX_list_priority"), glewGetExtension("GL_SGIX_list_priority"));
}

#endif /* GL_SGIX_list_priority */

#ifdef GL_SGIX_mpeg1

static void _glewInfo_GL_SGIX_mpeg1 (void)
{
  glewPrintExt("GL_SGIX_mpeg1", GLEW_SGIX_mpeg1, glewIsSupported("GL_SGIX_mpeg1"), glewGetExtension("GL_SGIX_mpeg1"));

  glewInfoFunc("glAllocMPEGPredictorsSGIX", glAllocMPEGPredictorsSGIX == NULL);
  glewInfoFunc("glDeleteMPEGPredictorsSGIX", glDeleteMPEGPredictorsSGIX == NULL);
  glewInfoFunc("glGenMPEGPredictorsSGIX", glGenMPEGPredictorsSGIX == NULL);
  glewInfoFunc("glGetMPEGParameterfvSGIX", glGetMPEGParameterfvSGIX == NULL);
  glewInfoFunc("glGetMPEGParameterivSGIX", glGetMPEGParameterivSGIX == NULL);
  glewInfoFunc("glGetMPEGPredictorSGIX", glGetMPEGPredictorSGIX == NULL);
  glewInfoFunc("glGetMPEGQuantTableubv", glGetMPEGQuantTableubv == NULL);
  glewInfoFunc("glIsMPEGPredictorSGIX", glIsMPEGPredictorSGIX == NULL);
  glewInfoFunc("glMPEGPredictorSGIX", glMPEGPredictorSGIX == NULL);
  glewInfoFunc("glMPEGQuantTableubv", glMPEGQuantTableubv == NULL);
  glewInfoFunc("glSwapMPEGPredictorsSGIX", glSwapMPEGPredictorsSGIX == NULL);
}

#endif /* GL_SGIX_mpeg1 */

#ifdef GL_SGIX_mpeg2

static void _glewInfo_GL_SGIX_mpeg2 (void)
{
  glewPrintExt("GL_SGIX_mpeg2", GLEW_SGIX_mpeg2, glewIsSupported("GL_SGIX_mpeg2"), glewGetExtension("GL_SGIX_mpeg2"));
}

#endif /* GL_SGIX_mpeg2 */

#ifdef GL_SGIX_nonlinear_lighting_pervertex

static void _glewInfo_GL_SGIX_nonlinear_lighting_pervertex (void)
{
  glewPrintExt("GL_SGIX_nonlinear_lighting_pervertex", GLEW_SGIX_nonlinear_lighting_pervertex, glewIsSupported("GL_SGIX_nonlinear_lighting_pervertex"), glewGetExtension("GL_SGIX_nonlinear_lighting_pervertex"));

  glewInfoFunc("glGetNonlinLightfvSGIX", glGetNonlinLightfvSGIX == NULL);
  glewInfoFunc("glGetNonlinMaterialfvSGIX", glGetNonlinMaterialfvSGIX == NULL);
  glewInfoFunc("glNonlinLightfvSGIX", glNonlinLightfvSGIX == NULL);
  glewInfoFunc("glNonlinMaterialfvSGIX", glNonlinMaterialfvSGIX == NULL);
}

#endif /* GL_SGIX_nonlinear_lighting_pervertex */

#ifdef GL_SGIX_nurbs_eval

static void _glewInfo_GL_SGIX_nurbs_eval (void)
{
  glewPrintExt("GL_SGIX_nurbs_eval", GLEW_SGIX_nurbs_eval, glewIsSupported("GL_SGIX_nurbs_eval"), glewGetExtension("GL_SGIX_nurbs_eval"));
}

#endif /* GL_SGIX_nurbs_eval */

#ifdef GL_SGIX_occlusion_instrument

static void _glewInfo_GL_SGIX_occlusion_instrument (void)
{
  glewPrintExt("GL_SGIX_occlusion_instrument", GLEW_SGIX_occlusion_instrument, glewIsSupported("GL_SGIX_occlusion_instrument"), glewGetExtension("GL_SGIX_occlusion_instrument"));
}

#endif /* GL_SGIX_occlusion_instrument */

#ifdef GL_SGIX_packed_6bytes

static void _glewInfo_GL_SGIX_packed_6bytes (void)
{
  glewPrintExt("GL_SGIX_packed_6bytes", GLEW_SGIX_packed_6bytes, glewIsSupported("GL_SGIX_packed_6bytes"), glewGetExtension("GL_SGIX_packed_6bytes"));
}

#endif /* GL_SGIX_packed_6bytes */

#ifdef GL_SGIX_pixel_texture

static void _glewInfo_GL_SGIX_pixel_texture (void)
{
  glewPrintExt("GL_SGIX_pixel_texture", GLEW_SGIX_pixel_texture, glewIsSupported("GL_SGIX_pixel_texture"), glewGetExtension("GL_SGIX_pixel_texture"));

  glewInfoFunc("glPixelTexGenSGIX", glPixelTexGenSGIX == NULL);
}

#endif /* GL_SGIX_pixel_texture */

#ifdef GL_SGIX_pixel_texture_bits

static void _glewInfo_GL_SGIX_pixel_texture_bits (void)
{
  glewPrintExt("GL_SGIX_pixel_texture_bits", GLEW_SGIX_pixel_texture_bits, glewIsSupported("GL_SGIX_pixel_texture_bits"), glewGetExtension("GL_SGIX_pixel_texture_bits"));
}

#endif /* GL_SGIX_pixel_texture_bits */

#ifdef GL_SGIX_pixel_texture_lod

static void _glewInfo_GL_SGIX_pixel_texture_lod (void)
{
  glewPrintExt("GL_SGIX_pixel_texture_lod", GLEW_SGIX_pixel_texture_lod, glewIsSupported("GL_SGIX_pixel_texture_lod"), glewGetExtension("GL_SGIX_pixel_texture_lod"));
}

#endif /* GL_SGIX_pixel_texture_lod */

#ifdef GL_SGIX_pixel_tiles

static void _glewInfo_GL_SGIX_pixel_tiles (void)
{
  glewPrintExt("GL_SGIX_pixel_tiles", GLEW_SGIX_pixel_tiles, glewIsSupported("GL_SGIX_pixel_tiles"), glewGetExtension("GL_SGIX_pixel_tiles"));
}

#endif /* GL_SGIX_pixel_tiles */

#ifdef GL_SGIX_polynomial_ffd

static void _glewInfo_GL_SGIX_polynomial_ffd (void)
{
  glewPrintExt("GL_SGIX_polynomial_ffd", GLEW_SGIX_polynomial_ffd, glewIsSupported("GL_SGIX_polynomial_ffd"), glewGetExtension("GL_SGIX_polynomial_ffd"));

  glewInfoFunc("glDeformSGIX", glDeformSGIX == NULL);
  glewInfoFunc("glLoadIdentityDeformationMapSGIX", glLoadIdentityDeformationMapSGIX == NULL);
}

#endif /* GL_SGIX_polynomial_ffd */

#ifdef GL_SGIX_quad_mesh

static void _glewInfo_GL_SGIX_quad_mesh (void)
{
  glewPrintExt("GL_SGIX_quad_mesh", GLEW_SGIX_quad_mesh, glewIsSupported("GL_SGIX_quad_mesh"), glewGetExtension("GL_SGIX_quad_mesh"));

  glewInfoFunc("glMeshBreadthSGIX", glMeshBreadthSGIX == NULL);
  glewInfoFunc("glMeshStrideSGIX", glMeshStrideSGIX == NULL);
}

#endif /* GL_SGIX_quad_mesh */

#ifdef GL_SGIX_reference_plane

static void _glewInfo_GL_SGIX_reference_plane (void)
{
  glewPrintExt("GL_SGIX_reference_plane", GLEW_SGIX_reference_plane, glewIsSupported("GL_SGIX_reference_plane"), glewGetExtension("GL_SGIX_reference_plane"));

  glewInfoFunc("glReferencePlaneSGIX", glReferencePlaneSGIX == NULL);
}

#endif /* GL_SGIX_reference_plane */

#ifdef GL_SGIX_resample

static void _glewInfo_GL_SGIX_resample (void)
{
  glewPrintExt("GL_SGIX_resample", GLEW_SGIX_resample, glewIsSupported("GL_SGIX_resample"), glewGetExtension("GL_SGIX_resample"));
}

#endif /* GL_SGIX_resample */

#ifdef GL_SGIX_scalebias_hint

static void _glewInfo_GL_SGIX_scalebias_hint (void)
{
  glewPrintExt("GL_SGIX_scalebias_hint", GLEW_SGIX_scalebias_hint, glewIsSupported("GL_SGIX_scalebias_hint"), glewGetExtension("GL_SGIX_scalebias_hint"));
}

#endif /* GL_SGIX_scalebias_hint */

#ifdef GL_SGIX_shadow

static void _glewInfo_GL_SGIX_shadow (void)
{
  glewPrintExt("GL_SGIX_shadow", GLEW_SGIX_shadow, glewIsSupported("GL_SGIX_shadow"), glewGetExtension("GL_SGIX_shadow"));
}

#endif /* GL_SGIX_shadow */

#ifdef GL_SGIX_shadow_ambient

static void _glewInfo_GL_SGIX_shadow_ambient (void)
{
  glewPrintExt("GL_SGIX_shadow_ambient", GLEW_SGIX_shadow_ambient, glewIsSupported("GL_SGIX_shadow_ambient"), glewGetExtension("GL_SGIX_shadow_ambient"));
}

#endif /* GL_SGIX_shadow_ambient */

#ifdef GL_SGIX_slim

static void _glewInfo_GL_SGIX_slim (void)
{
  glewPrintExt("GL_SGIX_slim", GLEW_SGIX_slim, glewIsSupported("GL_SGIX_slim"), glewGetExtension("GL_SGIX_slim"));
}

#endif /* GL_SGIX_slim */

#ifdef GL_SGIX_spotlight_cutoff

static void _glewInfo_GL_SGIX_spotlight_cutoff (void)
{
  glewPrintExt("GL_SGIX_spotlight_cutoff", GLEW_SGIX_spotlight_cutoff, glewIsSupported("GL_SGIX_spotlight_cutoff"), glewGetExtension("GL_SGIX_spotlight_cutoff"));
}

#endif /* GL_SGIX_spotlight_cutoff */

#ifdef GL_SGIX_sprite

static void _glewInfo_GL_SGIX_sprite (void)
{
  glewPrintExt("GL_SGIX_sprite", GLEW_SGIX_sprite, glewIsSupported("GL_SGIX_sprite"), glewGetExtension("GL_SGIX_sprite"));

  glewInfoFunc("glSpriteParameterfSGIX", glSpriteParameterfSGIX == NULL);
  glewInfoFunc("glSpriteParameterfvSGIX", glSpriteParameterfvSGIX == NULL);
  glewInfoFunc("glSpriteParameteriSGIX", glSpriteParameteriSGIX == NULL);
  glewInfoFunc("glSpriteParameterivSGIX", glSpriteParameterivSGIX == NULL);
}

#endif /* GL_SGIX_sprite */

#ifdef GL_SGIX_subdiv_patch

static void _glewInfo_GL_SGIX_subdiv_patch (void)
{
  glewPrintExt("GL_SGIX_subdiv_patch", GLEW_SGIX_subdiv_patch, glewIsSupported("GL_SGIX_subdiv_patch"), glewGetExtension("GL_SGIX_subdiv_patch"));
}

#endif /* GL_SGIX_subdiv_patch */

#ifdef GL_SGIX_subsample

static void _glewInfo_GL_SGIX_subsample (void)
{
  glewPrintExt("GL_SGIX_subsample", GLEW_SGIX_subsample, glewIsSupported("GL_SGIX_subsample"), glewGetExtension("GL_SGIX_subsample"));
}

#endif /* GL_SGIX_subsample */

#ifdef GL_SGIX_tag_sample_buffer

static void _glewInfo_GL_SGIX_tag_sample_buffer (void)
{
  glewPrintExt("GL_SGIX_tag_sample_buffer", GLEW_SGIX_tag_sample_buffer, glewIsSupported("GL_SGIX_tag_sample_buffer"), glewGetExtension("GL_SGIX_tag_sample_buffer"));

  glewInfoFunc("glTagSampleBufferSGIX", glTagSampleBufferSGIX == NULL);
}

#endif /* GL_SGIX_tag_sample_buffer */

#ifdef GL_SGIX_texture_add_env

static void _glewInfo_GL_SGIX_texture_add_env (void)
{
  glewPrintExt("GL_SGIX_texture_add_env", GLEW_SGIX_texture_add_env, glewIsSupported("GL_SGIX_texture_add_env"), glewGetExtension("GL_SGIX_texture_add_env"));
}

#endif /* GL_SGIX_texture_add_env */

#ifdef GL_SGIX_texture_coordinate_clamp

static void _glewInfo_GL_SGIX_texture_coordinate_clamp (void)
{
  glewPrintExt("GL_SGIX_texture_coordinate_clamp", GLEW_SGIX_texture_coordinate_clamp, glewIsSupported("GL_SGIX_texture_coordinate_clamp"), glewGetExtension("GL_SGIX_texture_coordinate_clamp"));
}

#endif /* GL_SGIX_texture_coordinate_clamp */

#ifdef GL_SGIX_texture_lod_bias

static void _glewInfo_GL_SGIX_texture_lod_bias (void)
{
  glewPrintExt("GL_SGIX_texture_lod_bias", GLEW_SGIX_texture_lod_bias, glewIsSupported("GL_SGIX_texture_lod_bias"), glewGetExtension("GL_SGIX_texture_lod_bias"));
}

#endif /* GL_SGIX_texture_lod_bias */

#ifdef GL_SGIX_texture_mipmap_anisotropic

static void _glewInfo_GL_SGIX_texture_mipmap_anisotropic (void)
{
  glewPrintExt("GL_SGIX_texture_mipmap_anisotropic", GLEW_SGIX_texture_mipmap_anisotropic, glewIsSupported("GL_SGIX_texture_mipmap_anisotropic"), glewGetExtension("GL_SGIX_texture_mipmap_anisotropic"));
}

#endif /* GL_SGIX_texture_mipmap_anisotropic */

#ifdef GL_SGIX_texture_multi_buffer

static void _glewInfo_GL_SGIX_texture_multi_buffer (void)
{
  glewPrintExt("GL_SGIX_texture_multi_buffer", GLEW_SGIX_texture_multi_buffer, glewIsSupported("GL_SGIX_texture_multi_buffer"), glewGetExtension("GL_SGIX_texture_multi_buffer"));
}

#endif /* GL_SGIX_texture_multi_buffer */

#ifdef GL_SGIX_texture_phase

static void _glewInfo_GL_SGIX_texture_phase (void)
{
  glewPrintExt("GL_SGIX_texture_phase", GLEW_SGIX_texture_phase, glewIsSupported("GL_SGIX_texture_phase"), glewGetExtension("GL_SGIX_texture_phase"));
}

#endif /* GL_SGIX_texture_phase */

#ifdef GL_SGIX_texture_range

static void _glewInfo_GL_SGIX_texture_range (void)
{
  glewPrintExt("GL_SGIX_texture_range", GLEW_SGIX_texture_range, glewIsSupported("GL_SGIX_texture_range"), glewGetExtension("GL_SGIX_texture_range"));
}

#endif /* GL_SGIX_texture_range */

#ifdef GL_SGIX_texture_scale_bias

static void _glewInfo_GL_SGIX_texture_scale_bias (void)
{
  glewPrintExt("GL_SGIX_texture_scale_bias", GLEW_SGIX_texture_scale_bias, glewIsSupported("GL_SGIX_texture_scale_bias"), glewGetExtension("GL_SGIX_texture_scale_bias"));
}

#endif /* GL_SGIX_texture_scale_bias */

#ifdef GL_SGIX_texture_supersample

static void _glewInfo_GL_SGIX_texture_supersample (void)
{
  glewPrintExt("GL_SGIX_texture_supersample", GLEW_SGIX_texture_supersample, glewIsSupported("GL_SGIX_texture_supersample"), glewGetExtension("GL_SGIX_texture_supersample"));
}

#endif /* GL_SGIX_texture_supersample */

#ifdef GL_SGIX_vector_ops

static void _glewInfo_GL_SGIX_vector_ops (void)
{
  glewPrintExt("GL_SGIX_vector_ops", GLEW_SGIX_vector_ops, glewIsSupported("GL_SGIX_vector_ops"), glewGetExtension("GL_SGIX_vector_ops"));

  glewInfoFunc("glGetVectorOperationSGIX", glGetVectorOperationSGIX == NULL);
  glewInfoFunc("glVectorOperationSGIX", glVectorOperationSGIX == NULL);
}

#endif /* GL_SGIX_vector_ops */

#ifdef GL_SGIX_vertex_array_object

static void _glewInfo_GL_SGIX_vertex_array_object (void)
{
  glewPrintExt("GL_SGIX_vertex_array_object", GLEW_SGIX_vertex_array_object, glewIsSupported("GL_SGIX_vertex_array_object"), glewGetExtension("GL_SGIX_vertex_array_object"));

  glewInfoFunc("glAreVertexArraysResidentSGIX", glAreVertexArraysResidentSGIX == NULL);
  glewInfoFunc("glBindVertexArraySGIX", glBindVertexArraySGIX == NULL);
  glewInfoFunc("glDeleteVertexArraysSGIX", glDeleteVertexArraysSGIX == NULL);
  glewInfoFunc("glGenVertexArraysSGIX", glGenVertexArraysSGIX == NULL);
  glewInfoFunc("glIsVertexArraySGIX", glIsVertexArraySGIX == NULL);
  glewInfoFunc("glPrioritizeVertexArraysSGIX", glPrioritizeVertexArraysSGIX == NULL);
}

#endif /* GL_SGIX_vertex_array_object */

#ifdef GL_SGIX_vertex_preclip

static void _glewInfo_GL_SGIX_vertex_preclip (void)
{
  glewPrintExt("GL_SGIX_vertex_preclip", GLEW_SGIX_vertex_preclip, glewIsSupported("GL_SGIX_vertex_preclip"), glewGetExtension("GL_SGIX_vertex_preclip"));
}

#endif /* GL_SGIX_vertex_preclip */

#ifdef GL_SGIX_vertex_preclip_hint

static void _glewInfo_GL_SGIX_vertex_preclip_hint (void)
{
  glewPrintExt("GL_SGIX_vertex_preclip_hint", GLEW_SGIX_vertex_preclip_hint, glewIsSupported("GL_SGIX_vertex_preclip_hint"), glewGetExtension("GL_SGIX_vertex_preclip_hint"));
}

#endif /* GL_SGIX_vertex_preclip_hint */

#ifdef GL_SGIX_ycrcb

static void _glewInfo_GL_SGIX_ycrcb (void)
{
  glewPrintExt("GL_SGIX_ycrcb", GLEW_SGIX_ycrcb, glewIsSupported("GL_SGIX_ycrcb"), glewGetExtension("GL_SGIX_ycrcb"));
}

#endif /* GL_SGIX_ycrcb */

#ifdef GL_SGIX_ycrcb_subsample

static void _glewInfo_GL_SGIX_ycrcb_subsample (void)
{
  glewPrintExt("GL_SGIX_ycrcb_subsample", GLEW_SGIX_ycrcb_subsample, glewIsSupported("GL_SGIX_ycrcb_subsample"), glewGetExtension("GL_SGIX_ycrcb_subsample"));
}

#endif /* GL_SGIX_ycrcb_subsample */

#ifdef GL_SGIX_ycrcba

static void _glewInfo_GL_SGIX_ycrcba (void)
{
  glewPrintExt("GL_SGIX_ycrcba", GLEW_SGIX_ycrcba, glewIsSupported("GL_SGIX_ycrcba"), glewGetExtension("GL_SGIX_ycrcba"));
}

#endif /* GL_SGIX_ycrcba */

#ifdef GL_SGI_color_matrix

static void _glewInfo_GL_SGI_color_matrix (void)
{
  glewPrintExt("GL_SGI_color_matrix", GLEW_SGI_color_matrix, glewIsSupported("GL_SGI_color_matrix"), glewGetExtension("GL_SGI_color_matrix"));
}

#endif /* GL_SGI_color_matrix */

#ifdef GL_SGI_color_table

static void _glewInfo_GL_SGI_color_table (void)
{
  glewPrintExt("GL_SGI_color_table", GLEW_SGI_color_table, glewIsSupported("GL_SGI_color_table"), glewGetExtension("GL_SGI_color_table"));

  glewInfoFunc("glColorTableParameterfvSGI", glColorTableParameterfvSGI == NULL);
  glewInfoFunc("glColorTableParameterivSGI", glColorTableParameterivSGI == NULL);
  glewInfoFunc("glColorTableSGI", glColorTableSGI == NULL);
  glewInfoFunc("glCopyColorTableSGI", glCopyColorTableSGI == NULL);
  glewInfoFunc("glGetColorTableParameterfvSGI", glGetColorTableParameterfvSGI == NULL);
  glewInfoFunc("glGetColorTableParameterivSGI", glGetColorTableParameterivSGI == NULL);
  glewInfoFunc("glGetColorTableSGI", glGetColorTableSGI == NULL);
}

#endif /* GL_SGI_color_table */

#ifdef GL_SGI_complex

static void _glewInfo_GL_SGI_complex (void)
{
  glewPrintExt("GL_SGI_complex", GLEW_SGI_complex, glewIsSupported("GL_SGI_complex"), glewGetExtension("GL_SGI_complex"));
}

#endif /* GL_SGI_complex */

#ifdef GL_SGI_complex_type

static void _glewInfo_GL_SGI_complex_type (void)
{
  glewPrintExt("GL_SGI_complex_type", GLEW_SGI_complex_type, glewIsSupported("GL_SGI_complex_type"), glewGetExtension("GL_SGI_complex_type"));
}

#endif /* GL_SGI_complex_type */

#ifdef GL_SGI_fft

static void _glewInfo_GL_SGI_fft (void)
{
  glewPrintExt("GL_SGI_fft", GLEW_SGI_fft, glewIsSupported("GL_SGI_fft"), glewGetExtension("GL_SGI_fft"));

  glewInfoFunc("glGetPixelTransformParameterfvSGI", glGetPixelTransformParameterfvSGI == NULL);
  glewInfoFunc("glGetPixelTransformParameterivSGI", glGetPixelTransformParameterivSGI == NULL);
  glewInfoFunc("glPixelTransformParameterfSGI", glPixelTransformParameterfSGI == NULL);
  glewInfoFunc("glPixelTransformParameterfvSGI", glPixelTransformParameterfvSGI == NULL);
  glewInfoFunc("glPixelTransformParameteriSGI", glPixelTransformParameteriSGI == NULL);
  glewInfoFunc("glPixelTransformParameterivSGI", glPixelTransformParameterivSGI == NULL);
  glewInfoFunc("glPixelTransformSGI", glPixelTransformSGI == NULL);
}

#endif /* GL_SGI_fft */

#ifdef GL_SGI_texture_color_table

static void _glewInfo_GL_SGI_texture_color_table (void)
{
  glewPrintExt("GL_SGI_texture_color_table", GLEW_SGI_texture_color_table, glewIsSupported("GL_SGI_texture_color_table"), glewGetExtension("GL_SGI_texture_color_table"));
}

#endif /* GL_SGI_texture_color_table */

#ifdef GL_SUNX_constant_data

static void _glewInfo_GL_SUNX_constant_data (void)
{
  glewPrintExt("GL_SUNX_constant_data", GLEW_SUNX_constant_data, glewIsSupported("GL_SUNX_constant_data"), glewGetExtension("GL_SUNX_constant_data"));

  glewInfoFunc("glFinishTextureSUNX", glFinishTextureSUNX == NULL);
}

#endif /* GL_SUNX_constant_data */

#ifdef GL_SUN_convolution_border_modes

static void _glewInfo_GL_SUN_convolution_border_modes (void)
{
  glewPrintExt("GL_SUN_convolution_border_modes", GLEW_SUN_convolution_border_modes, glewIsSupported("GL_SUN_convolution_border_modes"), glewGetExtension("GL_SUN_convolution_border_modes"));
}

#endif /* GL_SUN_convolution_border_modes */

#ifdef GL_SUN_global_alpha

static void _glewInfo_GL_SUN_global_alpha (void)
{
  glewPrintExt("GL_SUN_global_alpha", GLEW_SUN_global_alpha, glewIsSupported("GL_SUN_global_alpha"), glewGetExtension("GL_SUN_global_alpha"));

  glewInfoFunc("glGlobalAlphaFactorbSUN", glGlobalAlphaFactorbSUN == NULL);
  glewInfoFunc("glGlobalAlphaFactordSUN", glGlobalAlphaFactordSUN == NULL);
  glewInfoFunc("glGlobalAlphaFactorfSUN", glGlobalAlphaFactorfSUN == NULL);
  glewInfoFunc("glGlobalAlphaFactoriSUN", glGlobalAlphaFactoriSUN == NULL);
  glewInfoFunc("glGlobalAlphaFactorsSUN", glGlobalAlphaFactorsSUN == NULL);
  glewInfoFunc("glGlobalAlphaFactorubSUN", glGlobalAlphaFactorubSUN == NULL);
  glewInfoFunc("glGlobalAlphaFactoruiSUN", glGlobalAlphaFactoruiSUN == NULL);
  glewInfoFunc("glGlobalAlphaFactorusSUN", glGlobalAlphaFactorusSUN == NULL);
}

#endif /* GL_SUN_global_alpha */

#ifdef GL_SUN_mesh_array

static void _glewInfo_GL_SUN_mesh_array (void)
{
  glewPrintExt("GL_SUN_mesh_array", GLEW_SUN_mesh_array, glewIsSupported("GL_SUN_mesh_array"), glewGetExtension("GL_SUN_mesh_array"));
}

#endif /* GL_SUN_mesh_array */

#ifdef GL_SUN_read_video_pixels

static void _glewInfo_GL_SUN_read_video_pixels (void)
{
  glewPrintExt("GL_SUN_read_video_pixels", GLEW_SUN_read_video_pixels, glewIsSupported("GL_SUN_read_video_pixels"), glewGetExtension("GL_SUN_read_video_pixels"));

  glewInfoFunc("glReadVideoPixelsSUN", glReadVideoPixelsSUN == NULL);
}

#endif /* GL_SUN_read_video_pixels */

#ifdef GL_SUN_slice_accum

static void _glewInfo_GL_SUN_slice_accum (void)
{
  glewPrintExt("GL_SUN_slice_accum", GLEW_SUN_slice_accum, glewIsSupported("GL_SUN_slice_accum"), glewGetExtension("GL_SUN_slice_accum"));
}

#endif /* GL_SUN_slice_accum */

#ifdef GL_SUN_triangle_list

static void _glewInfo_GL_SUN_triangle_list (void)
{
  glewPrintExt("GL_SUN_triangle_list", GLEW_SUN_triangle_list, glewIsSupported("GL_SUN_triangle_list"), glewGetExtension("GL_SUN_triangle_list"));

  glewInfoFunc("glReplacementCodePointerSUN", glReplacementCodePointerSUN == NULL);
  glewInfoFunc("glReplacementCodeubSUN", glReplacementCodeubSUN == NULL);
  glewInfoFunc("glReplacementCodeubvSUN", glReplacementCodeubvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiSUN", glReplacementCodeuiSUN == NULL);
  glewInfoFunc("glReplacementCodeuivSUN", glReplacementCodeuivSUN == NULL);
  glewInfoFunc("glReplacementCodeusSUN", glReplacementCodeusSUN == NULL);
  glewInfoFunc("glReplacementCodeusvSUN", glReplacementCodeusvSUN == NULL);
}

#endif /* GL_SUN_triangle_list */

#ifdef GL_SUN_vertex

static void _glewInfo_GL_SUN_vertex (void)
{
  glewPrintExt("GL_SUN_vertex", GLEW_SUN_vertex, glewIsSupported("GL_SUN_vertex"), glewGetExtension("GL_SUN_vertex"));

  glewInfoFunc("glColor3fVertex3fSUN", glColor3fVertex3fSUN == NULL);
  glewInfoFunc("glColor3fVertex3fvSUN", glColor3fVertex3fvSUN == NULL);
  glewInfoFunc("glColor4fNormal3fVertex3fSUN", glColor4fNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glColor4fNormal3fVertex3fvSUN", glColor4fNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glColor4ubVertex2fSUN", glColor4ubVertex2fSUN == NULL);
  glewInfoFunc("glColor4ubVertex2fvSUN", glColor4ubVertex2fvSUN == NULL);
  glewInfoFunc("glColor4ubVertex3fSUN", glColor4ubVertex3fSUN == NULL);
  glewInfoFunc("glColor4ubVertex3fvSUN", glColor4ubVertex3fvSUN == NULL);
  glewInfoFunc("glNormal3fVertex3fSUN", glNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glNormal3fVertex3fvSUN", glNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiColor3fVertex3fSUN", glReplacementCodeuiColor3fVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiColor3fVertex3fvSUN", glReplacementCodeuiColor3fVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiColor4fNormal3fVertex3fSUN", glReplacementCodeuiColor4fNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiColor4fNormal3fVertex3fvSUN", glReplacementCodeuiColor4fNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiColor4ubVertex3fSUN", glReplacementCodeuiColor4ubVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiColor4ubVertex3fvSUN", glReplacementCodeuiColor4ubVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiNormal3fVertex3fSUN", glReplacementCodeuiNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiNormal3fVertex3fvSUN", glReplacementCodeuiNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN", glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN", glReplacementCodeuiTexCoord2fColor4fNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN", glReplacementCodeuiTexCoord2fNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN", glReplacementCodeuiTexCoord2fNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiTexCoord2fVertex3fSUN", glReplacementCodeuiTexCoord2fVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiTexCoord2fVertex3fvSUN", glReplacementCodeuiTexCoord2fVertex3fvSUN == NULL);
  glewInfoFunc("glReplacementCodeuiVertex3fSUN", glReplacementCodeuiVertex3fSUN == NULL);
  glewInfoFunc("glReplacementCodeuiVertex3fvSUN", glReplacementCodeuiVertex3fvSUN == NULL);
  glewInfoFunc("glTexCoord2fColor3fVertex3fSUN", glTexCoord2fColor3fVertex3fSUN == NULL);
  glewInfoFunc("glTexCoord2fColor3fVertex3fvSUN", glTexCoord2fColor3fVertex3fvSUN == NULL);
  glewInfoFunc("glTexCoord2fColor4fNormal3fVertex3fSUN", glTexCoord2fColor4fNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glTexCoord2fColor4fNormal3fVertex3fvSUN", glTexCoord2fColor4fNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glTexCoord2fColor4ubVertex3fSUN", glTexCoord2fColor4ubVertex3fSUN == NULL);
  glewInfoFunc("glTexCoord2fColor4ubVertex3fvSUN", glTexCoord2fColor4ubVertex3fvSUN == NULL);
  glewInfoFunc("glTexCoord2fNormal3fVertex3fSUN", glTexCoord2fNormal3fVertex3fSUN == NULL);
  glewInfoFunc("glTexCoord2fNormal3fVertex3fvSUN", glTexCoord2fNormal3fVertex3fvSUN == NULL);
  glewInfoFunc("glTexCoord2fVertex3fSUN", glTexCoord2fVertex3fSUN == NULL);
  glewInfoFunc("glTexCoord2fVertex3fvSUN", glTexCoord2fVertex3fvSUN == NULL);
  glewInfoFunc("glTexCoord4fColor4fNormal3fVertex4fSUN", glTexCoord4fColor4fNormal3fVertex4fSUN == NULL);
  glewInfoFunc("glTexCoord4fColor4fNormal3fVertex4fvSUN", glTexCoord4fColor4fNormal3fVertex4fvSUN == NULL);
  glewInfoFunc("glTexCoord4fVertex4fSUN", glTexCoord4fVertex4fSUN == NULL);
  glewInfoFunc("glTexCoord4fVertex4fvSUN", glTexCoord4fVertex4fvSUN == NULL);
}

#endif /* GL_SUN_vertex */

#ifdef GL_WIN_phong_shading

static void _glewInfo_GL_WIN_phong_shading (void)
{
  glewPrintExt("GL_WIN_phong_shading", GLEW_WIN_phong_shading, glewIsSupported("GL_WIN_phong_shading"), glewGetExtension("GL_WIN_phong_shading"));
}

#endif /* GL_WIN_phong_shading */

#ifdef GL_WIN_scene_markerXXX

static void _glewInfo_GL_WIN_scene_markerXXX (void)
{
  glewPrintExt("GL_WIN_scene_markerXXX", GLEW_WIN_scene_markerXXX, glewIsSupported("GL_WIN_scene_markerXXX"), glewGetExtension("GL_WIN_scene_markerXXX"));
}

#endif /* GL_WIN_scene_markerXXX */

#ifdef GL_WIN_specular_fog

static void _glewInfo_GL_WIN_specular_fog (void)
{
  glewPrintExt("GL_WIN_specular_fog", GLEW_WIN_specular_fog, glewIsSupported("GL_WIN_specular_fog"), glewGetExtension("GL_WIN_specular_fog"));
}

#endif /* GL_WIN_specular_fog */

#ifdef GL_WIN_swap_hint

static void _glewInfo_GL_WIN_swap_hint (void)
{
  glewPrintExt("GL_WIN_swap_hint", GLEW_WIN_swap_hint, glewIsSupported("GL_WIN_swap_hint"), glewGetExtension("GL_WIN_swap_hint"));

  glewInfoFunc("glAddSwapHintRectWIN", glAddSwapHintRectWIN == NULL);
}

#endif /* GL_WIN_swap_hint */

#if defined(GLEW_EGL)

#ifdef EGL_VERSION_1_0

static void _glewInfo_EGL_VERSION_1_0 (void)
{
  glewPrintExt("EGL_VERSION_1_0", EGLEW_VERSION_1_0, EGLEW_VERSION_1_0, EGLEW_VERSION_1_0);

  glewInfoFunc("eglChooseConfig", eglChooseConfig == NULL);
  glewInfoFunc("eglCopyBuffers", eglCopyBuffers == NULL);
  glewInfoFunc("eglCreateContext", eglCreateContext == NULL);
  glewInfoFunc("eglCreatePbufferSurface", eglCreatePbufferSurface == NULL);
  glewInfoFunc("eglCreatePixmapSurface", eglCreatePixmapSurface == NULL);
  glewInfoFunc("eglCreateWindowSurface", eglCreateWindowSurface == NULL);
  glewInfoFunc("eglDestroyContext", eglDestroyContext == NULL);
  glewInfoFunc("eglDestroySurface", eglDestroySurface == NULL);
  glewInfoFunc("eglGetConfigAttrib", eglGetConfigAttrib == NULL);
  glewInfoFunc("eglGetConfigs", eglGetConfigs == NULL);
  glewInfoFunc("eglGetCurrentDisplay", eglGetCurrentDisplay == NULL);
  glewInfoFunc("eglGetCurrentSurface", eglGetCurrentSurface == NULL);
  glewInfoFunc("eglGetDisplay", eglGetDisplay == NULL);
  glewInfoFunc("eglGetError", eglGetError == NULL);
  glewInfoFunc("eglInitialize", eglInitialize == NULL);
  glewInfoFunc("eglMakeCurrent", eglMakeCurrent == NULL);
  glewInfoFunc("eglQueryContext", eglQueryContext == NULL);
  glewInfoFunc("eglQueryString", eglQueryString == NULL);
  glewInfoFunc("eglQuerySurface", eglQuerySurface == NULL);
  glewInfoFunc("eglSwapBuffers", eglSwapBuffers == NULL);
  glewInfoFunc("eglTerminate", eglTerminate == NULL);
  glewInfoFunc("eglWaitGL", eglWaitGL == NULL);
  glewInfoFunc("eglWaitNative", eglWaitNative == NULL);
}

#endif /* EGL_VERSION_1_0 */

#ifdef EGL_VERSION_1_1

static void _glewInfo_EGL_VERSION_1_1 (void)
{
  glewPrintExt("EGL_VERSION_1_1", EGLEW_VERSION_1_1, EGLEW_VERSION_1_1, EGLEW_VERSION_1_1);

  glewInfoFunc("eglBindTexImage", eglBindTexImage == NULL);
  glewInfoFunc("eglReleaseTexImage", eglReleaseTexImage == NULL);
  glewInfoFunc("eglSurfaceAttrib", eglSurfaceAttrib == NULL);
  glewInfoFunc("eglSwapInterval", eglSwapInterval == NULL);
}

#endif /* EGL_VERSION_1_1 */

#ifdef EGL_VERSION_1_2

static void _glewInfo_EGL_VERSION_1_2 (void)
{
  glewPrintExt("EGL_VERSION_1_2", EGLEW_VERSION_1_2, EGLEW_VERSION_1_2, EGLEW_VERSION_1_2);

  glewInfoFunc("eglBindAPI", eglBindAPI == NULL);
  glewInfoFunc("eglCreatePbufferFromClientBuffer", eglCreatePbufferFromClientBuffer == NULL);
  glewInfoFunc("eglQueryAPI", eglQueryAPI == NULL);
  glewInfoFunc("eglReleaseThread", eglReleaseThread == NULL);
  glewInfoFunc("eglWaitClient", eglWaitClient == NULL);
}

#endif /* EGL_VERSION_1_2 */

#ifdef EGL_VERSION_1_3

static void _glewInfo_EGL_VERSION_1_3 (void)
{
  glewPrintExt("EGL_VERSION_1_3", EGLEW_VERSION_1_3, EGLEW_VERSION_1_3, EGLEW_VERSION_1_3);
}

#endif /* EGL_VERSION_1_3 */

#ifdef EGL_VERSION_1_4

static void _glewInfo_EGL_VERSION_1_4 (void)
{
  glewPrintExt("EGL_VERSION_1_4", EGLEW_VERSION_1_4, EGLEW_VERSION_1_4, EGLEW_VERSION_1_4);

  glewInfoFunc("eglGetCurrentContext", eglGetCurrentContext == NULL);
}

#endif /* EGL_VERSION_1_4 */

#ifdef EGL_VERSION_1_5

static void _glewInfo_EGL_VERSION_1_5 (void)
{
  glewPrintExt("EGL_VERSION_1_5", EGLEW_VERSION_1_5, EGLEW_VERSION_1_5, EGLEW_VERSION_1_5);

  glewInfoFunc("eglClientWaitSync", eglClientWaitSync == NULL);
  glewInfoFunc("eglCreateImage", eglCreateImage == NULL);
  glewInfoFunc("eglCreatePlatformPixmapSurface", eglCreatePlatformPixmapSurface == NULL);
  glewInfoFunc("eglCreatePlatformWindowSurface", eglCreatePlatformWindowSurface == NULL);
  glewInfoFunc("eglCreateSync", eglCreateSync == NULL);
  glewInfoFunc("eglDestroyImage", eglDestroyImage == NULL);
  glewInfoFunc("eglDestroySync", eglDestroySync == NULL);
  glewInfoFunc("eglGetPlatformDisplay", eglGetPlatformDisplay == NULL);
  glewInfoFunc("eglGetSyncAttrib", eglGetSyncAttrib == NULL);
  glewInfoFunc("eglWaitSync", eglWaitSync == NULL);
}

#endif /* EGL_VERSION_1_5 */

#ifdef EGL_ANDROID_blob_cache

static void _glewInfo_EGL_ANDROID_blob_cache (void)
{
  glewPrintExt("EGL_ANDROID_blob_cache", EGLEW_ANDROID_blob_cache, eglewIsSupported("EGL_ANDROID_blob_cache"), eglewGetExtension("EGL_ANDROID_blob_cache"));

  glewInfoFunc("eglSetBlobCacheFuncsANDROID", eglSetBlobCacheFuncsANDROID == NULL);
}

#endif /* EGL_ANDROID_blob_cache */

#ifdef EGL_ANDROID_create_native_client_buffer

static void _glewInfo_EGL_ANDROID_create_native_client_buffer (void)
{
  glewPrintExt("EGL_ANDROID_create_native_client_buffer", EGLEW_ANDROID_create_native_client_buffer, eglewIsSupported("EGL_ANDROID_create_native_client_buffer"), eglewGetExtension("EGL_ANDROID_create_native_client_buffer"));

  glewInfoFunc("eglCreateNativeClientBufferANDROID", eglCreateNativeClientBufferANDROID == NULL);
}

#endif /* EGL_ANDROID_create_native_client_buffer */

#ifdef EGL_ANDROID_framebuffer_target

static void _glewInfo_EGL_ANDROID_framebuffer_target (void)
{
  glewPrintExt("EGL_ANDROID_framebuffer_target", EGLEW_ANDROID_framebuffer_target, eglewIsSupported("EGL_ANDROID_framebuffer_target"), eglewGetExtension("EGL_ANDROID_framebuffer_target"));
}

#endif /* EGL_ANDROID_framebuffer_target */

#ifdef EGL_ANDROID_front_buffer_auto_refresh

static void _glewInfo_EGL_ANDROID_front_buffer_auto_refresh (void)
{
  glewPrintExt("EGL_ANDROID_front_buffer_auto_refresh", EGLEW_ANDROID_front_buffer_auto_refresh, eglewIsSupported("EGL_ANDROID_front_buffer_auto_refresh"), eglewGetExtension("EGL_ANDROID_front_buffer_auto_refresh"));
}

#endif /* EGL_ANDROID_front_buffer_auto_refresh */

#ifdef EGL_ANDROID_image_native_buffer

static void _glewInfo_EGL_ANDROID_image_native_buffer (void)
{
  glewPrintExt("EGL_ANDROID_image_native_buffer", EGLEW_ANDROID_image_native_buffer, eglewIsSupported("EGL_ANDROID_image_native_buffer"), eglewGetExtension("EGL_ANDROID_image_native_buffer"));
}

#endif /* EGL_ANDROID_image_native_buffer */

#ifdef EGL_ANDROID_native_fence_sync

static void _glewInfo_EGL_ANDROID_native_fence_sync (void)
{
  glewPrintExt("EGL_ANDROID_native_fence_sync", EGLEW_ANDROID_native_fence_sync, eglewIsSupported("EGL_ANDROID_native_fence_sync"), eglewGetExtension("EGL_ANDROID_native_fence_sync"));

  glewInfoFunc("eglDupNativeFenceFDANDROID", eglDupNativeFenceFDANDROID == NULL);
}

#endif /* EGL_ANDROID_native_fence_sync */

#ifdef EGL_ANDROID_presentation_time

static void _glewInfo_EGL_ANDROID_presentation_time (void)
{
  glewPrintExt("EGL_ANDROID_presentation_time", EGLEW_ANDROID_presentation_time, eglewIsSupported("EGL_ANDROID_presentation_time"), eglewGetExtension("EGL_ANDROID_presentation_time"));

  glewInfoFunc("eglPresentationTimeANDROID", eglPresentationTimeANDROID == NULL);
}

#endif /* EGL_ANDROID_presentation_time */

#ifdef EGL_ANDROID_recordable

static void _glewInfo_EGL_ANDROID_recordable (void)
{
  glewPrintExt("EGL_ANDROID_recordable", EGLEW_ANDROID_recordable, eglewIsSupported("EGL_ANDROID_recordable"), eglewGetExtension("EGL_ANDROID_recordable"));
}

#endif /* EGL_ANDROID_recordable */

#ifdef EGL_ANGLE_d3d_share_handle_client_buffer

static void _glewInfo_EGL_ANGLE_d3d_share_handle_client_buffer (void)
{
  glewPrintExt("EGL_ANGLE_d3d_share_handle_client_buffer", EGLEW_ANGLE_d3d_share_handle_client_buffer, eglewIsSupported("EGL_ANGLE_d3d_share_handle_client_buffer"), eglewGetExtension("EGL_ANGLE_d3d_share_handle_client_buffer"));
}

#endif /* EGL_ANGLE_d3d_share_handle_client_buffer */

#ifdef EGL_ANGLE_device_d3d

static void _glewInfo_EGL_ANGLE_device_d3d (void)
{
  glewPrintExt("EGL_ANGLE_device_d3d", EGLEW_ANGLE_device_d3d, eglewIsSupported("EGL_ANGLE_device_d3d"), eglewGetExtension("EGL_ANGLE_device_d3d"));
}

#endif /* EGL_ANGLE_device_d3d */

#ifdef EGL_ANGLE_query_surface_pointer

static void _glewInfo_EGL_ANGLE_query_surface_pointer (void)
{
  glewPrintExt("EGL_ANGLE_query_surface_pointer", EGLEW_ANGLE_query_surface_pointer, eglewIsSupported("EGL_ANGLE_query_surface_pointer"), eglewGetExtension("EGL_ANGLE_query_surface_pointer"));

  glewInfoFunc("eglQuerySurfacePointerANGLE", eglQuerySurfacePointerANGLE == NULL);
}

#endif /* EGL_ANGLE_query_surface_pointer */

#ifdef EGL_ANGLE_surface_d3d_texture_2d_share_handle

static void _glewInfo_EGL_ANGLE_surface_d3d_texture_2d_share_handle (void)
{
  glewPrintExt("EGL_ANGLE_surface_d3d_texture_2d_share_handle", EGLEW_ANGLE_surface_d3d_texture_2d_share_handle, eglewIsSupported("EGL_ANGLE_surface_d3d_texture_2d_share_handle"), eglewGetExtension("EGL_ANGLE_surface_d3d_texture_2d_share_handle"));
}

#endif /* EGL_ANGLE_surface_d3d_texture_2d_share_handle */

#ifdef EGL_ANGLE_window_fixed_size

static void _glewInfo_EGL_ANGLE_window_fixed_size (void)
{
  glewPrintExt("EGL_ANGLE_window_fixed_size", EGLEW_ANGLE_window_fixed_size, eglewIsSupported("EGL_ANGLE_window_fixed_size"), eglewGetExtension("EGL_ANGLE_window_fixed_size"));
}

#endif /* EGL_ANGLE_window_fixed_size */

#ifdef EGL_ARM_implicit_external_sync

static void _glewInfo_EGL_ARM_implicit_external_sync (void)
{
  glewPrintExt("EGL_ARM_implicit_external_sync", EGLEW_ARM_implicit_external_sync, eglewIsSupported("EGL_ARM_implicit_external_sync"), eglewGetExtension("EGL_ARM_implicit_external_sync"));
}

#endif /* EGL_ARM_implicit_external_sync */

#ifdef EGL_ARM_pixmap_multisample_discard

static void _glewInfo_EGL_ARM_pixmap_multisample_discard (void)
{
  glewPrintExt("EGL_ARM_pixmap_multisample_discard", EGLEW_ARM_pixmap_multisample_discard, eglewIsSupported("EGL_ARM_pixmap_multisample_discard"), eglewGetExtension("EGL_ARM_pixmap_multisample_discard"));
}

#endif /* EGL_ARM_pixmap_multisample_discard */

#ifdef EGL_EXT_buffer_age

static void _glewInfo_EGL_EXT_buffer_age (void)
{
  glewPrintExt("EGL_EXT_buffer_age", EGLEW_EXT_buffer_age, eglewIsSupported("EGL_EXT_buffer_age"), eglewGetExtension("EGL_EXT_buffer_age"));
}

#endif /* EGL_EXT_buffer_age */

#ifdef EGL_EXT_client_extensions

static void _glewInfo_EGL_EXT_client_extensions (void)
{
  glewPrintExt("EGL_EXT_client_extensions", EGLEW_EXT_client_extensions, eglewIsSupported("EGL_EXT_client_extensions"), eglewGetExtension("EGL_EXT_client_extensions"));
}

#endif /* EGL_EXT_client_extensions */

#ifdef EGL_EXT_create_context_robustness

static void _glewInfo_EGL_EXT_create_context_robustness (void)
{
  glewPrintExt("EGL_EXT_create_context_robustness", EGLEW_EXT_create_context_robustness, eglewIsSupported("EGL_EXT_create_context_robustness"), eglewGetExtension("EGL_EXT_create_context_robustness"));
}

#endif /* EGL_EXT_create_context_robustness */

#ifdef EGL_EXT_device_base

static void _glewInfo_EGL_EXT_device_base (void)
{
  glewPrintExt("EGL_EXT_device_base", EGLEW_EXT_device_base, eglewIsSupported("EGL_EXT_device_base"), eglewGetExtension("EGL_EXT_device_base"));
}

#endif /* EGL_EXT_device_base */

#ifdef EGL_EXT_device_drm

static void _glewInfo_EGL_EXT_device_drm (void)
{
  glewPrintExt("EGL_EXT_device_drm", EGLEW_EXT_device_drm, eglewIsSupported("EGL_EXT_device_drm"), eglewGetExtension("EGL_EXT_device_drm"));
}

#endif /* EGL_EXT_device_drm */

#ifdef EGL_EXT_device_enumeration

static void _glewInfo_EGL_EXT_device_enumeration (void)
{
  glewPrintExt("EGL_EXT_device_enumeration", EGLEW_EXT_device_enumeration, eglewIsSupported("EGL_EXT_device_enumeration"), eglewGetExtension("EGL_EXT_device_enumeration"));

  glewInfoFunc("eglQueryDevicesEXT", eglQueryDevicesEXT == NULL);
}

#endif /* EGL_EXT_device_enumeration */

#ifdef EGL_EXT_device_openwf

static void _glewInfo_EGL_EXT_device_openwf (void)
{
  glewPrintExt("EGL_EXT_device_openwf", EGLEW_EXT_device_openwf, eglewIsSupported("EGL_EXT_device_openwf"), eglewGetExtension("EGL_EXT_device_openwf"));
}

#endif /* EGL_EXT_device_openwf */

#ifdef EGL_EXT_device_query

static void _glewInfo_EGL_EXT_device_query (void)
{
  glewPrintExt("EGL_EXT_device_query", EGLEW_EXT_device_query, eglewIsSupported("EGL_EXT_device_query"), eglewGetExtension("EGL_EXT_device_query"));

  glewInfoFunc("eglQueryDeviceAttribEXT", eglQueryDeviceAttribEXT == NULL);
  glewInfoFunc("eglQueryDeviceStringEXT", eglQueryDeviceStringEXT == NULL);
  glewInfoFunc("eglQueryDisplayAttribEXT", eglQueryDisplayAttribEXT == NULL);
}

#endif /* EGL_EXT_device_query */

#ifdef EGL_EXT_gl_colorspace_bt2020_linear

static void _glewInfo_EGL_EXT_gl_colorspace_bt2020_linear (void)
{
  glewPrintExt("EGL_EXT_gl_colorspace_bt2020_linear", EGLEW_EXT_gl_colorspace_bt2020_linear, eglewIsSupported("EGL_EXT_gl_colorspace_bt2020_linear"), eglewGetExtension("EGL_EXT_gl_colorspace_bt2020_linear"));
}

#endif /* EGL_EXT_gl_colorspace_bt2020_linear */

#ifdef EGL_EXT_gl_colorspace_bt2020_pq

static void _glewInfo_EGL_EXT_gl_colorspace_bt2020_pq (void)
{
  glewPrintExt("EGL_EXT_gl_colorspace_bt2020_pq", EGLEW_EXT_gl_colorspace_bt2020_pq, eglewIsSupported("EGL_EXT_gl_colorspace_bt2020_pq"), eglewGetExtension("EGL_EXT_gl_colorspace_bt2020_pq"));
}

#endif /* EGL_EXT_gl_colorspace_bt2020_pq */

#ifdef EGL_EXT_gl_colorspace_scrgb_linear

static void _glewInfo_EGL_EXT_gl_colorspace_scrgb_linear (void)
{
  glewPrintExt("EGL_EXT_gl_colorspace_scrgb_linear", EGLEW_EXT_gl_colorspace_scrgb_linear, eglewIsSupported("EGL_EXT_gl_colorspace_scrgb_linear"), eglewGetExtension("EGL_EXT_gl_colorspace_scrgb_linear"));
}

#endif /* EGL_EXT_gl_colorspace_scrgb_linear */

#ifdef EGL_EXT_image_dma_buf_import

static void _glewInfo_EGL_EXT_image_dma_buf_import (void)
{
  glewPrintExt("EGL_EXT_image_dma_buf_import", EGLEW_EXT_image_dma_buf_import, eglewIsSupported("EGL_EXT_image_dma_buf_import"), eglewGetExtension("EGL_EXT_image_dma_buf_import"));
}

#endif /* EGL_EXT_image_dma_buf_import */

#ifdef EGL_EXT_image_dma_buf_import_modifiers

static void _glewInfo_EGL_EXT_image_dma_buf_import_modifiers (void)
{
  glewPrintExt("EGL_EXT_image_dma_buf_import_modifiers", EGLEW_EXT_image_dma_buf_import_modifiers, eglewIsSupported("EGL_EXT_image_dma_buf_import_modifiers"), eglewGetExtension("EGL_EXT_image_dma_buf_import_modifiers"));

  glewInfoFunc("eglQueryDmaBufFormatsEXT", eglQueryDmaBufFormatsEXT == NULL);
  glewInfoFunc("eglQueryDmaBufModifiersEXT", eglQueryDmaBufModifiersEXT == NULL);
}

#endif /* EGL_EXT_image_dma_buf_import_modifiers */

#ifdef EGL_EXT_multiview_window

static void _glewInfo_EGL_EXT_multiview_window (void)
{
  glewPrintExt("EGL_EXT_multiview_window", EGLEW_EXT_multiview_window, eglewIsSupported("EGL_EXT_multiview_window"), eglewGetExtension("EGL_EXT_multiview_window"));
}

#endif /* EGL_EXT_multiview_window */

#ifdef EGL_EXT_output_base

static void _glewInfo_EGL_EXT_output_base (void)
{
  glewPrintExt("EGL_EXT_output_base", EGLEW_EXT_output_base, eglewIsSupported("EGL_EXT_output_base"), eglewGetExtension("EGL_EXT_output_base"));

  glewInfoFunc("eglGetOutputLayersEXT", eglGetOutputLayersEXT == NULL);
  glewInfoFunc("eglGetOutputPortsEXT", eglGetOutputPortsEXT == NULL);
  glewInfoFunc("eglOutputLayerAttribEXT", eglOutputLayerAttribEXT == NULL);
  glewInfoFunc("eglOutputPortAttribEXT", eglOutputPortAttribEXT == NULL);
  glewInfoFunc("eglQueryOutputLayerAttribEXT", eglQueryOutputLayerAttribEXT == NULL);
  glewInfoFunc("eglQueryOutputLayerStringEXT", eglQueryOutputLayerStringEXT == NULL);
  glewInfoFunc("eglQueryOutputPortAttribEXT", eglQueryOutputPortAttribEXT == NULL);
  glewInfoFunc("eglQueryOutputPortStringEXT", eglQueryOutputPortStringEXT == NULL);
}

#endif /* EGL_EXT_output_base */

#ifdef EGL_EXT_output_drm

static void _glewInfo_EGL_EXT_output_drm (void)
{
  glewPrintExt("EGL_EXT_output_drm", EGLEW_EXT_output_drm, eglewIsSupported("EGL_EXT_output_drm"), eglewGetExtension("EGL_EXT_output_drm"));
}

#endif /* EGL_EXT_output_drm */

#ifdef EGL_EXT_output_openwf

static void _glewInfo_EGL_EXT_output_openwf (void)
{
  glewPrintExt("EGL_EXT_output_openwf", EGLEW_EXT_output_openwf, eglewIsSupported("EGL_EXT_output_openwf"), eglewGetExtension("EGL_EXT_output_openwf"));
}

#endif /* EGL_EXT_output_openwf */

#ifdef EGL_EXT_pixel_format_float

static void _glewInfo_EGL_EXT_pixel_format_float (void)
{
  glewPrintExt("EGL_EXT_pixel_format_float", EGLEW_EXT_pixel_format_float, eglewIsSupported("EGL_EXT_pixel_format_float"), eglewGetExtension("EGL_EXT_pixel_format_float"));
}

#endif /* EGL_EXT_pixel_format_float */

#ifdef EGL_EXT_platform_base

static void _glewInfo_EGL_EXT_platform_base (void)
{
  glewPrintExt("EGL_EXT_platform_base", EGLEW_EXT_platform_base, eglewIsSupported("EGL_EXT_platform_base"), eglewGetExtension("EGL_EXT_platform_base"));

  glewInfoFunc("eglCreatePlatformPixmapSurfaceEXT", eglCreatePlatformPixmapSurfaceEXT == NULL);
  glewInfoFunc("eglCreatePlatformWindowSurfaceEXT", eglCreatePlatformWindowSurfaceEXT == NULL);
  glewInfoFunc("eglGetPlatformDisplayEXT", eglGetPlatformDisplayEXT == NULL);
}

#endif /* EGL_EXT_platform_base */

#ifdef EGL_EXT_platform_device

static void _glewInfo_EGL_EXT_platform_device (void)
{
  glewPrintExt("EGL_EXT_platform_device", EGLEW_EXT_platform_device, eglewIsSupported("EGL_EXT_platform_device"), eglewGetExtension("EGL_EXT_platform_device"));
}

#endif /* EGL_EXT_platform_device */

#ifdef EGL_EXT_platform_wayland

static void _glewInfo_EGL_EXT_platform_wayland (void)
{
  glewPrintExt("EGL_EXT_platform_wayland", EGLEW_EXT_platform_wayland, eglewIsSupported("EGL_EXT_platform_wayland"), eglewGetExtension("EGL_EXT_platform_wayland"));
}

#endif /* EGL_EXT_platform_wayland */

#ifdef EGL_EXT_platform_x11

static void _glewInfo_EGL_EXT_platform_x11 (void)
{
  glewPrintExt("EGL_EXT_platform_x11", EGLEW_EXT_platform_x11, eglewIsSupported("EGL_EXT_platform_x11"), eglewGetExtension("EGL_EXT_platform_x11"));
}

#endif /* EGL_EXT_platform_x11 */

#ifdef EGL_EXT_protected_content

static void _glewInfo_EGL_EXT_protected_content (void)
{
  glewPrintExt("EGL_EXT_protected_content", EGLEW_EXT_protected_content, eglewIsSupported("EGL_EXT_protected_content"), eglewGetExtension("EGL_EXT_protected_content"));
}

#endif /* EGL_EXT_protected_content */

#ifdef EGL_EXT_protected_surface

static void _glewInfo_EGL_EXT_protected_surface (void)
{
  glewPrintExt("EGL_EXT_protected_surface", EGLEW_EXT_protected_surface, eglewIsSupported("EGL_EXT_protected_surface"), eglewGetExtension("EGL_EXT_protected_surface"));
}

#endif /* EGL_EXT_protected_surface */

#ifdef EGL_EXT_stream_consumer_egloutput

static void _glewInfo_EGL_EXT_stream_consumer_egloutput (void)
{
  glewPrintExt("EGL_EXT_stream_consumer_egloutput", EGLEW_EXT_stream_consumer_egloutput, eglewIsSupported("EGL_EXT_stream_consumer_egloutput"), eglewGetExtension("EGL_EXT_stream_consumer_egloutput"));

  glewInfoFunc("eglStreamConsumerOutputEXT", eglStreamConsumerOutputEXT == NULL);
}

#endif /* EGL_EXT_stream_consumer_egloutput */

#ifdef EGL_EXT_surface_SMPTE2086_metadata

static void _glewInfo_EGL_EXT_surface_SMPTE2086_metadata (void)
{
  glewPrintExt("EGL_EXT_surface_SMPTE2086_metadata", EGLEW_EXT_surface_SMPTE2086_metadata, eglewIsSupported("EGL_EXT_surface_SMPTE2086_metadata"), eglewGetExtension("EGL_EXT_surface_SMPTE2086_metadata"));
}

#endif /* EGL_EXT_surface_SMPTE2086_metadata */

#ifdef EGL_EXT_swap_buffers_with_damage

static void _glewInfo_EGL_EXT_swap_buffers_with_damage (void)
{
  glewPrintExt("EGL_EXT_swap_buffers_with_damage", EGLEW_EXT_swap_buffers_with_damage, eglewIsSupported("EGL_EXT_swap_buffers_with_damage"), eglewGetExtension("EGL_EXT_swap_buffers_with_damage"));

  glewInfoFunc("eglSwapBuffersWithDamageEXT", eglSwapBuffersWithDamageEXT == NULL);
}

#endif /* EGL_EXT_swap_buffers_with_damage */

#ifdef EGL_EXT_yuv_surface

static void _glewInfo_EGL_EXT_yuv_surface (void)
{
  glewPrintExt("EGL_EXT_yuv_surface", EGLEW_EXT_yuv_surface, eglewIsSupported("EGL_EXT_yuv_surface"), eglewGetExtension("EGL_EXT_yuv_surface"));
}

#endif /* EGL_EXT_yuv_surface */

#ifdef EGL_HI_clientpixmap

static void _glewInfo_EGL_HI_clientpixmap (void)
{
  glewPrintExt("EGL_HI_clientpixmap", EGLEW_HI_clientpixmap, eglewIsSupported("EGL_HI_clientpixmap"), eglewGetExtension("EGL_HI_clientpixmap"));

  glewInfoFunc("eglCreatePixmapSurfaceHI", eglCreatePixmapSurfaceHI == NULL);
}

#endif /* EGL_HI_clientpixmap */

#ifdef EGL_HI_colorformats

static void _glewInfo_EGL_HI_colorformats (void)
{
  glewPrintExt("EGL_HI_colorformats", EGLEW_HI_colorformats, eglewIsSupported("EGL_HI_colorformats"), eglewGetExtension("EGL_HI_colorformats"));
}

#endif /* EGL_HI_colorformats */

#ifdef EGL_IMG_context_priority

static void _glewInfo_EGL_IMG_context_priority (void)
{
  glewPrintExt("EGL_IMG_context_priority", EGLEW_IMG_context_priority, eglewIsSupported("EGL_IMG_context_priority"), eglewGetExtension("EGL_IMG_context_priority"));
}

#endif /* EGL_IMG_context_priority */

#ifdef EGL_IMG_image_plane_attribs

static void _glewInfo_EGL_IMG_image_plane_attribs (void)
{
  glewPrintExt("EGL_IMG_image_plane_attribs", EGLEW_IMG_image_plane_attribs, eglewIsSupported("EGL_IMG_image_plane_attribs"), eglewGetExtension("EGL_IMG_image_plane_attribs"));
}

#endif /* EGL_IMG_image_plane_attribs */

#ifdef EGL_KHR_cl_event

static void _glewInfo_EGL_KHR_cl_event (void)
{
  glewPrintExt("EGL_KHR_cl_event", EGLEW_KHR_cl_event, eglewIsSupported("EGL_KHR_cl_event"), eglewGetExtension("EGL_KHR_cl_event"));
}

#endif /* EGL_KHR_cl_event */

#ifdef EGL_KHR_cl_event2

static void _glewInfo_EGL_KHR_cl_event2 (void)
{
  glewPrintExt("EGL_KHR_cl_event2", EGLEW_KHR_cl_event2, eglewIsSupported("EGL_KHR_cl_event2"), eglewGetExtension("EGL_KHR_cl_event2"));

  glewInfoFunc("eglCreateSync64KHR", eglCreateSync64KHR == NULL);
}

#endif /* EGL_KHR_cl_event2 */

#ifdef EGL_KHR_client_get_all_proc_addresses

static void _glewInfo_EGL_KHR_client_get_all_proc_addresses (void)
{
  glewPrintExt("EGL_KHR_client_get_all_proc_addresses", EGLEW_KHR_client_get_all_proc_addresses, eglewIsSupported("EGL_KHR_client_get_all_proc_addresses"), eglewGetExtension("EGL_KHR_client_get_all_proc_addresses"));
}

#endif /* EGL_KHR_client_get_all_proc_addresses */

#ifdef EGL_KHR_config_attribs

static void _glewInfo_EGL_KHR_config_attribs (void)
{
  glewPrintExt("EGL_KHR_config_attribs", EGLEW_KHR_config_attribs, eglewIsSupported("EGL_KHR_config_attribs"), eglewGetExtension("EGL_KHR_config_attribs"));
}

#endif /* EGL_KHR_config_attribs */

#ifdef EGL_KHR_context_flush_control

static void _glewInfo_EGL_KHR_context_flush_control (void)
{
  glewPrintExt("EGL_KHR_context_flush_control", EGLEW_KHR_context_flush_control, eglewIsSupported("EGL_KHR_context_flush_control"), eglewGetExtension("EGL_KHR_context_flush_control"));
}

#endif /* EGL_KHR_context_flush_control */

#ifdef EGL_KHR_create_context

static void _glewInfo_EGL_KHR_create_context (void)
{
  glewPrintExt("EGL_KHR_create_context", EGLEW_KHR_create_context, eglewIsSupported("EGL_KHR_create_context"), eglewGetExtension("EGL_KHR_create_context"));
}

#endif /* EGL_KHR_create_context */

#ifdef EGL_KHR_create_context_no_error

static void _glewInfo_EGL_KHR_create_context_no_error (void)
{
  glewPrintExt("EGL_KHR_create_context_no_error", EGLEW_KHR_create_context_no_error, eglewIsSupported("EGL_KHR_create_context_no_error"), eglewGetExtension("EGL_KHR_create_context_no_error"));
}

#endif /* EGL_KHR_create_context_no_error */

#ifdef EGL_KHR_debug

static void _glewInfo_EGL_KHR_debug (void)
{
  glewPrintExt("EGL_KHR_debug", EGLEW_KHR_debug, eglewIsSupported("EGL_KHR_debug"), eglewGetExtension("EGL_KHR_debug"));

  glewInfoFunc("eglDebugMessageControlKHR", eglDebugMessageControlKHR == NULL);
  glewInfoFunc("eglLabelObjectKHR", eglLabelObjectKHR == NULL);
  glewInfoFunc("eglQueryDebugKHR", eglQueryDebugKHR == NULL);
}

#endif /* EGL_KHR_debug */

#ifdef EGL_KHR_fence_sync

static void _glewInfo_EGL_KHR_fence_sync (void)
{
  glewPrintExt("EGL_KHR_fence_sync", EGLEW_KHR_fence_sync, eglewIsSupported("EGL_KHR_fence_sync"), eglewGetExtension("EGL_KHR_fence_sync"));
}

#endif /* EGL_KHR_fence_sync */

#ifdef EGL_KHR_get_all_proc_addresses

static void _glewInfo_EGL_KHR_get_all_proc_addresses (void)
{
  glewPrintExt("EGL_KHR_get_all_proc_addresses", EGLEW_KHR_get_all_proc_addresses, eglewIsSupported("EGL_KHR_get_all_proc_addresses"), eglewGetExtension("EGL_KHR_get_all_proc_addresses"));
}

#endif /* EGL_KHR_get_all_proc_addresses */

#ifdef EGL_KHR_gl_colorspace

static void _glewInfo_EGL_KHR_gl_colorspace (void)
{
  glewPrintExt("EGL_KHR_gl_colorspace", EGLEW_KHR_gl_colorspace, eglewIsSupported("EGL_KHR_gl_colorspace"), eglewGetExtension("EGL_KHR_gl_colorspace"));
}

#endif /* EGL_KHR_gl_colorspace */

#ifdef EGL_KHR_gl_renderbuffer_image

static void _glewInfo_EGL_KHR_gl_renderbuffer_image (void)
{
  glewPrintExt("EGL_KHR_gl_renderbuffer_image", EGLEW_KHR_gl_renderbuffer_image, eglewIsSupported("EGL_KHR_gl_renderbuffer_image"), eglewGetExtension("EGL_KHR_gl_renderbuffer_image"));
}

#endif /* EGL_KHR_gl_renderbuffer_image */

#ifdef EGL_KHR_gl_texture_2D_image

static void _glewInfo_EGL_KHR_gl_texture_2D_image (void)
{
  glewPrintExt("EGL_KHR_gl_texture_2D_image", EGLEW_KHR_gl_texture_2D_image, eglewIsSupported("EGL_KHR_gl_texture_2D_image"), eglewGetExtension("EGL_KHR_gl_texture_2D_image"));
}

#endif /* EGL_KHR_gl_texture_2D_image */

#ifdef EGL_KHR_gl_texture_3D_image

static void _glewInfo_EGL_KHR_gl_texture_3D_image (void)
{
  glewPrintExt("EGL_KHR_gl_texture_3D_image", EGLEW_KHR_gl_texture_3D_image, eglewIsSupported("EGL_KHR_gl_texture_3D_image"), eglewGetExtension("EGL_KHR_gl_texture_3D_image"));
}

#endif /* EGL_KHR_gl_texture_3D_image */

#ifdef EGL_KHR_gl_texture_cubemap_image

static void _glewInfo_EGL_KHR_gl_texture_cubemap_image (void)
{
  glewPrintExt("EGL_KHR_gl_texture_cubemap_image", EGLEW_KHR_gl_texture_cubemap_image, eglewIsSupported("EGL_KHR_gl_texture_cubemap_image"), eglewGetExtension("EGL_KHR_gl_texture_cubemap_image"));
}

#endif /* EGL_KHR_gl_texture_cubemap_image */

#ifdef EGL_KHR_image

static void _glewInfo_EGL_KHR_image (void)
{
  glewPrintExt("EGL_KHR_image", EGLEW_KHR_image, eglewIsSupported("EGL_KHR_image"), eglewGetExtension("EGL_KHR_image"));

  glewInfoFunc("eglCreateImageKHR", eglCreateImageKHR == NULL);
  glewInfoFunc("eglDestroyImageKHR", eglDestroyImageKHR == NULL);
}

#endif /* EGL_KHR_image */

#ifdef EGL_KHR_image_base

static void _glewInfo_EGL_KHR_image_base (void)
{
  glewPrintExt("EGL_KHR_image_base", EGLEW_KHR_image_base, eglewIsSupported("EGL_KHR_image_base"), eglewGetExtension("EGL_KHR_image_base"));
}

#endif /* EGL_KHR_image_base */

#ifdef EGL_KHR_image_pixmap

static void _glewInfo_EGL_KHR_image_pixmap (void)
{
  glewPrintExt("EGL_KHR_image_pixmap", EGLEW_KHR_image_pixmap, eglewIsSupported("EGL_KHR_image_pixmap"), eglewGetExtension("EGL_KHR_image_pixmap"));
}

#endif /* EGL_KHR_image_pixmap */

#ifdef EGL_KHR_lock_surface

static void _glewInfo_EGL_KHR_lock_surface (void)
{
  glewPrintExt("EGL_KHR_lock_surface", EGLEW_KHR_lock_surface, eglewIsSupported("EGL_KHR_lock_surface"), eglewGetExtension("EGL_KHR_lock_surface"));

  glewInfoFunc("eglLockSurfaceKHR", eglLockSurfaceKHR == NULL);
  glewInfoFunc("eglUnlockSurfaceKHR", eglUnlockSurfaceKHR == NULL);
}

#endif /* EGL_KHR_lock_surface */

#ifdef EGL_KHR_lock_surface2

static void _glewInfo_EGL_KHR_lock_surface2 (void)
{
  glewPrintExt("EGL_KHR_lock_surface2", EGLEW_KHR_lock_surface2, eglewIsSupported("EGL_KHR_lock_surface2"), eglewGetExtension("EGL_KHR_lock_surface2"));
}

#endif /* EGL_KHR_lock_surface2 */

#ifdef EGL_KHR_lock_surface3

static void _glewInfo_EGL_KHR_lock_surface3 (void)
{
  glewPrintExt("EGL_KHR_lock_surface3", EGLEW_KHR_lock_surface3, eglewIsSupported("EGL_KHR_lock_surface3"), eglewGetExtension("EGL_KHR_lock_surface3"));

  glewInfoFunc("eglQuerySurface64KHR", eglQuerySurface64KHR == NULL);
}

#endif /* EGL_KHR_lock_surface3 */

#ifdef EGL_KHR_mutable_render_buffer

static void _glewInfo_EGL_KHR_mutable_render_buffer (void)
{
  glewPrintExt("EGL_KHR_mutable_render_buffer", EGLEW_KHR_mutable_render_buffer, eglewIsSupported("EGL_KHR_mutable_render_buffer"), eglewGetExtension("EGL_KHR_mutable_render_buffer"));
}

#endif /* EGL_KHR_mutable_render_buffer */

#ifdef EGL_KHR_no_config_context

static void _glewInfo_EGL_KHR_no_config_context (void)
{
  glewPrintExt("EGL_KHR_no_config_context", EGLEW_KHR_no_config_context, eglewIsSupported("EGL_KHR_no_config_context"), eglewGetExtension("EGL_KHR_no_config_context"));
}

#endif /* EGL_KHR_no_config_context */

#ifdef EGL_KHR_partial_update

static void _glewInfo_EGL_KHR_partial_update (void)
{
  glewPrintExt("EGL_KHR_partial_update", EGLEW_KHR_partial_update, eglewIsSupported("EGL_KHR_partial_update"), eglewGetExtension("EGL_KHR_partial_update"));

  glewInfoFunc("eglSetDamageRegionKHR", eglSetDamageRegionKHR == NULL);
}

#endif /* EGL_KHR_partial_update */

#ifdef EGL_KHR_platform_android

static void _glewInfo_EGL_KHR_platform_android (void)
{
  glewPrintExt("EGL_KHR_platform_android", EGLEW_KHR_platform_android, eglewIsSupported("EGL_KHR_platform_android"), eglewGetExtension("EGL_KHR_platform_android"));
}

#endif /* EGL_KHR_platform_android */

#ifdef EGL_KHR_platform_gbm

static void _glewInfo_EGL_KHR_platform_gbm (void)
{
  glewPrintExt("EGL_KHR_platform_gbm", EGLEW_KHR_platform_gbm, eglewIsSupported("EGL_KHR_platform_gbm"), eglewGetExtension("EGL_KHR_platform_gbm"));
}

#endif /* EGL_KHR_platform_gbm */

#ifdef EGL_KHR_platform_wayland

static void _glewInfo_EGL_KHR_platform_wayland (void)
{
  glewPrintExt("EGL_KHR_platform_wayland", EGLEW_KHR_platform_wayland, eglewIsSupported("EGL_KHR_platform_wayland"), eglewGetExtension("EGL_KHR_platform_wayland"));
}

#endif /* EGL_KHR_platform_wayland */

#ifdef EGL_KHR_platform_x11

static void _glewInfo_EGL_KHR_platform_x11 (void)
{
  glewPrintExt("EGL_KHR_platform_x11", EGLEW_KHR_platform_x11, eglewIsSupported("EGL_KHR_platform_x11"), eglewGetExtension("EGL_KHR_platform_x11"));
}

#endif /* EGL_KHR_platform_x11 */

#ifdef EGL_KHR_reusable_sync

static void _glewInfo_EGL_KHR_reusable_sync (void)
{
  glewPrintExt("EGL_KHR_reusable_sync", EGLEW_KHR_reusable_sync, eglewIsSupported("EGL_KHR_reusable_sync"), eglewGetExtension("EGL_KHR_reusable_sync"));

  glewInfoFunc("eglClientWaitSyncKHR", eglClientWaitSyncKHR == NULL);
  glewInfoFunc("eglCreateSyncKHR", eglCreateSyncKHR == NULL);
  glewInfoFunc("eglDestroySyncKHR", eglDestroySyncKHR == NULL);
  glewInfoFunc("eglGetSyncAttribKHR", eglGetSyncAttribKHR == NULL);
  glewInfoFunc("eglSignalSyncKHR", eglSignalSyncKHR == NULL);
}

#endif /* EGL_KHR_reusable_sync */

#ifdef EGL_KHR_stream

static void _glewInfo_EGL_KHR_stream (void)
{
  glewPrintExt("EGL_KHR_stream", EGLEW_KHR_stream, eglewIsSupported("EGL_KHR_stream"), eglewGetExtension("EGL_KHR_stream"));

  glewInfoFunc("eglCreateStreamKHR", eglCreateStreamKHR == NULL);
  glewInfoFunc("eglDestroyStreamKHR", eglDestroyStreamKHR == NULL);
  glewInfoFunc("eglQueryStreamKHR", eglQueryStreamKHR == NULL);
  glewInfoFunc("eglQueryStreamu64KHR", eglQueryStreamu64KHR == NULL);
  glewInfoFunc("eglStreamAttribKHR", eglStreamAttribKHR == NULL);
}

#endif /* EGL_KHR_stream */

#ifdef EGL_KHR_stream_attrib

static void _glewInfo_EGL_KHR_stream_attrib (void)
{
  glewPrintExt("EGL_KHR_stream_attrib", EGLEW_KHR_stream_attrib, eglewIsSupported("EGL_KHR_stream_attrib"), eglewGetExtension("EGL_KHR_stream_attrib"));

  glewInfoFunc("eglCreateStreamAttribKHR", eglCreateStreamAttribKHR == NULL);
  glewInfoFunc("eglQueryStreamAttribKHR", eglQueryStreamAttribKHR == NULL);
  glewInfoFunc("eglSetStreamAttribKHR", eglSetStreamAttribKHR == NULL);
  glewInfoFunc("eglStreamConsumerAcquireAttribKHR", eglStreamConsumerAcquireAttribKHR == NULL);
  glewInfoFunc("eglStreamConsumerReleaseAttribKHR", eglStreamConsumerReleaseAttribKHR == NULL);
}

#endif /* EGL_KHR_stream_attrib */

#ifdef EGL_KHR_stream_consumer_gltexture

static void _glewInfo_EGL_KHR_stream_consumer_gltexture (void)
{
  glewPrintExt("EGL_KHR_stream_consumer_gltexture", EGLEW_KHR_stream_consumer_gltexture, eglewIsSupported("EGL_KHR_stream_consumer_gltexture"), eglewGetExtension("EGL_KHR_stream_consumer_gltexture"));

  glewInfoFunc("eglStreamConsumerAcquireKHR", eglStreamConsumerAcquireKHR == NULL);
  glewInfoFunc("eglStreamConsumerGLTextureExternalKHR", eglStreamConsumerGLTextureExternalKHR == NULL);
  glewInfoFunc("eglStreamConsumerReleaseKHR", eglStreamConsumerReleaseKHR == NULL);
}

#endif /* EGL_KHR_stream_consumer_gltexture */

#ifdef EGL_KHR_stream_cross_process_fd

static void _glewInfo_EGL_KHR_stream_cross_process_fd (void)
{
  glewPrintExt("EGL_KHR_stream_cross_process_fd", EGLEW_KHR_stream_cross_process_fd, eglewIsSupported("EGL_KHR_stream_cross_process_fd"), eglewGetExtension("EGL_KHR_stream_cross_process_fd"));

  glewInfoFunc("eglCreateStreamFromFileDescriptorKHR", eglCreateStreamFromFileDescriptorKHR == NULL);
  glewInfoFunc("eglGetStreamFileDescriptorKHR", eglGetStreamFileDescriptorKHR == NULL);
}

#endif /* EGL_KHR_stream_cross_process_fd */

#ifdef EGL_KHR_stream_fifo

static void _glewInfo_EGL_KHR_stream_fifo (void)
{
  glewPrintExt("EGL_KHR_stream_fifo", EGLEW_KHR_stream_fifo, eglewIsSupported("EGL_KHR_stream_fifo"), eglewGetExtension("EGL_KHR_stream_fifo"));

  glewInfoFunc("eglQueryStreamTimeKHR", eglQueryStreamTimeKHR == NULL);
}

#endif /* EGL_KHR_stream_fifo */

#ifdef EGL_KHR_stream_producer_aldatalocator

static void _glewInfo_EGL_KHR_stream_producer_aldatalocator (void)
{
  glewPrintExt("EGL_KHR_stream_producer_aldatalocator", EGLEW_KHR_stream_producer_aldatalocator, eglewIsSupported("EGL_KHR_stream_producer_aldatalocator"), eglewGetExtension("EGL_KHR_stream_producer_aldatalocator"));
}

#endif /* EGL_KHR_stream_producer_aldatalocator */

#ifdef EGL_KHR_stream_producer_eglsurface

static void _glewInfo_EGL_KHR_stream_producer_eglsurface (void)
{
  glewPrintExt("EGL_KHR_stream_producer_eglsurface", EGLEW_KHR_stream_producer_eglsurface, eglewIsSupported("EGL_KHR_stream_producer_eglsurface"), eglewGetExtension("EGL_KHR_stream_producer_eglsurface"));

  glewInfoFunc("eglCreateStreamProducerSurfaceKHR", eglCreateStreamProducerSurfaceKHR == NULL);
}

#endif /* EGL_KHR_stream_producer_eglsurface */

#ifdef EGL_KHR_surfaceless_context

static void _glewInfo_EGL_KHR_surfaceless_context (void)
{
  glewPrintExt("EGL_KHR_surfaceless_context", EGLEW_KHR_surfaceless_context, eglewIsSupported("EGL_KHR_surfaceless_context"), eglewGetExtension("EGL_KHR_surfaceless_context"));
}

#endif /* EGL_KHR_surfaceless_context */

#ifdef EGL_KHR_swap_buffers_with_damage

static void _glewInfo_EGL_KHR_swap_buffers_with_damage (void)
{
  glewPrintExt("EGL_KHR_swap_buffers_with_damage", EGLEW_KHR_swap_buffers_with_damage, eglewIsSupported("EGL_KHR_swap_buffers_with_damage"), eglewGetExtension("EGL_KHR_swap_buffers_with_damage"));

  glewInfoFunc("eglSwapBuffersWithDamageKHR", eglSwapBuffersWithDamageKHR == NULL);
}

#endif /* EGL_KHR_swap_buffers_with_damage */

#ifdef EGL_KHR_vg_parent_image

static void _glewInfo_EGL_KHR_vg_parent_image (void)
{
  glewPrintExt("EGL_KHR_vg_parent_image", EGLEW_KHR_vg_parent_image, eglewIsSupported("EGL_KHR_vg_parent_image"), eglewGetExtension("EGL_KHR_vg_parent_image"));
}

#endif /* EGL_KHR_vg_parent_image */

#ifdef EGL_KHR_wait_sync

static void _glewInfo_EGL_KHR_wait_sync (void)
{
  glewPrintExt("EGL_KHR_wait_sync", EGLEW_KHR_wait_sync, eglewIsSupported("EGL_KHR_wait_sync"), eglewGetExtension("EGL_KHR_wait_sync"));

  glewInfoFunc("eglWaitSyncKHR", eglWaitSyncKHR == NULL);
}

#endif /* EGL_KHR_wait_sync */

#ifdef EGL_MESA_drm_image

static void _glewInfo_EGL_MESA_drm_image (void)
{
  glewPrintExt("EGL_MESA_drm_image", EGLEW_MESA_drm_image, eglewIsSupported("EGL_MESA_drm_image"), eglewGetExtension("EGL_MESA_drm_image"));

  glewInfoFunc("eglCreateDRMImageMESA", eglCreateDRMImageMESA == NULL);
  glewInfoFunc("eglExportDRMImageMESA", eglExportDRMImageMESA == NULL);
}

#endif /* EGL_MESA_drm_image */

#ifdef EGL_MESA_image_dma_buf_export

static void _glewInfo_EGL_MESA_image_dma_buf_export (void)
{
  glewPrintExt("EGL_MESA_image_dma_buf_export", EGLEW_MESA_image_dma_buf_export, eglewIsSupported("EGL_MESA_image_dma_buf_export"), eglewGetExtension("EGL_MESA_image_dma_buf_export"));

  glewInfoFunc("eglExportDMABUFImageMESA", eglExportDMABUFImageMESA == NULL);
  glewInfoFunc("eglExportDMABUFImageQueryMESA", eglExportDMABUFImageQueryMESA == NULL);
}

#endif /* EGL_MESA_image_dma_buf_export */

#ifdef EGL_MESA_platform_gbm

static void _glewInfo_EGL_MESA_platform_gbm (void)
{
  glewPrintExt("EGL_MESA_platform_gbm", EGLEW_MESA_platform_gbm, eglewIsSupported("EGL_MESA_platform_gbm"), eglewGetExtension("EGL_MESA_platform_gbm"));
}

#endif /* EGL_MESA_platform_gbm */

#ifdef EGL_MESA_platform_surfaceless

static void _glewInfo_EGL_MESA_platform_surfaceless (void)
{
  glewPrintExt("EGL_MESA_platform_surfaceless", EGLEW_MESA_platform_surfaceless, eglewIsSupported("EGL_MESA_platform_surfaceless"), eglewGetExtension("EGL_MESA_platform_surfaceless"));
}

#endif /* EGL_MESA_platform_surfaceless */

#ifdef EGL_NOK_swap_region

static void _glewInfo_EGL_NOK_swap_region (void)
{
  glewPrintExt("EGL_NOK_swap_region", EGLEW_NOK_swap_region, eglewIsSupported("EGL_NOK_swap_region"), eglewGetExtension("EGL_NOK_swap_region"));

  glewInfoFunc("eglSwapBuffersRegionNOK", eglSwapBuffersRegionNOK == NULL);
}

#endif /* EGL_NOK_swap_region */

#ifdef EGL_NOK_swap_region2

static void _glewInfo_EGL_NOK_swap_region2 (void)
{
  glewPrintExt("EGL_NOK_swap_region2", EGLEW_NOK_swap_region2, eglewIsSupported("EGL_NOK_swap_region2"), eglewGetExtension("EGL_NOK_swap_region2"));

  glewInfoFunc("eglSwapBuffersRegion2NOK", eglSwapBuffersRegion2NOK == NULL);
}

#endif /* EGL_NOK_swap_region2 */

#ifdef EGL_NOK_texture_from_pixmap

static void _glewInfo_EGL_NOK_texture_from_pixmap (void)
{
  glewPrintExt("EGL_NOK_texture_from_pixmap", EGLEW_NOK_texture_from_pixmap, eglewIsSupported("EGL_NOK_texture_from_pixmap"), eglewGetExtension("EGL_NOK_texture_from_pixmap"));
}

#endif /* EGL_NOK_texture_from_pixmap */

#ifdef EGL_NV_3dvision_surface

static void _glewInfo_EGL_NV_3dvision_surface (void)
{
  glewPrintExt("EGL_NV_3dvision_surface", EGLEW_NV_3dvision_surface, eglewIsSupported("EGL_NV_3dvision_surface"), eglewGetExtension("EGL_NV_3dvision_surface"));
}

#endif /* EGL_NV_3dvision_surface */

#ifdef EGL_NV_coverage_sample

static void _glewInfo_EGL_NV_coverage_sample (void)
{
  glewPrintExt("EGL_NV_coverage_sample", EGLEW_NV_coverage_sample, eglewIsSupported("EGL_NV_coverage_sample"), eglewGetExtension("EGL_NV_coverage_sample"));
}

#endif /* EGL_NV_coverage_sample */

#ifdef EGL_NV_coverage_sample_resolve

static void _glewInfo_EGL_NV_coverage_sample_resolve (void)
{
  glewPrintExt("EGL_NV_coverage_sample_resolve", EGLEW_NV_coverage_sample_resolve, eglewIsSupported("EGL_NV_coverage_sample_resolve"), eglewGetExtension("EGL_NV_coverage_sample_resolve"));
}

#endif /* EGL_NV_coverage_sample_resolve */

#ifdef EGL_NV_cuda_event

static void _glewInfo_EGL_NV_cuda_event (void)
{
  glewPrintExt("EGL_NV_cuda_event", EGLEW_NV_cuda_event, eglewIsSupported("EGL_NV_cuda_event"), eglewGetExtension("EGL_NV_cuda_event"));
}

#endif /* EGL_NV_cuda_event */

#ifdef EGL_NV_depth_nonlinear

static void _glewInfo_EGL_NV_depth_nonlinear (void)
{
  glewPrintExt("EGL_NV_depth_nonlinear", EGLEW_NV_depth_nonlinear, eglewIsSupported("EGL_NV_depth_nonlinear"), eglewGetExtension("EGL_NV_depth_nonlinear"));
}

#endif /* EGL_NV_depth_nonlinear */

#ifdef EGL_NV_device_cuda

static void _glewInfo_EGL_NV_device_cuda (void)
{
  glewPrintExt("EGL_NV_device_cuda", EGLEW_NV_device_cuda, eglewIsSupported("EGL_NV_device_cuda"), eglewGetExtension("EGL_NV_device_cuda"));
}

#endif /* EGL_NV_device_cuda */

#ifdef EGL_NV_native_query

static void _glewInfo_EGL_NV_native_query (void)
{
  glewPrintExt("EGL_NV_native_query", EGLEW_NV_native_query, eglewIsSupported("EGL_NV_native_query"), eglewGetExtension("EGL_NV_native_query"));

  glewInfoFunc("eglQueryNativeDisplayNV", eglQueryNativeDisplayNV == NULL);
  glewInfoFunc("eglQueryNativePixmapNV", eglQueryNativePixmapNV == NULL);
  glewInfoFunc("eglQueryNativeWindowNV", eglQueryNativeWindowNV == NULL);
}

#endif /* EGL_NV_native_query */

#ifdef EGL_NV_post_convert_rounding

static void _glewInfo_EGL_NV_post_convert_rounding (void)
{
  glewPrintExt("EGL_NV_post_convert_rounding", EGLEW_NV_post_convert_rounding, eglewIsSupported("EGL_NV_post_convert_rounding"), eglewGetExtension("EGL_NV_post_convert_rounding"));
}

#endif /* EGL_NV_post_convert_rounding */

#ifdef EGL_NV_post_sub_buffer

static void _glewInfo_EGL_NV_post_sub_buffer (void)
{
  glewPrintExt("EGL_NV_post_sub_buffer", EGLEW_NV_post_sub_buffer, eglewIsSupported("EGL_NV_post_sub_buffer"), eglewGetExtension("EGL_NV_post_sub_buffer"));

  glewInfoFunc("eglPostSubBufferNV", eglPostSubBufferNV == NULL);
}

#endif /* EGL_NV_post_sub_buffer */

#ifdef EGL_NV_robustness_video_memory_purge

static void _glewInfo_EGL_NV_robustness_video_memory_purge (void)
{
  glewPrintExt("EGL_NV_robustness_video_memory_purge", EGLEW_NV_robustness_video_memory_purge, eglewIsSupported("EGL_NV_robustness_video_memory_purge"), eglewGetExtension("EGL_NV_robustness_video_memory_purge"));
}

#endif /* EGL_NV_robustness_video_memory_purge */

#ifdef EGL_NV_stream_consumer_gltexture_yuv

static void _glewInfo_EGL_NV_stream_consumer_gltexture_yuv (void)
{
  glewPrintExt("EGL_NV_stream_consumer_gltexture_yuv", EGLEW_NV_stream_consumer_gltexture_yuv, eglewIsSupported("EGL_NV_stream_consumer_gltexture_yuv"), eglewGetExtension("EGL_NV_stream_consumer_gltexture_yuv"));

  glewInfoFunc("eglStreamConsumerGLTextureExternalAttribsNV", eglStreamConsumerGLTextureExternalAttribsNV == NULL);
}

#endif /* EGL_NV_stream_consumer_gltexture_yuv */

#ifdef EGL_NV_stream_cross_display

static void _glewInfo_EGL_NV_stream_cross_display (void)
{
  glewPrintExt("EGL_NV_stream_cross_display", EGLEW_NV_stream_cross_display, eglewIsSupported("EGL_NV_stream_cross_display"), eglewGetExtension("EGL_NV_stream_cross_display"));
}

#endif /* EGL_NV_stream_cross_display */

#ifdef EGL_NV_stream_cross_object

static void _glewInfo_EGL_NV_stream_cross_object (void)
{
  glewPrintExt("EGL_NV_stream_cross_object", EGLEW_NV_stream_cross_object, eglewIsSupported("EGL_NV_stream_cross_object"), eglewGetExtension("EGL_NV_stream_cross_object"));
}

#endif /* EGL_NV_stream_cross_object */

#ifdef EGL_NV_stream_cross_partition

static void _glewInfo_EGL_NV_stream_cross_partition (void)
{
  glewPrintExt("EGL_NV_stream_cross_partition", EGLEW_NV_stream_cross_partition, eglewIsSupported("EGL_NV_stream_cross_partition"), eglewGetExtension("EGL_NV_stream_cross_partition"));
}

#endif /* EGL_NV_stream_cross_partition */

#ifdef EGL_NV_stream_cross_process

static void _glewInfo_EGL_NV_stream_cross_process (void)
{
  glewPrintExt("EGL_NV_stream_cross_process", EGLEW_NV_stream_cross_process, eglewIsSupported("EGL_NV_stream_cross_process"), eglewGetExtension("EGL_NV_stream_cross_process"));
}

#endif /* EGL_NV_stream_cross_process */

#ifdef EGL_NV_stream_cross_system

static void _glewInfo_EGL_NV_stream_cross_system (void)
{
  glewPrintExt("EGL_NV_stream_cross_system", EGLEW_NV_stream_cross_system, eglewIsSupported("EGL_NV_stream_cross_system"), eglewGetExtension("EGL_NV_stream_cross_system"));
}

#endif /* EGL_NV_stream_cross_system */

#ifdef EGL_NV_stream_fifo_next

static void _glewInfo_EGL_NV_stream_fifo_next (void)
{
  glewPrintExt("EGL_NV_stream_fifo_next", EGLEW_NV_stream_fifo_next, eglewIsSupported("EGL_NV_stream_fifo_next"), eglewGetExtension("EGL_NV_stream_fifo_next"));
}

#endif /* EGL_NV_stream_fifo_next */

#ifdef EGL_NV_stream_fifo_synchronous

static void _glewInfo_EGL_NV_stream_fifo_synchronous (void)
{
  glewPrintExt("EGL_NV_stream_fifo_synchronous", EGLEW_NV_stream_fifo_synchronous, eglewIsSupported("EGL_NV_stream_fifo_synchronous"), eglewGetExtension("EGL_NV_stream_fifo_synchronous"));
}

#endif /* EGL_NV_stream_fifo_synchronous */

#ifdef EGL_NV_stream_frame_limits

static void _glewInfo_EGL_NV_stream_frame_limits (void)
{
  glewPrintExt("EGL_NV_stream_frame_limits", EGLEW_NV_stream_frame_limits, eglewIsSupported("EGL_NV_stream_frame_limits"), eglewGetExtension("EGL_NV_stream_frame_limits"));
}

#endif /* EGL_NV_stream_frame_limits */

#ifdef EGL_NV_stream_metadata

static void _glewInfo_EGL_NV_stream_metadata (void)
{
  glewPrintExt("EGL_NV_stream_metadata", EGLEW_NV_stream_metadata, eglewIsSupported("EGL_NV_stream_metadata"), eglewGetExtension("EGL_NV_stream_metadata"));

  glewInfoFunc("eglQueryDisplayAttribNV", eglQueryDisplayAttribNV == NULL);
  glewInfoFunc("eglQueryStreamMetadataNV", eglQueryStreamMetadataNV == NULL);
  glewInfoFunc("eglSetStreamMetadataNV", eglSetStreamMetadataNV == NULL);
}

#endif /* EGL_NV_stream_metadata */

#ifdef EGL_NV_stream_remote

static void _glewInfo_EGL_NV_stream_remote (void)
{
  glewPrintExt("EGL_NV_stream_remote", EGLEW_NV_stream_remote, eglewIsSupported("EGL_NV_stream_remote"), eglewGetExtension("EGL_NV_stream_remote"));
}

#endif /* EGL_NV_stream_remote */

#ifdef EGL_NV_stream_reset

static void _glewInfo_EGL_NV_stream_reset (void)
{
  glewPrintExt("EGL_NV_stream_reset", EGLEW_NV_stream_reset, eglewIsSupported("EGL_NV_stream_reset"), eglewGetExtension("EGL_NV_stream_reset"));

  glewInfoFunc("eglResetStreamNV", eglResetStreamNV == NULL);
}

#endif /* EGL_NV_stream_reset */

#ifdef EGL_NV_stream_socket

static void _glewInfo_EGL_NV_stream_socket (void)
{
  glewPrintExt("EGL_NV_stream_socket", EGLEW_NV_stream_socket, eglewIsSupported("EGL_NV_stream_socket"), eglewGetExtension("EGL_NV_stream_socket"));
}

#endif /* EGL_NV_stream_socket */

#ifdef EGL_NV_stream_socket_inet

static void _glewInfo_EGL_NV_stream_socket_inet (void)
{
  glewPrintExt("EGL_NV_stream_socket_inet", EGLEW_NV_stream_socket_inet, eglewIsSupported("EGL_NV_stream_socket_inet"), eglewGetExtension("EGL_NV_stream_socket_inet"));
}

#endif /* EGL_NV_stream_socket_inet */

#ifdef EGL_NV_stream_socket_unix

static void _glewInfo_EGL_NV_stream_socket_unix (void)
{
  glewPrintExt("EGL_NV_stream_socket_unix", EGLEW_NV_stream_socket_unix, eglewIsSupported("EGL_NV_stream_socket_unix"), eglewGetExtension("EGL_NV_stream_socket_unix"));
}

#endif /* EGL_NV_stream_socket_unix */

#ifdef EGL_NV_stream_sync

static void _glewInfo_EGL_NV_stream_sync (void)
{
  glewPrintExt("EGL_NV_stream_sync", EGLEW_NV_stream_sync, eglewIsSupported("EGL_NV_stream_sync"), eglewGetExtension("EGL_NV_stream_sync"));

  glewInfoFunc("eglCreateStreamSyncNV", eglCreateStreamSyncNV == NULL);
}

#endif /* EGL_NV_stream_sync */

#ifdef EGL_NV_sync

static void _glewInfo_EGL_NV_sync (void)
{
  glewPrintExt("EGL_NV_sync", EGLEW_NV_sync, eglewIsSupported("EGL_NV_sync"), eglewGetExtension("EGL_NV_sync"));

  glewInfoFunc("eglClientWaitSyncNV", eglClientWaitSyncNV == NULL);
  glewInfoFunc("eglCreateFenceSyncNV", eglCreateFenceSyncNV == NULL);
  glewInfoFunc("eglDestroySyncNV", eglDestroySyncNV == NULL);
  glewInfoFunc("eglFenceNV", eglFenceNV == NULL);
  glewInfoFunc("eglGetSyncAttribNV", eglGetSyncAttribNV == NULL);
  glewInfoFunc("eglSignalSyncNV", eglSignalSyncNV == NULL);
}

#endif /* EGL_NV_sync */

#ifdef EGL_NV_system_time

static void _glewInfo_EGL_NV_system_time (void)
{
  glewPrintExt("EGL_NV_system_time", EGLEW_NV_system_time, eglewIsSupported("EGL_NV_system_time"), eglewGetExtension("EGL_NV_system_time"));

  glewInfoFunc("eglGetSystemTimeFrequencyNV", eglGetSystemTimeFrequencyNV == NULL);
  glewInfoFunc("eglGetSystemTimeNV", eglGetSystemTimeNV == NULL);
}

#endif /* EGL_NV_system_time */

#ifdef EGL_TIZEN_image_native_buffer

static void _glewInfo_EGL_TIZEN_image_native_buffer (void)
{
  glewPrintExt("EGL_TIZEN_image_native_buffer", EGLEW_TIZEN_image_native_buffer, eglewIsSupported("EGL_TIZEN_image_native_buffer"), eglewGetExtension("EGL_TIZEN_image_native_buffer"));
}

#endif /* EGL_TIZEN_image_native_buffer */

#ifdef EGL_TIZEN_image_native_surface

static void _glewInfo_EGL_TIZEN_image_native_surface (void)
{
  glewPrintExt("EGL_TIZEN_image_native_surface", EGLEW_TIZEN_image_native_surface, eglewIsSupported("EGL_TIZEN_image_native_surface"), eglewGetExtension("EGL_TIZEN_image_native_surface"));
}

#endif /* EGL_TIZEN_image_native_surface */

#elif _WIN32

#ifdef WGL_3DFX_multisample

static void _glewInfo_WGL_3DFX_multisample (void)
{
  glewPrintExt("WGL_3DFX_multisample", WGLEW_3DFX_multisample, wglewIsSupported("WGL_3DFX_multisample"), wglewGetExtension("WGL_3DFX_multisample"));
}

#endif /* WGL_3DFX_multisample */

#ifdef WGL_3DL_stereo_control

static void _glewInfo_WGL_3DL_stereo_control (void)
{
  glewPrintExt("WGL_3DL_stereo_control", WGLEW_3DL_stereo_control, wglewIsSupported("WGL_3DL_stereo_control"), wglewGetExtension("WGL_3DL_stereo_control"));

  glewInfoFunc("wglSetStereoEmitterState3DL", wglSetStereoEmitterState3DL == NULL);
}

#endif /* WGL_3DL_stereo_control */

#ifdef WGL_AMD_gpu_association

static void _glewInfo_WGL_AMD_gpu_association (void)
{
  glewPrintExt("WGL_AMD_gpu_association", WGLEW_AMD_gpu_association, wglewIsSupported("WGL_AMD_gpu_association"), wglewGetExtension("WGL_AMD_gpu_association"));

  glewInfoFunc("wglBlitContextFramebufferAMD", wglBlitContextFramebufferAMD == NULL);
  glewInfoFunc("wglCreateAssociatedContextAMD", wglCreateAssociatedContextAMD == NULL);
  glewInfoFunc("wglCreateAssociatedContextAttribsAMD", wglCreateAssociatedContextAttribsAMD == NULL);
  glewInfoFunc("wglDeleteAssociatedContextAMD", wglDeleteAssociatedContextAMD == NULL);
  glewInfoFunc("wglGetContextGPUIDAMD", wglGetContextGPUIDAMD == NULL);
  glewInfoFunc("wglGetCurrentAssociatedContextAMD", wglGetCurrentAssociatedContextAMD == NULL);
  glewInfoFunc("wglGetGPUIDsAMD", wglGetGPUIDsAMD == NULL);
  glewInfoFunc("wglGetGPUInfoAMD", wglGetGPUInfoAMD == NULL);
  glewInfoFunc("wglMakeAssociatedContextCurrentAMD", wglMakeAssociatedContextCurrentAMD == NULL);
}

#endif /* WGL_AMD_gpu_association */

#ifdef WGL_ARB_buffer_region

static void _glewInfo_WGL_ARB_buffer_region (void)
{
  glewPrintExt("WGL_ARB_buffer_region", WGLEW_ARB_buffer_region, wglewIsSupported("WGL_ARB_buffer_region"), wglewGetExtension("WGL_ARB_buffer_region"));

  glewInfoFunc("wglCreateBufferRegionARB", wglCreateBufferRegionARB == NULL);
  glewInfoFunc("wglDeleteBufferRegionARB", wglDeleteBufferRegionARB == NULL);
  glewInfoFunc("wglRestoreBufferRegionARB", wglRestoreBufferRegionARB == NULL);
  glewInfoFunc("wglSaveBufferRegionARB", wglSaveBufferRegionARB == NULL);
}

#endif /* WGL_ARB_buffer_region */

#ifdef WGL_ARB_context_flush_control

static void _glewInfo_WGL_ARB_context_flush_control (void)
{
  glewPrintExt("WGL_ARB_context_flush_control", WGLEW_ARB_context_flush_control, wglewIsSupported("WGL_ARB_context_flush_control"), wglewGetExtension("WGL_ARB_context_flush_control"));
}

#endif /* WGL_ARB_context_flush_control */

#ifdef WGL_ARB_create_context

static void _glewInfo_WGL_ARB_create_context (void)
{
  glewPrintExt("WGL_ARB_create_context", WGLEW_ARB_create_context, wglewIsSupported("WGL_ARB_create_context"), wglewGetExtension("WGL_ARB_create_context"));

  glewInfoFunc("wglCreateContextAttribsARB", wglCreateContextAttribsARB == NULL);
}

#endif /* WGL_ARB_create_context */

#ifdef WGL_ARB_create_context_no_error

static void _glewInfo_WGL_ARB_create_context_no_error (void)
{
  glewPrintExt("WGL_ARB_create_context_no_error", WGLEW_ARB_create_context_no_error, wglewIsSupported("WGL_ARB_create_context_no_error"), wglewGetExtension("WGL_ARB_create_context_no_error"));
}

#endif /* WGL_ARB_create_context_no_error */

#ifdef WGL_ARB_create_context_profile

static void _glewInfo_WGL_ARB_create_context_profile (void)
{
  glewPrintExt("WGL_ARB_create_context_profile", WGLEW_ARB_create_context_profile, wglewIsSupported("WGL_ARB_create_context_profile"), wglewGetExtension("WGL_ARB_create_context_profile"));
}

#endif /* WGL_ARB_create_context_profile */

#ifdef WGL_ARB_create_context_robustness

static void _glewInfo_WGL_ARB_create_context_robustness (void)
{
  glewPrintExt("WGL_ARB_create_context_robustness", WGLEW_ARB_create_context_robustness, wglewIsSupported("WGL_ARB_create_context_robustness"), wglewGetExtension("WGL_ARB_create_context_robustness"));
}

#endif /* WGL_ARB_create_context_robustness */

#ifdef WGL_ARB_extensions_string

static void _glewInfo_WGL_ARB_extensions_string (void)
{
  glewPrintExt("WGL_ARB_extensions_string", WGLEW_ARB_extensions_string, wglewIsSupported("WGL_ARB_extensions_string"), wglewGetExtension("WGL_ARB_extensions_string"));

  glewInfoFunc("wglGetExtensionsStringARB", wglGetExtensionsStringARB == NULL);
}

#endif /* WGL_ARB_extensions_string */

#ifdef WGL_ARB_framebuffer_sRGB

static void _glewInfo_WGL_ARB_framebuffer_sRGB (void)
{
  glewPrintExt("WGL_ARB_framebuffer_sRGB", WGLEW_ARB_framebuffer_sRGB, wglewIsSupported("WGL_ARB_framebuffer_sRGB"), wglewGetExtension("WGL_ARB_framebuffer_sRGB"));
}

#endif /* WGL_ARB_framebuffer_sRGB */

#ifdef WGL_ARB_make_current_read

static void _glewInfo_WGL_ARB_make_current_read (void)
{
  glewPrintExt("WGL_ARB_make_current_read", WGLEW_ARB_make_current_read, wglewIsSupported("WGL_ARB_make_current_read"), wglewGetExtension("WGL_ARB_make_current_read"));

  glewInfoFunc("wglGetCurrentReadDCARB", wglGetCurrentReadDCARB == NULL);
  glewInfoFunc("wglMakeContextCurrentARB", wglMakeContextCurrentARB == NULL);
}

#endif /* WGL_ARB_make_current_read */

#ifdef WGL_ARB_multisample

static void _glewInfo_WGL_ARB_multisample (void)
{
  glewPrintExt("WGL_ARB_multisample", WGLEW_ARB_multisample, wglewIsSupported("WGL_ARB_multisample"), wglewGetExtension("WGL_ARB_multisample"));
}

#endif /* WGL_ARB_multisample */

#ifdef WGL_ARB_pbuffer

static void _glewInfo_WGL_ARB_pbuffer (void)
{
  glewPrintExt("WGL_ARB_pbuffer", WGLEW_ARB_pbuffer, wglewIsSupported("WGL_ARB_pbuffer"), wglewGetExtension("WGL_ARB_pbuffer"));

  glewInfoFunc("wglCreatePbufferARB", wglCreatePbufferARB == NULL);
  glewInfoFunc("wglDestroyPbufferARB", wglDestroyPbufferARB == NULL);
  glewInfoFunc("wglGetPbufferDCARB", wglGetPbufferDCARB == NULL);
  glewInfoFunc("wglQueryPbufferARB", wglQueryPbufferARB == NULL);
  glewInfoFunc("wglReleasePbufferDCARB", wglReleasePbufferDCARB == NULL);
}

#endif /* WGL_ARB_pbuffer */

#ifdef WGL_ARB_pixel_format

static void _glewInfo_WGL_ARB_pixel_format (void)
{
  glewPrintExt("WGL_ARB_pixel_format", WGLEW_ARB_pixel_format, wglewIsSupported("WGL_ARB_pixel_format"), wglewGetExtension("WGL_ARB_pixel_format"));

  glewInfoFunc("wglChoosePixelFormatARB", wglChoosePixelFormatARB == NULL);
  glewInfoFunc("wglGetPixelFormatAttribfvARB", wglGetPixelFormatAttribfvARB == NULL);
  glewInfoFunc("wglGetPixelFormatAttribivARB", wglGetPixelFormatAttribivARB == NULL);
}

#endif /* WGL_ARB_pixel_format */

#ifdef WGL_ARB_pixel_format_float

static void _glewInfo_WGL_ARB_pixel_format_float (void)
{
  glewPrintExt("WGL_ARB_pixel_format_float", WGLEW_ARB_pixel_format_float, wglewIsSupported("WGL_ARB_pixel_format_float"), wglewGetExtension("WGL_ARB_pixel_format_float"));
}

#endif /* WGL_ARB_pixel_format_float */

#ifdef WGL_ARB_render_texture

static void _glewInfo_WGL_ARB_render_texture (void)
{
  glewPrintExt("WGL_ARB_render_texture", WGLEW_ARB_render_texture, wglewIsSupported("WGL_ARB_render_texture"), wglewGetExtension("WGL_ARB_render_texture"));

  glewInfoFunc("wglBindTexImageARB", wglBindTexImageARB == NULL);
  glewInfoFunc("wglReleaseTexImageARB", wglReleaseTexImageARB == NULL);
  glewInfoFunc("wglSetPbufferAttribARB", wglSetPbufferAttribARB == NULL);
}

#endif /* WGL_ARB_render_texture */

#ifdef WGL_ARB_robustness_application_isolation

static void _glewInfo_WGL_ARB_robustness_application_isolation (void)
{
  glewPrintExt("WGL_ARB_robustness_application_isolation", WGLEW_ARB_robustness_application_isolation, wglewIsSupported("WGL_ARB_robustness_application_isolation"), wglewGetExtension("WGL_ARB_robustness_application_isolation"));
}

#endif /* WGL_ARB_robustness_application_isolation */

#ifdef WGL_ARB_robustness_share_group_isolation

static void _glewInfo_WGL_ARB_robustness_share_group_isolation (void)
{
  glewPrintExt("WGL_ARB_robustness_share_group_isolation", WGLEW_ARB_robustness_share_group_isolation, wglewIsSupported("WGL_ARB_robustness_share_group_isolation"), wglewGetExtension("WGL_ARB_robustness_share_group_isolation"));
}

#endif /* WGL_ARB_robustness_share_group_isolation */

#ifdef WGL_ATI_pixel_format_float

static void _glewInfo_WGL_ATI_pixel_format_float (void)
{
  glewPrintExt("WGL_ATI_pixel_format_float", WGLEW_ATI_pixel_format_float, wglewIsSupported("WGL_ATI_pixel_format_float"), wglewGetExtension("WGL_ATI_pixel_format_float"));
}

#endif /* WGL_ATI_pixel_format_float */

#ifdef WGL_ATI_render_texture_rectangle

static void _glewInfo_WGL_ATI_render_texture_rectangle (void)
{
  glewPrintExt("WGL_ATI_render_texture_rectangle", WGLEW_ATI_render_texture_rectangle, wglewIsSupported("WGL_ATI_render_texture_rectangle"), wglewGetExtension("WGL_ATI_render_texture_rectangle"));
}

#endif /* WGL_ATI_render_texture_rectangle */

#ifdef WGL_EXT_colorspace

static void _glewInfo_WGL_EXT_colorspace (void)
{
  glewPrintExt("WGL_EXT_colorspace", WGLEW_EXT_colorspace, wglewIsSupported("WGL_EXT_colorspace"), wglewGetExtension("WGL_EXT_colorspace"));
}

#endif /* WGL_EXT_colorspace */

#ifdef WGL_EXT_create_context_es2_profile

static void _glewInfo_WGL_EXT_create_context_es2_profile (void)
{
  glewPrintExt("WGL_EXT_create_context_es2_profile", WGLEW_EXT_create_context_es2_profile, wglewIsSupported("WGL_EXT_create_context_es2_profile"), wglewGetExtension("WGL_EXT_create_context_es2_profile"));
}

#endif /* WGL_EXT_create_context_es2_profile */

#ifdef WGL_EXT_create_context_es_profile

static void _glewInfo_WGL_EXT_create_context_es_profile (void)
{
  glewPrintExt("WGL_EXT_create_context_es_profile", WGLEW_EXT_create_context_es_profile, wglewIsSupported("WGL_EXT_create_context_es_profile"), wglewGetExtension("WGL_EXT_create_context_es_profile"));
}

#endif /* WGL_EXT_create_context_es_profile */

#ifdef WGL_EXT_depth_float

static void _glewInfo_WGL_EXT_depth_float (void)
{
  glewPrintExt("WGL_EXT_depth_float", WGLEW_EXT_depth_float, wglewIsSupported("WGL_EXT_depth_float"), wglewGetExtension("WGL_EXT_depth_float"));
}

#endif /* WGL_EXT_depth_float */

#ifdef WGL_EXT_display_color_table

static void _glewInfo_WGL_EXT_display_color_table (void)
{
  glewPrintExt("WGL_EXT_display_color_table", WGLEW_EXT_display_color_table, wglewIsSupported("WGL_EXT_display_color_table"), wglewGetExtension("WGL_EXT_display_color_table"));

  glewInfoFunc("wglBindDisplayColorTableEXT", wglBindDisplayColorTableEXT == NULL);
  glewInfoFunc("wglCreateDisplayColorTableEXT", wglCreateDisplayColorTableEXT == NULL);
  glewInfoFunc("wglDestroyDisplayColorTableEXT", wglDestroyDisplayColorTableEXT == NULL);
  glewInfoFunc("wglLoadDisplayColorTableEXT", wglLoadDisplayColorTableEXT == NULL);
}

#endif /* WGL_EXT_display_color_table */

#ifdef WGL_EXT_extensions_string

static void _glewInfo_WGL_EXT_extensions_string (void)
{
  glewPrintExt("WGL_EXT_extensions_string", WGLEW_EXT_extensions_string, wglewIsSupported("WGL_EXT_extensions_string"), wglewGetExtension("WGL_EXT_extensions_string"));

  glewInfoFunc("wglGetExtensionsStringEXT", wglGetExtensionsStringEXT == NULL);
}

#endif /* WGL_EXT_extensions_string */

#ifdef WGL_EXT_framebuffer_sRGB

static void _glewInfo_WGL_EXT_framebuffer_sRGB (void)
{
  glewPrintExt("WGL_EXT_framebuffer_sRGB", WGLEW_EXT_framebuffer_sRGB, wglewIsSupported("WGL_EXT_framebuffer_sRGB"), wglewGetExtension("WGL_EXT_framebuffer_sRGB"));
}

#endif /* WGL_EXT_framebuffer_sRGB */

#ifdef WGL_EXT_make_current_read

static void _glewInfo_WGL_EXT_make_current_read (void)
{
  glewPrintExt("WGL_EXT_make_current_read", WGLEW_EXT_make_current_read, wglewIsSupported("WGL_EXT_make_current_read"), wglewGetExtension("WGL_EXT_make_current_read"));

  glewInfoFunc("wglGetCurrentReadDCEXT", wglGetCurrentReadDCEXT == NULL);
  glewInfoFunc("wglMakeContextCurrentEXT", wglMakeContextCurrentEXT == NULL);
}

#endif /* WGL_EXT_make_current_read */

#ifdef WGL_EXT_multisample

static void _glewInfo_WGL_EXT_multisample (void)
{
  glewPrintExt("WGL_EXT_multisample", WGLEW_EXT_multisample, wglewIsSupported("WGL_EXT_multisample"), wglewGetExtension("WGL_EXT_multisample"));
}

#endif /* WGL_EXT_multisample */

#ifdef WGL_EXT_pbuffer

static void _glewInfo_WGL_EXT_pbuffer (void)
{
  glewPrintExt("WGL_EXT_pbuffer", WGLEW_EXT_pbuffer, wglewIsSupported("WGL_EXT_pbuffer"), wglewGetExtension("WGL_EXT_pbuffer"));

  glewInfoFunc("wglCreatePbufferEXT", wglCreatePbufferEXT == NULL);
  glewInfoFunc("wglDestroyPbufferEXT", wglDestroyPbufferEXT == NULL);
  glewInfoFunc("wglGetPbufferDCEXT", wglGetPbufferDCEXT == NULL);
  glewInfoFunc("wglQueryPbufferEXT", wglQueryPbufferEXT == NULL);
  glewInfoFunc("wglReleasePbufferDCEXT", wglReleasePbufferDCEXT == NULL);
}

#endif /* WGL_EXT_pbuffer */

#ifdef WGL_EXT_pixel_format

static void _glewInfo_WGL_EXT_pixel_format (void)
{
  glewPrintExt("WGL_EXT_pixel_format", WGLEW_EXT_pixel_format, wglewIsSupported("WGL_EXT_pixel_format"), wglewGetExtension("WGL_EXT_pixel_format"));

  glewInfoFunc("wglChoosePixelFormatEXT", wglChoosePixelFormatEXT == NULL);
  glewInfoFunc("wglGetPixelFormatAttribfvEXT", wglGetPixelFormatAttribfvEXT == NULL);
  glewInfoFunc("wglGetPixelFormatAttribivEXT", wglGetPixelFormatAttribivEXT == NULL);
}

#endif /* WGL_EXT_pixel_format */

#ifdef WGL_EXT_pixel_format_packed_float

static void _glewInfo_WGL_EXT_pixel_format_packed_float (void)
{
  glewPrintExt("WGL_EXT_pixel_format_packed_float", WGLEW_EXT_pixel_format_packed_float, wglewIsSupported("WGL_EXT_pixel_format_packed_float"), wglewGetExtension("WGL_EXT_pixel_format_packed_float"));
}

#endif /* WGL_EXT_pixel_format_packed_float */

#ifdef WGL_EXT_swap_control

static void _glewInfo_WGL_EXT_swap_control (void)
{
  glewPrintExt("WGL_EXT_swap_control", WGLEW_EXT_swap_control, wglewIsSupported("WGL_EXT_swap_control"), wglewGetExtension("WGL_EXT_swap_control"));

  glewInfoFunc("wglGetSwapIntervalEXT", wglGetSwapIntervalEXT == NULL);
  glewInfoFunc("wglSwapIntervalEXT", wglSwapIntervalEXT == NULL);
}

#endif /* WGL_EXT_swap_control */

#ifdef WGL_EXT_swap_control_tear

static void _glewInfo_WGL_EXT_swap_control_tear (void)
{
  glewPrintExt("WGL_EXT_swap_control_tear", WGLEW_EXT_swap_control_tear, wglewIsSupported("WGL_EXT_swap_control_tear"), wglewGetExtension("WGL_EXT_swap_control_tear"));
}

#endif /* WGL_EXT_swap_control_tear */

#ifdef WGL_I3D_digital_video_control

static void _glewInfo_WGL_I3D_digital_video_control (void)
{
  glewPrintExt("WGL_I3D_digital_video_control", WGLEW_I3D_digital_video_control, wglewIsSupported("WGL_I3D_digital_video_control"), wglewGetExtension("WGL_I3D_digital_video_control"));

  glewInfoFunc("wglGetDigitalVideoParametersI3D", wglGetDigitalVideoParametersI3D == NULL);
  glewInfoFunc("wglSetDigitalVideoParametersI3D", wglSetDigitalVideoParametersI3D == NULL);
}

#endif /* WGL_I3D_digital_video_control */

#ifdef WGL_I3D_gamma

static void _glewInfo_WGL_I3D_gamma (void)
{
  glewPrintExt("WGL_I3D_gamma", WGLEW_I3D_gamma, wglewIsSupported("WGL_I3D_gamma"), wglewGetExtension("WGL_I3D_gamma"));

  glewInfoFunc("wglGetGammaTableI3D", wglGetGammaTableI3D == NULL);
  glewInfoFunc("wglGetGammaTableParametersI3D", wglGetGammaTableParametersI3D == NULL);
  glewInfoFunc("wglSetGammaTableI3D", wglSetGammaTableI3D == NULL);
  glewInfoFunc("wglSetGammaTableParametersI3D", wglSetGammaTableParametersI3D == NULL);
}

#endif /* WGL_I3D_gamma */

#ifdef WGL_I3D_genlock

static void _glewInfo_WGL_I3D_genlock (void)
{
  glewPrintExt("WGL_I3D_genlock", WGLEW_I3D_genlock, wglewIsSupported("WGL_I3D_genlock"), wglewGetExtension("WGL_I3D_genlock"));

  glewInfoFunc("wglDisableGenlockI3D", wglDisableGenlockI3D == NULL);
  glewInfoFunc("wglEnableGenlockI3D", wglEnableGenlockI3D == NULL);
  glewInfoFunc("wglGenlockSampleRateI3D", wglGenlockSampleRateI3D == NULL);
  glewInfoFunc("wglGenlockSourceDelayI3D", wglGenlockSourceDelayI3D == NULL);
  glewInfoFunc("wglGenlockSourceEdgeI3D", wglGenlockSourceEdgeI3D == NULL);
  glewInfoFunc("wglGenlockSourceI3D", wglGenlockSourceI3D == NULL);
  glewInfoFunc("wglGetGenlockSampleRateI3D", wglGetGenlockSampleRateI3D == NULL);
  glewInfoFunc("wglGetGenlockSourceDelayI3D", wglGetGenlockSourceDelayI3D == NULL);
  glewInfoFunc("wglGetGenlockSourceEdgeI3D", wglGetGenlockSourceEdgeI3D == NULL);
  glewInfoFunc("wglGetGenlockSourceI3D", wglGetGenlockSourceI3D == NULL);
  glewInfoFunc("wglIsEnabledGenlockI3D", wglIsEnabledGenlockI3D == NULL);
  glewInfoFunc("wglQueryGenlockMaxSourceDelayI3D", wglQueryGenlockMaxSourceDelayI3D == NULL);
}

#endif /* WGL_I3D_genlock */

#ifdef WGL_I3D_image_buffer

static void _glewInfo_WGL_I3D_image_buffer (void)
{
  glewPrintExt("WGL_I3D_image_buffer", WGLEW_I3D_image_buffer, wglewIsSupported("WGL_I3D_image_buffer"), wglewGetExtension("WGL_I3D_image_buffer"));

  glewInfoFunc("wglAssociateImageBufferEventsI3D", wglAssociateImageBufferEventsI3D == NULL);
  glewInfoFunc("wglCreateImageBufferI3D", wglCreateImageBufferI3D == NULL);
  glewInfoFunc("wglDestroyImageBufferI3D", wglDestroyImageBufferI3D == NULL);
  glewInfoFunc("wglReleaseImageBufferEventsI3D", wglReleaseImageBufferEventsI3D == NULL);
}

#endif /* WGL_I3D_image_buffer */

#ifdef WGL_I3D_swap_frame_lock

static void _glewInfo_WGL_I3D_swap_frame_lock (void)
{
  glewPrintExt("WGL_I3D_swap_frame_lock", WGLEW_I3D_swap_frame_lock, wglewIsSupported("WGL_I3D_swap_frame_lock"), wglewGetExtension("WGL_I3D_swap_frame_lock"));

  glewInfoFunc("wglDisableFrameLockI3D", wglDisableFrameLockI3D == NULL);
  glewInfoFunc("wglEnableFrameLockI3D", wglEnableFrameLockI3D == NULL);
  glewInfoFunc("wglIsEnabledFrameLockI3D", wglIsEnabledFrameLockI3D == NULL);
  glewInfoFunc("wglQueryFrameLockMasterI3D", wglQueryFrameLockMasterI3D == NULL);
}

#endif /* WGL_I3D_swap_frame_lock */

#ifdef WGL_I3D_swap_frame_usage

static void _glewInfo_WGL_I3D_swap_frame_usage (void)
{
  glewPrintExt("WGL_I3D_swap_frame_usage", WGLEW_I3D_swap_frame_usage, wglewIsSupported("WGL_I3D_swap_frame_usage"), wglewGetExtension("WGL_I3D_swap_frame_usage"));

  glewInfoFunc("wglBeginFrameTrackingI3D", wglBeginFrameTrackingI3D == NULL);
  glewInfoFunc("wglEndFrameTrackingI3D", wglEndFrameTrackingI3D == NULL);
  glewInfoFunc("wglGetFrameUsageI3D", wglGetFrameUsageI3D == NULL);
  glewInfoFunc("wglQueryFrameTrackingI3D", wglQueryFrameTrackingI3D == NULL);
}

#endif /* WGL_I3D_swap_frame_usage */

#ifdef WGL_NV_DX_interop

static void _glewInfo_WGL_NV_DX_interop (void)
{
  glewPrintExt("WGL_NV_DX_interop", WGLEW_NV_DX_interop, wglewIsSupported("WGL_NV_DX_interop"), wglewGetExtension("WGL_NV_DX_interop"));

  glewInfoFunc("wglDXCloseDeviceNV", wglDXCloseDeviceNV == NULL);
  glewInfoFunc("wglDXLockObjectsNV", wglDXLockObjectsNV == NULL);
  glewInfoFunc("wglDXObjectAccessNV", wglDXObjectAccessNV == NULL);
  glewInfoFunc("wglDXOpenDeviceNV", wglDXOpenDeviceNV == NULL);
  glewInfoFunc("wglDXRegisterObjectNV", wglDXRegisterObjectNV == NULL);
  glewInfoFunc("wglDXSetResourceShareHandleNV", wglDXSetResourceShareHandleNV == NULL);
  glewInfoFunc("wglDXUnlockObjectsNV", wglDXUnlockObjectsNV == NULL);
  glewInfoFunc("wglDXUnregisterObjectNV", wglDXUnregisterObjectNV == NULL);
}

#endif /* WGL_NV_DX_interop */

#ifdef WGL_NV_DX_interop2

static void _glewInfo_WGL_NV_DX_interop2 (void)
{
  glewPrintExt("WGL_NV_DX_interop2", WGLEW_NV_DX_interop2, wglewIsSupported("WGL_NV_DX_interop2"), wglewGetExtension("WGL_NV_DX_interop2"));
}

#endif /* WGL_NV_DX_interop2 */

#ifdef WGL_NV_copy_image

static void _glewInfo_WGL_NV_copy_image (void)
{
  glewPrintExt("WGL_NV_copy_image", WGLEW_NV_copy_image, wglewIsSupported("WGL_NV_copy_image"), wglewGetExtension("WGL_NV_copy_image"));

  glewInfoFunc("wglCopyImageSubDataNV", wglCopyImageSubDataNV == NULL);
}

#endif /* WGL_NV_copy_image */

#ifdef WGL_NV_delay_before_swap

static void _glewInfo_WGL_NV_delay_before_swap (void)
{
  glewPrintExt("WGL_NV_delay_before_swap", WGLEW_NV_delay_before_swap, wglewIsSupported("WGL_NV_delay_before_swap"), wglewGetExtension("WGL_NV_delay_before_swap"));

  glewInfoFunc("wglDelayBeforeSwapNV", wglDelayBeforeSwapNV == NULL);
}

#endif /* WGL_NV_delay_before_swap */

#ifdef WGL_NV_float_buffer

static void _glewInfo_WGL_NV_float_buffer (void)
{
  glewPrintExt("WGL_NV_float_buffer", WGLEW_NV_float_buffer, wglewIsSupported("WGL_NV_float_buffer"), wglewGetExtension("WGL_NV_float_buffer"));
}

#endif /* WGL_NV_float_buffer */

#ifdef WGL_NV_gpu_affinity

static void _glewInfo_WGL_NV_gpu_affinity (void)
{
  glewPrintExt("WGL_NV_gpu_affinity", WGLEW_NV_gpu_affinity, wglewIsSupported("WGL_NV_gpu_affinity"), wglewGetExtension("WGL_NV_gpu_affinity"));

  glewInfoFunc("wglCreateAffinityDCNV", wglCreateAffinityDCNV == NULL);
  glewInfoFunc("wglDeleteDCNV", wglDeleteDCNV == NULL);
  glewInfoFunc("wglEnumGpuDevicesNV", wglEnumGpuDevicesNV == NULL);
  glewInfoFunc("wglEnumGpusFromAffinityDCNV", wglEnumGpusFromAffinityDCNV == NULL);
  glewInfoFunc("wglEnumGpusNV", wglEnumGpusNV == NULL);
}

#endif /* WGL_NV_gpu_affinity */

#ifdef WGL_NV_multisample_coverage

static void _glewInfo_WGL_NV_multisample_coverage (void)
{
  glewPrintExt("WGL_NV_multisample_coverage", WGLEW_NV_multisample_coverage, wglewIsSupported("WGL_NV_multisample_coverage"), wglewGetExtension("WGL_NV_multisample_coverage"));
}

#endif /* WGL_NV_multisample_coverage */

#ifdef WGL_NV_present_video

static void _glewInfo_WGL_NV_present_video (void)
{
  glewPrintExt("WGL_NV_present_video", WGLEW_NV_present_video, wglewIsSupported("WGL_NV_present_video"), wglewGetExtension("WGL_NV_present_video"));

  glewInfoFunc("wglBindVideoDeviceNV", wglBindVideoDeviceNV == NULL);
  glewInfoFunc("wglEnumerateVideoDevicesNV", wglEnumerateVideoDevicesNV == NULL);
  glewInfoFunc("wglQueryCurrentContextNV", wglQueryCurrentContextNV == NULL);
}

#endif /* WGL_NV_present_video */

#ifdef WGL_NV_render_depth_texture

static void _glewInfo_WGL_NV_render_depth_texture (void)
{
  glewPrintExt("WGL_NV_render_depth_texture", WGLEW_NV_render_depth_texture, wglewIsSupported("WGL_NV_render_depth_texture"), wglewGetExtension("WGL_NV_render_depth_texture"));
}

#endif /* WGL_NV_render_depth_texture */

#ifdef WGL_NV_render_texture_rectangle

static void _glewInfo_WGL_NV_render_texture_rectangle (void)
{
  glewPrintExt("WGL_NV_render_texture_rectangle", WGLEW_NV_render_texture_rectangle, wglewIsSupported("WGL_NV_render_texture_rectangle"), wglewGetExtension("WGL_NV_render_texture_rectangle"));
}

#endif /* WGL_NV_render_texture_rectangle */

#ifdef WGL_NV_swap_group

static void _glewInfo_WGL_NV_swap_group (void)
{
  glewPrintExt("WGL_NV_swap_group", WGLEW_NV_swap_group, wglewIsSupported("WGL_NV_swap_group"), wglewGetExtension("WGL_NV_swap_group"));

  glewInfoFunc("wglBindSwapBarrierNV", wglBindSwapBarrierNV == NULL);
  glewInfoFunc("wglJoinSwapGroupNV", wglJoinSwapGroupNV == NULL);
  glewInfoFunc("wglQueryFrameCountNV", wglQueryFrameCountNV == NULL);
  glewInfoFunc("wglQueryMaxSwapGroupsNV", wglQueryMaxSwapGroupsNV == NULL);
  glewInfoFunc("wglQuerySwapGroupNV", wglQuerySwapGroupNV == NULL);
  glewInfoFunc("wglResetFrameCountNV", wglResetFrameCountNV == NULL);
}

#endif /* WGL_NV_swap_group */

#ifdef WGL_NV_vertex_array_range

static void _glewInfo_WGL_NV_vertex_array_range (void)
{
  glewPrintExt("WGL_NV_vertex_array_range", WGLEW_NV_vertex_array_range, wglewIsSupported("WGL_NV_vertex_array_range"), wglewGetExtension("WGL_NV_vertex_array_range"));

  glewInfoFunc("wglAllocateMemoryNV", wglAllocateMemoryNV == NULL);
  glewInfoFunc("wglFreeMemoryNV", wglFreeMemoryNV == NULL);
}

#endif /* WGL_NV_vertex_array_range */

#ifdef WGL_NV_video_capture

static void _glewInfo_WGL_NV_video_capture (void)
{
  glewPrintExt("WGL_NV_video_capture", WGLEW_NV_video_capture, wglewIsSupported("WGL_NV_video_capture"), wglewGetExtension("WGL_NV_video_capture"));

  glewInfoFunc("wglBindVideoCaptureDeviceNV", wglBindVideoCaptureDeviceNV == NULL);
  glewInfoFunc("wglEnumerateVideoCaptureDevicesNV", wglEnumerateVideoCaptureDevicesNV == NULL);
  glewInfoFunc("wglLockVideoCaptureDeviceNV", wglLockVideoCaptureDeviceNV == NULL);
  glewInfoFunc("wglQueryVideoCaptureDeviceNV", wglQueryVideoCaptureDeviceNV == NULL);
  glewInfoFunc("wglReleaseVideoCaptureDeviceNV", wglReleaseVideoCaptureDeviceNV == NULL);
}

#endif /* WGL_NV_video_capture */

#ifdef WGL_NV_video_output

static void _glewInfo_WGL_NV_video_output (void)
{
  glewPrintExt("WGL_NV_video_output", WGLEW_NV_video_output, wglewIsSupported("WGL_NV_video_output"), wglewGetExtension("WGL_NV_video_output"));

  glewInfoFunc("wglBindVideoImageNV", wglBindVideoImageNV == NULL);
  glewInfoFunc("wglGetVideoDeviceNV", wglGetVideoDeviceNV == NULL);
  glewInfoFunc("wglGetVideoInfoNV", wglGetVideoInfoNV == NULL);
  glewInfoFunc("wglReleaseVideoDeviceNV", wglReleaseVideoDeviceNV == NULL);
  glewInfoFunc("wglReleaseVideoImageNV", wglReleaseVideoImageNV == NULL);
  glewInfoFunc("wglSendPbufferToVideoNV", wglSendPbufferToVideoNV == NULL);
}

#endif /* WGL_NV_video_output */

#ifdef WGL_OML_sync_control

static void _glewInfo_WGL_OML_sync_control (void)
{
  glewPrintExt("WGL_OML_sync_control", WGLEW_OML_sync_control, wglewIsSupported("WGL_OML_sync_control"), wglewGetExtension("WGL_OML_sync_control"));

  glewInfoFunc("wglGetMscRateOML", wglGetMscRateOML == NULL);
  glewInfoFunc("wglGetSyncValuesOML", wglGetSyncValuesOML == NULL);
  glewInfoFunc("wglSwapBuffersMscOML", wglSwapBuffersMscOML == NULL);
  glewInfoFunc("wglSwapLayerBuffersMscOML", wglSwapLayerBuffersMscOML == NULL);
  glewInfoFunc("wglWaitForMscOML", wglWaitForMscOML == NULL);
  glewInfoFunc("wglWaitForSbcOML", wglWaitForSbcOML == NULL);
}

#endif /* WGL_OML_sync_control */

#else /* _UNIX */

#ifdef GLX_VERSION_1_2

static void _glewInfo_GLX_VERSION_1_2 (void)
{
  glewPrintExt("GLX_VERSION_1_2", GLXEW_VERSION_1_2, GLXEW_VERSION_1_2, GLXEW_VERSION_1_2);

  glewInfoFunc("glXGetCurrentDisplay", glXGetCurrentDisplay == NULL);
}

#endif /* GLX_VERSION_1_2 */

#ifdef GLX_VERSION_1_3

static void _glewInfo_GLX_VERSION_1_3 (void)
{
  glewPrintExt("GLX_VERSION_1_3", GLXEW_VERSION_1_3, GLXEW_VERSION_1_3, GLXEW_VERSION_1_3);

  glewInfoFunc("glXChooseFBConfig", glXChooseFBConfig == NULL);
  glewInfoFunc("glXCreateNewContext", glXCreateNewContext == NULL);
  glewInfoFunc("glXCreatePbuffer", glXCreatePbuffer == NULL);
  glewInfoFunc("glXCreatePixmap", glXCreatePixmap == NULL);
  glewInfoFunc("glXCreateWindow", glXCreateWindow == NULL);
  glewInfoFunc("glXDestroyPbuffer", glXDestroyPbuffer == NULL);
  glewInfoFunc("glXDestroyPixmap", glXDestroyPixmap == NULL);
  glewInfoFunc("glXDestroyWindow", glXDestroyWindow == NULL);
  glewInfoFunc("glXGetCurrentReadDrawable", glXGetCurrentReadDrawable == NULL);
  glewInfoFunc("glXGetFBConfigAttrib", glXGetFBConfigAttrib == NULL);
  glewInfoFunc("glXGetFBConfigs", glXGetFBConfigs == NULL);
  glewInfoFunc("glXGetSelectedEvent", glXGetSelectedEvent == NULL);
  glewInfoFunc("glXGetVisualFromFBConfig", glXGetVisualFromFBConfig == NULL);
  glewInfoFunc("glXMakeContextCurrent", glXMakeContextCurrent == NULL);
  glewInfoFunc("glXQueryContext", glXQueryContext == NULL);
  glewInfoFunc("glXQueryDrawable", glXQueryDrawable == NULL);
  glewInfoFunc("glXSelectEvent", glXSelectEvent == NULL);
}

#endif /* GLX_VERSION_1_3 */

#ifdef GLX_VERSION_1_4

static void _glewInfo_GLX_VERSION_1_4 (void)
{
  glewPrintExt("GLX_VERSION_1_4", GLXEW_VERSION_1_4, GLXEW_VERSION_1_4, GLXEW_VERSION_1_4);
}

#endif /* GLX_VERSION_1_4 */

#ifdef GLX_3DFX_multisample

static void _glewInfo_GLX_3DFX_multisample (void)
{
  glewPrintExt("GLX_3DFX_multisample", GLXEW_3DFX_multisample, glxewIsSupported("GLX_3DFX_multisample"), glxewGetExtension("GLX_3DFX_multisample"));
}

#endif /* GLX_3DFX_multisample */

#ifdef GLX_AMD_gpu_association

static void _glewInfo_GLX_AMD_gpu_association (void)
{
  glewPrintExt("GLX_AMD_gpu_association", GLXEW_AMD_gpu_association, glxewIsSupported("GLX_AMD_gpu_association"), glxewGetExtension("GLX_AMD_gpu_association"));

  glewInfoFunc("glXBlitContextFramebufferAMD", glXBlitContextFramebufferAMD == NULL);
  glewInfoFunc("glXCreateAssociatedContextAMD", glXCreateAssociatedContextAMD == NULL);
  glewInfoFunc("glXCreateAssociatedContextAttribsAMD", glXCreateAssociatedContextAttribsAMD == NULL);
  glewInfoFunc("glXDeleteAssociatedContextAMD", glXDeleteAssociatedContextAMD == NULL);
  glewInfoFunc("glXGetContextGPUIDAMD", glXGetContextGPUIDAMD == NULL);
  glewInfoFunc("glXGetCurrentAssociatedContextAMD", glXGetCurrentAssociatedContextAMD == NULL);
  glewInfoFunc("glXGetGPUIDsAMD", glXGetGPUIDsAMD == NULL);
  glewInfoFunc("glXGetGPUInfoAMD", glXGetGPUInfoAMD == NULL);
  glewInfoFunc("glXMakeAssociatedContextCurrentAMD", glXMakeAssociatedContextCurrentAMD == NULL);
}

#endif /* GLX_AMD_gpu_association */

#ifdef GLX_ARB_context_flush_control

static void _glewInfo_GLX_ARB_context_flush_control (void)
{
  glewPrintExt("GLX_ARB_context_flush_control", GLXEW_ARB_context_flush_control, glxewIsSupported("GLX_ARB_context_flush_control"), glxewGetExtension("GLX_ARB_context_flush_control"));
}

#endif /* GLX_ARB_context_flush_control */

#ifdef GLX_ARB_create_context

static void _glewInfo_GLX_ARB_create_context (void)
{
  glewPrintExt("GLX_ARB_create_context", GLXEW_ARB_create_context, glxewIsSupported("GLX_ARB_create_context"), glxewGetExtension("GLX_ARB_create_context"));

  glewInfoFunc("glXCreateContextAttribsARB", glXCreateContextAttribsARB == NULL);
}

#endif /* GLX_ARB_create_context */

#ifdef GLX_ARB_create_context_no_error

static void _glewInfo_GLX_ARB_create_context_no_error (void)
{
  glewPrintExt("GLX_ARB_create_context_no_error", GLXEW_ARB_create_context_no_error, glxewIsSupported("GLX_ARB_create_context_no_error"), glxewGetExtension("GLX_ARB_create_context_no_error"));
}

#endif /* GLX_ARB_create_context_no_error */

#ifdef GLX_ARB_create_context_profile

static void _glewInfo_GLX_ARB_create_context_profile (void)
{
  glewPrintExt("GLX_ARB_create_context_profile", GLXEW_ARB_create_context_profile, glxewIsSupported("GLX_ARB_create_context_profile"), glxewGetExtension("GLX_ARB_create_context_profile"));
}

#endif /* GLX_ARB_create_context_profile */

#ifdef GLX_ARB_create_context_robustness

static void _glewInfo_GLX_ARB_create_context_robustness (void)
{
  glewPrintExt("GLX_ARB_create_context_robustness", GLXEW_ARB_create_context_robustness, glxewIsSupported("GLX_ARB_create_context_robustness"), glxewGetExtension("GLX_ARB_create_context_robustness"));
}

#endif /* GLX_ARB_create_context_robustness */

#ifdef GLX_ARB_fbconfig_float

static void _glewInfo_GLX_ARB_fbconfig_float (void)
{
  glewPrintExt("GLX_ARB_fbconfig_float", GLXEW_ARB_fbconfig_float, glxewIsSupported("GLX_ARB_fbconfig_float"), glxewGetExtension("GLX_ARB_fbconfig_float"));
}

#endif /* GLX_ARB_fbconfig_float */

#ifdef GLX_ARB_framebuffer_sRGB

static void _glewInfo_GLX_ARB_framebuffer_sRGB (void)
{
  glewPrintExt("GLX_ARB_framebuffer_sRGB", GLXEW_ARB_framebuffer_sRGB, glxewIsSupported("GLX_ARB_framebuffer_sRGB"), glxewGetExtension("GLX_ARB_framebuffer_sRGB"));
}

#endif /* GLX_ARB_framebuffer_sRGB */

#ifdef GLX_ARB_get_proc_address

static void _glewInfo_GLX_ARB_get_proc_address (void)
{
  glewPrintExt("GLX_ARB_get_proc_address", GLXEW_ARB_get_proc_address, glxewIsSupported("GLX_ARB_get_proc_address"), glxewGetExtension("GLX_ARB_get_proc_address"));
}

#endif /* GLX_ARB_get_proc_address */

#ifdef GLX_ARB_multisample

static void _glewInfo_GLX_ARB_multisample (void)
{
  glewPrintExt("GLX_ARB_multisample", GLXEW_ARB_multisample, glxewIsSupported("GLX_ARB_multisample"), glxewGetExtension("GLX_ARB_multisample"));
}

#endif /* GLX_ARB_multisample */

#ifdef GLX_ARB_robustness_application_isolation

static void _glewInfo_GLX_ARB_robustness_application_isolation (void)
{
  glewPrintExt("GLX_ARB_robustness_application_isolation", GLXEW_ARB_robustness_application_isolation, glxewIsSupported("GLX_ARB_robustness_application_isolation"), glxewGetExtension("GLX_ARB_robustness_application_isolation"));
}

#endif /* GLX_ARB_robustness_application_isolation */

#ifdef GLX_ARB_robustness_share_group_isolation

static void _glewInfo_GLX_ARB_robustness_share_group_isolation (void)
{
  glewPrintExt("GLX_ARB_robustness_share_group_isolation", GLXEW_ARB_robustness_share_group_isolation, glxewIsSupported("GLX_ARB_robustness_share_group_isolation"), glxewGetExtension("GLX_ARB_robustness_share_group_isolation"));
}

#endif /* GLX_ARB_robustness_share_group_isolation */

#ifdef GLX_ARB_vertex_buffer_object

static void _glewInfo_GLX_ARB_vertex_buffer_object (void)
{
  glewPrintExt("GLX_ARB_vertex_buffer_object", GLXEW_ARB_vertex_buffer_object, glxewIsSupported("GLX_ARB_vertex_buffer_object"), glxewGetExtension("GLX_ARB_vertex_buffer_object"));
}

#endif /* GLX_ARB_vertex_buffer_object */

#ifdef GLX_ATI_pixel_format_float

static void _glewInfo_GLX_ATI_pixel_format_float (void)
{
  glewPrintExt("GLX_ATI_pixel_format_float", GLXEW_ATI_pixel_format_float, glxewIsSupported("GLX_ATI_pixel_format_float"), glxewGetExtension("GLX_ATI_pixel_format_float"));
}

#endif /* GLX_ATI_pixel_format_float */

#ifdef GLX_ATI_render_texture

static void _glewInfo_GLX_ATI_render_texture (void)
{
  glewPrintExt("GLX_ATI_render_texture", GLXEW_ATI_render_texture, glxewIsSupported("GLX_ATI_render_texture"), glxewGetExtension("GLX_ATI_render_texture"));

  glewInfoFunc("glXBindTexImageATI", glXBindTexImageATI == NULL);
  glewInfoFunc("glXDrawableAttribATI", glXDrawableAttribATI == NULL);
  glewInfoFunc("glXReleaseTexImageATI", glXReleaseTexImageATI == NULL);
}

#endif /* GLX_ATI_render_texture */

#ifdef GLX_EXT_buffer_age

static void _glewInfo_GLX_EXT_buffer_age (void)
{
  glewPrintExt("GLX_EXT_buffer_age", GLXEW_EXT_buffer_age, glxewIsSupported("GLX_EXT_buffer_age"), glxewGetExtension("GLX_EXT_buffer_age"));
}

#endif /* GLX_EXT_buffer_age */

#ifdef GLX_EXT_create_context_es2_profile

static void _glewInfo_GLX_EXT_create_context_es2_profile (void)
{
  glewPrintExt("GLX_EXT_create_context_es2_profile", GLXEW_EXT_create_context_es2_profile, glxewIsSupported("GLX_EXT_create_context_es2_profile"), glxewGetExtension("GLX_EXT_create_context_es2_profile"));
}

#endif /* GLX_EXT_create_context_es2_profile */

#ifdef GLX_EXT_create_context_es_profile

static void _glewInfo_GLX_EXT_create_context_es_profile (void)
{
  glewPrintExt("GLX_EXT_create_context_es_profile", GLXEW_EXT_create_context_es_profile, glxewIsSupported("GLX_EXT_create_context_es_profile"), glxewGetExtension("GLX_EXT_create_context_es_profile"));
}

#endif /* GLX_EXT_create_context_es_profile */

#ifdef GLX_EXT_fbconfig_packed_float

static void _glewInfo_GLX_EXT_fbconfig_packed_float (void)
{
  glewPrintExt("GLX_EXT_fbconfig_packed_float", GLXEW_EXT_fbconfig_packed_float, glxewIsSupported("GLX_EXT_fbconfig_packed_float"), glxewGetExtension("GLX_EXT_fbconfig_packed_float"));
}

#endif /* GLX_EXT_fbconfig_packed_float */

#ifdef GLX_EXT_framebuffer_sRGB

static void _glewInfo_GLX_EXT_framebuffer_sRGB (void)
{
  glewPrintExt("GLX_EXT_framebuffer_sRGB", GLXEW_EXT_framebuffer_sRGB, glxewIsSupported("GLX_EXT_framebuffer_sRGB"), glxewGetExtension("GLX_EXT_framebuffer_sRGB"));
}

#endif /* GLX_EXT_framebuffer_sRGB */

#ifdef GLX_EXT_import_context

static void _glewInfo_GLX_EXT_import_context (void)
{
  glewPrintExt("GLX_EXT_import_context", GLXEW_EXT_import_context, glxewIsSupported("GLX_EXT_import_context"), glxewGetExtension("GLX_EXT_import_context"));

  glewInfoFunc("glXFreeContextEXT", glXFreeContextEXT == NULL);
  glewInfoFunc("glXGetContextIDEXT", glXGetContextIDEXT == NULL);
  glewInfoFunc("glXImportContextEXT", glXImportContextEXT == NULL);
  glewInfoFunc("glXQueryContextInfoEXT", glXQueryContextInfoEXT == NULL);
}

#endif /* GLX_EXT_import_context */

#ifdef GLX_EXT_libglvnd

static void _glewInfo_GLX_EXT_libglvnd (void)
{
  glewPrintExt("GLX_EXT_libglvnd", GLXEW_EXT_libglvnd, glxewIsSupported("GLX_EXT_libglvnd"), glxewGetExtension("GLX_EXT_libglvnd"));
}

#endif /* GLX_EXT_libglvnd */

#ifdef GLX_EXT_scene_marker

static void _glewInfo_GLX_EXT_scene_marker (void)
{
  glewPrintExt("GLX_EXT_scene_marker", GLXEW_EXT_scene_marker, glxewIsSupported("GLX_EXT_scene_marker"), glxewGetExtension("GLX_EXT_scene_marker"));
}

#endif /* GLX_EXT_scene_marker */

#ifdef GLX_EXT_stereo_tree

static void _glewInfo_GLX_EXT_stereo_tree (void)
{
  glewPrintExt("GLX_EXT_stereo_tree", GLXEW_EXT_stereo_tree, glxewIsSupported("GLX_EXT_stereo_tree"), glxewGetExtension("GLX_EXT_stereo_tree"));
}

#endif /* GLX_EXT_stereo_tree */

#ifdef GLX_EXT_swap_control

static void _glewInfo_GLX_EXT_swap_control (void)
{
  glewPrintExt("GLX_EXT_swap_control", GLXEW_EXT_swap_control, glxewIsSupported("GLX_EXT_swap_control"), glxewGetExtension("GLX_EXT_swap_control"));

  glewInfoFunc("glXSwapIntervalEXT", glXSwapIntervalEXT == NULL);
}

#endif /* GLX_EXT_swap_control */

#ifdef GLX_EXT_swap_control_tear

static void _glewInfo_GLX_EXT_swap_control_tear (void)
{
  glewPrintExt("GLX_EXT_swap_control_tear", GLXEW_EXT_swap_control_tear, glxewIsSupported("GLX_EXT_swap_control_tear"), glxewGetExtension("GLX_EXT_swap_control_tear"));
}

#endif /* GLX_EXT_swap_control_tear */

#ifdef GLX_EXT_texture_from_pixmap

static void _glewInfo_GLX_EXT_texture_from_pixmap (void)
{
  glewPrintExt("GLX_EXT_texture_from_pixmap", GLXEW_EXT_texture_from_pixmap, glxewIsSupported("GLX_EXT_texture_from_pixmap"), glxewGetExtension("GLX_EXT_texture_from_pixmap"));

  glewInfoFunc("glXBindTexImageEXT", glXBindTexImageEXT == NULL);
  glewInfoFunc("glXReleaseTexImageEXT", glXReleaseTexImageEXT == NULL);
}

#endif /* GLX_EXT_texture_from_pixmap */

#ifdef GLX_EXT_visual_info

static void _glewInfo_GLX_EXT_visual_info (void)
{
  glewPrintExt("GLX_EXT_visual_info", GLXEW_EXT_visual_info, glxewIsSupported("GLX_EXT_visual_info"), glxewGetExtension("GLX_EXT_visual_info"));
}

#endif /* GLX_EXT_visual_info */

#ifdef GLX_EXT_visual_rating

static void _glewInfo_GLX_EXT_visual_rating (void)
{
  glewPrintExt("GLX_EXT_visual_rating", GLXEW_EXT_visual_rating, glxewIsSupported("GLX_EXT_visual_rating"), glxewGetExtension("GLX_EXT_visual_rating"));
}

#endif /* GLX_EXT_visual_rating */

#ifdef GLX_INTEL_swap_event

static void _glewInfo_GLX_INTEL_swap_event (void)
{
  glewPrintExt("GLX_INTEL_swap_event", GLXEW_INTEL_swap_event, glxewIsSupported("GLX_INTEL_swap_event"), glxewGetExtension("GLX_INTEL_swap_event"));
}

#endif /* GLX_INTEL_swap_event */

#ifdef GLX_MESA_agp_offset

static void _glewInfo_GLX_MESA_agp_offset (void)
{
  glewPrintExt("GLX_MESA_agp_offset", GLXEW_MESA_agp_offset, glxewIsSupported("GLX_MESA_agp_offset"), glxewGetExtension("GLX_MESA_agp_offset"));

  glewInfoFunc("glXGetAGPOffsetMESA", glXGetAGPOffsetMESA == NULL);
}

#endif /* GLX_MESA_agp_offset */

#ifdef GLX_MESA_copy_sub_buffer

static void _glewInfo_GLX_MESA_copy_sub_buffer (void)
{
  glewPrintExt("GLX_MESA_copy_sub_buffer", GLXEW_MESA_copy_sub_buffer, glxewIsSupported("GLX_MESA_copy_sub_buffer"), glxewGetExtension("GLX_MESA_copy_sub_buffer"));

  glewInfoFunc("glXCopySubBufferMESA", glXCopySubBufferMESA == NULL);
}

#endif /* GLX_MESA_copy_sub_buffer */

#ifdef GLX_MESA_pixmap_colormap

static void _glewInfo_GLX_MESA_pixmap_colormap (void)
{
  glewPrintExt("GLX_MESA_pixmap_colormap", GLXEW_MESA_pixmap_colormap, glxewIsSupported("GLX_MESA_pixmap_colormap"), glxewGetExtension("GLX_MESA_pixmap_colormap"));

  glewInfoFunc("glXCreateGLXPixmapMESA", glXCreateGLXPixmapMESA == NULL);
}

#endif /* GLX_MESA_pixmap_colormap */

#ifdef GLX_MESA_query_renderer

static void _glewInfo_GLX_MESA_query_renderer (void)
{
  glewPrintExt("GLX_MESA_query_renderer", GLXEW_MESA_query_renderer, glxewIsSupported("GLX_MESA_query_renderer"), glxewGetExtension("GLX_MESA_query_renderer"));

  glewInfoFunc("glXQueryCurrentRendererIntegerMESA", glXQueryCurrentRendererIntegerMESA == NULL);
  glewInfoFunc("glXQueryCurrentRendererStringMESA", glXQueryCurrentRendererStringMESA == NULL);
  glewInfoFunc("glXQueryRendererIntegerMESA", glXQueryRendererIntegerMESA == NULL);
  glewInfoFunc("glXQueryRendererStringMESA", glXQueryRendererStringMESA == NULL);
}

#endif /* GLX_MESA_query_renderer */

#ifdef GLX_MESA_release_buffers

static void _glewInfo_GLX_MESA_release_buffers (void)
{
  glewPrintExt("GLX_MESA_release_buffers", GLXEW_MESA_release_buffers, glxewIsSupported("GLX_MESA_release_buffers"), glxewGetExtension("GLX_MESA_release_buffers"));

  glewInfoFunc("glXReleaseBuffersMESA", glXReleaseBuffersMESA == NULL);
}

#endif /* GLX_MESA_release_buffers */

#ifdef GLX_MESA_set_3dfx_mode

static void _glewInfo_GLX_MESA_set_3dfx_mode (void)
{
  glewPrintExt("GLX_MESA_set_3dfx_mode", GLXEW_MESA_set_3dfx_mode, glxewIsSupported("GLX_MESA_set_3dfx_mode"), glxewGetExtension("GLX_MESA_set_3dfx_mode"));

  glewInfoFunc("glXSet3DfxModeMESA", glXSet3DfxModeMESA == NULL);
}

#endif /* GLX_MESA_set_3dfx_mode */

#ifdef GLX_MESA_swap_control

static void _glewInfo_GLX_MESA_swap_control (void)
{
  glewPrintExt("GLX_MESA_swap_control", GLXEW_MESA_swap_control, glxewIsSupported("GLX_MESA_swap_control"), glxewGetExtension("GLX_MESA_swap_control"));

  glewInfoFunc("glXGetSwapIntervalMESA", glXGetSwapIntervalMESA == NULL);
  glewInfoFunc("glXSwapIntervalMESA", glXSwapIntervalMESA == NULL);
}

#endif /* GLX_MESA_swap_control */

#ifdef GLX_NV_copy_buffer

static void _glewInfo_GLX_NV_copy_buffer (void)
{
  glewPrintExt("GLX_NV_copy_buffer", GLXEW_NV_copy_buffer, glxewIsSupported("GLX_NV_copy_buffer"), glxewGetExtension("GLX_NV_copy_buffer"));

  glewInfoFunc("glXCopyBufferSubDataNV", glXCopyBufferSubDataNV == NULL);
  glewInfoFunc("glXNamedCopyBufferSubDataNV", glXNamedCopyBufferSubDataNV == NULL);
}

#endif /* GLX_NV_copy_buffer */

#ifdef GLX_NV_copy_image

static void _glewInfo_GLX_NV_copy_image (void)
{
  glewPrintExt("GLX_NV_copy_image", GLXEW_NV_copy_image, glxewIsSupported("GLX_NV_copy_image"), glxewGetExtension("GLX_NV_copy_image"));

  glewInfoFunc("glXCopyImageSubDataNV", glXCopyImageSubDataNV == NULL);
}

#endif /* GLX_NV_copy_image */

#ifdef GLX_NV_delay_before_swap

static void _glewInfo_GLX_NV_delay_before_swap (void)
{
  glewPrintExt("GLX_NV_delay_before_swap", GLXEW_NV_delay_before_swap, glxewIsSupported("GLX_NV_delay_before_swap"), glxewGetExtension("GLX_NV_delay_before_swap"));

  glewInfoFunc("glXDelayBeforeSwapNV", glXDelayBeforeSwapNV == NULL);
}

#endif /* GLX_NV_delay_before_swap */

#ifdef GLX_NV_float_buffer

static void _glewInfo_GLX_NV_float_buffer (void)
{
  glewPrintExt("GLX_NV_float_buffer", GLXEW_NV_float_buffer, glxewIsSupported("GLX_NV_float_buffer"), glxewGetExtension("GLX_NV_float_buffer"));
}

#endif /* GLX_NV_float_buffer */

#ifdef GLX_NV_multisample_coverage

static void _glewInfo_GLX_NV_multisample_coverage (void)
{
  glewPrintExt("GLX_NV_multisample_coverage", GLXEW_NV_multisample_coverage, glxewIsSupported("GLX_NV_multisample_coverage"), glxewGetExtension("GLX_NV_multisample_coverage"));
}

#endif /* GLX_NV_multisample_coverage */

#ifdef GLX_NV_present_video

static void _glewInfo_GLX_NV_present_video (void)
{
  glewPrintExt("GLX_NV_present_video", GLXEW_NV_present_video, glxewIsSupported("GLX_NV_present_video"), glxewGetExtension("GLX_NV_present_video"));

  glewInfoFunc("glXBindVideoDeviceNV", glXBindVideoDeviceNV == NULL);
  glewInfoFunc("glXEnumerateVideoDevicesNV", glXEnumerateVideoDevicesNV == NULL);
}

#endif /* GLX_NV_present_video */

#ifdef GLX_NV_robustness_video_memory_purge

static void _glewInfo_GLX_NV_robustness_video_memory_purge (void)
{
  glewPrintExt("GLX_NV_robustness_video_memory_purge", GLXEW_NV_robustness_video_memory_purge, glxewIsSupported("GLX_NV_robustness_video_memory_purge"), glxewGetExtension("GLX_NV_robustness_video_memory_purge"));
}

#endif /* GLX_NV_robustness_video_memory_purge */

#ifdef GLX_NV_swap_group

static void _glewInfo_GLX_NV_swap_group (void)
{
  glewPrintExt("GLX_NV_swap_group", GLXEW_NV_swap_group, glxewIsSupported("GLX_NV_swap_group"), glxewGetExtension("GLX_NV_swap_group"));

  glewInfoFunc("glXBindSwapBarrierNV", glXBindSwapBarrierNV == NULL);
  glewInfoFunc("glXJoinSwapGroupNV", glXJoinSwapGroupNV == NULL);
  glewInfoFunc("glXQueryFrameCountNV", glXQueryFrameCountNV == NULL);
  glewInfoFunc("glXQueryMaxSwapGroupsNV", glXQueryMaxSwapGroupsNV == NULL);
  glewInfoFunc("glXQuerySwapGroupNV", glXQuerySwapGroupNV == NULL);
  glewInfoFunc("glXResetFrameCountNV", glXResetFrameCountNV == NULL);
}

#endif /* GLX_NV_swap_group */

#ifdef GLX_NV_vertex_array_range

static void _glewInfo_GLX_NV_vertex_array_range (void)
{
  glewPrintExt("GLX_NV_vertex_array_range", GLXEW_NV_vertex_array_range, glxewIsSupported("GLX_NV_vertex_array_range"), glxewGetExtension("GLX_NV_vertex_array_range"));

  glewInfoFunc("glXAllocateMemoryNV", glXAllocateMemoryNV == NULL);
  glewInfoFunc("glXFreeMemoryNV", glXFreeMemoryNV == NULL);
}

#endif /* GLX_NV_vertex_array_range */

#ifdef GLX_NV_video_capture

static void _glewInfo_GLX_NV_video_capture (void)
{
  glewPrintExt("GLX_NV_video_capture", GLXEW_NV_video_capture, glxewIsSupported("GLX_NV_video_capture"), glxewGetExtension("GLX_NV_video_capture"));

  glewInfoFunc("glXBindVideoCaptureDeviceNV", glXBindVideoCaptureDeviceNV == NULL);
  glewInfoFunc("glXEnumerateVideoCaptureDevicesNV", glXEnumerateVideoCaptureDevicesNV == NULL);
  glewInfoFunc("glXLockVideoCaptureDeviceNV", glXLockVideoCaptureDeviceNV == NULL);
  glewInfoFunc("glXQueryVideoCaptureDeviceNV", glXQueryVideoCaptureDeviceNV == NULL);
  glewInfoFunc("glXReleaseVideoCaptureDeviceNV", glXReleaseVideoCaptureDeviceNV == NULL);
}

#endif /* GLX_NV_video_capture */

#ifdef GLX_NV_video_out

static void _glewInfo_GLX_NV_video_out (void)
{
  glewPrintExt("GLX_NV_video_out", GLXEW_NV_video_out, glxewIsSupported("GLX_NV_video_out"), glxewGetExtension("GLX_NV_video_out"));

  glewInfoFunc("glXBindVideoImageNV", glXBindVideoImageNV == NULL);
  glewInfoFunc("glXGetVideoDeviceNV", glXGetVideoDeviceNV == NULL);
  glewInfoFunc("glXGetVideoInfoNV", glXGetVideoInfoNV == NULL);
  glewInfoFunc("glXReleaseVideoDeviceNV", glXReleaseVideoDeviceNV == NULL);
  glewInfoFunc("glXReleaseVideoImageNV", glXReleaseVideoImageNV == NULL);
  glewInfoFunc("glXSendPbufferToVideoNV", glXSendPbufferToVideoNV == NULL);
}

#endif /* GLX_NV_video_out */

#ifdef GLX_OML_swap_method

static void _glewInfo_GLX_OML_swap_method (void)
{
  glewPrintExt("GLX_OML_swap_method", GLXEW_OML_swap_method, glxewIsSupported("GLX_OML_swap_method"), glxewGetExtension("GLX_OML_swap_method"));
}

#endif /* GLX_OML_swap_method */

#ifdef GLX_OML_sync_control

static void _glewInfo_GLX_OML_sync_control (void)
{
  glewPrintExt("GLX_OML_sync_control", GLXEW_OML_sync_control, glxewIsSupported("GLX_OML_sync_control"), glxewGetExtension("GLX_OML_sync_control"));

  glewInfoFunc("glXGetMscRateOML", glXGetMscRateOML == NULL);
  glewInfoFunc("glXGetSyncValuesOML", glXGetSyncValuesOML == NULL);
  glewInfoFunc("glXSwapBuffersMscOML", glXSwapBuffersMscOML == NULL);
  glewInfoFunc("glXWaitForMscOML", glXWaitForMscOML == NULL);
  glewInfoFunc("glXWaitForSbcOML", glXWaitForSbcOML == NULL);
}

#endif /* GLX_OML_sync_control */

#ifdef GLX_SGIS_blended_overlay

static void _glewInfo_GLX_SGIS_blended_overlay (void)
{
  glewPrintExt("GLX_SGIS_blended_overlay", GLXEW_SGIS_blended_overlay, glxewIsSupported("GLX_SGIS_blended_overlay"), glxewGetExtension("GLX_SGIS_blended_overlay"));
}

#endif /* GLX_SGIS_blended_overlay */

#ifdef GLX_SGIS_color_range

static void _glewInfo_GLX_SGIS_color_range (void)
{
  glewPrintExt("GLX_SGIS_color_range", GLXEW_SGIS_color_range, glxewIsSupported("GLX_SGIS_color_range"), glxewGetExtension("GLX_SGIS_color_range"));
}

#endif /* GLX_SGIS_color_range */

#ifdef GLX_SGIS_multisample

static void _glewInfo_GLX_SGIS_multisample (void)
{
  glewPrintExt("GLX_SGIS_multisample", GLXEW_SGIS_multisample, glxewIsSupported("GLX_SGIS_multisample"), glxewGetExtension("GLX_SGIS_multisample"));
}

#endif /* GLX_SGIS_multisample */

#ifdef GLX_SGIS_shared_multisample

static void _glewInfo_GLX_SGIS_shared_multisample (void)
{
  glewPrintExt("GLX_SGIS_shared_multisample", GLXEW_SGIS_shared_multisample, glxewIsSupported("GLX_SGIS_shared_multisample"), glxewGetExtension("GLX_SGIS_shared_multisample"));
}

#endif /* GLX_SGIS_shared_multisample */

#ifdef GLX_SGIX_fbconfig

static void _glewInfo_GLX_SGIX_fbconfig (void)
{
  glewPrintExt("GLX_SGIX_fbconfig", GLXEW_SGIX_fbconfig, glxewIsSupported("GLX_SGIX_fbconfig"), glxewGetExtension("GLX_SGIX_fbconfig"));

  glewInfoFunc("glXChooseFBConfigSGIX", glXChooseFBConfigSGIX == NULL);
  glewInfoFunc("glXCreateContextWithConfigSGIX", glXCreateContextWithConfigSGIX == NULL);
  glewInfoFunc("glXCreateGLXPixmapWithConfigSGIX", glXCreateGLXPixmapWithConfigSGIX == NULL);
  glewInfoFunc("glXGetFBConfigAttribSGIX", glXGetFBConfigAttribSGIX == NULL);
  glewInfoFunc("glXGetFBConfigFromVisualSGIX", glXGetFBConfigFromVisualSGIX == NULL);
  glewInfoFunc("glXGetVisualFromFBConfigSGIX", glXGetVisualFromFBConfigSGIX == NULL);
}

#endif /* GLX_SGIX_fbconfig */

#ifdef GLX_SGIX_hyperpipe

static void _glewInfo_GLX_SGIX_hyperpipe (void)
{
  glewPrintExt("GLX_SGIX_hyperpipe", GLXEW_SGIX_hyperpipe, glxewIsSupported("GLX_SGIX_hyperpipe"), glxewGetExtension("GLX_SGIX_hyperpipe"));

  glewInfoFunc("glXBindHyperpipeSGIX", glXBindHyperpipeSGIX == NULL);
  glewInfoFunc("glXDestroyHyperpipeConfigSGIX", glXDestroyHyperpipeConfigSGIX == NULL);
  glewInfoFunc("glXHyperpipeAttribSGIX", glXHyperpipeAttribSGIX == NULL);
  glewInfoFunc("glXHyperpipeConfigSGIX", glXHyperpipeConfigSGIX == NULL);
  glewInfoFunc("glXQueryHyperpipeAttribSGIX", glXQueryHyperpipeAttribSGIX == NULL);
  glewInfoFunc("glXQueryHyperpipeBestAttribSGIX", glXQueryHyperpipeBestAttribSGIX == NULL);
  glewInfoFunc("glXQueryHyperpipeConfigSGIX", glXQueryHyperpipeConfigSGIX == NULL);
  glewInfoFunc("glXQueryHyperpipeNetworkSGIX", glXQueryHyperpipeNetworkSGIX == NULL);
}

#endif /* GLX_SGIX_hyperpipe */

#ifdef GLX_SGIX_pbuffer

static void _glewInfo_GLX_SGIX_pbuffer (void)
{
  glewPrintExt("GLX_SGIX_pbuffer", GLXEW_SGIX_pbuffer, glxewIsSupported("GLX_SGIX_pbuffer"), glxewGetExtension("GLX_SGIX_pbuffer"));

  glewInfoFunc("glXCreateGLXPbufferSGIX", glXCreateGLXPbufferSGIX == NULL);
  glewInfoFunc("glXDestroyGLXPbufferSGIX", glXDestroyGLXPbufferSGIX == NULL);
  glewInfoFunc("glXGetSelectedEventSGIX", glXGetSelectedEventSGIX == NULL);
  glewInfoFunc("glXQueryGLXPbufferSGIX", glXQueryGLXPbufferSGIX == NULL);
  glewInfoFunc("glXSelectEventSGIX", glXSelectEventSGIX == NULL);
}

#endif /* GLX_SGIX_pbuffer */

#ifdef GLX_SGIX_swap_barrier

static void _glewInfo_GLX_SGIX_swap_barrier (void)
{
  glewPrintExt("GLX_SGIX_swap_barrier", GLXEW_SGIX_swap_barrier, glxewIsSupported("GLX_SGIX_swap_barrier"), glxewGetExtension("GLX_SGIX_swap_barrier"));

  glewInfoFunc("glXBindSwapBarrierSGIX", glXBindSwapBarrierSGIX == NULL);
  glewInfoFunc("glXQueryMaxSwapBarriersSGIX", glXQueryMaxSwapBarriersSGIX == NULL);
}

#endif /* GLX_SGIX_swap_barrier */

#ifdef GLX_SGIX_swap_group

static void _glewInfo_GLX_SGIX_swap_group (void)
{
  glewPrintExt("GLX_SGIX_swap_group", GLXEW_SGIX_swap_group, glxewIsSupported("GLX_SGIX_swap_group"), glxewGetExtension("GLX_SGIX_swap_group"));

  glewInfoFunc("glXJoinSwapGroupSGIX", glXJoinSwapGroupSGIX == NULL);
}

#endif /* GLX_SGIX_swap_group */

#ifdef GLX_SGIX_video_resize

static void _glewInfo_GLX_SGIX_video_resize (void)
{
  glewPrintExt("GLX_SGIX_video_resize", GLXEW_SGIX_video_resize, glxewIsSupported("GLX_SGIX_video_resize"), glxewGetExtension("GLX_SGIX_video_resize"));

  glewInfoFunc("glXBindChannelToWindowSGIX", glXBindChannelToWindowSGIX == NULL);
  glewInfoFunc("glXChannelRectSGIX", glXChannelRectSGIX == NULL);
  glewInfoFunc("glXChannelRectSyncSGIX", glXChannelRectSyncSGIX == NULL);
  glewInfoFunc("glXQueryChannelDeltasSGIX", glXQueryChannelDeltasSGIX == NULL);
  glewInfoFunc("glXQueryChannelRectSGIX", glXQueryChannelRectSGIX == NULL);
}

#endif /* GLX_SGIX_video_resize */

#ifdef GLX_SGIX_visual_select_group

static void _glewInfo_GLX_SGIX_visual_select_group (void)
{
  glewPrintExt("GLX_SGIX_visual_select_group", GLXEW_SGIX_visual_select_group, glxewIsSupported("GLX_SGIX_visual_select_group"), glxewGetExtension("GLX_SGIX_visual_select_group"));
}

#endif /* GLX_SGIX_visual_select_group */

#ifdef GLX_SGI_cushion

static void _glewInfo_GLX_SGI_cushion (void)
{
  glewPrintExt("GLX_SGI_cushion", GLXEW_SGI_cushion, glxewIsSupported("GLX_SGI_cushion"), glxewGetExtension("GLX_SGI_cushion"));

  glewInfoFunc("glXCushionSGI", glXCushionSGI == NULL);
}

#endif /* GLX_SGI_cushion */

#ifdef GLX_SGI_make_current_read

static void _glewInfo_GLX_SGI_make_current_read (void)
{
  glewPrintExt("GLX_SGI_make_current_read", GLXEW_SGI_make_current_read, glxewIsSupported("GLX_SGI_make_current_read"), glxewGetExtension("GLX_SGI_make_current_read"));

  glewInfoFunc("glXGetCurrentReadDrawableSGI", glXGetCurrentReadDrawableSGI == NULL);
  glewInfoFunc("glXMakeCurrentReadSGI", glXMakeCurrentReadSGI == NULL);
}

#endif /* GLX_SGI_make_current_read */

#ifdef GLX_SGI_swap_control

static void _glewInfo_GLX_SGI_swap_control (void)
{
  glewPrintExt("GLX_SGI_swap_control", GLXEW_SGI_swap_control, glxewIsSupported("GLX_SGI_swap_control"), glxewGetExtension("GLX_SGI_swap_control"));

  glewInfoFunc("glXSwapIntervalSGI", glXSwapIntervalSGI == NULL);
}

#endif /* GLX_SGI_swap_control */

#ifdef GLX_SGI_video_sync

static void _glewInfo_GLX_SGI_video_sync (void)
{
  glewPrintExt("GLX_SGI_video_sync", GLXEW_SGI_video_sync, glxewIsSupported("GLX_SGI_video_sync"), glxewGetExtension("GLX_SGI_video_sync"));

  glewInfoFunc("glXGetVideoSyncSGI", glXGetVideoSyncSGI == NULL);
  glewInfoFunc("glXWaitVideoSyncSGI", glXWaitVideoSyncSGI == NULL);
}

#endif /* GLX_SGI_video_sync */

#ifdef GLX_SUN_get_transparent_index

static void _glewInfo_GLX_SUN_get_transparent_index (void)
{
  glewPrintExt("GLX_SUN_get_transparent_index", GLXEW_SUN_get_transparent_index, glxewIsSupported("GLX_SUN_get_transparent_index"), glxewGetExtension("GLX_SUN_get_transparent_index"));

  glewInfoFunc("glXGetTransparentIndexSUN", glXGetTransparentIndexSUN == NULL);
}

#endif /* GLX_SUN_get_transparent_index */

#ifdef GLX_SUN_video_resize

static void _glewInfo_GLX_SUN_video_resize (void)
{
  glewPrintExt("GLX_SUN_video_resize", GLXEW_SUN_video_resize, glxewIsSupported("GLX_SUN_video_resize"), glxewGetExtension("GLX_SUN_video_resize"));

  glewInfoFunc("glXGetVideoResizeSUN", glXGetVideoResizeSUN == NULL);
  glewInfoFunc("glXVideoResizeSUN", glXVideoResizeSUN == NULL);
}

#endif /* GLX_SUN_video_resize */

#endif /* _WIN32 */

/* ------------------------------------------------------------------------ */

static void glewInfo (void)
{
#ifdef GL_VERSION_1_1
  _glewInfo_GL_VERSION_1_1();
#endif /* GL_VERSION_1_1 */
#ifdef GL_VERSION_1_2
  _glewInfo_GL_VERSION_1_2();
#endif /* GL_VERSION_1_2 */
#ifdef GL_VERSION_1_2_1
  _glewInfo_GL_VERSION_1_2_1();
#endif /* GL_VERSION_1_2_1 */
#ifdef GL_VERSION_1_3
  _glewInfo_GL_VERSION_1_3();
#endif /* GL_VERSION_1_3 */
#ifdef GL_VERSION_1_4
  _glewInfo_GL_VERSION_1_4();
#endif /* GL_VERSION_1_4 */
#ifdef GL_VERSION_1_5
  _glewInfo_GL_VERSION_1_5();
#endif /* GL_VERSION_1_5 */
#ifdef GL_VERSION_2_0
  _glewInfo_GL_VERSION_2_0();
#endif /* GL_VERSION_2_0 */
#ifdef GL_VERSION_2_1
  _glewInfo_GL_VERSION_2_1();
#endif /* GL_VERSION_2_1 */
#ifdef GL_VERSION_3_0
  _glewInfo_GL_VERSION_3_0();
#endif /* GL_VERSION_3_0 */
#ifdef GL_VERSION_3_1
  _glewInfo_GL_VERSION_3_1();
#endif /* GL_VERSION_3_1 */
#ifdef GL_VERSION_3_2
  _glewInfo_GL_VERSION_3_2();
#endif /* GL_VERSION_3_2 */
#ifdef GL_VERSION_3_3
  _glewInfo_GL_VERSION_3_3();
#endif /* GL_VERSION_3_3 */
#ifdef GL_VERSION_4_0
  _glewInfo_GL_VERSION_4_0();
#endif /* GL_VERSION_4_0 */
#ifdef GL_VERSION_4_1
  _glewInfo_GL_VERSION_4_1();
#endif /* GL_VERSION_4_1 */
#ifdef GL_VERSION_4_2
  _glewInfo_GL_VERSION_4_2();
#endif /* GL_VERSION_4_2 */
#ifdef GL_VERSION_4_3
  _glewInfo_GL_VERSION_4_3();
#endif /* GL_VERSION_4_3 */
#ifdef GL_VERSION_4_4
  _glewInfo_GL_VERSION_4_4();
#endif /* GL_VERSION_4_4 */
#ifdef GL_VERSION_4_5
  _glewInfo_GL_VERSION_4_5();
#endif /* GL_VERSION_4_5 */
#ifdef GL_VERSION_4_6
  _glewInfo_GL_VERSION_4_6();
#endif /* GL_VERSION_4_6 */
#ifdef GL_3DFX_multisample
  _glewInfo_GL_3DFX_multisample();
#endif /* GL_3DFX_multisample */
#ifdef GL_3DFX_tbuffer
  _glewInfo_GL_3DFX_tbuffer();
#endif /* GL_3DFX_tbuffer */
#ifdef GL_3DFX_texture_compression_FXT1
  _glewInfo_GL_3DFX_texture_compression_FXT1();
#endif /* GL_3DFX_texture_compression_FXT1 */
#ifdef GL_AMD_blend_minmax_factor
  _glewInfo_GL_AMD_blend_minmax_factor();
#endif /* GL_AMD_blend_minmax_factor */
#ifdef GL_AMD_compressed_3DC_texture
  _glewInfo_GL_AMD_compressed_3DC_texture();
#endif /* GL_AMD_compressed_3DC_texture */
#ifdef GL_AMD_compressed_ATC_texture
  _glewInfo_GL_AMD_compressed_ATC_texture();
#endif /* GL_AMD_compressed_ATC_texture */
#ifdef GL_AMD_conservative_depth
  _glewInfo_GL_AMD_conservative_depth();
#endif /* GL_AMD_conservative_depth */
#ifdef GL_AMD_debug_output
  _glewInfo_GL_AMD_debug_output();
#endif /* GL_AMD_debug_output */
#ifdef GL_AMD_depth_clamp_separate
  _glewInfo_GL_AMD_depth_clamp_separate();
#endif /* GL_AMD_depth_clamp_separate */
#ifdef GL_AMD_draw_buffers_blend
  _glewInfo_GL_AMD_draw_buffers_blend();
#endif /* GL_AMD_draw_buffers_blend */
#ifdef GL_AMD_framebuffer_sample_positions
  _glewInfo_GL_AMD_framebuffer_sample_positions();
#endif /* GL_AMD_framebuffer_sample_positions */
#ifdef GL_AMD_gcn_shader
  _glewInfo_GL_AMD_gcn_shader();
#endif /* GL_AMD_gcn_shader */
#ifdef GL_AMD_gpu_shader_half_float
  _glewInfo_GL_AMD_gpu_shader_half_float();
#endif /* GL_AMD_gpu_shader_half_float */
#ifdef GL_AMD_gpu_shader_int16
  _glewInfo_GL_AMD_gpu_shader_int16();
#endif /* GL_AMD_gpu_shader_int16 */
#ifdef GL_AMD_gpu_shader_int64
  _glewInfo_GL_AMD_gpu_shader_int64();
#endif /* GL_AMD_gpu_shader_int64 */
#ifdef GL_AMD_interleaved_elements
  _glewInfo_GL_AMD_interleaved_elements();
#endif /* GL_AMD_interleaved_elements */
#ifdef GL_AMD_multi_draw_indirect
  _glewInfo_GL_AMD_multi_draw_indirect();
#endif /* GL_AMD_multi_draw_indirect */
#ifdef GL_AMD_name_gen_delete
  _glewInfo_GL_AMD_name_gen_delete();
#endif /* GL_AMD_name_gen_delete */
#ifdef GL_AMD_occlusion_query_event
  _glewInfo_GL_AMD_occlusion_query_event();
#endif /* GL_AMD_occlusion_query_event */
#ifdef GL_AMD_performance_monitor
  _glewInfo_GL_AMD_performance_monitor();
#endif /* GL_AMD_performance_monitor */
#ifdef GL_AMD_pinned_memory
  _glewInfo_GL_AMD_pinned_memory();
#endif /* GL_AMD_pinned_memory */
#ifdef GL_AMD_program_binary_Z400
  _glewInfo_GL_AMD_program_binary_Z400();
#endif /* GL_AMD_program_binary_Z400 */
#ifdef GL_AMD_query_buffer_object
  _glewInfo_GL_AMD_query_buffer_object();
#endif /* GL_AMD_query_buffer_object */
#ifdef GL_AMD_sample_positions
  _glewInfo_GL_AMD_sample_positions();
#endif /* GL_AMD_sample_positions */
#ifdef GL_AMD_seamless_cubemap_per_texture
  _glewInfo_GL_AMD_seamless_cubemap_per_texture();
#endif /* GL_AMD_seamless_cubemap_per_texture */
#ifdef GL_AMD_shader_atomic_counter_ops
  _glewInfo_GL_AMD_shader_atomic_counter_ops();
#endif /* GL_AMD_shader_atomic_counter_ops */
#ifdef GL_AMD_shader_ballot
  _glewInfo_GL_AMD_shader_ballot();
#endif /* GL_AMD_shader_ballot */
#ifdef GL_AMD_shader_explicit_vertex_parameter
  _glewInfo_GL_AMD_shader_explicit_vertex_parameter();
#endif /* GL_AMD_shader_explicit_vertex_parameter */
#ifdef GL_AMD_shader_stencil_export
  _glewInfo_GL_AMD_shader_stencil_export();
#endif /* GL_AMD_shader_stencil_export */
#ifdef GL_AMD_shader_stencil_value_export
  _glewInfo_GL_AMD_shader_stencil_value_export();
#endif /* GL_AMD_shader_stencil_value_export */
#ifdef GL_AMD_shader_trinary_minmax
  _glewInfo_GL_AMD_shader_trinary_minmax();
#endif /* GL_AMD_shader_trinary_minmax */
#ifdef GL_AMD_sparse_texture
  _glewInfo_GL_AMD_sparse_texture();
#endif /* GL_AMD_sparse_texture */
#ifdef GL_AMD_stencil_operation_extended
  _glewInfo_GL_AMD_stencil_operation_extended();
#endif /* GL_AMD_stencil_operation_extended */
#ifdef GL_AMD_texture_gather_bias_lod
  _glewInfo_GL_AMD_texture_gather_bias_lod();
#endif /* GL_AMD_texture_gather_bias_lod */
#ifdef GL_AMD_texture_texture4
  _glewInfo_GL_AMD_texture_texture4();
#endif /* GL_AMD_texture_texture4 */
#ifdef GL_AMD_transform_feedback3_lines_triangles
  _glewInfo_GL_AMD_transform_feedback3_lines_triangles();
#endif /* GL_AMD_transform_feedback3_lines_triangles */
#ifdef GL_AMD_transform_feedback4
  _glewInfo_GL_AMD_transform_feedback4();
#endif /* GL_AMD_transform_feedback4 */
#ifdef GL_AMD_vertex_shader_layer
  _glewInfo_GL_AMD_vertex_shader_layer();
#endif /* GL_AMD_vertex_shader_layer */
#ifdef GL_AMD_vertex_shader_tessellator
  _glewInfo_GL_AMD_vertex_shader_tessellator();
#endif /* GL_AMD_vertex_shader_tessellator */
#ifdef GL_AMD_vertex_shader_viewport_index
  _glewInfo_GL_AMD_vertex_shader_viewport_index();
#endif /* GL_AMD_vertex_shader_viewport_index */
#ifdef GL_ANDROID_extension_pack_es31a
  _glewInfo_GL_ANDROID_extension_pack_es31a();
#endif /* GL_ANDROID_extension_pack_es31a */
#ifdef GL_ANGLE_depth_texture
  _glewInfo_GL_ANGLE_depth_texture();
#endif /* GL_ANGLE_depth_texture */
#ifdef GL_ANGLE_framebuffer_blit
  _glewInfo_GL_ANGLE_framebuffer_blit();
#endif /* GL_ANGLE_framebuffer_blit */
#ifdef GL_ANGLE_framebuffer_multisample
  _glewInfo_GL_ANGLE_framebuffer_multisample();
#endif /* GL_ANGLE_framebuffer_multisample */
#ifdef GL_ANGLE_instanced_arrays
  _glewInfo_GL_ANGLE_instanced_arrays();
#endif /* GL_ANGLE_instanced_arrays */
#ifdef GL_ANGLE_pack_reverse_row_order
  _glewInfo_GL_ANGLE_pack_reverse_row_order();
#endif /* GL_ANGLE_pack_reverse_row_order */
#ifdef GL_ANGLE_program_binary
  _glewInfo_GL_ANGLE_program_binary();
#endif /* GL_ANGLE_program_binary */
#ifdef GL_ANGLE_texture_compression_dxt1
  _glewInfo_GL_ANGLE_texture_compression_dxt1();
#endif /* GL_ANGLE_texture_compression_dxt1 */
#ifdef GL_ANGLE_texture_compression_dxt3
  _glewInfo_GL_ANGLE_texture_compression_dxt3();
#endif /* GL_ANGLE_texture_compression_dxt3 */
#ifdef GL_ANGLE_texture_compression_dxt5
  _glewInfo_GL_ANGLE_texture_compression_dxt5();
#endif /* GL_ANGLE_texture_compression_dxt5 */
#ifdef GL_ANGLE_texture_usage
  _glewInfo_GL_ANGLE_texture_usage();
#endif /* GL_ANGLE_texture_usage */
#ifdef GL_ANGLE_timer_query
  _glewInfo_GL_ANGLE_timer_query();
#endif /* GL_ANGLE_timer_query */
#ifdef GL_ANGLE_translated_shader_source
  _glewInfo_GL_ANGLE_translated_shader_source();
#endif /* GL_ANGLE_translated_shader_source */
#ifdef GL_APPLE_aux_depth_stencil
  _glewInfo_GL_APPLE_aux_depth_stencil();
#endif /* GL_APPLE_aux_depth_stencil */
#ifdef GL_APPLE_client_storage
  _glewInfo_GL_APPLE_client_storage();
#endif /* GL_APPLE_client_storage */
#ifdef GL_APPLE_clip_distance
  _glewInfo_GL_APPLE_clip_distance();
#endif /* GL_APPLE_clip_distance */
#ifdef GL_APPLE_color_buffer_packed_float
  _glewInfo_GL_APPLE_color_buffer_packed_float();
#endif /* GL_APPLE_color_buffer_packed_float */
#ifdef GL_APPLE_copy_texture_levels
  _glewInfo_GL_APPLE_copy_texture_levels();
#endif /* GL_APPLE_copy_texture_levels */
#ifdef GL_APPLE_element_array
  _glewInfo_GL_APPLE_element_array();
#endif /* GL_APPLE_element_array */
#ifdef GL_APPLE_fence
  _glewInfo_GL_APPLE_fence();
#endif /* GL_APPLE_fence */
#ifdef GL_APPLE_float_pixels
  _glewInfo_GL_APPLE_float_pixels();
#endif /* GL_APPLE_float_pixels */
#ifdef GL_APPLE_flush_buffer_range
  _glewInfo_GL_APPLE_flush_buffer_range();
#endif /* GL_APPLE_flush_buffer_range */
#ifdef GL_APPLE_framebuffer_multisample
  _glewInfo_GL_APPLE_framebuffer_multisample();
#endif /* GL_APPLE_framebuffer_multisample */
#ifdef GL_APPLE_object_purgeable
  _glewInfo_GL_APPLE_object_purgeable();
#endif /* GL_APPLE_object_purgeable */
#ifdef GL_APPLE_pixel_buffer
  _glewInfo_GL_APPLE_pixel_buffer();
#endif /* GL_APPLE_pixel_buffer */
#ifdef GL_APPLE_rgb_422
  _glewInfo_GL_APPLE_rgb_422();
#endif /* GL_APPLE_rgb_422 */
#ifdef GL_APPLE_row_bytes
  _glewInfo_GL_APPLE_row_bytes();
#endif /* GL_APPLE_row_bytes */
#ifdef GL_APPLE_specular_vector
  _glewInfo_GL_APPLE_specular_vector();
#endif /* GL_APPLE_specular_vector */
#ifdef GL_APPLE_sync
  _glewInfo_GL_APPLE_sync();
#endif /* GL_APPLE_sync */
#ifdef GL_APPLE_texture_2D_limited_npot
  _glewInfo_GL_APPLE_texture_2D_limited_npot();
#endif /* GL_APPLE_texture_2D_limited_npot */
#ifdef GL_APPLE_texture_format_BGRA8888
  _glewInfo_GL_APPLE_texture_format_BGRA8888();
#endif /* GL_APPLE_texture_format_BGRA8888 */
#ifdef GL_APPLE_texture_max_level
  _glewInfo_GL_APPLE_texture_max_level();
#endif /* GL_APPLE_texture_max_level */
#ifdef GL_APPLE_texture_packed_float
  _glewInfo_GL_APPLE_texture_packed_float();
#endif /* GL_APPLE_texture_packed_float */
#ifdef GL_APPLE_texture_range
  _glewInfo_GL_APPLE_texture_range();
#endif /* GL_APPLE_texture_range */
#ifdef GL_APPLE_transform_hint
  _glewInfo_GL_APPLE_transform_hint();
#endif /* GL_APPLE_transform_hint */
#ifdef GL_APPLE_vertex_array_object
  _glewInfo_GL_APPLE_vertex_array_object();
#endif /* GL_APPLE_vertex_array_object */
#ifdef GL_APPLE_vertex_array_range
  _glewInfo_GL_APPLE_vertex_array_range();
#endif /* GL_APPLE_vertex_array_range */
#ifdef GL_APPLE_vertex_program_evaluators
  _glewInfo_GL_APPLE_vertex_program_evaluators();
#endif /* GL_APPLE_vertex_program_evaluators */
#ifdef GL_APPLE_ycbcr_422
  _glewInfo_GL_APPLE_ycbcr_422();
#endif /* GL_APPLE_ycbcr_422 */
#ifdef GL_ARB_ES2_compatibility
  _glewInfo_GL_ARB_ES2_compatibility();
#endif /* GL_ARB_ES2_compatibility */
#ifdef GL_ARB_ES3_1_compatibility
  _glewInfo_GL_ARB_ES3_1_compatibility();
#endif /* GL_ARB_ES3_1_compatibility */
#ifdef GL_ARB_ES3_2_compatibility
  _glewInfo_GL_ARB_ES3_2_compatibility();
#endif /* GL_ARB_ES3_2_compatibility */
#ifdef GL_ARB_ES3_compatibility
  _glewInfo_GL_ARB_ES3_compatibility();
#endif /* GL_ARB_ES3_compatibility */
#ifdef GL_ARB_arrays_of_arrays
  _glewInfo_GL_ARB_arrays_of_arrays();
#endif /* GL_ARB_arrays_of_arrays */
#ifdef GL_ARB_base_instance
  _glewInfo_GL_ARB_base_instance();
#endif /* GL_ARB_base_instance */
#ifdef GL_ARB_bindless_texture
  _glewInfo_GL_ARB_bindless_texture();
#endif /* GL_ARB_bindless_texture */
#ifdef GL_ARB_blend_func_extended
  _glewInfo_GL_ARB_blend_func_extended();
#endif /* GL_ARB_blend_func_extended */
#ifdef GL_ARB_buffer_storage
  _glewInfo_GL_ARB_buffer_storage();
#endif /* GL_ARB_buffer_storage */
#ifdef GL_ARB_cl_event
  _glewInfo_GL_ARB_cl_event();
#endif /* GL_ARB_cl_event */
#ifdef GL_ARB_clear_buffer_object
  _glewInfo_GL_ARB_clear_buffer_object();
#endif /* GL_ARB_clear_buffer_object */
#ifdef GL_ARB_clear_texture
  _glewInfo_GL_ARB_clear_texture();
#endif /* GL_ARB_clear_texture */
#ifdef GL_ARB_clip_control
  _glewInfo_GL_ARB_clip_control();
#endif /* GL_ARB_clip_control */
#ifdef GL_ARB_color_buffer_float
  _glewInfo_GL_ARB_color_buffer_float();
#endif /* GL_ARB_color_buffer_float */
#ifdef GL_ARB_compatibility
  _glewInfo_GL_ARB_compatibility();
#endif /* GL_ARB_compatibility */
#ifdef GL_ARB_compressed_texture_pixel_storage
  _glewInfo_GL_ARB_compressed_texture_pixel_storage();
#endif /* GL_ARB_compressed_texture_pixel_storage */
#ifdef GL_ARB_compute_shader
  _glewInfo_GL_ARB_compute_shader();
#endif /* GL_ARB_compute_shader */
#ifdef GL_ARB_compute_variable_group_size
  _glewInfo_GL_ARB_compute_variable_group_size();
#endif /* GL_ARB_compute_variable_group_size */
#ifdef GL_ARB_conditional_render_inverted
  _glewInfo_GL_ARB_conditional_render_inverted();
#endif /* GL_ARB_conditional_render_inverted */
#ifdef GL_ARB_conservative_depth
  _glewInfo_GL_ARB_conservative_depth();
#endif /* GL_ARB_conservative_depth */
#ifdef GL_ARB_copy_buffer
  _glewInfo_GL_ARB_copy_buffer();
#endif /* GL_ARB_copy_buffer */
#ifdef GL_ARB_copy_image
  _glewInfo_GL_ARB_copy_image();
#endif /* GL_ARB_copy_image */
#ifdef GL_ARB_cull_distance
  _glewInfo_GL_ARB_cull_distance();
#endif /* GL_ARB_cull_distance */
#ifdef GL_ARB_debug_output
  _glewInfo_GL_ARB_debug_output();
#endif /* GL_ARB_debug_output */
#ifdef GL_ARB_depth_buffer_float
  _glewInfo_GL_ARB_depth_buffer_float();
#endif /* GL_ARB_depth_buffer_float */
#ifdef GL_ARB_depth_clamp
  _glewInfo_GL_ARB_depth_clamp();
#endif /* GL_ARB_depth_clamp */
#ifdef GL_ARB_depth_texture
  _glewInfo_GL_ARB_depth_texture();
#endif /* GL_ARB_depth_texture */
#ifdef GL_ARB_derivative_control
  _glewInfo_GL_ARB_derivative_control();
#endif /* GL_ARB_derivative_control */
#ifdef GL_ARB_direct_state_access
  _glewInfo_GL_ARB_direct_state_access();
#endif /* GL_ARB_direct_state_access */
#ifdef GL_ARB_draw_buffers
  _glewInfo_GL_ARB_draw_buffers();
#endif /* GL_ARB_draw_buffers */
#ifdef GL_ARB_draw_buffers_blend
  _glewInfo_GL_ARB_draw_buffers_blend();
#endif /* GL_ARB_draw_buffers_blend */
#ifdef GL_ARB_draw_elements_base_vertex
  _glewInfo_GL_ARB_draw_elements_base_vertex();
#endif /* GL_ARB_draw_elements_base_vertex */
#ifdef GL_ARB_draw_indirect
  _glewInfo_GL_ARB_draw_indirect();
#endif /* GL_ARB_draw_indirect */
#ifdef GL_ARB_draw_instanced
  _glewInfo_GL_ARB_draw_instanced();
#endif /* GL_ARB_draw_instanced */
#ifdef GL_ARB_enhanced_layouts
  _glewInfo_GL_ARB_enhanced_layouts();
#endif /* GL_ARB_enhanced_layouts */
#ifdef GL_ARB_explicit_attrib_location
  _glewInfo_GL_ARB_explicit_attrib_location();
#endif /* GL_ARB_explicit_attrib_location */
#ifdef GL_ARB_explicit_uniform_location
  _glewInfo_GL_ARB_explicit_uniform_location();
#endif /* GL_ARB_explicit_uniform_location */
#ifdef GL_ARB_fragment_coord_conventions
  _glewInfo_GL_ARB_fragment_coord_conventions();
#endif /* GL_ARB_fragment_coord_conventions */
#ifdef GL_ARB_fragment_layer_viewport
  _glewInfo_GL_ARB_fragment_layer_viewport();
#endif /* GL_ARB_fragment_layer_viewport */
#ifdef GL_ARB_fragment_program
  _glewInfo_GL_ARB_fragment_program();
#endif /* GL_ARB_fragment_program */
#ifdef GL_ARB_fragment_program_shadow
  _glewInfo_GL_ARB_fragment_program_shadow();
#endif /* GL_ARB_fragment_program_shadow */
#ifdef GL_ARB_fragment_shader
  _glewInfo_GL_ARB_fragment_shader();
#endif /* GL_ARB_fragment_shader */
#ifdef GL_ARB_fragment_shader_interlock
  _glewInfo_GL_ARB_fragment_shader_interlock();
#endif /* GL_ARB_fragment_shader_interlock */
#ifdef GL_ARB_framebuffer_no_attachments
  _glewInfo_GL_ARB_framebuffer_no_attachments();
#endif /* GL_ARB_framebuffer_no_attachments */
#ifdef GL_ARB_framebuffer_object
  _glewInfo_GL_ARB_framebuffer_object();
#endif /* GL_ARB_framebuffer_object */
#ifdef GL_ARB_framebuffer_sRGB
  _glewInfo_GL_ARB_framebuffer_sRGB();
#endif /* GL_ARB_framebuffer_sRGB */
#ifdef GL_ARB_geometry_shader4
  _glewInfo_GL_ARB_geometry_shader4();
#endif /* GL_ARB_geometry_shader4 */
#ifdef GL_ARB_get_program_binary
  _glewInfo_GL_ARB_get_program_binary();
#endif /* GL_ARB_get_program_binary */
#ifdef GL_ARB_get_texture_sub_image
  _glewInfo_GL_ARB_get_texture_sub_image();
#endif /* GL_ARB_get_texture_sub_image */
#ifdef GL_ARB_gl_spirv
  _glewInfo_GL_ARB_gl_spirv();
#endif /* GL_ARB_gl_spirv */
#ifdef GL_ARB_gpu_shader5
  _glewInfo_GL_ARB_gpu_shader5();
#endif /* GL_ARB_gpu_shader5 */
#ifdef GL_ARB_gpu_shader_fp64
  _glewInfo_GL_ARB_gpu_shader_fp64();
#endif /* GL_ARB_gpu_shader_fp64 */
#ifdef GL_ARB_gpu_shader_int64
  _glewInfo_GL_ARB_gpu_shader_int64();
#endif /* GL_ARB_gpu_shader_int64 */
#ifdef GL_ARB_half_float_pixel
  _glewInfo_GL_ARB_half_float_pixel();
#endif /* GL_ARB_half_float_pixel */
#ifdef GL_ARB_half_float_vertex
  _glewInfo_GL_ARB_half_float_vertex();
#endif /* GL_ARB_half_float_vertex */
#ifdef GL_ARB_imaging
  _glewInfo_GL_ARB_imaging();
#endif /* GL_ARB_imaging */
#ifdef GL_ARB_indirect_parameters
  _glewInfo_GL_ARB_indirect_parameters();
#endif /* GL_ARB_indirect_parameters */
#ifdef GL_ARB_instanced_arrays
  _glewInfo_GL_ARB_instanced_arrays();
#endif /* GL_ARB_instanced_arrays */
#ifdef GL_ARB_internalformat_query
  _glewInfo_GL_ARB_internalformat_query();
#endif /* GL_ARB_internalformat_query */
#ifdef GL_ARB_internalformat_query2
  _glewInfo_GL_ARB_internalformat_query2();
#endif /* GL_ARB_internalformat_query2 */
#ifdef GL_ARB_invalidate_subdata
  _glewInfo_GL_ARB_invalidate_subdata();
#endif /* GL_ARB_invalidate_subdata */
#ifdef GL_ARB_map_buffer_alignment
  _glewInfo_GL_ARB_map_buffer_alignment();
#endif /* GL_ARB_map_buffer_alignment */
#ifdef GL_ARB_map_buffer_range
  _glewInfo_GL_ARB_map_buffer_range();
#endif /* GL_ARB_map_buffer_range */
#ifdef GL_ARB_matrix_palette
  _glewInfo_GL_ARB_matrix_palette();
#endif /* GL_ARB_matrix_palette */
#ifdef GL_ARB_multi_bind
  _glewInfo_GL_ARB_multi_bind();
#endif /* GL_ARB_multi_bind */
#ifdef GL_ARB_multi_draw_indirect
  _glewInfo_GL_ARB_multi_draw_indirect();
#endif /* GL_ARB_multi_draw_indirect */
#ifdef GL_ARB_multisample
  _glewInfo_GL_ARB_multisample();
#endif /* GL_ARB_multisample */
#ifdef GL_ARB_multitexture
  _glewInfo_GL_ARB_multitexture();
#endif /* GL_ARB_multitexture */
#ifdef GL_ARB_occlusion_query
  _glewInfo_GL_ARB_occlusion_query();
#endif /* GL_ARB_occlusion_query */
#ifdef GL_ARB_occlusion_query2
  _glewInfo_GL_ARB_occlusion_query2();
#endif /* GL_ARB_occlusion_query2 */
#ifdef GL_ARB_parallel_shader_compile
  _glewInfo_GL_ARB_parallel_shader_compile();
#endif /* GL_ARB_parallel_shader_compile */
#ifdef GL_ARB_pipeline_statistics_query
  _glewInfo_GL_ARB_pipeline_statistics_query();
#endif /* GL_ARB_pipeline_statistics_query */
#ifdef GL_ARB_pixel_buffer_object
  _glewInfo_GL_ARB_pixel_buffer_object();
#endif /* GL_ARB_pixel_buffer_object */
#ifdef GL_ARB_point_parameters
  _glewInfo_GL_ARB_point_parameters();
#endif /* GL_ARB_point_parameters */
#ifdef GL_ARB_point_sprite
  _glewInfo_GL_ARB_point_sprite();
#endif /* GL_ARB_point_sprite */
#ifdef GL_ARB_polygon_offset_clamp
  _glewInfo_GL_ARB_polygon_offset_clamp();
#endif /* GL_ARB_polygon_offset_clamp */
#ifdef GL_ARB_post_depth_coverage
  _glewInfo_GL_ARB_post_depth_coverage();
#endif /* GL_ARB_post_depth_coverage */
#ifdef GL_ARB_program_interface_query
  _glewInfo_GL_ARB_program_interface_query();
#endif /* GL_ARB_program_interface_query */
#ifdef GL_ARB_provoking_vertex
  _glewInfo_GL_ARB_provoking_vertex();
#endif /* GL_ARB_provoking_vertex */
#ifdef GL_ARB_query_buffer_object
  _glewInfo_GL_ARB_query_buffer_object();
#endif /* GL_ARB_query_buffer_object */
#ifdef GL_ARB_robust_buffer_access_behavior
  _glewInfo_GL_ARB_robust_buffer_access_behavior();
#endif /* GL_ARB_robust_buffer_access_behavior */
#ifdef GL_ARB_robustness
  _glewInfo_GL_ARB_robustness();
#endif /* GL_ARB_robustness */
#ifdef GL_ARB_robustness_application_isolation
  _glewInfo_GL_ARB_robustness_application_isolation();
#endif /* GL_ARB_robustness_application_isolation */
#ifdef GL_ARB_robustness_share_group_isolation
  _glewInfo_GL_ARB_robustness_share_group_isolation();
#endif /* GL_ARB_robustness_share_group_isolation */
#ifdef GL_ARB_sample_locations
  _glewInfo_GL_ARB_sample_locations();
#endif /* GL_ARB_sample_locations */
#ifdef GL_ARB_sample_shading
  _glewInfo_GL_ARB_sample_shading();
#endif /* GL_ARB_sample_shading */
#ifdef GL_ARB_sampler_objects
  _glewInfo_GL_ARB_sampler_objects();
#endif /* GL_ARB_sampler_objects */
#ifdef GL_ARB_seamless_cube_map
  _glewInfo_GL_ARB_seamless_cube_map();
#endif /* GL_ARB_seamless_cube_map */
#ifdef GL_ARB_seamless_cubemap_per_texture
  _glewInfo_GL_ARB_seamless_cubemap_per_texture();
#endif /* GL_ARB_seamless_cubemap_per_texture */
#ifdef GL_ARB_separate_shader_objects
  _glewInfo_GL_ARB_separate_shader_objects();
#endif /* GL_ARB_separate_shader_objects */
#ifdef GL_ARB_shader_atomic_counter_ops
  _glewInfo_GL_ARB_shader_atomic_counter_ops();
#endif /* GL_ARB_shader_atomic_counter_ops */
#ifdef GL_ARB_shader_atomic_counters
  _glewInfo_GL_ARB_shader_atomic_counters();
#endif /* GL_ARB_shader_atomic_counters */
#ifdef GL_ARB_shader_ballot
  _glewInfo_GL_ARB_shader_ballot();
#endif /* GL_ARB_shader_ballot */
#ifdef GL_ARB_shader_bit_encoding
  _glewInfo_GL_ARB_shader_bit_encoding();
#endif /* GL_ARB_shader_bit_encoding */
#ifdef GL_ARB_shader_clock
  _glewInfo_GL_ARB_shader_clock();
#endif /* GL_ARB_shader_clock */
#ifdef GL_ARB_shader_draw_parameters
  _glewInfo_GL_ARB_shader_draw_parameters();
#endif /* GL_ARB_shader_draw_parameters */
#ifdef GL_ARB_shader_group_vote
  _glewInfo_GL_ARB_shader_group_vote();
#endif /* GL_ARB_shader_group_vote */
#ifdef GL_ARB_shader_image_load_store
  _glewInfo_GL_ARB_shader_image_load_store();
#endif /* GL_ARB_shader_image_load_store */
#ifdef GL_ARB_shader_image_size
  _glewInfo_GL_ARB_shader_image_size();
#endif /* GL_ARB_shader_image_size */
#ifdef GL_ARB_shader_objects
  _glewInfo_GL_ARB_shader_objects();
#endif /* GL_ARB_shader_objects */
#ifdef GL_ARB_shader_precision
  _glewInfo_GL_ARB_shader_precision();
#endif /* GL_ARB_shader_precision */
#ifdef GL_ARB_shader_stencil_export
  _glewInfo_GL_ARB_shader_stencil_export();
#endif /* GL_ARB_shader_stencil_export */
#ifdef GL_ARB_shader_storage_buffer_object
  _glewInfo_GL_ARB_shader_storage_buffer_object();
#endif /* GL_ARB_shader_storage_buffer_object */
#ifdef GL_ARB_shader_subroutine
  _glewInfo_GL_ARB_shader_subroutine();
#endif /* GL_ARB_shader_subroutine */
#ifdef GL_ARB_shader_texture_image_samples
  _glewInfo_GL_ARB_shader_texture_image_samples();
#endif /* GL_ARB_shader_texture_image_samples */
#ifdef GL_ARB_shader_texture_lod
  _glewInfo_GL_ARB_shader_texture_lod();
#endif /* GL_ARB_shader_texture_lod */
#ifdef GL_ARB_shader_viewport_layer_array
  _glewInfo_GL_ARB_shader_viewport_layer_array();
#endif /* GL_ARB_shader_viewport_layer_array */
#ifdef GL_ARB_shading_language_100
  _glewInfo_GL_ARB_shading_language_100();
#endif /* GL_ARB_shading_language_100 */
#ifdef GL_ARB_shading_language_420pack
  _glewInfo_GL_ARB_shading_language_420pack();
#endif /* GL_ARB_shading_language_420pack */
#ifdef GL_ARB_shading_language_include
  _glewInfo_GL_ARB_shading_language_include();
#endif /* GL_ARB_shading_language_include */
#ifdef GL_ARB_shading_language_packing
  _glewInfo_GL_ARB_shading_language_packing();
#endif /* GL_ARB_shading_language_packing */
#ifdef GL_ARB_shadow
  _glewInfo_GL_ARB_shadow();
#endif /* GL_ARB_shadow */
#ifdef GL_ARB_shadow_ambient
  _glewInfo_GL_ARB_shadow_ambient();
#endif /* GL_ARB_shadow_ambient */
#ifdef GL_ARB_sparse_buffer
  _glewInfo_GL_ARB_sparse_buffer();
#endif /* GL_ARB_sparse_buffer */
#ifdef GL_ARB_sparse_texture
  _glewInfo_GL_ARB_sparse_texture();
#endif /* GL_ARB_sparse_texture */
#ifdef GL_ARB_sparse_texture2
  _glewInfo_GL_ARB_sparse_texture2();
#endif /* GL_ARB_sparse_texture2 */
#ifdef GL_ARB_sparse_texture_clamp
  _glewInfo_GL_ARB_sparse_texture_clamp();
#endif /* GL_ARB_sparse_texture_clamp */
#ifdef GL_ARB_spirv_extensions
  _glewInfo_GL_ARB_spirv_extensions();
#endif /* GL_ARB_spirv_extensions */
#ifdef GL_ARB_stencil_texturing
  _glewInfo_GL_ARB_stencil_texturing();
#endif /* GL_ARB_stencil_texturing */
#ifdef GL_ARB_sync
  _glewInfo_GL_ARB_sync();
#endif /* GL_ARB_sync */
#ifdef GL_ARB_tessellation_shader
  _glewInfo_GL_ARB_tessellation_shader();
#endif /* GL_ARB_tessellation_shader */
#ifdef GL_ARB_texture_barrier
  _glewInfo_GL_ARB_texture_barrier();
#endif /* GL_ARB_texture_barrier */
#ifdef GL_ARB_texture_border_clamp
  _glewInfo_GL_ARB_texture_border_clamp();
#endif /* GL_ARB_texture_border_clamp */
#ifdef GL_ARB_texture_buffer_object
  _glewInfo_GL_ARB_texture_buffer_object();
#endif /* GL_ARB_texture_buffer_object */
#ifdef GL_ARB_texture_buffer_object_rgb32
  _glewInfo_GL_ARB_texture_buffer_object_rgb32();
#endif /* GL_ARB_texture_buffer_object_rgb32 */
#ifdef GL_ARB_texture_buffer_range
  _glewInfo_GL_ARB_texture_buffer_range();
#endif /* GL_ARB_texture_buffer_range */
#ifdef GL_ARB_texture_compression
  _glewInfo_GL_ARB_texture_compression();
#endif /* GL_ARB_texture_compression */
#ifdef GL_ARB_texture_compression_bptc
  _glewInfo_GL_ARB_texture_compression_bptc();
#endif /* GL_ARB_texture_compression_bptc */
#ifdef GL_ARB_texture_compression_rgtc
  _glewInfo_GL_ARB_texture_compression_rgtc();
#endif /* GL_ARB_texture_compression_rgtc */
#ifdef GL_ARB_texture_cube_map
  _glewInfo_GL_ARB_texture_cube_map();
#endif /* GL_ARB_texture_cube_map */
#ifdef GL_ARB_texture_cube_map_array
  _glewInfo_GL_ARB_texture_cube_map_array();
#endif /* GL_ARB_texture_cube_map_array */
#ifdef GL_ARB_texture_env_add
  _glewInfo_GL_ARB_texture_env_add();
#endif /* GL_ARB_texture_env_add */
#ifdef GL_ARB_texture_env_combine
  _glewInfo_GL_ARB_texture_env_combine();
#endif /* GL_ARB_texture_env_combine */
#ifdef GL_ARB_texture_env_crossbar
  _glewInfo_GL_ARB_texture_env_crossbar();
#endif /* GL_ARB_texture_env_crossbar */
#ifdef GL_ARB_texture_env_dot3
  _glewInfo_GL_ARB_texture_env_dot3();
#endif /* GL_ARB_texture_env_dot3 */
#ifdef GL_ARB_texture_filter_anisotropic
  _glewInfo_GL_ARB_texture_filter_anisotropic();
#endif /* GL_ARB_texture_filter_anisotropic */
#ifdef GL_ARB_texture_filter_minmax
  _glewInfo_GL_ARB_texture_filter_minmax();
#endif /* GL_ARB_texture_filter_minmax */
#ifdef GL_ARB_texture_float
  _glewInfo_GL_ARB_texture_float();
#endif /* GL_ARB_texture_float */
#ifdef GL_ARB_texture_gather
  _glewInfo_GL_ARB_texture_gather();
#endif /* GL_ARB_texture_gather */
#ifdef GL_ARB_texture_mirror_clamp_to_edge
  _glewInfo_GL_ARB_texture_mirror_clamp_to_edge();
#endif /* GL_ARB_texture_mirror_clamp_to_edge */
#ifdef GL_ARB_texture_mirrored_repeat
  _glewInfo_GL_ARB_texture_mirrored_repeat();
#endif /* GL_ARB_texture_mirrored_repeat */
#ifdef GL_ARB_texture_multisample
  _glewInfo_GL_ARB_texture_multisample();
#endif /* GL_ARB_texture_multisample */
#ifdef GL_ARB_texture_non_power_of_two
  _glewInfo_GL_ARB_texture_non_power_of_two();
#endif /* GL_ARB_texture_non_power_of_two */
#ifdef GL_ARB_texture_query_levels
  _glewInfo_GL_ARB_texture_query_levels();
#endif /* GL_ARB_texture_query_levels */
#ifdef GL_ARB_texture_query_lod
  _glewInfo_GL_ARB_texture_query_lod();
#endif /* GL_ARB_texture_query_lod */
#ifdef GL_ARB_texture_rectangle
  _glewInfo_GL_ARB_texture_rectangle();
#endif /* GL_ARB_texture_rectangle */
#ifdef GL_ARB_texture_rg
  _glewInfo_GL_ARB_texture_rg();
#endif /* GL_ARB_texture_rg */
#ifdef GL_ARB_texture_rgb10_a2ui
  _glewInfo_GL_ARB_texture_rgb10_a2ui();
#endif /* GL_ARB_texture_rgb10_a2ui */
#ifdef GL_ARB_texture_stencil8
  _glewInfo_GL_ARB_texture_stencil8();
#endif /* GL_ARB_texture_stencil8 */
#ifdef GL_ARB_texture_storage
  _glewInfo_GL_ARB_texture_storage();
#endif /* GL_ARB_texture_storage */
#ifdef GL_ARB_texture_storage_multisample
  _glewInfo_GL_ARB_texture_storage_multisample();
#endif /* GL_ARB_texture_storage_multisample */
#ifdef GL_ARB_texture_swizzle
  _glewInfo_GL_ARB_texture_swizzle();
#endif /* GL_ARB_texture_swizzle */
#ifdef GL_ARB_texture_view
  _glewInfo_GL_ARB_texture_view();
#endif /* GL_ARB_texture_view */
#ifdef GL_ARB_timer_query
  _glewInfo_GL_ARB_timer_query();
#endif /* GL_ARB_timer_query */
#ifdef GL_ARB_transform_feedback2
  _glewInfo_GL_ARB_transform_feedback2();
#endif /* GL_ARB_transform_feedback2 */
#ifdef GL_ARB_transform_feedback3
  _glewInfo_GL_ARB_transform_feedback3();
#endif /* GL_ARB_transform_feedback3 */
#ifdef GL_ARB_transform_feedback_instanced
  _glewInfo_GL_ARB_transform_feedback_instanced();
#endif /* GL_ARB_transform_feedback_instanced */
#ifdef GL_ARB_transform_feedback_overflow_query
  _glewInfo_GL_ARB_transform_feedback_overflow_query();
#endif /* GL_ARB_transform_feedback_overflow_query */
#ifdef GL_ARB_transpose_matrix
  _glewInfo_GL_ARB_transpose_matrix();
#endif /* GL_ARB_transpose_matrix */
#ifdef GL_ARB_uniform_buffer_object
  _glewInfo_GL_ARB_uniform_buffer_object();
#endif /* GL_ARB_uniform_buffer_object */
#ifdef GL_ARB_vertex_array_bgra
  _glewInfo_GL_ARB_vertex_array_bgra();
#endif /* GL_ARB_vertex_array_bgra */
#ifdef GL_ARB_vertex_array_object
  _glewInfo_GL_ARB_vertex_array_object();
#endif /* GL_ARB_vertex_array_object */
#ifdef GL_ARB_vertex_attrib_64bit
  _glewInfo_GL_ARB_vertex_attrib_64bit();
#endif /* GL_ARB_vertex_attrib_64bit */
#ifdef GL_ARB_vertex_attrib_binding
  _glewInfo_GL_ARB_vertex_attrib_binding();
#endif /* GL_ARB_vertex_attrib_binding */
#ifdef GL_ARB_vertex_blend
  _glewInfo_GL_ARB_vertex_blend();
#endif /* GL_ARB_vertex_blend */
#ifdef GL_ARB_vertex_buffer_object
  _glewInfo_GL_ARB_vertex_buffer_object();
#endif /* GL_ARB_vertex_buffer_object */
#ifdef GL_ARB_vertex_program
  _glewInfo_GL_ARB_vertex_program();
#endif /* GL_ARB_vertex_program */
#ifdef GL_ARB_vertex_shader
  _glewInfo_GL_ARB_vertex_shader();
#endif /* GL_ARB_vertex_shader */
#ifdef GL_ARB_vertex_type_10f_11f_11f_rev
  _glewInfo_GL_ARB_vertex_type_10f_11f_11f_rev();
#endif /* GL_ARB_vertex_type_10f_11f_11f_rev */
#ifdef GL_ARB_vertex_type_2_10_10_10_rev
  _glewInfo_GL_ARB_vertex_type_2_10_10_10_rev();
#endif /* GL_ARB_vertex_type_2_10_10_10_rev */
#ifdef GL_ARB_viewport_array
  _glewInfo_GL_ARB_viewport_array();
#endif /* GL_ARB_viewport_array */
#ifdef GL_ARB_window_pos
  _glewInfo_GL_ARB_window_pos();
#endif /* GL_ARB_window_pos */
#ifdef GL_ARM_mali_program_binary
  _glewInfo_GL_ARM_mali_program_binary();
#endif /* GL_ARM_mali_program_binary */
#ifdef GL_ARM_mali_shader_binary
  _glewInfo_GL_ARM_mali_shader_binary();
#endif /* GL_ARM_mali_shader_binary */
#ifdef GL_ARM_rgba8
  _glewInfo_GL_ARM_rgba8();
#endif /* GL_ARM_rgba8 */
#ifdef GL_ARM_shader_framebuffer_fetch
  _glewInfo_GL_ARM_shader_framebuffer_fetch();
#endif /* GL_ARM_shader_framebuffer_fetch */
#ifdef GL_ARM_shader_framebuffer_fetch_depth_stencil
  _glewInfo_GL_ARM_shader_framebuffer_fetch_depth_stencil();
#endif /* GL_ARM_shader_framebuffer_fetch_depth_stencil */
#ifdef GL_ATIX_point_sprites
  _glewInfo_GL_ATIX_point_sprites();
#endif /* GL_ATIX_point_sprites */
#ifdef GL_ATIX_texture_env_combine3
  _glewInfo_GL_ATIX_texture_env_combine3();
#endif /* GL_ATIX_texture_env_combine3 */
#ifdef GL_ATIX_texture_env_route
  _glewInfo_GL_ATIX_texture_env_route();
#endif /* GL_ATIX_texture_env_route */
#ifdef GL_ATIX_vertex_shader_output_point_size
  _glewInfo_GL_ATIX_vertex_shader_output_point_size();
#endif /* GL_ATIX_vertex_shader_output_point_size */
#ifdef GL_ATI_draw_buffers
  _glewInfo_GL_ATI_draw_buffers();
#endif /* GL_ATI_draw_buffers */
#ifdef GL_ATI_element_array
  _glewInfo_GL_ATI_element_array();
#endif /* GL_ATI_element_array */
#ifdef GL_ATI_envmap_bumpmap
  _glewInfo_GL_ATI_envmap_bumpmap();
#endif /* GL_ATI_envmap_bumpmap */
#ifdef GL_ATI_fragment_shader
  _glewInfo_GL_ATI_fragment_shader();
#endif /* GL_ATI_fragment_shader */
#ifdef GL_ATI_map_object_buffer
  _glewInfo_GL_ATI_map_object_buffer();
#endif /* GL_ATI_map_object_buffer */
#ifdef GL_ATI_meminfo
  _glewInfo_GL_ATI_meminfo();
#endif /* GL_ATI_meminfo */
#ifdef GL_ATI_pn_triangles
  _glewInfo_GL_ATI_pn_triangles();
#endif /* GL_ATI_pn_triangles */
#ifdef GL_ATI_separate_stencil
  _glewInfo_GL_ATI_separate_stencil();
#endif /* GL_ATI_separate_stencil */
#ifdef GL_ATI_shader_texture_lod
  _glewInfo_GL_ATI_shader_texture_lod();
#endif /* GL_ATI_shader_texture_lod */
#ifdef GL_ATI_text_fragment_shader
  _glewInfo_GL_ATI_text_fragment_shader();
#endif /* GL_ATI_text_fragment_shader */
#ifdef GL_ATI_texture_compression_3dc
  _glewInfo_GL_ATI_texture_compression_3dc();
#endif /* GL_ATI_texture_compression_3dc */
#ifdef GL_ATI_texture_env_combine3
  _glewInfo_GL_ATI_texture_env_combine3();
#endif /* GL_ATI_texture_env_combine3 */
#ifdef GL_ATI_texture_float
  _glewInfo_GL_ATI_texture_float();
#endif /* GL_ATI_texture_float */
#ifdef GL_ATI_texture_mirror_once
  _glewInfo_GL_ATI_texture_mirror_once();
#endif /* GL_ATI_texture_mirror_once */
#ifdef GL_ATI_vertex_array_object
  _glewInfo_GL_ATI_vertex_array_object();
#endif /* GL_ATI_vertex_array_object */
#ifdef GL_ATI_vertex_attrib_array_object
  _glewInfo_GL_ATI_vertex_attrib_array_object();
#endif /* GL_ATI_vertex_attrib_array_object */
#ifdef GL_ATI_vertex_streams
  _glewInfo_GL_ATI_vertex_streams();
#endif /* GL_ATI_vertex_streams */
#ifdef GL_EGL_KHR_context_flush_control
  _glewInfo_GL_EGL_KHR_context_flush_control();
#endif /* GL_EGL_KHR_context_flush_control */
#ifdef GL_EGL_NV_robustness_video_memory_purge
  _glewInfo_GL_EGL_NV_robustness_video_memory_purge();
#endif /* GL_EGL_NV_robustness_video_memory_purge */
#ifdef GL_EXT_422_pixels
  _glewInfo_GL_EXT_422_pixels();
#endif /* GL_EXT_422_pixels */
#ifdef GL_EXT_Cg_shader
  _glewInfo_GL_EXT_Cg_shader();
#endif /* GL_EXT_Cg_shader */
#ifdef GL_EXT_EGL_image_array
  _glewInfo_GL_EXT_EGL_image_array();
#endif /* GL_EXT_EGL_image_array */
#ifdef GL_EXT_YUV_target
  _glewInfo_GL_EXT_YUV_target();
#endif /* GL_EXT_YUV_target */
#ifdef GL_EXT_abgr
  _glewInfo_GL_EXT_abgr();
#endif /* GL_EXT_abgr */
#ifdef GL_EXT_base_instance
  _glewInfo_GL_EXT_base_instance();
#endif /* GL_EXT_base_instance */
#ifdef GL_EXT_bgra
  _glewInfo_GL_EXT_bgra();
#endif /* GL_EXT_bgra */
#ifdef GL_EXT_bindable_uniform
  _glewInfo_GL_EXT_bindable_uniform();
#endif /* GL_EXT_bindable_uniform */
#ifdef GL_EXT_blend_color
  _glewInfo_GL_EXT_blend_color();
#endif /* GL_EXT_blend_color */
#ifdef GL_EXT_blend_equation_separate
  _glewInfo_GL_EXT_blend_equation_separate();
#endif /* GL_EXT_blend_equation_separate */
#ifdef GL_EXT_blend_func_extended
  _glewInfo_GL_EXT_blend_func_extended();
#endif /* GL_EXT_blend_func_extended */
#ifdef GL_EXT_blend_func_separate
  _glewInfo_GL_EXT_blend_func_separate();
#endif /* GL_EXT_blend_func_separate */
#ifdef GL_EXT_blend_logic_op
  _glewInfo_GL_EXT_blend_logic_op();
#endif /* GL_EXT_blend_logic_op */
#ifdef GL_EXT_blend_minmax
  _glewInfo_GL_EXT_blend_minmax();
#endif /* GL_EXT_blend_minmax */
#ifdef GL_EXT_blend_subtract
  _glewInfo_GL_EXT_blend_subtract();
#endif /* GL_EXT_blend_subtract */
#ifdef GL_EXT_buffer_storage
  _glewInfo_GL_EXT_buffer_storage();
#endif /* GL_EXT_buffer_storage */
#ifdef GL_EXT_clear_texture
  _glewInfo_GL_EXT_clear_texture();
#endif /* GL_EXT_clear_texture */
#ifdef GL_EXT_clip_cull_distance
  _glewInfo_GL_EXT_clip_cull_distance();
#endif /* GL_EXT_clip_cull_distance */
#ifdef GL_EXT_clip_volume_hint
  _glewInfo_GL_EXT_clip_volume_hint();
#endif /* GL_EXT_clip_volume_hint */
#ifdef GL_EXT_cmyka
  _glewInfo_GL_EXT_cmyka();
#endif /* GL_EXT_cmyka */
#ifdef GL_EXT_color_buffer_float
  _glewInfo_GL_EXT_color_buffer_float();
#endif /* GL_EXT_color_buffer_float */
#ifdef GL_EXT_color_buffer_half_float
  _glewInfo_GL_EXT_color_buffer_half_float();
#endif /* GL_EXT_color_buffer_half_float */
#ifdef GL_EXT_color_subtable
  _glewInfo_GL_EXT_color_subtable();
#endif /* GL_EXT_color_subtable */
#ifdef GL_EXT_compiled_vertex_array
  _glewInfo_GL_EXT_compiled_vertex_array();
#endif /* GL_EXT_compiled_vertex_array */
#ifdef GL_EXT_compressed_ETC1_RGB8_sub_texture
  _glewInfo_GL_EXT_compressed_ETC1_RGB8_sub_texture();
#endif /* GL_EXT_compressed_ETC1_RGB8_sub_texture */
#ifdef GL_EXT_conservative_depth
  _glewInfo_GL_EXT_conservative_depth();
#endif /* GL_EXT_conservative_depth */
#ifdef GL_EXT_convolution
  _glewInfo_GL_EXT_convolution();
#endif /* GL_EXT_convolution */
#ifdef GL_EXT_coordinate_frame
  _glewInfo_GL_EXT_coordinate_frame();
#endif /* GL_EXT_coordinate_frame */
#ifdef GL_EXT_copy_image
  _glewInfo_GL_EXT_copy_image();
#endif /* GL_EXT_copy_image */
#ifdef GL_EXT_copy_texture
  _glewInfo_GL_EXT_copy_texture();
#endif /* GL_EXT_copy_texture */
#ifdef GL_EXT_cull_vertex
  _glewInfo_GL_EXT_cull_vertex();
#endif /* GL_EXT_cull_vertex */
#ifdef GL_EXT_debug_label
  _glewInfo_GL_EXT_debug_label();
#endif /* GL_EXT_debug_label */
#ifdef GL_EXT_debug_marker
  _glewInfo_GL_EXT_debug_marker();
#endif /* GL_EXT_debug_marker */
#ifdef GL_EXT_depth_bounds_test
  _glewInfo_GL_EXT_depth_bounds_test();
#endif /* GL_EXT_depth_bounds_test */
#ifdef GL_EXT_direct_state_access
  _glewInfo_GL_EXT_direct_state_access();
#endif /* GL_EXT_direct_state_access */
#ifdef GL_EXT_discard_framebuffer
  _glewInfo_GL_EXT_discard_framebuffer();
#endif /* GL_EXT_discard_framebuffer */
#ifdef GL_EXT_draw_buffers
  _glewInfo_GL_EXT_draw_buffers();
#endif /* GL_EXT_draw_buffers */
#ifdef GL_EXT_draw_buffers2
  _glewInfo_GL_EXT_draw_buffers2();
#endif /* GL_EXT_draw_buffers2 */
#ifdef GL_EXT_draw_buffers_indexed
  _glewInfo_GL_EXT_draw_buffers_indexed();
#endif /* GL_EXT_draw_buffers_indexed */
#ifdef GL_EXT_draw_elements_base_vertex
  _glewInfo_GL_EXT_draw_elements_base_vertex();
#endif /* GL_EXT_draw_elements_base_vertex */
#ifdef GL_EXT_draw_instanced
  _glewInfo_GL_EXT_draw_instanced();
#endif /* GL_EXT_draw_instanced */
#ifdef GL_EXT_draw_range_elements
  _glewInfo_GL_EXT_draw_range_elements();
#endif /* GL_EXT_draw_range_elements */
#ifdef GL_EXT_external_buffer
  _glewInfo_GL_EXT_external_buffer();
#endif /* GL_EXT_external_buffer */
#ifdef GL_EXT_float_blend
  _glewInfo_GL_EXT_float_blend();
#endif /* GL_EXT_float_blend */
#ifdef GL_EXT_fog_coord
  _glewInfo_GL_EXT_fog_coord();
#endif /* GL_EXT_fog_coord */
#ifdef GL_EXT_frag_depth
  _glewInfo_GL_EXT_frag_depth();
#endif /* GL_EXT_frag_depth */
#ifdef GL_EXT_fragment_lighting
  _glewInfo_GL_EXT_fragment_lighting();
#endif /* GL_EXT_fragment_lighting */
#ifdef GL_EXT_framebuffer_blit
  _glewInfo_GL_EXT_framebuffer_blit();
#endif /* GL_EXT_framebuffer_blit */
#ifdef GL_EXT_framebuffer_multisample
  _glewInfo_GL_EXT_framebuffer_multisample();
#endif /* GL_EXT_framebuffer_multisample */
#ifdef GL_EXT_framebuffer_multisample_blit_scaled
  _glewInfo_GL_EXT_framebuffer_multisample_blit_scaled();
#endif /* GL_EXT_framebuffer_multisample_blit_scaled */
#ifdef GL_EXT_framebuffer_object
  _glewInfo_GL_EXT_framebuffer_object();
#endif /* GL_EXT_framebuffer_object */
#ifdef GL_EXT_framebuffer_sRGB
  _glewInfo_GL_EXT_framebuffer_sRGB();
#endif /* GL_EXT_framebuffer_sRGB */
#ifdef GL_EXT_geometry_point_size
  _glewInfo_GL_EXT_geometry_point_size();
#endif /* GL_EXT_geometry_point_size */
#ifdef GL_EXT_geometry_shader
  _glewInfo_GL_EXT_geometry_shader();
#endif /* GL_EXT_geometry_shader */
#ifdef GL_EXT_geometry_shader4
  _glewInfo_GL_EXT_geometry_shader4();
#endif /* GL_EXT_geometry_shader4 */
#ifdef GL_EXT_gpu_program_parameters
  _glewInfo_GL_EXT_gpu_program_parameters();
#endif /* GL_EXT_gpu_program_parameters */
#ifdef GL_EXT_gpu_shader4
  _glewInfo_GL_EXT_gpu_shader4();
#endif /* GL_EXT_gpu_shader4 */
#ifdef GL_EXT_gpu_shader5
  _glewInfo_GL_EXT_gpu_shader5();
#endif /* GL_EXT_gpu_shader5 */
#ifdef GL_EXT_histogram
  _glewInfo_GL_EXT_histogram();
#endif /* GL_EXT_histogram */
#ifdef GL_EXT_index_array_formats
  _glewInfo_GL_EXT_index_array_formats();
#endif /* GL_EXT_index_array_formats */
#ifdef GL_EXT_index_func
  _glewInfo_GL_EXT_index_func();
#endif /* GL_EXT_index_func */
#ifdef GL_EXT_index_material
  _glewInfo_GL_EXT_index_material();
#endif /* GL_EXT_index_material */
#ifdef GL_EXT_index_texture
  _glewInfo_GL_EXT_index_texture();
#endif /* GL_EXT_index_texture */
#ifdef GL_EXT_instanced_arrays
  _glewInfo_GL_EXT_instanced_arrays();
#endif /* GL_EXT_instanced_arrays */
#ifdef GL_EXT_light_texture
  _glewInfo_GL_EXT_light_texture();
#endif /* GL_EXT_light_texture */
#ifdef GL_EXT_map_buffer_range
  _glewInfo_GL_EXT_map_buffer_range();
#endif /* GL_EXT_map_buffer_range */
#ifdef GL_EXT_memory_object
  _glewInfo_GL_EXT_memory_object();
#endif /* GL_EXT_memory_object */
#ifdef GL_EXT_memory_object_fd
  _glewInfo_GL_EXT_memory_object_fd();
#endif /* GL_EXT_memory_object_fd */
#ifdef GL_EXT_memory_object_win32
  _glewInfo_GL_EXT_memory_object_win32();
#endif /* GL_EXT_memory_object_win32 */
#ifdef GL_EXT_misc_attribute
  _glewInfo_GL_EXT_misc_attribute();
#endif /* GL_EXT_misc_attribute */
#ifdef GL_EXT_multi_draw_arrays
  _glewInfo_GL_EXT_multi_draw_arrays();
#endif /* GL_EXT_multi_draw_arrays */
#ifdef GL_EXT_multi_draw_indirect
  _glewInfo_GL_EXT_multi_draw_indirect();
#endif /* GL_EXT_multi_draw_indirect */
#ifdef GL_EXT_multiple_textures
  _glewInfo_GL_EXT_multiple_textures();
#endif /* GL_EXT_multiple_textures */
#ifdef GL_EXT_multisample
  _glewInfo_GL_EXT_multisample();
#endif /* GL_EXT_multisample */
#ifdef GL_EXT_multisample_compatibility
  _glewInfo_GL_EXT_multisample_compatibility();
#endif /* GL_EXT_multisample_compatibility */
#ifdef GL_EXT_multisampled_render_to_texture
  _glewInfo_GL_EXT_multisampled_render_to_texture();
#endif /* GL_EXT_multisampled_render_to_texture */
#ifdef GL_EXT_multisampled_render_to_texture2
  _glewInfo_GL_EXT_multisampled_render_to_texture2();
#endif /* GL_EXT_multisampled_render_to_texture2 */
#ifdef GL_EXT_multiview_draw_buffers
  _glewInfo_GL_EXT_multiview_draw_buffers();
#endif /* GL_EXT_multiview_draw_buffers */
#ifdef GL_EXT_packed_depth_stencil
  _glewInfo_GL_EXT_packed_depth_stencil();
#endif /* GL_EXT_packed_depth_stencil */
#ifdef GL_EXT_packed_float
  _glewInfo_GL_EXT_packed_float();
#endif /* GL_EXT_packed_float */
#ifdef GL_EXT_packed_pixels
  _glewInfo_GL_EXT_packed_pixels();
#endif /* GL_EXT_packed_pixels */
#ifdef GL_EXT_paletted_texture
  _glewInfo_GL_EXT_paletted_texture();
#endif /* GL_EXT_paletted_texture */
#ifdef GL_EXT_pixel_buffer_object
  _glewInfo_GL_EXT_pixel_buffer_object();
#endif /* GL_EXT_pixel_buffer_object */
#ifdef GL_EXT_pixel_transform
  _glewInfo_GL_EXT_pixel_transform();
#endif /* GL_EXT_pixel_transform */
#ifdef GL_EXT_pixel_transform_color_table
  _glewInfo_GL_EXT_pixel_transform_color_table();
#endif /* GL_EXT_pixel_transform_color_table */
#ifdef GL_EXT_point_parameters
  _glewInfo_GL_EXT_point_parameters();
#endif /* GL_EXT_point_parameters */
#ifdef GL_EXT_polygon_offset
  _glewInfo_GL_EXT_polygon_offset();
#endif /* GL_EXT_polygon_offset */
#ifdef GL_EXT_polygon_offset_clamp
  _glewInfo_GL_EXT_polygon_offset_clamp();
#endif /* GL_EXT_polygon_offset_clamp */
#ifdef GL_EXT_post_depth_coverage
  _glewInfo_GL_EXT_post_depth_coverage();
#endif /* GL_EXT_post_depth_coverage */
#ifdef GL_EXT_provoking_vertex
  _glewInfo_GL_EXT_provoking_vertex();
#endif /* GL_EXT_provoking_vertex */
#ifdef GL_EXT_pvrtc_sRGB
  _glewInfo_GL_EXT_pvrtc_sRGB();
#endif /* GL_EXT_pvrtc_sRGB */
#ifdef GL_EXT_raster_multisample
  _glewInfo_GL_EXT_raster_multisample();
#endif /* GL_EXT_raster_multisample */
#ifdef GL_EXT_read_format_bgra
  _glewInfo_GL_EXT_read_format_bgra();
#endif /* GL_EXT_read_format_bgra */
#ifdef GL_EXT_render_snorm
  _glewInfo_GL_EXT_render_snorm();
#endif /* GL_EXT_render_snorm */
#ifdef GL_EXT_rescale_normal
  _glewInfo_GL_EXT_rescale_normal();
#endif /* GL_EXT_rescale_normal */
#ifdef GL_EXT_sRGB
  _glewInfo_GL_EXT_sRGB();
#endif /* GL_EXT_sRGB */
#ifdef GL_EXT_sRGB_write_control
  _glewInfo_GL_EXT_sRGB_write_control();
#endif /* GL_EXT_sRGB_write_control */
#ifdef GL_EXT_scene_marker
  _glewInfo_GL_EXT_scene_marker();
#endif /* GL_EXT_scene_marker */
#ifdef GL_EXT_secondary_color
  _glewInfo_GL_EXT_secondary_color();
#endif /* GL_EXT_secondary_color */
#ifdef GL_EXT_semaphore
  _glewInfo_GL_EXT_semaphore();
#endif /* GL_EXT_semaphore */
#ifdef GL_EXT_semaphore_fd
  _glewInfo_GL_EXT_semaphore_fd();
#endif /* GL_EXT_semaphore_fd */
#ifdef GL_EXT_semaphore_win32
  _glewInfo_GL_EXT_semaphore_win32();
#endif /* GL_EXT_semaphore_win32 */
#ifdef GL_EXT_separate_shader_objects
  _glewInfo_GL_EXT_separate_shader_objects();
#endif /* GL_EXT_separate_shader_objects */
#ifdef GL_EXT_separate_specular_color
  _glewInfo_GL_EXT_separate_specular_color();
#endif /* GL_EXT_separate_specular_color */
#ifdef GL_EXT_shader_framebuffer_fetch
  _glewInfo_GL_EXT_shader_framebuffer_fetch();
#endif /* GL_EXT_shader_framebuffer_fetch */
#ifdef GL_EXT_shader_group_vote
  _glewInfo_GL_EXT_shader_group_vote();
#endif /* GL_EXT_shader_group_vote */
#ifdef GL_EXT_shader_image_load_formatted
  _glewInfo_GL_EXT_shader_image_load_formatted();
#endif /* GL_EXT_shader_image_load_formatted */
#ifdef GL_EXT_shader_image_load_store
  _glewInfo_GL_EXT_shader_image_load_store();
#endif /* GL_EXT_shader_image_load_store */
#ifdef GL_EXT_shader_implicit_conversions
  _glewInfo_GL_EXT_shader_implicit_conversions();
#endif /* GL_EXT_shader_implicit_conversions */
#ifdef GL_EXT_shader_integer_mix
  _glewInfo_GL_EXT_shader_integer_mix();
#endif /* GL_EXT_shader_integer_mix */
#ifdef GL_EXT_shader_io_blocks
  _glewInfo_GL_EXT_shader_io_blocks();
#endif /* GL_EXT_shader_io_blocks */
#ifdef GL_EXT_shader_non_constant_global_initializers
  _glewInfo_GL_EXT_shader_non_constant_global_initializers();
#endif /* GL_EXT_shader_non_constant_global_initializers */
#ifdef GL_EXT_shader_pixel_local_storage
  _glewInfo_GL_EXT_shader_pixel_local_storage();
#endif /* GL_EXT_shader_pixel_local_storage */
#ifdef GL_EXT_shader_pixel_local_storage2
  _glewInfo_GL_EXT_shader_pixel_local_storage2();
#endif /* GL_EXT_shader_pixel_local_storage2 */
#ifdef GL_EXT_shader_texture_lod
  _glewInfo_GL_EXT_shader_texture_lod();
#endif /* GL_EXT_shader_texture_lod */
#ifdef GL_EXT_shadow_funcs
  _glewInfo_GL_EXT_shadow_funcs();
#endif /* GL_EXT_shadow_funcs */
#ifdef GL_EXT_shadow_samplers
  _glewInfo_GL_EXT_shadow_samplers();
#endif /* GL_EXT_shadow_samplers */
#ifdef GL_EXT_shared_texture_palette
  _glewInfo_GL_EXT_shared_texture_palette();
#endif /* GL_EXT_shared_texture_palette */
#ifdef GL_EXT_sparse_texture
  _glewInfo_GL_EXT_sparse_texture();
#endif /* GL_EXT_sparse_texture */
#ifdef GL_EXT_sparse_texture2
  _glewInfo_GL_EXT_sparse_texture2();
#endif /* GL_EXT_sparse_texture2 */
#ifdef GL_EXT_stencil_clear_tag
  _glewInfo_GL_EXT_stencil_clear_tag();
#endif /* GL_EXT_stencil_clear_tag */
#ifdef GL_EXT_stencil_two_side
  _glewInfo_GL_EXT_stencil_two_side();
#endif /* GL_EXT_stencil_two_side */
#ifdef GL_EXT_stencil_wrap
  _glewInfo_GL_EXT_stencil_wrap();
#endif /* GL_EXT_stencil_wrap */
#ifdef GL_EXT_subtexture
  _glewInfo_GL_EXT_subtexture();
#endif /* GL_EXT_subtexture */
#ifdef GL_EXT_texture
  _glewInfo_GL_EXT_texture();
#endif /* GL_EXT_texture */
#ifdef GL_EXT_texture3D
  _glewInfo_GL_EXT_texture3D();
#endif /* GL_EXT_texture3D */
#ifdef GL_EXT_texture_array
  _glewInfo_GL_EXT_texture_array();
#endif /* GL_EXT_texture_array */
#ifdef GL_EXT_texture_buffer_object
  _glewInfo_GL_EXT_texture_buffer_object();
#endif /* GL_EXT_texture_buffer_object */
#ifdef GL_EXT_texture_compression_astc_decode_mode
  _glewInfo_GL_EXT_texture_compression_astc_decode_mode();
#endif /* GL_EXT_texture_compression_astc_decode_mode */
#ifdef GL_EXT_texture_compression_astc_decode_mode_rgb9e5
  _glewInfo_GL_EXT_texture_compression_astc_decode_mode_rgb9e5();
#endif /* GL_EXT_texture_compression_astc_decode_mode_rgb9e5 */
#ifdef GL_EXT_texture_compression_bptc
  _glewInfo_GL_EXT_texture_compression_bptc();
#endif /* GL_EXT_texture_compression_bptc */
#ifdef GL_EXT_texture_compression_dxt1
  _glewInfo_GL_EXT_texture_compression_dxt1();
#endif /* GL_EXT_texture_compression_dxt1 */
#ifdef GL_EXT_texture_compression_latc
  _glewInfo_GL_EXT_texture_compression_latc();
#endif /* GL_EXT_texture_compression_latc */
#ifdef GL_EXT_texture_compression_rgtc
  _glewInfo_GL_EXT_texture_compression_rgtc();
#endif /* GL_EXT_texture_compression_rgtc */
#ifdef GL_EXT_texture_compression_s3tc
  _glewInfo_GL_EXT_texture_compression_s3tc();
#endif /* GL_EXT_texture_compression_s3tc */
#ifdef GL_EXT_texture_cube_map
  _glewInfo_GL_EXT_texture_cube_map();
#endif /* GL_EXT_texture_cube_map */
#ifdef GL_EXT_texture_cube_map_array
  _glewInfo_GL_EXT_texture_cube_map_array();
#endif /* GL_EXT_texture_cube_map_array */
#ifdef GL_EXT_texture_edge_clamp
  _glewInfo_GL_EXT_texture_edge_clamp();
#endif /* GL_EXT_texture_edge_clamp */
#ifdef GL_EXT_texture_env
  _glewInfo_GL_EXT_texture_env();
#endif /* GL_EXT_texture_env */
#ifdef GL_EXT_texture_env_add
  _glewInfo_GL_EXT_texture_env_add();
#endif /* GL_EXT_texture_env_add */
#ifdef GL_EXT_texture_env_combine
  _glewInfo_GL_EXT_texture_env_combine();
#endif /* GL_EXT_texture_env_combine */
#ifdef GL_EXT_texture_env_dot3
  _glewInfo_GL_EXT_texture_env_dot3();
#endif /* GL_EXT_texture_env_dot3 */
#ifdef GL_EXT_texture_filter_anisotropic
  _glewInfo_GL_EXT_texture_filter_anisotropic();
#endif /* GL_EXT_texture_filter_anisotropic */
#ifdef GL_EXT_texture_filter_minmax
  _glewInfo_GL_EXT_texture_filter_minmax();
#endif /* GL_EXT_texture_filter_minmax */
#ifdef GL_EXT_texture_format_BGRA8888
  _glewInfo_GL_EXT_texture_format_BGRA8888();
#endif /* GL_EXT_texture_format_BGRA8888 */
#ifdef GL_EXT_texture_integer
  _glewInfo_GL_EXT_texture_integer();
#endif /* GL_EXT_texture_integer */
#ifdef GL_EXT_texture_lod_bias
  _glewInfo_GL_EXT_texture_lod_bias();
#endif /* GL_EXT_texture_lod_bias */
#ifdef GL_EXT_texture_mirror_clamp
  _glewInfo_GL_EXT_texture_mirror_clamp();
#endif /* GL_EXT_texture_mirror_clamp */
#ifdef GL_EXT_texture_norm16
  _glewInfo_GL_EXT_texture_norm16();
#endif /* GL_EXT_texture_norm16 */
#ifdef GL_EXT_texture_object
  _glewInfo_GL_EXT_texture_object();
#endif /* GL_EXT_texture_object */
#ifdef GL_EXT_texture_perturb_normal
  _glewInfo_GL_EXT_texture_perturb_normal();
#endif /* GL_EXT_texture_perturb_normal */
#ifdef GL_EXT_texture_rectangle
  _glewInfo_GL_EXT_texture_rectangle();
#endif /* GL_EXT_texture_rectangle */
#ifdef GL_EXT_texture_rg
  _glewInfo_GL_EXT_texture_rg();
#endif /* GL_EXT_texture_rg */
#ifdef GL_EXT_texture_sRGB
  _glewInfo_GL_EXT_texture_sRGB();
#endif /* GL_EXT_texture_sRGB */
#ifdef GL_EXT_texture_sRGB_R8
  _glewInfo_GL_EXT_texture_sRGB_R8();
#endif /* GL_EXT_texture_sRGB_R8 */
#ifdef GL_EXT_texture_sRGB_RG8
  _glewInfo_GL_EXT_texture_sRGB_RG8();
#endif /* GL_EXT_texture_sRGB_RG8 */
#ifdef GL_EXT_texture_sRGB_decode
  _glewInfo_GL_EXT_texture_sRGB_decode();
#endif /* GL_EXT_texture_sRGB_decode */
#ifdef GL_EXT_texture_shared_exponent
  _glewInfo_GL_EXT_texture_shared_exponent();
#endif /* GL_EXT_texture_shared_exponent */
#ifdef GL_EXT_texture_snorm
  _glewInfo_GL_EXT_texture_snorm();
#endif /* GL_EXT_texture_snorm */
#ifdef GL_EXT_texture_storage
  _glewInfo_GL_EXT_texture_storage();
#endif /* GL_EXT_texture_storage */
#ifdef GL_EXT_texture_swizzle
  _glewInfo_GL_EXT_texture_swizzle();
#endif /* GL_EXT_texture_swizzle */
#ifdef GL_EXT_texture_type_2_10_10_10_REV
  _glewInfo_GL_EXT_texture_type_2_10_10_10_REV();
#endif /* GL_EXT_texture_type_2_10_10_10_REV */
#ifdef GL_EXT_texture_view
  _glewInfo_GL_EXT_texture_view();
#endif /* GL_EXT_texture_view */
#ifdef GL_EXT_timer_query
  _glewInfo_GL_EXT_timer_query();
#endif /* GL_EXT_timer_query */
#ifdef GL_EXT_transform_feedback
  _glewInfo_GL_EXT_transform_feedback();
#endif /* GL_EXT_transform_feedback */
#ifdef GL_EXT_unpack_subimage
  _glewInfo_GL_EXT_unpack_subimage();
#endif /* GL_EXT_unpack_subimage */
#ifdef GL_EXT_vertex_array
  _glewInfo_GL_EXT_vertex_array();
#endif /* GL_EXT_vertex_array */
#ifdef GL_EXT_vertex_array_bgra
  _glewInfo_GL_EXT_vertex_array_bgra();
#endif /* GL_EXT_vertex_array_bgra */
#ifdef GL_EXT_vertex_array_setXXX
  _glewInfo_GL_EXT_vertex_array_setXXX();
#endif /* GL_EXT_vertex_array_setXXX */
#ifdef GL_EXT_vertex_attrib_64bit
  _glewInfo_GL_EXT_vertex_attrib_64bit();
#endif /* GL_EXT_vertex_attrib_64bit */
#ifdef GL_EXT_vertex_shader
  _glewInfo_GL_EXT_vertex_shader();
#endif /* GL_EXT_vertex_shader */
#ifdef GL_EXT_vertex_weighting
  _glewInfo_GL_EXT_vertex_weighting();
#endif /* GL_EXT_vertex_weighting */
#ifdef GL_EXT_win32_keyed_mutex
  _glewInfo_GL_EXT_win32_keyed_mutex();
#endif /* GL_EXT_win32_keyed_mutex */
#ifdef GL_EXT_window_rectangles
  _glewInfo_GL_EXT_window_rectangles();
#endif /* GL_EXT_window_rectangles */
#ifdef GL_EXT_x11_sync_object
  _glewInfo_GL_EXT_x11_sync_object();
#endif /* GL_EXT_x11_sync_object */
#ifdef GL_GREMEDY_frame_terminator
  _glewInfo_GL_GREMEDY_frame_terminator();
#endif /* GL_GREMEDY_frame_terminator */
#ifdef GL_GREMEDY_string_marker
  _glewInfo_GL_GREMEDY_string_marker();
#endif /* GL_GREMEDY_string_marker */
#ifdef GL_HP_convolution_border_modes
  _glewInfo_GL_HP_convolution_border_modes();
#endif /* GL_HP_convolution_border_modes */
#ifdef GL_HP_image_transform
  _glewInfo_GL_HP_image_transform();
#endif /* GL_HP_image_transform */
#ifdef GL_HP_occlusion_test
  _glewInfo_GL_HP_occlusion_test();
#endif /* GL_HP_occlusion_test */
#ifdef GL_HP_texture_lighting
  _glewInfo_GL_HP_texture_lighting();
#endif /* GL_HP_texture_lighting */
#ifdef GL_IBM_cull_vertex
  _glewInfo_GL_IBM_cull_vertex();
#endif /* GL_IBM_cull_vertex */
#ifdef GL_IBM_multimode_draw_arrays
  _glewInfo_GL_IBM_multimode_draw_arrays();
#endif /* GL_IBM_multimode_draw_arrays */
#ifdef GL_IBM_rasterpos_clip
  _glewInfo_GL_IBM_rasterpos_clip();
#endif /* GL_IBM_rasterpos_clip */
#ifdef GL_IBM_static_data
  _glewInfo_GL_IBM_static_data();
#endif /* GL_IBM_static_data */
#ifdef GL_IBM_texture_mirrored_repeat
  _glewInfo_GL_IBM_texture_mirrored_repeat();
#endif /* GL_IBM_texture_mirrored_repeat */
#ifdef GL_IBM_vertex_array_lists
  _glewInfo_GL_IBM_vertex_array_lists();
#endif /* GL_IBM_vertex_array_lists */
#ifdef GL_INGR_color_clamp
  _glewInfo_GL_INGR_color_clamp();
#endif /* GL_INGR_color_clamp */
#ifdef GL_INGR_interlace_read
  _glewInfo_GL_INGR_interlace_read();
#endif /* GL_INGR_interlace_read */
#ifdef GL_INTEL_conservative_rasterization
  _glewInfo_GL_INTEL_conservative_rasterization();
#endif /* GL_INTEL_conservative_rasterization */
#ifdef GL_INTEL_fragment_shader_ordering
  _glewInfo_GL_INTEL_fragment_shader_ordering();
#endif /* GL_INTEL_fragment_shader_ordering */
#ifdef GL_INTEL_framebuffer_CMAA
  _glewInfo_GL_INTEL_framebuffer_CMAA();
#endif /* GL_INTEL_framebuffer_CMAA */
#ifdef GL_INTEL_map_texture
  _glewInfo_GL_INTEL_map_texture();
#endif /* GL_INTEL_map_texture */
#ifdef GL_INTEL_parallel_arrays
  _glewInfo_GL_INTEL_parallel_arrays();
#endif /* GL_INTEL_parallel_arrays */
#ifdef GL_INTEL_performance_query
  _glewInfo_GL_INTEL_performance_query();
#endif /* GL_INTEL_performance_query */
#ifdef GL_INTEL_texture_scissor
  _glewInfo_GL_INTEL_texture_scissor();
#endif /* GL_INTEL_texture_scissor */
#ifdef GL_KHR_blend_equation_advanced
  _glewInfo_GL_KHR_blend_equation_advanced();
#endif /* GL_KHR_blend_equation_advanced */
#ifdef GL_KHR_blend_equation_advanced_coherent
  _glewInfo_GL_KHR_blend_equation_advanced_coherent();
#endif /* GL_KHR_blend_equation_advanced_coherent */
#ifdef GL_KHR_context_flush_control
  _glewInfo_GL_KHR_context_flush_control();
#endif /* GL_KHR_context_flush_control */
#ifdef GL_KHR_debug
  _glewInfo_GL_KHR_debug();
#endif /* GL_KHR_debug */
#ifdef GL_KHR_no_error
  _glewInfo_GL_KHR_no_error();
#endif /* GL_KHR_no_error */
#ifdef GL_KHR_parallel_shader_compile
  _glewInfo_GL_KHR_parallel_shader_compile();
#endif /* GL_KHR_parallel_shader_compile */
#ifdef GL_KHR_robust_buffer_access_behavior
  _glewInfo_GL_KHR_robust_buffer_access_behavior();
#endif /* GL_KHR_robust_buffer_access_behavior */
#ifdef GL_KHR_robustness
  _glewInfo_GL_KHR_robustness();
#endif /* GL_KHR_robustness */
#ifdef GL_KHR_texture_compression_astc_hdr
  _glewInfo_GL_KHR_texture_compression_astc_hdr();
#endif /* GL_KHR_texture_compression_astc_hdr */
#ifdef GL_KHR_texture_compression_astc_ldr
  _glewInfo_GL_KHR_texture_compression_astc_ldr();
#endif /* GL_KHR_texture_compression_astc_ldr */
#ifdef GL_KHR_texture_compression_astc_sliced_3d
  _glewInfo_GL_KHR_texture_compression_astc_sliced_3d();
#endif /* GL_KHR_texture_compression_astc_sliced_3d */
#ifdef GL_KTX_buffer_region
  _glewInfo_GL_KTX_buffer_region();
#endif /* GL_KTX_buffer_region */
#ifdef GL_MESAX_texture_stack
  _glewInfo_GL_MESAX_texture_stack();
#endif /* GL_MESAX_texture_stack */
#ifdef GL_MESA_pack_invert
  _glewInfo_GL_MESA_pack_invert();
#endif /* GL_MESA_pack_invert */
#ifdef GL_MESA_resize_buffers
  _glewInfo_GL_MESA_resize_buffers();
#endif /* GL_MESA_resize_buffers */
#ifdef GL_MESA_shader_integer_functions
  _glewInfo_GL_MESA_shader_integer_functions();
#endif /* GL_MESA_shader_integer_functions */
#ifdef GL_MESA_window_pos
  _glewInfo_GL_MESA_window_pos();
#endif /* GL_MESA_window_pos */
#ifdef GL_MESA_ycbcr_texture
  _glewInfo_GL_MESA_ycbcr_texture();
#endif /* GL_MESA_ycbcr_texture */
#ifdef GL_NVX_blend_equation_advanced_multi_draw_buffers
  _glewInfo_GL_NVX_blend_equation_advanced_multi_draw_buffers();
#endif /* GL_NVX_blend_equation_advanced_multi_draw_buffers */
#ifdef GL_NVX_conditional_render
  _glewInfo_GL_NVX_conditional_render();
#endif /* GL_NVX_conditional_render */
#ifdef GL_NVX_gpu_memory_info
  _glewInfo_GL_NVX_gpu_memory_info();
#endif /* GL_NVX_gpu_memory_info */
#ifdef GL_NVX_linked_gpu_multicast
  _glewInfo_GL_NVX_linked_gpu_multicast();
#endif /* GL_NVX_linked_gpu_multicast */
#ifdef GL_NV_3dvision_settings
  _glewInfo_GL_NV_3dvision_settings();
#endif /* GL_NV_3dvision_settings */
#ifdef GL_NV_EGL_stream_consumer_external
  _glewInfo_GL_NV_EGL_stream_consumer_external();
#endif /* GL_NV_EGL_stream_consumer_external */
#ifdef GL_NV_alpha_to_coverage_dither_control
  _glewInfo_GL_NV_alpha_to_coverage_dither_control();
#endif /* GL_NV_alpha_to_coverage_dither_control */
#ifdef GL_NV_bgr
  _glewInfo_GL_NV_bgr();
#endif /* GL_NV_bgr */
#ifdef GL_NV_bindless_multi_draw_indirect
  _glewInfo_GL_NV_bindless_multi_draw_indirect();
#endif /* GL_NV_bindless_multi_draw_indirect */
#ifdef GL_NV_bindless_multi_draw_indirect_count
  _glewInfo_GL_NV_bindless_multi_draw_indirect_count();
#endif /* GL_NV_bindless_multi_draw_indirect_count */
#ifdef GL_NV_bindless_texture
  _glewInfo_GL_NV_bindless_texture();
#endif /* GL_NV_bindless_texture */
#ifdef GL_NV_blend_equation_advanced
  _glewInfo_GL_NV_blend_equation_advanced();
#endif /* GL_NV_blend_equation_advanced */
#ifdef GL_NV_blend_equation_advanced_coherent
  _glewInfo_GL_NV_blend_equation_advanced_coherent();
#endif /* GL_NV_blend_equation_advanced_coherent */
#ifdef GL_NV_blend_minmax_factor
  _glewInfo_GL_NV_blend_minmax_factor();
#endif /* GL_NV_blend_minmax_factor */
#ifdef GL_NV_blend_square
  _glewInfo_GL_NV_blend_square();
#endif /* GL_NV_blend_square */
#ifdef GL_NV_clip_space_w_scaling
  _glewInfo_GL_NV_clip_space_w_scaling();
#endif /* GL_NV_clip_space_w_scaling */
#ifdef GL_NV_command_list
  _glewInfo_GL_NV_command_list();
#endif /* GL_NV_command_list */
#ifdef GL_NV_compute_program5
  _glewInfo_GL_NV_compute_program5();
#endif /* GL_NV_compute_program5 */
#ifdef GL_NV_conditional_render
  _glewInfo_GL_NV_conditional_render();
#endif /* GL_NV_conditional_render */
#ifdef GL_NV_conservative_raster
  _glewInfo_GL_NV_conservative_raster();
#endif /* GL_NV_conservative_raster */
#ifdef GL_NV_conservative_raster_dilate
  _glewInfo_GL_NV_conservative_raster_dilate();
#endif /* GL_NV_conservative_raster_dilate */
#ifdef GL_NV_conservative_raster_pre_snap_triangles
  _glewInfo_GL_NV_conservative_raster_pre_snap_triangles();
#endif /* GL_NV_conservative_raster_pre_snap_triangles */
#ifdef GL_NV_copy_buffer
  _glewInfo_GL_NV_copy_buffer();
#endif /* GL_NV_copy_buffer */
#ifdef GL_NV_copy_depth_to_color
  _glewInfo_GL_NV_copy_depth_to_color();
#endif /* GL_NV_copy_depth_to_color */
#ifdef GL_NV_copy_image
  _glewInfo_GL_NV_copy_image();
#endif /* GL_NV_copy_image */
#ifdef GL_NV_deep_texture3D
  _glewInfo_GL_NV_deep_texture3D();
#endif /* GL_NV_deep_texture3D */
#ifdef GL_NV_depth_buffer_float
  _glewInfo_GL_NV_depth_buffer_float();
#endif /* GL_NV_depth_buffer_float */
#ifdef GL_NV_depth_clamp
  _glewInfo_GL_NV_depth_clamp();
#endif /* GL_NV_depth_clamp */
#ifdef GL_NV_depth_range_unclamped
  _glewInfo_GL_NV_depth_range_unclamped();
#endif /* GL_NV_depth_range_unclamped */
#ifdef GL_NV_draw_buffers
  _glewInfo_GL_NV_draw_buffers();
#endif /* GL_NV_draw_buffers */
#ifdef GL_NV_draw_instanced
  _glewInfo_GL_NV_draw_instanced();
#endif /* GL_NV_draw_instanced */
#ifdef GL_NV_draw_texture
  _glewInfo_GL_NV_draw_texture();
#endif /* GL_NV_draw_texture */
#ifdef GL_NV_draw_vulkan_image
  _glewInfo_GL_NV_draw_vulkan_image();
#endif /* GL_NV_draw_vulkan_image */
#ifdef GL_NV_evaluators
  _glewInfo_GL_NV_evaluators();
#endif /* GL_NV_evaluators */
#ifdef GL_NV_explicit_attrib_location
  _glewInfo_GL_NV_explicit_attrib_location();
#endif /* GL_NV_explicit_attrib_location */
#ifdef GL_NV_explicit_multisample
  _glewInfo_GL_NV_explicit_multisample();
#endif /* GL_NV_explicit_multisample */
#ifdef GL_NV_fbo_color_attachments
  _glewInfo_GL_NV_fbo_color_attachments();
#endif /* GL_NV_fbo_color_attachments */
#ifdef GL_NV_fence
  _glewInfo_GL_NV_fence();
#endif /* GL_NV_fence */
#ifdef GL_NV_fill_rectangle
  _glewInfo_GL_NV_fill_rectangle();
#endif /* GL_NV_fill_rectangle */
#ifdef GL_NV_float_buffer
  _glewInfo_GL_NV_float_buffer();
#endif /* GL_NV_float_buffer */
#ifdef GL_NV_fog_distance
  _glewInfo_GL_NV_fog_distance();
#endif /* GL_NV_fog_distance */
#ifdef GL_NV_fragment_coverage_to_color
  _glewInfo_GL_NV_fragment_coverage_to_color();
#endif /* GL_NV_fragment_coverage_to_color */
#ifdef GL_NV_fragment_program
  _glewInfo_GL_NV_fragment_program();
#endif /* GL_NV_fragment_program */
#ifdef GL_NV_fragment_program2
  _glewInfo_GL_NV_fragment_program2();
#endif /* GL_NV_fragment_program2 */
#ifdef GL_NV_fragment_program4
  _glewInfo_GL_NV_fragment_program4();
#endif /* GL_NV_fragment_program4 */
#ifdef GL_NV_fragment_program_option
  _glewInfo_GL_NV_fragment_program_option();
#endif /* GL_NV_fragment_program_option */
#ifdef GL_NV_fragment_shader_interlock
  _glewInfo_GL_NV_fragment_shader_interlock();
#endif /* GL_NV_fragment_shader_interlock */
#ifdef GL_NV_framebuffer_blit
  _glewInfo_GL_NV_framebuffer_blit();
#endif /* GL_NV_framebuffer_blit */
#ifdef GL_NV_framebuffer_mixed_samples
  _glewInfo_GL_NV_framebuffer_mixed_samples();
#endif /* GL_NV_framebuffer_mixed_samples */
#ifdef GL_NV_framebuffer_multisample
  _glewInfo_GL_NV_framebuffer_multisample();
#endif /* GL_NV_framebuffer_multisample */
#ifdef GL_NV_framebuffer_multisample_coverage
  _glewInfo_GL_NV_framebuffer_multisample_coverage();
#endif /* GL_NV_framebuffer_multisample_coverage */
#ifdef GL_NV_generate_mipmap_sRGB
  _glewInfo_GL_NV_generate_mipmap_sRGB();
#endif /* GL_NV_generate_mipmap_sRGB */
#ifdef GL_NV_geometry_program4
  _glewInfo_GL_NV_geometry_program4();
#endif /* GL_NV_geometry_program4 */
#ifdef GL_NV_geometry_shader4
  _glewInfo_GL_NV_geometry_shader4();
#endif /* GL_NV_geometry_shader4 */
#ifdef GL_NV_geometry_shader_passthrough
  _glewInfo_GL_NV_geometry_shader_passthrough();
#endif /* GL_NV_geometry_shader_passthrough */
#ifdef GL_NV_gpu_multicast
  _glewInfo_GL_NV_gpu_multicast();
#endif /* GL_NV_gpu_multicast */
#ifdef GL_NV_gpu_program4
  _glewInfo_GL_NV_gpu_program4();
#endif /* GL_NV_gpu_program4 */
#ifdef GL_NV_gpu_program5
  _glewInfo_GL_NV_gpu_program5();
#endif /* GL_NV_gpu_program5 */
#ifdef GL_NV_gpu_program5_mem_extended
  _glewInfo_GL_NV_gpu_program5_mem_extended();
#endif /* GL_NV_gpu_program5_mem_extended */
#ifdef GL_NV_gpu_program_fp64
  _glewInfo_GL_NV_gpu_program_fp64();
#endif /* GL_NV_gpu_program_fp64 */
#ifdef GL_NV_gpu_shader5
  _glewInfo_GL_NV_gpu_shader5();
#endif /* GL_NV_gpu_shader5 */
#ifdef GL_NV_half_float
  _glewInfo_GL_NV_half_float();
#endif /* GL_NV_half_float */
#ifdef GL_NV_image_formats
  _glewInfo_GL_NV_image_formats();
#endif /* GL_NV_image_formats */
#ifdef GL_NV_instanced_arrays
  _glewInfo_GL_NV_instanced_arrays();
#endif /* GL_NV_instanced_arrays */
#ifdef GL_NV_internalformat_sample_query
  _glewInfo_GL_NV_internalformat_sample_query();
#endif /* GL_NV_internalformat_sample_query */
#ifdef GL_NV_light_max_exponent
  _glewInfo_GL_NV_light_max_exponent();
#endif /* GL_NV_light_max_exponent */
#ifdef GL_NV_multisample_coverage
  _glewInfo_GL_NV_multisample_coverage();
#endif /* GL_NV_multisample_coverage */
#ifdef GL_NV_multisample_filter_hint
  _glewInfo_GL_NV_multisample_filter_hint();
#endif /* GL_NV_multisample_filter_hint */
#ifdef GL_NV_non_square_matrices
  _glewInfo_GL_NV_non_square_matrices();
#endif /* GL_NV_non_square_matrices */
#ifdef GL_NV_occlusion_query
  _glewInfo_GL_NV_occlusion_query();
#endif /* GL_NV_occlusion_query */
#ifdef GL_NV_pack_subimage
  _glewInfo_GL_NV_pack_subimage();
#endif /* GL_NV_pack_subimage */
#ifdef GL_NV_packed_depth_stencil
  _glewInfo_GL_NV_packed_depth_stencil();
#endif /* GL_NV_packed_depth_stencil */
#ifdef GL_NV_packed_float
  _glewInfo_GL_NV_packed_float();
#endif /* GL_NV_packed_float */
#ifdef GL_NV_packed_float_linear
  _glewInfo_GL_NV_packed_float_linear();
#endif /* GL_NV_packed_float_linear */
#ifdef GL_NV_parameter_buffer_object
  _glewInfo_GL_NV_parameter_buffer_object();
#endif /* GL_NV_parameter_buffer_object */
#ifdef GL_NV_parameter_buffer_object2
  _glewInfo_GL_NV_parameter_buffer_object2();
#endif /* GL_NV_parameter_buffer_object2 */
#ifdef GL_NV_path_rendering
  _glewInfo_GL_NV_path_rendering();
#endif /* GL_NV_path_rendering */
#ifdef GL_NV_path_rendering_shared_edge
  _glewInfo_GL_NV_path_rendering_shared_edge();
#endif /* GL_NV_path_rendering_shared_edge */
#ifdef GL_NV_pixel_buffer_object
  _glewInfo_GL_NV_pixel_buffer_object();
#endif /* GL_NV_pixel_buffer_object */
#ifdef GL_NV_pixel_data_range
  _glewInfo_GL_NV_pixel_data_range();
#endif /* GL_NV_pixel_data_range */
#ifdef GL_NV_platform_binary
  _glewInfo_GL_NV_platform_binary();
#endif /* GL_NV_platform_binary */
#ifdef GL_NV_point_sprite
  _glewInfo_GL_NV_point_sprite();
#endif /* GL_NV_point_sprite */
#ifdef GL_NV_polygon_mode
  _glewInfo_GL_NV_polygon_mode();
#endif /* GL_NV_polygon_mode */
#ifdef GL_NV_present_video
  _glewInfo_GL_NV_present_video();
#endif /* GL_NV_present_video */
#ifdef GL_NV_primitive_restart
  _glewInfo_GL_NV_primitive_restart();
#endif /* GL_NV_primitive_restart */
#ifdef GL_NV_read_depth
  _glewInfo_GL_NV_read_depth();
#endif /* GL_NV_read_depth */
#ifdef GL_NV_read_depth_stencil
  _glewInfo_GL_NV_read_depth_stencil();
#endif /* GL_NV_read_depth_stencil */
#ifdef GL_NV_read_stencil
  _glewInfo_GL_NV_read_stencil();
#endif /* GL_NV_read_stencil */
#ifdef GL_NV_register_combiners
  _glewInfo_GL_NV_register_combiners();
#endif /* GL_NV_register_combiners */
#ifdef GL_NV_register_combiners2
  _glewInfo_GL_NV_register_combiners2();
#endif /* GL_NV_register_combiners2 */
#ifdef GL_NV_robustness_video_memory_purge
  _glewInfo_GL_NV_robustness_video_memory_purge();
#endif /* GL_NV_robustness_video_memory_purge */
#ifdef GL_NV_sRGB_formats
  _glewInfo_GL_NV_sRGB_formats();
#endif /* GL_NV_sRGB_formats */
#ifdef GL_NV_sample_locations
  _glewInfo_GL_NV_sample_locations();
#endif /* GL_NV_sample_locations */
#ifdef GL_NV_sample_mask_override_coverage
  _glewInfo_GL_NV_sample_mask_override_coverage();
#endif /* GL_NV_sample_mask_override_coverage */
#ifdef GL_NV_shader_atomic_counters
  _glewInfo_GL_NV_shader_atomic_counters();
#endif /* GL_NV_shader_atomic_counters */
#ifdef GL_NV_shader_atomic_float
  _glewInfo_GL_NV_shader_atomic_float();
#endif /* GL_NV_shader_atomic_float */
#ifdef GL_NV_shader_atomic_float64
  _glewInfo_GL_NV_shader_atomic_float64();
#endif /* GL_NV_shader_atomic_float64 */
#ifdef GL_NV_shader_atomic_fp16_vector
  _glewInfo_GL_NV_shader_atomic_fp16_vector();
#endif /* GL_NV_shader_atomic_fp16_vector */
#ifdef GL_NV_shader_atomic_int64
  _glewInfo_GL_NV_shader_atomic_int64();
#endif /* GL_NV_shader_atomic_int64 */
#ifdef GL_NV_shader_buffer_load
  _glewInfo_GL_NV_shader_buffer_load();
#endif /* GL_NV_shader_buffer_load */
#ifdef GL_NV_shader_noperspective_interpolation
  _glewInfo_GL_NV_shader_noperspective_interpolation();
#endif /* GL_NV_shader_noperspective_interpolation */
#ifdef GL_NV_shader_storage_buffer_object
  _glewInfo_GL_NV_shader_storage_buffer_object();
#endif /* GL_NV_shader_storage_buffer_object */
#ifdef GL_NV_shader_thread_group
  _glewInfo_GL_NV_shader_thread_group();
#endif /* GL_NV_shader_thread_group */
#ifdef GL_NV_shader_thread_shuffle
  _glewInfo_GL_NV_shader_thread_shuffle();
#endif /* GL_NV_shader_thread_shuffle */
#ifdef GL_NV_shadow_samplers_array
  _glewInfo_GL_NV_shadow_samplers_array();
#endif /* GL_NV_shadow_samplers_array */
#ifdef GL_NV_shadow_samplers_cube
  _glewInfo_GL_NV_shadow_samplers_cube();
#endif /* GL_NV_shadow_samplers_cube */
#ifdef GL_NV_stereo_view_rendering
  _glewInfo_GL_NV_stereo_view_rendering();
#endif /* GL_NV_stereo_view_rendering */
#ifdef GL_NV_tessellation_program5
  _glewInfo_GL_NV_tessellation_program5();
#endif /* GL_NV_tessellation_program5 */
#ifdef GL_NV_texgen_emboss
  _glewInfo_GL_NV_texgen_emboss();
#endif /* GL_NV_texgen_emboss */
#ifdef GL_NV_texgen_reflection
  _glewInfo_GL_NV_texgen_reflection();
#endif /* GL_NV_texgen_reflection */
#ifdef GL_NV_texture_array
  _glewInfo_GL_NV_texture_array();
#endif /* GL_NV_texture_array */
#ifdef GL_NV_texture_barrier
  _glewInfo_GL_NV_texture_barrier();
#endif /* GL_NV_texture_barrier */
#ifdef GL_NV_texture_border_clamp
  _glewInfo_GL_NV_texture_border_clamp();
#endif /* GL_NV_texture_border_clamp */
#ifdef GL_NV_texture_compression_latc
  _glewInfo_GL_NV_texture_compression_latc();
#endif /* GL_NV_texture_compression_latc */
#ifdef GL_NV_texture_compression_s3tc
  _glewInfo_GL_NV_texture_compression_s3tc();
#endif /* GL_NV_texture_compression_s3tc */
#ifdef GL_NV_texture_compression_s3tc_update
  _glewInfo_GL_NV_texture_compression_s3tc_update();
#endif /* GL_NV_texture_compression_s3tc_update */
#ifdef GL_NV_texture_compression_vtc
  _glewInfo_GL_NV_texture_compression_vtc();
#endif /* GL_NV_texture_compression_vtc */
#ifdef GL_NV_texture_env_combine4
  _glewInfo_GL_NV_texture_env_combine4();
#endif /* GL_NV_texture_env_combine4 */
#ifdef GL_NV_texture_expand_normal
  _glewInfo_GL_NV_texture_expand_normal();
#endif /* GL_NV_texture_expand_normal */
#ifdef GL_NV_texture_multisample
  _glewInfo_GL_NV_texture_multisample();
#endif /* GL_NV_texture_multisample */
#ifdef GL_NV_texture_npot_2D_mipmap
  _glewInfo_GL_NV_texture_npot_2D_mipmap();
#endif /* GL_NV_texture_npot_2D_mipmap */
#ifdef GL_NV_texture_rectangle
  _glewInfo_GL_NV_texture_rectangle();
#endif /* GL_NV_texture_rectangle */
#ifdef GL_NV_texture_rectangle_compressed
  _glewInfo_GL_NV_texture_rectangle_compressed();
#endif /* GL_NV_texture_rectangle_compressed */
#ifdef GL_NV_texture_shader
  _glewInfo_GL_NV_texture_shader();
#endif /* GL_NV_texture_shader */
#ifdef GL_NV_texture_shader2
  _glewInfo_GL_NV_texture_shader2();
#endif /* GL_NV_texture_shader2 */
#ifdef GL_NV_texture_shader3
  _glewInfo_GL_NV_texture_shader3();
#endif /* GL_NV_texture_shader3 */
#ifdef GL_NV_transform_feedback
  _glewInfo_GL_NV_transform_feedback();
#endif /* GL_NV_transform_feedback */
#ifdef GL_NV_transform_feedback2
  _glewInfo_GL_NV_transform_feedback2();
#endif /* GL_NV_transform_feedback2 */
#ifdef GL_NV_uniform_buffer_unified_memory
  _glewInfo_GL_NV_uniform_buffer_unified_memory();
#endif /* GL_NV_uniform_buffer_unified_memory */
#ifdef GL_NV_vdpau_interop
  _glewInfo_GL_NV_vdpau_interop();
#endif /* GL_NV_vdpau_interop */
#ifdef GL_NV_vertex_array_range
  _glewInfo_GL_NV_vertex_array_range();
#endif /* GL_NV_vertex_array_range */
#ifdef GL_NV_vertex_array_range2
  _glewInfo_GL_NV_vertex_array_range2();
#endif /* GL_NV_vertex_array_range2 */
#ifdef GL_NV_vertex_attrib_integer_64bit
  _glewInfo_GL_NV_vertex_attrib_integer_64bit();
#endif /* GL_NV_vertex_attrib_integer_64bit */
#ifdef GL_NV_vertex_buffer_unified_memory
  _glewInfo_GL_NV_vertex_buffer_unified_memory();
#endif /* GL_NV_vertex_buffer_unified_memory */
#ifdef GL_NV_vertex_program
  _glewInfo_GL_NV_vertex_program();
#endif /* GL_NV_vertex_program */
#ifdef GL_NV_vertex_program1_1
  _glewInfo_GL_NV_vertex_program1_1();
#endif /* GL_NV_vertex_program1_1 */
#ifdef GL_NV_vertex_program2
  _glewInfo_GL_NV_vertex_program2();
#endif /* GL_NV_vertex_program2 */
#ifdef GL_NV_vertex_program2_option
  _glewInfo_GL_NV_vertex_program2_option();
#endif /* GL_NV_vertex_program2_option */
#ifdef GL_NV_vertex_program3
  _glewInfo_GL_NV_vertex_program3();
#endif /* GL_NV_vertex_program3 */
#ifdef GL_NV_vertex_program4
  _glewInfo_GL_NV_vertex_program4();
#endif /* GL_NV_vertex_program4 */
#ifdef GL_NV_video_capture
  _glewInfo_GL_NV_video_capture();
#endif /* GL_NV_video_capture */
#ifdef GL_NV_viewport_array
  _glewInfo_GL_NV_viewport_array();
#endif /* GL_NV_viewport_array */
#ifdef GL_NV_viewport_array2
  _glewInfo_GL_NV_viewport_array2();
#endif /* GL_NV_viewport_array2 */
#ifdef GL_NV_viewport_swizzle
  _glewInfo_GL_NV_viewport_swizzle();
#endif /* GL_NV_viewport_swizzle */
#ifdef GL_OES_byte_coordinates
  _glewInfo_GL_OES_byte_coordinates();
#endif /* GL_OES_byte_coordinates */
#ifdef GL_OML_interlace
  _glewInfo_GL_OML_interlace();
#endif /* GL_OML_interlace */
#ifdef GL_OML_resample
  _glewInfo_GL_OML_resample();
#endif /* GL_OML_resample */
#ifdef GL_OML_subsample
  _glewInfo_GL_OML_subsample();
#endif /* GL_OML_subsample */
#ifdef GL_OVR_multiview
  _glewInfo_GL_OVR_multiview();
#endif /* GL_OVR_multiview */
#ifdef GL_OVR_multiview2
  _glewInfo_GL_OVR_multiview2();
#endif /* GL_OVR_multiview2 */
#ifdef GL_OVR_multiview_multisampled_render_to_texture
  _glewInfo_GL_OVR_multiview_multisampled_render_to_texture();
#endif /* GL_OVR_multiview_multisampled_render_to_texture */
#ifdef GL_PGI_misc_hints
  _glewInfo_GL_PGI_misc_hints();
#endif /* GL_PGI_misc_hints */
#ifdef GL_PGI_vertex_hints
  _glewInfo_GL_PGI_vertex_hints();
#endif /* GL_PGI_vertex_hints */
#ifdef GL_QCOM_alpha_test
  _glewInfo_GL_QCOM_alpha_test();
#endif /* GL_QCOM_alpha_test */
#ifdef GL_QCOM_binning_control
  _glewInfo_GL_QCOM_binning_control();
#endif /* GL_QCOM_binning_control */
#ifdef GL_QCOM_driver_control
  _glewInfo_GL_QCOM_driver_control();
#endif /* GL_QCOM_driver_control */
#ifdef GL_QCOM_extended_get
  _glewInfo_GL_QCOM_extended_get();
#endif /* GL_QCOM_extended_get */
#ifdef GL_QCOM_extended_get2
  _glewInfo_GL_QCOM_extended_get2();
#endif /* GL_QCOM_extended_get2 */
#ifdef GL_QCOM_framebuffer_foveated
  _glewInfo_GL_QCOM_framebuffer_foveated();
#endif /* GL_QCOM_framebuffer_foveated */
#ifdef GL_QCOM_perfmon_global_mode
  _glewInfo_GL_QCOM_perfmon_global_mode();
#endif /* GL_QCOM_perfmon_global_mode */
#ifdef GL_QCOM_shader_framebuffer_fetch_noncoherent
  _glewInfo_GL_QCOM_shader_framebuffer_fetch_noncoherent();
#endif /* GL_QCOM_shader_framebuffer_fetch_noncoherent */
#ifdef GL_QCOM_tiled_rendering
  _glewInfo_GL_QCOM_tiled_rendering();
#endif /* GL_QCOM_tiled_rendering */
#ifdef GL_QCOM_writeonly_rendering
  _glewInfo_GL_QCOM_writeonly_rendering();
#endif /* GL_QCOM_writeonly_rendering */
#ifdef GL_REGAL_ES1_0_compatibility
  _glewInfo_GL_REGAL_ES1_0_compatibility();
#endif /* GL_REGAL_ES1_0_compatibility */
#ifdef GL_REGAL_ES1_1_compatibility
  _glewInfo_GL_REGAL_ES1_1_compatibility();
#endif /* GL_REGAL_ES1_1_compatibility */
#ifdef GL_REGAL_enable
  _glewInfo_GL_REGAL_enable();
#endif /* GL_REGAL_enable */
#ifdef GL_REGAL_error_string
  _glewInfo_GL_REGAL_error_string();
#endif /* GL_REGAL_error_string */
#ifdef GL_REGAL_extension_query
  _glewInfo_GL_REGAL_extension_query();
#endif /* GL_REGAL_extension_query */
#ifdef GL_REGAL_log
  _glewInfo_GL_REGAL_log();
#endif /* GL_REGAL_log */
#ifdef GL_REGAL_proc_address
  _glewInfo_GL_REGAL_proc_address();
#endif /* GL_REGAL_proc_address */
#ifdef GL_REND_screen_coordinates
  _glewInfo_GL_REND_screen_coordinates();
#endif /* GL_REND_screen_coordinates */
#ifdef GL_S3_s3tc
  _glewInfo_GL_S3_s3tc();
#endif /* GL_S3_s3tc */
#ifdef GL_SGIS_clip_band_hint
  _glewInfo_GL_SGIS_clip_band_hint();
#endif /* GL_SGIS_clip_band_hint */
#ifdef GL_SGIS_color_range
  _glewInfo_GL_SGIS_color_range();
#endif /* GL_SGIS_color_range */
#ifdef GL_SGIS_detail_texture
  _glewInfo_GL_SGIS_detail_texture();
#endif /* GL_SGIS_detail_texture */
#ifdef GL_SGIS_fog_function
  _glewInfo_GL_SGIS_fog_function();
#endif /* GL_SGIS_fog_function */
#ifdef GL_SGIS_generate_mipmap
  _glewInfo_GL_SGIS_generate_mipmap();
#endif /* GL_SGIS_generate_mipmap */
#ifdef GL_SGIS_line_texgen
  _glewInfo_GL_SGIS_line_texgen();
#endif /* GL_SGIS_line_texgen */
#ifdef GL_SGIS_multisample
  _glewInfo_GL_SGIS_multisample();
#endif /* GL_SGIS_multisample */
#ifdef GL_SGIS_multitexture
  _glewInfo_GL_SGIS_multitexture();
#endif /* GL_SGIS_multitexture */
#ifdef GL_SGIS_pixel_texture
  _glewInfo_GL_SGIS_pixel_texture();
#endif /* GL_SGIS_pixel_texture */
#ifdef GL_SGIS_point_line_texgen
  _glewInfo_GL_SGIS_point_line_texgen();
#endif /* GL_SGIS_point_line_texgen */
#ifdef GL_SGIS_shared_multisample
  _glewInfo_GL_SGIS_shared_multisample();
#endif /* GL_SGIS_shared_multisample */
#ifdef GL_SGIS_sharpen_texture
  _glewInfo_GL_SGIS_sharpen_texture();
#endif /* GL_SGIS_sharpen_texture */
#ifdef GL_SGIS_texture4D
  _glewInfo_GL_SGIS_texture4D();
#endif /* GL_SGIS_texture4D */
#ifdef GL_SGIS_texture_border_clamp
  _glewInfo_GL_SGIS_texture_border_clamp();
#endif /* GL_SGIS_texture_border_clamp */
#ifdef GL_SGIS_texture_edge_clamp
  _glewInfo_GL_SGIS_texture_edge_clamp();
#endif /* GL_SGIS_texture_edge_clamp */
#ifdef GL_SGIS_texture_filter4
  _glewInfo_GL_SGIS_texture_filter4();
#endif /* GL_SGIS_texture_filter4 */
#ifdef GL_SGIS_texture_lod
  _glewInfo_GL_SGIS_texture_lod();
#endif /* GL_SGIS_texture_lod */
#ifdef GL_SGIS_texture_select
  _glewInfo_GL_SGIS_texture_select();
#endif /* GL_SGIS_texture_select */
#ifdef GL_SGIX_async
  _glewInfo_GL_SGIX_async();
#endif /* GL_SGIX_async */
#ifdef GL_SGIX_async_histogram
  _glewInfo_GL_SGIX_async_histogram();
#endif /* GL_SGIX_async_histogram */
#ifdef GL_SGIX_async_pixel
  _glewInfo_GL_SGIX_async_pixel();
#endif /* GL_SGIX_async_pixel */
#ifdef GL_SGIX_bali_g_instruments
  _glewInfo_GL_SGIX_bali_g_instruments();
#endif /* GL_SGIX_bali_g_instruments */
#ifdef GL_SGIX_bali_r_instruments
  _glewInfo_GL_SGIX_bali_r_instruments();
#endif /* GL_SGIX_bali_r_instruments */
#ifdef GL_SGIX_bali_timer_instruments
  _glewInfo_GL_SGIX_bali_timer_instruments();
#endif /* GL_SGIX_bali_timer_instruments */
#ifdef GL_SGIX_blend_alpha_minmax
  _glewInfo_GL_SGIX_blend_alpha_minmax();
#endif /* GL_SGIX_blend_alpha_minmax */
#ifdef GL_SGIX_blend_cadd
  _glewInfo_GL_SGIX_blend_cadd();
#endif /* GL_SGIX_blend_cadd */
#ifdef GL_SGIX_blend_cmultiply
  _glewInfo_GL_SGIX_blend_cmultiply();
#endif /* GL_SGIX_blend_cmultiply */
#ifdef GL_SGIX_calligraphic_fragment
  _glewInfo_GL_SGIX_calligraphic_fragment();
#endif /* GL_SGIX_calligraphic_fragment */
#ifdef GL_SGIX_clipmap
  _glewInfo_GL_SGIX_clipmap();
#endif /* GL_SGIX_clipmap */
#ifdef GL_SGIX_color_matrix_accuracy
  _glewInfo_GL_SGIX_color_matrix_accuracy();
#endif /* GL_SGIX_color_matrix_accuracy */
#ifdef GL_SGIX_color_table_index_mode
  _glewInfo_GL_SGIX_color_table_index_mode();
#endif /* GL_SGIX_color_table_index_mode */
#ifdef GL_SGIX_complex_polar
  _glewInfo_GL_SGIX_complex_polar();
#endif /* GL_SGIX_complex_polar */
#ifdef GL_SGIX_convolution_accuracy
  _glewInfo_GL_SGIX_convolution_accuracy();
#endif /* GL_SGIX_convolution_accuracy */
#ifdef GL_SGIX_cube_map
  _glewInfo_GL_SGIX_cube_map();
#endif /* GL_SGIX_cube_map */
#ifdef GL_SGIX_cylinder_texgen
  _glewInfo_GL_SGIX_cylinder_texgen();
#endif /* GL_SGIX_cylinder_texgen */
#ifdef GL_SGIX_datapipe
  _glewInfo_GL_SGIX_datapipe();
#endif /* GL_SGIX_datapipe */
#ifdef GL_SGIX_decimation
  _glewInfo_GL_SGIX_decimation();
#endif /* GL_SGIX_decimation */
#ifdef GL_SGIX_depth_pass_instrument
  _glewInfo_GL_SGIX_depth_pass_instrument();
#endif /* GL_SGIX_depth_pass_instrument */
#ifdef GL_SGIX_depth_texture
  _glewInfo_GL_SGIX_depth_texture();
#endif /* GL_SGIX_depth_texture */
#ifdef GL_SGIX_dvc
  _glewInfo_GL_SGIX_dvc();
#endif /* GL_SGIX_dvc */
#ifdef GL_SGIX_flush_raster
  _glewInfo_GL_SGIX_flush_raster();
#endif /* GL_SGIX_flush_raster */
#ifdef GL_SGIX_fog_blend
  _glewInfo_GL_SGIX_fog_blend();
#endif /* GL_SGIX_fog_blend */
#ifdef GL_SGIX_fog_factor_to_alpha
  _glewInfo_GL_SGIX_fog_factor_to_alpha();
#endif /* GL_SGIX_fog_factor_to_alpha */
#ifdef GL_SGIX_fog_layers
  _glewInfo_GL_SGIX_fog_layers();
#endif /* GL_SGIX_fog_layers */
#ifdef GL_SGIX_fog_offset
  _glewInfo_GL_SGIX_fog_offset();
#endif /* GL_SGIX_fog_offset */
#ifdef GL_SGIX_fog_patchy
  _glewInfo_GL_SGIX_fog_patchy();
#endif /* GL_SGIX_fog_patchy */
#ifdef GL_SGIX_fog_scale
  _glewInfo_GL_SGIX_fog_scale();
#endif /* GL_SGIX_fog_scale */
#ifdef GL_SGIX_fog_texture
  _glewInfo_GL_SGIX_fog_texture();
#endif /* GL_SGIX_fog_texture */
#ifdef GL_SGIX_fragment_lighting_space
  _glewInfo_GL_SGIX_fragment_lighting_space();
#endif /* GL_SGIX_fragment_lighting_space */
#ifdef GL_SGIX_fragment_specular_lighting
  _glewInfo_GL_SGIX_fragment_specular_lighting();
#endif /* GL_SGIX_fragment_specular_lighting */
#ifdef GL_SGIX_fragments_instrument
  _glewInfo_GL_SGIX_fragments_instrument();
#endif /* GL_SGIX_fragments_instrument */
#ifdef GL_SGIX_framezoom
  _glewInfo_GL_SGIX_framezoom();
#endif /* GL_SGIX_framezoom */
#ifdef GL_SGIX_icc_texture
  _glewInfo_GL_SGIX_icc_texture();
#endif /* GL_SGIX_icc_texture */
#ifdef GL_SGIX_igloo_interface
  _glewInfo_GL_SGIX_igloo_interface();
#endif /* GL_SGIX_igloo_interface */
#ifdef GL_SGIX_image_compression
  _glewInfo_GL_SGIX_image_compression();
#endif /* GL_SGIX_image_compression */
#ifdef GL_SGIX_impact_pixel_texture
  _glewInfo_GL_SGIX_impact_pixel_texture();
#endif /* GL_SGIX_impact_pixel_texture */
#ifdef GL_SGIX_instrument_error
  _glewInfo_GL_SGIX_instrument_error();
#endif /* GL_SGIX_instrument_error */
#ifdef GL_SGIX_interlace
  _glewInfo_GL_SGIX_interlace();
#endif /* GL_SGIX_interlace */
#ifdef GL_SGIX_ir_instrument1
  _glewInfo_GL_SGIX_ir_instrument1();
#endif /* GL_SGIX_ir_instrument1 */
#ifdef GL_SGIX_line_quality_hint
  _glewInfo_GL_SGIX_line_quality_hint();
#endif /* GL_SGIX_line_quality_hint */
#ifdef GL_SGIX_list_priority
  _glewInfo_GL_SGIX_list_priority();
#endif /* GL_SGIX_list_priority */
#ifdef GL_SGIX_mpeg1
  _glewInfo_GL_SGIX_mpeg1();
#endif /* GL_SGIX_mpeg1 */
#ifdef GL_SGIX_mpeg2
  _glewInfo_GL_SGIX_mpeg2();
#endif /* GL_SGIX_mpeg2 */
#ifdef GL_SGIX_nonlinear_lighting_pervertex
  _glewInfo_GL_SGIX_nonlinear_lighting_pervertex();
#endif /* GL_SGIX_nonlinear_lighting_pervertex */
#ifdef GL_SGIX_nurbs_eval
  _glewInfo_GL_SGIX_nurbs_eval();
#endif /* GL_SGIX_nurbs_eval */
#ifdef GL_SGIX_occlusion_instrument
  _glewInfo_GL_SGIX_occlusion_instrument();
#endif /* GL_SGIX_occlusion_instrument */
#ifdef GL_SGIX_packed_6bytes
  _glewInfo_GL_SGIX_packed_6bytes();
#endif /* GL_SGIX_packed_6bytes */
#ifdef GL_SGIX_pixel_texture
  _glewInfo_GL_SGIX_pixel_texture();
#endif /* GL_SGIX_pixel_texture */
#ifdef GL_SGIX_pixel_texture_bits
  _glewInfo_GL_SGIX_pixel_texture_bits();
#endif /* GL_SGIX_pixel_texture_bits */
#ifdef GL_SGIX_pixel_texture_lod
  _glewInfo_GL_SGIX_pixel_texture_lod();
#endif /* GL_SGIX_pixel_texture_lod */
#ifdef GL_SGIX_pixel_tiles
  _glewInfo_GL_SGIX_pixel_tiles();
#endif /* GL_SGIX_pixel_tiles */
#ifdef GL_SGIX_polynomial_ffd
  _glewInfo_GL_SGIX_polynomial_ffd();
#endif /* GL_SGIX_polynomial_ffd */
#ifdef GL_SGIX_quad_mesh
  _glewInfo_GL_SGIX_quad_mesh();
#endif /* GL_SGIX_quad_mesh */
#ifdef GL_SGIX_reference_plane
  _glewInfo_GL_SGIX_reference_plane();
#endif /* GL_SGIX_reference_plane */
#ifdef GL_SGIX_resample
  _glewInfo_GL_SGIX_resample();
#endif /* GL_SGIX_resample */
#ifdef GL_SGIX_scalebias_hint
  _glewInfo_GL_SGIX_scalebias_hint();
#endif /* GL_SGIX_scalebias_hint */
#ifdef GL_SGIX_shadow
  _glewInfo_GL_SGIX_shadow();
#endif /* GL_SGIX_shadow */
#ifdef GL_SGIX_shadow_ambient
  _glewInfo_GL_SGIX_shadow_ambient();
#endif /* GL_SGIX_shadow_ambient */
#ifdef GL_SGIX_slim
  _glewInfo_GL_SGIX_slim();
#endif /* GL_SGIX_slim */
#ifdef GL_SGIX_spotlight_cutoff
  _glewInfo_GL_SGIX_spotlight_cutoff();
#endif /* GL_SGIX_spotlight_cutoff */
#ifdef GL_SGIX_sprite
  _glewInfo_GL_SGIX_sprite();
#endif /* GL_SGIX_sprite */
#ifdef GL_SGIX_subdiv_patch
  _glewInfo_GL_SGIX_subdiv_patch();
#endif /* GL_SGIX_subdiv_patch */
#ifdef GL_SGIX_subsample
  _glewInfo_GL_SGIX_subsample();
#endif /* GL_SGIX_subsample */
#ifdef GL_SGIX_tag_sample_buffer
  _glewInfo_GL_SGIX_tag_sample_buffer();
#endif /* GL_SGIX_tag_sample_buffer */
#ifdef GL_SGIX_texture_add_env
  _glewInfo_GL_SGIX_texture_add_env();
#endif /* GL_SGIX_texture_add_env */
#ifdef GL_SGIX_texture_coordinate_clamp
  _glewInfo_GL_SGIX_texture_coordinate_clamp();
#endif /* GL_SGIX_texture_coordinate_clamp */
#ifdef GL_SGIX_texture_lod_bias
  _glewInfo_GL_SGIX_texture_lod_bias();
#endif /* GL_SGIX_texture_lod_bias */
#ifdef GL_SGIX_texture_mipmap_anisotropic
  _glewInfo_GL_SGIX_texture_mipmap_anisotropic();
#endif /* GL_SGIX_texture_mipmap_anisotropic */
#ifdef GL_SGIX_texture_multi_buffer
  _glewInfo_GL_SGIX_texture_multi_buffer();
#endif /* GL_SGIX_texture_multi_buffer */
#ifdef GL_SGIX_texture_phase
  _glewInfo_GL_SGIX_texture_phase();
#endif /* GL_SGIX_texture_phase */
#ifdef GL_SGIX_texture_range
  _glewInfo_GL_SGIX_texture_range();
#endif /* GL_SGIX_texture_range */
#ifdef GL_SGIX_texture_scale_bias
  _glewInfo_GL_SGIX_texture_scale_bias();
#endif /* GL_SGIX_texture_scale_bias */
#ifdef GL_SGIX_texture_supersample
  _glewInfo_GL_SGIX_texture_supersample();
#endif /* GL_SGIX_texture_supersample */
#ifdef GL_SGIX_vector_ops
  _glewInfo_GL_SGIX_vector_ops();
#endif /* GL_SGIX_vector_ops */
#ifdef GL_SGIX_vertex_array_object
  _glewInfo_GL_SGIX_vertex_array_object();
#endif /* GL_SGIX_vertex_array_object */
#ifdef GL_SGIX_vertex_preclip
  _glewInfo_GL_SGIX_vertex_preclip();
#endif /* GL_SGIX_vertex_preclip */
#ifdef GL_SGIX_vertex_preclip_hint
  _glewInfo_GL_SGIX_vertex_preclip_hint();
#endif /* GL_SGIX_vertex_preclip_hint */
#ifdef GL_SGIX_ycrcb
  _glewInfo_GL_SGIX_ycrcb();
#endif /* GL_SGIX_ycrcb */
#ifdef GL_SGIX_ycrcb_subsample
  _glewInfo_GL_SGIX_ycrcb_subsample();
#endif /* GL_SGIX_ycrcb_subsample */
#ifdef GL_SGIX_ycrcba
  _glewInfo_GL_SGIX_ycrcba();
#endif /* GL_SGIX_ycrcba */
#ifdef GL_SGI_color_matrix
  _glewInfo_GL_SGI_color_matrix();
#endif /* GL_SGI_color_matrix */
#ifdef GL_SGI_color_table
  _glewInfo_GL_SGI_color_table();
#endif /* GL_SGI_color_table */
#ifdef GL_SGI_complex
  _glewInfo_GL_SGI_complex();
#endif /* GL_SGI_complex */
#ifdef GL_SGI_complex_type
  _glewInfo_GL_SGI_complex_type();
#endif /* GL_SGI_complex_type */
#ifdef GL_SGI_fft
  _glewInfo_GL_SGI_fft();
#endif /* GL_SGI_fft */
#ifdef GL_SGI_texture_color_table
  _glewInfo_GL_SGI_texture_color_table();
#endif /* GL_SGI_texture_color_table */
#ifdef GL_SUNX_constant_data
  _glewInfo_GL_SUNX_constant_data();
#endif /* GL_SUNX_constant_data */
#ifdef GL_SUN_convolution_border_modes
  _glewInfo_GL_SUN_convolution_border_modes();
#endif /* GL_SUN_convolution_border_modes */
#ifdef GL_SUN_global_alpha
  _glewInfo_GL_SUN_global_alpha();
#endif /* GL_SUN_global_alpha */
#ifdef GL_SUN_mesh_array
  _glewInfo_GL_SUN_mesh_array();
#endif /* GL_SUN_mesh_array */
#ifdef GL_SUN_read_video_pixels
  _glewInfo_GL_SUN_read_video_pixels();
#endif /* GL_SUN_read_video_pixels */
#ifdef GL_SUN_slice_accum
  _glewInfo_GL_SUN_slice_accum();
#endif /* GL_SUN_slice_accum */
#ifdef GL_SUN_triangle_list
  _glewInfo_GL_SUN_triangle_list();
#endif /* GL_SUN_triangle_list */
#ifdef GL_SUN_vertex
  _glewInfo_GL_SUN_vertex();
#endif /* GL_SUN_vertex */
#ifdef GL_WIN_phong_shading
  _glewInfo_GL_WIN_phong_shading();
#endif /* GL_WIN_phong_shading */
#ifdef GL_WIN_scene_markerXXX
  _glewInfo_GL_WIN_scene_markerXXX();
#endif /* GL_WIN_scene_markerXXX */
#ifdef GL_WIN_specular_fog
  _glewInfo_GL_WIN_specular_fog();
#endif /* GL_WIN_specular_fog */
#ifdef GL_WIN_swap_hint
  _glewInfo_GL_WIN_swap_hint();
#endif /* GL_WIN_swap_hint */
}

/* ------------------------------------------------------------------------ */

#if defined(_WIN32) && !defined(GLEW_EGL) && !defined(GLEW_OSMESA)

static void wglewInfo ()
{
#ifdef WGL_3DFX_multisample
  _glewInfo_WGL_3DFX_multisample();
#endif /* WGL_3DFX_multisample */
#ifdef WGL_3DL_stereo_control
  _glewInfo_WGL_3DL_stereo_control();
#endif /* WGL_3DL_stereo_control */
#ifdef WGL_AMD_gpu_association
  _glewInfo_WGL_AMD_gpu_association();
#endif /* WGL_AMD_gpu_association */
#ifdef WGL_ARB_buffer_region
  _glewInfo_WGL_ARB_buffer_region();
#endif /* WGL_ARB_buffer_region */
#ifdef WGL_ARB_context_flush_control
  _glewInfo_WGL_ARB_context_flush_control();
#endif /* WGL_ARB_context_flush_control */
#ifdef WGL_ARB_create_context
  _glewInfo_WGL_ARB_create_context();
#endif /* WGL_ARB_create_context */
#ifdef WGL_ARB_create_context_no_error
  _glewInfo_WGL_ARB_create_context_no_error();
#endif /* WGL_ARB_create_context_no_error */
#ifdef WGL_ARB_create_context_profile
  _glewInfo_WGL_ARB_create_context_profile();
#endif /* WGL_ARB_create_context_profile */
#ifdef WGL_ARB_create_context_robustness
  _glewInfo_WGL_ARB_create_context_robustness();
#endif /* WGL_ARB_create_context_robustness */
#ifdef WGL_ARB_extensions_string
  _glewInfo_WGL_ARB_extensions_string();
#endif /* WGL_ARB_extensions_string */
#ifdef WGL_ARB_framebuffer_sRGB
  _glewInfo_WGL_ARB_framebuffer_sRGB();
#endif /* WGL_ARB_framebuffer_sRGB */
#ifdef WGL_ARB_make_current_read
  _glewInfo_WGL_ARB_make_current_read();
#endif /* WGL_ARB_make_current_read */
#ifdef WGL_ARB_multisample
  _glewInfo_WGL_ARB_multisample();
#endif /* WGL_ARB_multisample */
#ifdef WGL_ARB_pbuffer
  _glewInfo_WGL_ARB_pbuffer();
#endif /* WGL_ARB_pbuffer */
#ifdef WGL_ARB_pixel_format
  _glewInfo_WGL_ARB_pixel_format();
#endif /* WGL_ARB_pixel_format */
#ifdef WGL_ARB_pixel_format_float
  _glewInfo_WGL_ARB_pixel_format_float();
#endif /* WGL_ARB_pixel_format_float */
#ifdef WGL_ARB_render_texture
  _glewInfo_WGL_ARB_render_texture();
#endif /* WGL_ARB_render_texture */
#ifdef WGL_ARB_robustness_application_isolation
  _glewInfo_WGL_ARB_robustness_application_isolation();
#endif /* WGL_ARB_robustness_application_isolation */
#ifdef WGL_ARB_robustness_share_group_isolation
  _glewInfo_WGL_ARB_robustness_share_group_isolation();
#endif /* WGL_ARB_robustness_share_group_isolation */
#ifdef WGL_ATI_pixel_format_float
  _glewInfo_WGL_ATI_pixel_format_float();
#endif /* WGL_ATI_pixel_format_float */
#ifdef WGL_ATI_render_texture_rectangle
  _glewInfo_WGL_ATI_render_texture_rectangle();
#endif /* WGL_ATI_render_texture_rectangle */
#ifdef WGL_EXT_colorspace
  _glewInfo_WGL_EXT_colorspace();
#endif /* WGL_EXT_colorspace */
#ifdef WGL_EXT_create_context_es2_profile
  _glewInfo_WGL_EXT_create_context_es2_profile();
#endif /* WGL_EXT_create_context_es2_profile */
#ifdef WGL_EXT_create_context_es_profile
  _glewInfo_WGL_EXT_create_context_es_profile();
#endif /* WGL_EXT_create_context_es_profile */
#ifdef WGL_EXT_depth_float
  _glewInfo_WGL_EXT_depth_float();
#endif /* WGL_EXT_depth_float */
#ifdef WGL_EXT_display_color_table
  _glewInfo_WGL_EXT_display_color_table();
#endif /* WGL_EXT_display_color_table */
#ifdef WGL_EXT_extensions_string
  _glewInfo_WGL_EXT_extensions_string();
#endif /* WGL_EXT_extensions_string */
#ifdef WGL_EXT_framebuffer_sRGB
  _glewInfo_WGL_EXT_framebuffer_sRGB();
#endif /* WGL_EXT_framebuffer_sRGB */
#ifdef WGL_EXT_make_current_read
  _glewInfo_WGL_EXT_make_current_read();
#endif /* WGL_EXT_make_current_read */
#ifdef WGL_EXT_multisample
  _glewInfo_WGL_EXT_multisample();
#endif /* WGL_EXT_multisample */
#ifdef WGL_EXT_pbuffer
  _glewInfo_WGL_EXT_pbuffer();
#endif /* WGL_EXT_pbuffer */
#ifdef WGL_EXT_pixel_format
  _glewInfo_WGL_EXT_pixel_format();
#endif /* WGL_EXT_pixel_format */
#ifdef WGL_EXT_pixel_format_packed_float
  _glewInfo_WGL_EXT_pixel_format_packed_float();
#endif /* WGL_EXT_pixel_format_packed_float */
#ifdef WGL_EXT_swap_control
  _glewInfo_WGL_EXT_swap_control();
#endif /* WGL_EXT_swap_control */
#ifdef WGL_EXT_swap_control_tear
  _glewInfo_WGL_EXT_swap_control_tear();
#endif /* WGL_EXT_swap_control_tear */
#ifdef WGL_I3D_digital_video_control
  _glewInfo_WGL_I3D_digital_video_control();
#endif /* WGL_I3D_digital_video_control */
#ifdef WGL_I3D_gamma
  _glewInfo_WGL_I3D_gamma();
#endif /* WGL_I3D_gamma */
#ifdef WGL_I3D_genlock
  _glewInfo_WGL_I3D_genlock();
#endif /* WGL_I3D_genlock */
#ifdef WGL_I3D_image_buffer
  _glewInfo_WGL_I3D_image_buffer();
#endif /* WGL_I3D_image_buffer */
#ifdef WGL_I3D_swap_frame_lock
  _glewInfo_WGL_I3D_swap_frame_lock();
#endif /* WGL_I3D_swap_frame_lock */
#ifdef WGL_I3D_swap_frame_usage
  _glewInfo_WGL_I3D_swap_frame_usage();
#endif /* WGL_I3D_swap_frame_usage */
#ifdef WGL_NV_DX_interop
  _glewInfo_WGL_NV_DX_interop();
#endif /* WGL_NV_DX_interop */
#ifdef WGL_NV_DX_interop2
  _glewInfo_WGL_NV_DX_interop2();
#endif /* WGL_NV_DX_interop2 */
#ifdef WGL_NV_copy_image
  _glewInfo_WGL_NV_copy_image();
#endif /* WGL_NV_copy_image */
#ifdef WGL_NV_delay_before_swap
  _glewInfo_WGL_NV_delay_before_swap();
#endif /* WGL_NV_delay_before_swap */
#ifdef WGL_NV_float_buffer
  _glewInfo_WGL_NV_float_buffer();
#endif /* WGL_NV_float_buffer */
#ifdef WGL_NV_gpu_affinity
  _glewInfo_WGL_NV_gpu_affinity();
#endif /* WGL_NV_gpu_affinity */
#ifdef WGL_NV_multisample_coverage
  _glewInfo_WGL_NV_multisample_coverage();
#endif /* WGL_NV_multisample_coverage */
#ifdef WGL_NV_present_video
  _glewInfo_WGL_NV_present_video();
#endif /* WGL_NV_present_video */
#ifdef WGL_NV_render_depth_texture
  _glewInfo_WGL_NV_render_depth_texture();
#endif /* WGL_NV_render_depth_texture */
#ifdef WGL_NV_render_texture_rectangle
  _glewInfo_WGL_NV_render_texture_rectangle();
#endif /* WGL_NV_render_texture_rectangle */
#ifdef WGL_NV_swap_group
  _glewInfo_WGL_NV_swap_group();
#endif /* WGL_NV_swap_group */
#ifdef WGL_NV_vertex_array_range
  _glewInfo_WGL_NV_vertex_array_range();
#endif /* WGL_NV_vertex_array_range */
#ifdef WGL_NV_video_capture
  _glewInfo_WGL_NV_video_capture();
#endif /* WGL_NV_video_capture */
#ifdef WGL_NV_video_output
  _glewInfo_WGL_NV_video_output();
#endif /* WGL_NV_video_output */
#ifdef WGL_OML_sync_control
  _glewInfo_WGL_OML_sync_control();
#endif /* WGL_OML_sync_control */
}

#elif !defined(GLEW_EGL) && !defined(GLEW_OSMESA) /* _UNIX */

static void glxewInfo ()
{
#ifdef GLX_VERSION_1_2
  _glewInfo_GLX_VERSION_1_2();
#endif /* GLX_VERSION_1_2 */
#ifdef GLX_VERSION_1_3
  _glewInfo_GLX_VERSION_1_3();
#endif /* GLX_VERSION_1_3 */
#ifdef GLX_VERSION_1_4
  _glewInfo_GLX_VERSION_1_4();
#endif /* GLX_VERSION_1_4 */
#ifdef GLX_3DFX_multisample
  _glewInfo_GLX_3DFX_multisample();
#endif /* GLX_3DFX_multisample */
#ifdef GLX_AMD_gpu_association
  _glewInfo_GLX_AMD_gpu_association();
#endif /* GLX_AMD_gpu_association */
#ifdef GLX_ARB_context_flush_control
  _glewInfo_GLX_ARB_context_flush_control();
#endif /* GLX_ARB_context_flush_control */
#ifdef GLX_ARB_create_context
  _glewInfo_GLX_ARB_create_context();
#endif /* GLX_ARB_create_context */
#ifdef GLX_ARB_create_context_no_error
  _glewInfo_GLX_ARB_create_context_no_error();
#endif /* GLX_ARB_create_context_no_error */
#ifdef GLX_ARB_create_context_profile
  _glewInfo_GLX_ARB_create_context_profile();
#endif /* GLX_ARB_create_context_profile */
#ifdef GLX_ARB_create_context_robustness
  _glewInfo_GLX_ARB_create_context_robustness();
#endif /* GLX_ARB_create_context_robustness */
#ifdef GLX_ARB_fbconfig_float
  _glewInfo_GLX_ARB_fbconfig_float();
#endif /* GLX_ARB_fbconfig_float */
#ifdef GLX_ARB_framebuffer_sRGB
  _glewInfo_GLX_ARB_framebuffer_sRGB();
#endif /* GLX_ARB_framebuffer_sRGB */
#ifdef GLX_ARB_get_proc_address
  _glewInfo_GLX_ARB_get_proc_address();
#endif /* GLX_ARB_get_proc_address */
#ifdef GLX_ARB_multisample
  _glewInfo_GLX_ARB_multisample();
#endif /* GLX_ARB_multisample */
#ifdef GLX_ARB_robustness_application_isolation
  _glewInfo_GLX_ARB_robustness_application_isolation();
#endif /* GLX_ARB_robustness_application_isolation */
#ifdef GLX_ARB_robustness_share_group_isolation
  _glewInfo_GLX_ARB_robustness_share_group_isolation();
#endif /* GLX_ARB_robustness_share_group_isolation */
#ifdef GLX_ARB_vertex_buffer_object
  _glewInfo_GLX_ARB_vertex_buffer_object();
#endif /* GLX_ARB_vertex_buffer_object */
#ifdef GLX_ATI_pixel_format_float
  _glewInfo_GLX_ATI_pixel_format_float();
#endif /* GLX_ATI_pixel_format_float */
#ifdef GLX_ATI_render_texture
  _glewInfo_GLX_ATI_render_texture();
#endif /* GLX_ATI_render_texture */
#ifdef GLX_EXT_buffer_age
  _glewInfo_GLX_EXT_buffer_age();
#endif /* GLX_EXT_buffer_age */
#ifdef GLX_EXT_create_context_es2_profile
  _glewInfo_GLX_EXT_create_context_es2_profile();
#endif /* GLX_EXT_create_context_es2_profile */
#ifdef GLX_EXT_create_context_es_profile
  _glewInfo_GLX_EXT_create_context_es_profile();
#endif /* GLX_EXT_create_context_es_profile */
#ifdef GLX_EXT_fbconfig_packed_float
  _glewInfo_GLX_EXT_fbconfig_packed_float();
#endif /* GLX_EXT_fbconfig_packed_float */
#ifdef GLX_EXT_framebuffer_sRGB
  _glewInfo_GLX_EXT_framebuffer_sRGB();
#endif /* GLX_EXT_framebuffer_sRGB */
#ifdef GLX_EXT_import_context
  _glewInfo_GLX_EXT_import_context();
#endif /* GLX_EXT_import_context */
#ifdef GLX_EXT_libglvnd
  _glewInfo_GLX_EXT_libglvnd();
#endif /* GLX_EXT_libglvnd */
#ifdef GLX_EXT_scene_marker
  _glewInfo_GLX_EXT_scene_marker();
#endif /* GLX_EXT_scene_marker */
#ifdef GLX_EXT_stereo_tree
  _glewInfo_GLX_EXT_stereo_tree();
#endif /* GLX_EXT_stereo_tree */
#ifdef GLX_EXT_swap_control
  _glewInfo_GLX_EXT_swap_control();
#endif /* GLX_EXT_swap_control */
#ifdef GLX_EXT_swap_control_tear
  _glewInfo_GLX_EXT_swap_control_tear();
#endif /* GLX_EXT_swap_control_tear */
#ifdef GLX_EXT_texture_from_pixmap
  _glewInfo_GLX_EXT_texture_from_pixmap();
#endif /* GLX_EXT_texture_from_pixmap */
#ifdef GLX_EXT_visual_info
  _glewInfo_GLX_EXT_visual_info();
#endif /* GLX_EXT_visual_info */
#ifdef GLX_EXT_visual_rating
  _glewInfo_GLX_EXT_visual_rating();
#endif /* GLX_EXT_visual_rating */
#ifdef GLX_INTEL_swap_event
  _glewInfo_GLX_INTEL_swap_event();
#endif /* GLX_INTEL_swap_event */
#ifdef GLX_MESA_agp_offset
  _glewInfo_GLX_MESA_agp_offset();
#endif /* GLX_MESA_agp_offset */
#ifdef GLX_MESA_copy_sub_buffer
  _glewInfo_GLX_MESA_copy_sub_buffer();
#endif /* GLX_MESA_copy_sub_buffer */
#ifdef GLX_MESA_pixmap_colormap
  _glewInfo_GLX_MESA_pixmap_colormap();
#endif /* GLX_MESA_pixmap_colormap */
#ifdef GLX_MESA_query_renderer
  _glewInfo_GLX_MESA_query_renderer();
#endif /* GLX_MESA_query_renderer */
#ifdef GLX_MESA_release_buffers
  _glewInfo_GLX_MESA_release_buffers();
#endif /* GLX_MESA_release_buffers */
#ifdef GLX_MESA_set_3dfx_mode
  _glewInfo_GLX_MESA_set_3dfx_mode();
#endif /* GLX_MESA_set_3dfx_mode */
#ifdef GLX_MESA_swap_control
  _glewInfo_GLX_MESA_swap_control();
#endif /* GLX_MESA_swap_control */
#ifdef GLX_NV_copy_buffer
  _glewInfo_GLX_NV_copy_buffer();
#endif /* GLX_NV_copy_buffer */
#ifdef GLX_NV_copy_image
  _glewInfo_GLX_NV_copy_image();
#endif /* GLX_NV_copy_image */
#ifdef GLX_NV_delay_before_swap
  _glewInfo_GLX_NV_delay_before_swap();
#endif /* GLX_NV_delay_before_swap */
#ifdef GLX_NV_float_buffer
  _glewInfo_GLX_NV_float_buffer();
#endif /* GLX_NV_float_buffer */
#ifdef GLX_NV_multisample_coverage
  _glewInfo_GLX_NV_multisample_coverage();
#endif /* GLX_NV_multisample_coverage */
#ifdef GLX_NV_present_video
  _glewInfo_GLX_NV_present_video();
#endif /* GLX_NV_present_video */
#ifdef GLX_NV_robustness_video_memory_purge
  _glewInfo_GLX_NV_robustness_video_memory_purge();
#endif /* GLX_NV_robustness_video_memory_purge */
#ifdef GLX_NV_swap_group
  _glewInfo_GLX_NV_swap_group();
#endif /* GLX_NV_swap_group */
#ifdef GLX_NV_vertex_array_range
  _glewInfo_GLX_NV_vertex_array_range();
#endif /* GLX_NV_vertex_array_range */
#ifdef GLX_NV_video_capture
  _glewInfo_GLX_NV_video_capture();
#endif /* GLX_NV_video_capture */
#ifdef GLX_NV_video_out
  _glewInfo_GLX_NV_video_out();
#endif /* GLX_NV_video_out */
#ifdef GLX_OML_swap_method
  _glewInfo_GLX_OML_swap_method();
#endif /* GLX_OML_swap_method */
#ifdef GLX_OML_sync_control
  _glewInfo_GLX_OML_sync_control();
#endif /* GLX_OML_sync_control */
#ifdef GLX_SGIS_blended_overlay
  _glewInfo_GLX_SGIS_blended_overlay();
#endif /* GLX_SGIS_blended_overlay */
#ifdef GLX_SGIS_color_range
  _glewInfo_GLX_SGIS_color_range();
#endif /* GLX_SGIS_color_range */
#ifdef GLX_SGIS_multisample
  _glewInfo_GLX_SGIS_multisample();
#endif /* GLX_SGIS_multisample */
#ifdef GLX_SGIS_shared_multisample
  _glewInfo_GLX_SGIS_shared_multisample();
#endif /* GLX_SGIS_shared_multisample */
#ifdef GLX_SGIX_fbconfig
  _glewInfo_GLX_SGIX_fbconfig();
#endif /* GLX_SGIX_fbconfig */
#ifdef GLX_SGIX_hyperpipe
  _glewInfo_GLX_SGIX_hyperpipe();
#endif /* GLX_SGIX_hyperpipe */
#ifdef GLX_SGIX_pbuffer
  _glewInfo_GLX_SGIX_pbuffer();
#endif /* GLX_SGIX_pbuffer */
#ifdef GLX_SGIX_swap_barrier
  _glewInfo_GLX_SGIX_swap_barrier();
#endif /* GLX_SGIX_swap_barrier */
#ifdef GLX_SGIX_swap_group
  _glewInfo_GLX_SGIX_swap_group();
#endif /* GLX_SGIX_swap_group */
#ifdef GLX_SGIX_video_resize
  _glewInfo_GLX_SGIX_video_resize();
#endif /* GLX_SGIX_video_resize */
#ifdef GLX_SGIX_visual_select_group
  _glewInfo_GLX_SGIX_visual_select_group();
#endif /* GLX_SGIX_visual_select_group */
#ifdef GLX_SGI_cushion
  _glewInfo_GLX_SGI_cushion();
#endif /* GLX_SGI_cushion */
#ifdef GLX_SGI_make_current_read
  _glewInfo_GLX_SGI_make_current_read();
#endif /* GLX_SGI_make_current_read */
#ifdef GLX_SGI_swap_control
  _glewInfo_GLX_SGI_swap_control();
#endif /* GLX_SGI_swap_control */
#ifdef GLX_SGI_video_sync
  _glewInfo_GLX_SGI_video_sync();
#endif /* GLX_SGI_video_sync */
#ifdef GLX_SUN_get_transparent_index
  _glewInfo_GLX_SUN_get_transparent_index();
#endif /* GLX_SUN_get_transparent_index */
#ifdef GLX_SUN_video_resize
  _glewInfo_GLX_SUN_video_resize();
#endif /* GLX_SUN_video_resize */
}

#elif defined(GLEW_EGL)

static void eglewInfo ()
{
#ifdef EGL_VERSION_1_0
  _glewInfo_EGL_VERSION_1_0();
#endif /* EGL_VERSION_1_0 */
#ifdef EGL_VERSION_1_1
  _glewInfo_EGL_VERSION_1_1();
#endif /* EGL_VERSION_1_1 */
#ifdef EGL_VERSION_1_2
  _glewInfo_EGL_VERSION_1_2();
#endif /* EGL_VERSION_1_2 */
#ifdef EGL_VERSION_1_3
  _glewInfo_EGL_VERSION_1_3();
#endif /* EGL_VERSION_1_3 */
#ifdef EGL_VERSION_1_4
  _glewInfo_EGL_VERSION_1_4();
#endif /* EGL_VERSION_1_4 */
#ifdef EGL_VERSION_1_5
  _glewInfo_EGL_VERSION_1_5();
#endif /* EGL_VERSION_1_5 */
#ifdef EGL_ANDROID_blob_cache
  _glewInfo_EGL_ANDROID_blob_cache();
#endif /* EGL_ANDROID_blob_cache */
#ifdef EGL_ANDROID_create_native_client_buffer
  _glewInfo_EGL_ANDROID_create_native_client_buffer();
#endif /* EGL_ANDROID_create_native_client_buffer */
#ifdef EGL_ANDROID_framebuffer_target
  _glewInfo_EGL_ANDROID_framebuffer_target();
#endif /* EGL_ANDROID_framebuffer_target */
#ifdef EGL_ANDROID_front_buffer_auto_refresh
  _glewInfo_EGL_ANDROID_front_buffer_auto_refresh();
#endif /* EGL_ANDROID_front_buffer_auto_refresh */
#ifdef EGL_ANDROID_image_native_buffer
  _glewInfo_EGL_ANDROID_image_native_buffer();
#endif /* EGL_ANDROID_image_native_buffer */
#ifdef EGL_ANDROID_native_fence_sync
  _glewInfo_EGL_ANDROID_native_fence_sync();
#endif /* EGL_ANDROID_native_fence_sync */
#ifdef EGL_ANDROID_presentation_time
  _glewInfo_EGL_ANDROID_presentation_time();
#endif /* EGL_ANDROID_presentation_time */
#ifdef EGL_ANDROID_recordable
  _glewInfo_EGL_ANDROID_recordable();
#endif /* EGL_ANDROID_recordable */
#ifdef EGL_ANGLE_d3d_share_handle_client_buffer
  _glewInfo_EGL_ANGLE_d3d_share_handle_client_buffer();
#endif /* EGL_ANGLE_d3d_share_handle_client_buffer */
#ifdef EGL_ANGLE_device_d3d
  _glewInfo_EGL_ANGLE_device_d3d();
#endif /* EGL_ANGLE_device_d3d */
#ifdef EGL_ANGLE_query_surface_pointer
  _glewInfo_EGL_ANGLE_query_surface_pointer();
#endif /* EGL_ANGLE_query_surface_pointer */
#ifdef EGL_ANGLE_surface_d3d_texture_2d_share_handle
  _glewInfo_EGL_ANGLE_surface_d3d_texture_2d_share_handle();
#endif /* EGL_ANGLE_surface_d3d_texture_2d_share_handle */
#ifdef EGL_ANGLE_window_fixed_size
  _glewInfo_EGL_ANGLE_window_fixed_size();
#endif /* EGL_ANGLE_window_fixed_size */
#ifdef EGL_ARM_implicit_external_sync
  _glewInfo_EGL_ARM_implicit_external_sync();
#endif /* EGL_ARM_implicit_external_sync */
#ifdef EGL_ARM_pixmap_multisample_discard
  _glewInfo_EGL_ARM_pixmap_multisample_discard();
#endif /* EGL_ARM_pixmap_multisample_discard */
#ifdef EGL_EXT_buffer_age
  _glewInfo_EGL_EXT_buffer_age();
#endif /* EGL_EXT_buffer_age */
#ifdef EGL_EXT_client_extensions
  _glewInfo_EGL_EXT_client_extensions();
#endif /* EGL_EXT_client_extensions */
#ifdef EGL_EXT_create_context_robustness
  _glewInfo_EGL_EXT_create_context_robustness();
#endif /* EGL_EXT_create_context_robustness */
#ifdef EGL_EXT_device_base
  _glewInfo_EGL_EXT_device_base();
#endif /* EGL_EXT_device_base */
#ifdef EGL_EXT_device_drm
  _glewInfo_EGL_EXT_device_drm();
#endif /* EGL_EXT_device_drm */
#ifdef EGL_EXT_device_enumeration
  _glewInfo_EGL_EXT_device_enumeration();
#endif /* EGL_EXT_device_enumeration */
#ifdef EGL_EXT_device_openwf
  _glewInfo_EGL_EXT_device_openwf();
#endif /* EGL_EXT_device_openwf */
#ifdef EGL_EXT_device_query
  _glewInfo_EGL_EXT_device_query();
#endif /* EGL_EXT_device_query */
#ifdef EGL_EXT_gl_colorspace_bt2020_linear
  _glewInfo_EGL_EXT_gl_colorspace_bt2020_linear();
#endif /* EGL_EXT_gl_colorspace_bt2020_linear */
#ifdef EGL_EXT_gl_colorspace_bt2020_pq
  _glewInfo_EGL_EXT_gl_colorspace_bt2020_pq();
#endif /* EGL_EXT_gl_colorspace_bt2020_pq */
#ifdef EGL_EXT_gl_colorspace_scrgb_linear
  _glewInfo_EGL_EXT_gl_colorspace_scrgb_linear();
#endif /* EGL_EXT_gl_colorspace_scrgb_linear */
#ifdef EGL_EXT_image_dma_buf_import
  _glewInfo_EGL_EXT_image_dma_buf_import();
#endif /* EGL_EXT_image_dma_buf_import */
#ifdef EGL_EXT_image_dma_buf_import_modifiers
  _glewInfo_EGL_EXT_image_dma_buf_import_modifiers();
#endif /* EGL_EXT_image_dma_buf_import_modifiers */
#ifdef EGL_EXT_multiview_window
  _glewInfo_EGL_EXT_multiview_window();
#endif /* EGL_EXT_multiview_window */
#ifdef EGL_EXT_output_base
  _glewInfo_EGL_EXT_output_base();
#endif /* EGL_EXT_output_base */
#ifdef EGL_EXT_output_drm
  _glewInfo_EGL_EXT_output_drm();
#endif /* EGL_EXT_output_drm */
#ifdef EGL_EXT_output_openwf
  _glewInfo_EGL_EXT_output_openwf();
#endif /* EGL_EXT_output_openwf */
#ifdef EGL_EXT_pixel_format_float
  _glewInfo_EGL_EXT_pixel_format_float();
#endif /* EGL_EXT_pixel_format_float */
#ifdef EGL_EXT_platform_base
  _glewInfo_EGL_EXT_platform_base();
#endif /* EGL_EXT_platform_base */
#ifdef EGL_EXT_platform_device
  _glewInfo_EGL_EXT_platform_device();
#endif /* EGL_EXT_platform_device */
#ifdef EGL_EXT_platform_wayland
  _glewInfo_EGL_EXT_platform_wayland();
#endif /* EGL_EXT_platform_wayland */
#ifdef EGL_EXT_platform_x11
  _glewInfo_EGL_EXT_platform_x11();
#endif /* EGL_EXT_platform_x11 */
#ifdef EGL_EXT_protected_content
  _glewInfo_EGL_EXT_protected_content();
#endif /* EGL_EXT_protected_content */
#ifdef EGL_EXT_protected_surface
  _glewInfo_EGL_EXT_protected_surface();
#endif /* EGL_EXT_protected_surface */
#ifdef EGL_EXT_stream_consumer_egloutput
  _glewInfo_EGL_EXT_stream_consumer_egloutput();
#endif /* EGL_EXT_stream_consumer_egloutput */
#ifdef EGL_EXT_surface_SMPTE2086_metadata
  _glewInfo_EGL_EXT_surface_SMPTE2086_metadata();
#endif /* EGL_EXT_surface_SMPTE2086_metadata */
#ifdef EGL_EXT_swap_buffers_with_damage
  _glewInfo_EGL_EXT_swap_buffers_with_damage();
#endif /* EGL_EXT_swap_buffers_with_damage */
#ifdef EGL_EXT_yuv_surface
  _glewInfo_EGL_EXT_yuv_surface();
#endif /* EGL_EXT_yuv_surface */
#ifdef EGL_HI_clientpixmap
  _glewInfo_EGL_HI_clientpixmap();
#endif /* EGL_HI_clientpixmap */
#ifdef EGL_HI_colorformats
  _glewInfo_EGL_HI_colorformats();
#endif /* EGL_HI_colorformats */
#ifdef EGL_IMG_context_priority
  _glewInfo_EGL_IMG_context_priority();
#endif /* EGL_IMG_context_priority */
#ifdef EGL_IMG_image_plane_attribs
  _glewInfo_EGL_IMG_image_plane_attribs();
#endif /* EGL_IMG_image_plane_attribs */
#ifdef EGL_KHR_cl_event
  _glewInfo_EGL_KHR_cl_event();
#endif /* EGL_KHR_cl_event */
#ifdef EGL_KHR_cl_event2
  _glewInfo_EGL_KHR_cl_event2();
#endif /* EGL_KHR_cl_event2 */
#ifdef EGL_KHR_client_get_all_proc_addresses
  _glewInfo_EGL_KHR_client_get_all_proc_addresses();
#endif /* EGL_KHR_client_get_all_proc_addresses */
#ifdef EGL_KHR_config_attribs
  _glewInfo_EGL_KHR_config_attribs();
#endif /* EGL_KHR_config_attribs */
#ifdef EGL_KHR_context_flush_control
  _glewInfo_EGL_KHR_context_flush_control();
#endif /* EGL_KHR_context_flush_control */
#ifdef EGL_KHR_create_context
  _glewInfo_EGL_KHR_create_context();
#endif /* EGL_KHR_create_context */
#ifdef EGL_KHR_create_context_no_error
  _glewInfo_EGL_KHR_create_context_no_error();
#endif /* EGL_KHR_create_context_no_error */
#ifdef EGL_KHR_debug
  _glewInfo_EGL_KHR_debug();
#endif /* EGL_KHR_debug */
#ifdef EGL_KHR_fence_sync
  _glewInfo_EGL_KHR_fence_sync();
#endif /* EGL_KHR_fence_sync */
#ifdef EGL_KHR_get_all_proc_addresses
  _glewInfo_EGL_KHR_get_all_proc_addresses();
#endif /* EGL_KHR_get_all_proc_addresses */
#ifdef EGL_KHR_gl_colorspace
  _glewInfo_EGL_KHR_gl_colorspace();
#endif /* EGL_KHR_gl_colorspace */
#ifdef EGL_KHR_gl_renderbuffer_image
  _glewInfo_EGL_KHR_gl_renderbuffer_image();
#endif /* EGL_KHR_gl_renderbuffer_image */
#ifdef EGL_KHR_gl_texture_2D_image
  _glewInfo_EGL_KHR_gl_texture_2D_image();
#endif /* EGL_KHR_gl_texture_2D_image */
#ifdef EGL_KHR_gl_texture_3D_image
  _glewInfo_EGL_KHR_gl_texture_3D_image();
#endif /* EGL_KHR_gl_texture_3D_image */
#ifdef EGL_KHR_gl_texture_cubemap_image
  _glewInfo_EGL_KHR_gl_texture_cubemap_image();
#endif /* EGL_KHR_gl_texture_cubemap_image */
#ifdef EGL_KHR_image
  _glewInfo_EGL_KHR_image();
#endif /* EGL_KHR_image */
#ifdef EGL_KHR_image_base
  _glewInfo_EGL_KHR_image_base();
#endif /* EGL_KHR_image_base */
#ifdef EGL_KHR_image_pixmap
  _glewInfo_EGL_KHR_image_pixmap();
#endif /* EGL_KHR_image_pixmap */
#ifdef EGL_KHR_lock_surface
  _glewInfo_EGL_KHR_lock_surface();
#endif /* EGL_KHR_lock_surface */
#ifdef EGL_KHR_lock_surface2
  _glewInfo_EGL_KHR_lock_surface2();
#endif /* EGL_KHR_lock_surface2 */
#ifdef EGL_KHR_lock_surface3
  _glewInfo_EGL_KHR_lock_surface3();
#endif /* EGL_KHR_lock_surface3 */
#ifdef EGL_KHR_mutable_render_buffer
  _glewInfo_EGL_KHR_mutable_render_buffer();
#endif /* EGL_KHR_mutable_render_buffer */
#ifdef EGL_KHR_no_config_context
  _glewInfo_EGL_KHR_no_config_context();
#endif /* EGL_KHR_no_config_context */
#ifdef EGL_KHR_partial_update
  _glewInfo_EGL_KHR_partial_update();
#endif /* EGL_KHR_partial_update */
#ifdef EGL_KHR_platform_android
  _glewInfo_EGL_KHR_platform_android();
#endif /* EGL_KHR_platform_android */
#ifdef EGL_KHR_platform_gbm
  _glewInfo_EGL_KHR_platform_gbm();
#endif /* EGL_KHR_platform_gbm */
#ifdef EGL_KHR_platform_wayland
  _glewInfo_EGL_KHR_platform_wayland();
#endif /* EGL_KHR_platform_wayland */
#ifdef EGL_KHR_platform_x11
  _glewInfo_EGL_KHR_platform_x11();
#endif /* EGL_KHR_platform_x11 */
#ifdef EGL_KHR_reusable_sync
  _glewInfo_EGL_KHR_reusable_sync();
#endif /* EGL_KHR_reusable_sync */
#ifdef EGL_KHR_stream
  _glewInfo_EGL_KHR_stream();
#endif /* EGL_KHR_stream */
#ifdef EGL_KHR_stream_attrib
  _glewInfo_EGL_KHR_stream_attrib();
#endif /* EGL_KHR_stream_attrib */
#ifdef EGL_KHR_stream_consumer_gltexture
  _glewInfo_EGL_KHR_stream_consumer_gltexture();
#endif /* EGL_KHR_stream_consumer_gltexture */
#ifdef EGL_KHR_stream_cross_process_fd
  _glewInfo_EGL_KHR_stream_cross_process_fd();
#endif /* EGL_KHR_stream_cross_process_fd */
#ifdef EGL_KHR_stream_fifo
  _glewInfo_EGL_KHR_stream_fifo();
#endif /* EGL_KHR_stream_fifo */
#ifdef EGL_KHR_stream_producer_aldatalocator
  _glewInfo_EGL_KHR_stream_producer_aldatalocator();
#endif /* EGL_KHR_stream_producer_aldatalocator */
#ifdef EGL_KHR_stream_producer_eglsurface
  _glewInfo_EGL_KHR_stream_producer_eglsurface();
#endif /* EGL_KHR_stream_producer_eglsurface */
#ifdef EGL_KHR_surfaceless_context
  _glewInfo_EGL_KHR_surfaceless_context();
#endif /* EGL_KHR_surfaceless_context */
#ifdef EGL_KHR_swap_buffers_with_damage
  _glewInfo_EGL_KHR_swap_buffers_with_damage();
#endif /* EGL_KHR_swap_buffers_with_damage */
#ifdef EGL_KHR_vg_parent_image
  _glewInfo_EGL_KHR_vg_parent_image();
#endif /* EGL_KHR_vg_parent_image */
#ifdef EGL_KHR_wait_sync
  _glewInfo_EGL_KHR_wait_sync();
#endif /* EGL_KHR_wait_sync */
#ifdef EGL_MESA_drm_image
  _glewInfo_EGL_MESA_drm_image();
#endif /* EGL_MESA_drm_image */
#ifdef EGL_MESA_image_dma_buf_export
  _glewInfo_EGL_MESA_image_dma_buf_export();
#endif /* EGL_MESA_image_dma_buf_export */
#ifdef EGL_MESA_platform_gbm
  _glewInfo_EGL_MESA_platform_gbm();
#endif /* EGL_MESA_platform_gbm */
#ifdef EGL_MESA_platform_surfaceless
  _glewInfo_EGL_MESA_platform_surfaceless();
#endif /* EGL_MESA_platform_surfaceless */
#ifdef EGL_NOK_swap_region
  _glewInfo_EGL_NOK_swap_region();
#endif /* EGL_NOK_swap_region */
#ifdef EGL_NOK_swap_region2
  _glewInfo_EGL_NOK_swap_region2();
#endif /* EGL_NOK_swap_region2 */
#ifdef EGL_NOK_texture_from_pixmap
  _glewInfo_EGL_NOK_texture_from_pixmap();
#endif /* EGL_NOK_texture_from_pixmap */
#ifdef EGL_NV_3dvision_surface
  _glewInfo_EGL_NV_3dvision_surface();
#endif /* EGL_NV_3dvision_surface */
#ifdef EGL_NV_coverage_sample
  _glewInfo_EGL_NV_coverage_sample();
#endif /* EGL_NV_coverage_sample */
#ifdef EGL_NV_coverage_sample_resolve
  _glewInfo_EGL_NV_coverage_sample_resolve();
#endif /* EGL_NV_coverage_sample_resolve */
#ifdef EGL_NV_cuda_event
  _glewInfo_EGL_NV_cuda_event();
#endif /* EGL_NV_cuda_event */
#ifdef EGL_NV_depth_nonlinear
  _glewInfo_EGL_NV_depth_nonlinear();
#endif /* EGL_NV_depth_nonlinear */
#ifdef EGL_NV_device_cuda
  _glewInfo_EGL_NV_device_cuda();
#endif /* EGL_NV_device_cuda */
#ifdef EGL_NV_native_query
  _glewInfo_EGL_NV_native_query();
#endif /* EGL_NV_native_query */
#ifdef EGL_NV_post_convert_rounding
  _glewInfo_EGL_NV_post_convert_rounding();
#endif /* EGL_NV_post_convert_rounding */
#ifdef EGL_NV_post_sub_buffer
  _glewInfo_EGL_NV_post_sub_buffer();
#endif /* EGL_NV_post_sub_buffer */
#ifdef EGL_NV_robustness_video_memory_purge
  _glewInfo_EGL_NV_robustness_video_memory_purge();
#endif /* EGL_NV_robustness_video_memory_purge */
#ifdef EGL_NV_stream_consumer_gltexture_yuv
  _glewInfo_EGL_NV_stream_consumer_gltexture_yuv();
#endif /* EGL_NV_stream_consumer_gltexture_yuv */
#ifdef EGL_NV_stream_cross_display
  _glewInfo_EGL_NV_stream_cross_display();
#endif /* EGL_NV_stream_cross_display */
#ifdef EGL_NV_stream_cross_object
  _glewInfo_EGL_NV_stream_cross_object();
#endif /* EGL_NV_stream_cross_object */
#ifdef EGL_NV_stream_cross_partition
  _glewInfo_EGL_NV_stream_cross_partition();
#endif /* EGL_NV_stream_cross_partition */
#ifdef EGL_NV_stream_cross_process
  _glewInfo_EGL_NV_stream_cross_process();
#endif /* EGL_NV_stream_cross_process */
#ifdef EGL_NV_stream_cross_system
  _glewInfo_EGL_NV_stream_cross_system();
#endif /* EGL_NV_stream_cross_system */
#ifdef EGL_NV_stream_fifo_next
  _glewInfo_EGL_NV_stream_fifo_next();
#endif /* EGL_NV_stream_fifo_next */
#ifdef EGL_NV_stream_fifo_synchronous
  _glewInfo_EGL_NV_stream_fifo_synchronous();
#endif /* EGL_NV_stream_fifo_synchronous */
#ifdef EGL_NV_stream_frame_limits
  _glewInfo_EGL_NV_stream_frame_limits();
#endif /* EGL_NV_stream_frame_limits */
#ifdef EGL_NV_stream_metadata
  _glewInfo_EGL_NV_stream_metadata();
#endif /* EGL_NV_stream_metadata */
#ifdef EGL_NV_stream_remote
  _glewInfo_EGL_NV_stream_remote();
#endif /* EGL_NV_stream_remote */
#ifdef EGL_NV_stream_reset
  _glewInfo_EGL_NV_stream_reset();
#endif /* EGL_NV_stream_reset */
#ifdef EGL_NV_stream_socket
  _glewInfo_EGL_NV_stream_socket();
#endif /* EGL_NV_stream_socket */
#ifdef EGL_NV_stream_socket_inet
  _glewInfo_EGL_NV_stream_socket_inet();
#endif /* EGL_NV_stream_socket_inet */
#ifdef EGL_NV_stream_socket_unix
  _glewInfo_EGL_NV_stream_socket_unix();
#endif /* EGL_NV_stream_socket_unix */
#ifdef EGL_NV_stream_sync
  _glewInfo_EGL_NV_stream_sync();
#endif /* EGL_NV_stream_sync */
#ifdef EGL_NV_sync
  _glewInfo_EGL_NV_sync();
#endif /* EGL_NV_sync */
#ifdef EGL_NV_system_time
  _glewInfo_EGL_NV_system_time();
#endif /* EGL_NV_system_time */
#ifdef EGL_TIZEN_image_native_buffer
  _glewInfo_EGL_TIZEN_image_native_buffer();
#endif /* EGL_TIZEN_image_native_buffer */
#ifdef EGL_TIZEN_image_native_surface
  _glewInfo_EGL_TIZEN_image_native_surface();
#endif /* EGL_TIZEN_image_native_surface */
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------------ */

int main (int argc, char** argv)
{
  GLuint err;
  struct createParams params =
  {
#if defined(GLEW_OSMESA)
#elif defined(GLEW_EGL)
#elif defined(_WIN32)
    -1,  /* pixelformat */
#elif !defined(__HAIKU__) && !defined(__APPLE__) || defined(GLEW_APPLE_GLX)
    "",  /* display */
    -1,  /* visual */
#endif
    0,   /* major */
    0,   /* minor */
    0,   /* profile mask */
    0    /* flags */
  };

#if defined(GLEW_EGL)
  typedef const GLubyte* (GLAPIENTRY * PFNGLGETSTRINGPROC) (GLenum name);
  PFNGLGETSTRINGPROC getString;
#endif

  if (glewParseArgs(argc-1, argv+1, &params))
  {
    fprintf(stderr, "Usage: glewinfo "
#if defined(GLEW_OSMESA)
#elif defined(GLEW_EGL)
#elif defined(_WIN32)
      "[-pf <pixelformat>] "
#elif !defined(__HAIKU__) && !defined(__APPLE__) || defined(GLEW_APPLE_GLX)
      "[-display <display>] "
      "[-visual <visual id>] "
#endif
      "[-version <OpenGL version>] "
      "[-profile core|compatibility] "
      "[-flag debug|forward]"
      "\n");
    return 1;
  }

  if (GL_TRUE == glewCreateContext(&params))
  {
    fprintf(stderr, "Error: glewCreateContext failed\n");
    glewDestroyContext();
    return 1;
  }
  glewExperimental = GL_TRUE;
  err = glewInit();
  if (GLEW_OK != err)
  {
    fprintf(stderr, "Error [main]: glewInit failed: %s\n", glewGetErrorString(err));
    glewDestroyContext();
    return 1;
  }

#if defined(GLEW_EGL)
  getString = (PFNGLGETSTRINGPROC) eglGetProcAddress("glGetString");
  if (!getString)
  {
    fprintf(stderr, "Error: eglGetProcAddress failed to fetch glGetString\n");
    glewDestroyContext();
    return 1;
  }
#endif

#if defined(_WIN32)
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
  if (fopen_s(&f, "glewinfo.txt", "w") != 0)
    f = stdout;
#else
  f = fopen("glewinfo.txt", "w");
#endif
  if (f == NULL) f = stdout;
#else
  f = stdout;
#endif
  fprintf(f, "---------------------------\n");
  fprintf(f, "    GLEW Extension Info\n");
  fprintf(f, "---------------------------\n\n");
  fprintf(f, "GLEW version %s\n", glewGetString(GLEW_VERSION));
#if defined(GLEW_OSMESA)
#elif defined(GLEW_EGL)
#elif defined(_WIN32)
  fprintf(f, "Reporting capabilities of pixelformat %d\n", params.pixelformat);
#elif !defined(__APPLE__) || defined(GLEW_APPLE_GLX)
  fprintf(f, "Reporting capabilities of display %s, visual 0x%x\n",
    params.display == NULL ? getenv("DISPLAY") : params.display, params.visual);
#endif
#if defined(GLEW_EGL)
  fprintf(f, "Running on a %s from %s\n",
    getString(GL_RENDERER), getString(GL_VENDOR));
  fprintf(f, "OpenGL version %s is supported\n", getString(GL_VERSION));
#else
  fprintf(f, "Running on a %s from %s\n",
    glGetString(GL_RENDERER), glGetString(GL_VENDOR));
  fprintf(f, "OpenGL version %s is supported\n", glGetString(GL_VERSION));
#endif
  glewInfo();
#if defined(GLEW_OSMESA)
#elif defined(GLEW_EGL)
  eglewInfo();
#elif defined(_WIN32)
  wglewInfo();
#else
  glxewInfo();
#endif
  if (f != stdout) fclose(f);
  glewDestroyContext();
  return 0;
}

/* ------------------------------------------------------------------------ */

GLboolean glewParseArgs (int argc, char** argv, struct createParams *params)
{
  int p = 0;
  while (p < argc)
  {
    if (!strcmp(argv[p], "-version"))
    {
      if (++p >= argc) return GL_TRUE;
      if (sscanf(argv[p++], "%d.%d", &params->major, &params->minor) != 2) return GL_TRUE;
    }
    else if (!strcmp(argv[p], "-profile"))
    {
      if (++p >= argc) return GL_TRUE;
      if      (strcmp("core",         argv[p]) == 0) params->profile |= 1;
      else if (strcmp("compatibility",argv[p]) == 0) params->profile |= 2;
      else return GL_TRUE;
      ++p;
    }
    else if (!strcmp(argv[p], "-flag"))
    {
      if (++p >= argc) return GL_TRUE;
      if      (strcmp("debug",  argv[p]) == 0) params->flags |= 1;
      else if (strcmp("forward",argv[p]) == 0) params->flags |= 2;
      else return GL_TRUE;
      ++p;
    }
#if defined(GLEW_OSMESA)
#elif defined(GLEW_EGL)
#elif defined(_WIN32)
    else if (!strcmp(argv[p], "-pf") || !strcmp(argv[p], "-pixelformat"))
    {
      if (++p >= argc) return GL_TRUE;
      params->pixelformat = strtol(argv[p++], NULL, 0);
    }
#elif !defined(__HAIKU__) && !defined(__APPLE__) || defined(GLEW_APPLE_GLX)
    else if (!strcmp(argv[p], "-display"))
    {
      if (++p >= argc) return GL_TRUE;
      params->display = argv[p++];
     }
    else if (!strcmp(argv[p], "-visual"))
    {
      if (++p >= argc) return GL_TRUE;
      params->visual = (int)strtol(argv[p++], NULL, 0);
    }
#endif
    else
      return GL_TRUE;
  }
  return GL_FALSE;
}

/* ------------------------------------------------------------------------ */

#if defined(GLEW_EGL)
EGLDisplay  display;
EGLContext  ctx;

/* See: http://stackoverflow.com/questions/12662227/opengl-es2-0-offscreen-context-for-fbo-rendering */

GLboolean glewCreateContext (struct createParams *params)
{
  EGLDeviceEXT devices[1];
  EGLint numDevices;
  EGLSurface  surface;
  EGLint majorVersion, minorVersion;
  EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
   };
  static const EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };
  static const EGLint pBufferAttribs[] = {
    EGL_WIDTH,  128,
    EGL_HEIGHT, 128,
    EGL_NONE
  };
  EGLConfig config;
  EGLint numConfig;
  EGLBoolean pBuffer;

  PFNEGLQUERYDEVICESEXTPROC       queryDevices = NULL;
  PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = NULL;
  PFNEGLGETERRORPROC              getError = NULL;
  PFNEGLGETDISPLAYPROC            getDisplay = NULL;
  PFNEGLINITIALIZEPROC            initialize = NULL;
  PFNEGLBINDAPIPROC               bindAPI    = NULL;
  PFNEGLCHOOSECONFIGPROC          chooseConfig = NULL;
  PFNEGLCREATEWINDOWSURFACEPROC   createWindowSurface = NULL;
  PFNEGLCREATECONTEXTPROC         createContext = NULL;
  PFNEGLMAKECURRENTPROC           makeCurrent = NULL;
  PFNEGLCREATEPBUFFERSURFACEPROC  createPbufferSurface = NULL;

  /* Load necessary entry points */
  queryDevices         = (PFNEGLQUERYDEVICESEXTPROC)       eglGetProcAddress("eglQueryDevicesEXT");
  getPlatformDisplay   = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
  getError             = (PFNEGLGETERRORPROC)              eglGetProcAddress("eglGetError");
  getDisplay           = (PFNEGLGETDISPLAYPROC)            eglGetProcAddress("eglGetDisplay");
  initialize           = (PFNEGLINITIALIZEPROC)            eglGetProcAddress("eglInitialize");
  bindAPI              = (PFNEGLBINDAPIPROC)               eglGetProcAddress("eglBindAPI");
  chooseConfig         = (PFNEGLCHOOSECONFIGPROC)          eglGetProcAddress("eglChooseConfig");
  createWindowSurface  = (PFNEGLCREATEWINDOWSURFACEPROC)   eglGetProcAddress("eglCreateWindowSurface");
  createPbufferSurface = (PFNEGLCREATEPBUFFERSURFACEPROC)  eglGetProcAddress("eglCreatePbufferSurface");
  createContext        = (PFNEGLCREATECONTEXTPROC)         eglGetProcAddress("eglCreateContext");
  makeCurrent          = (PFNEGLMAKECURRENTPROC)           eglGetProcAddress("eglMakeCurrent");
  if (!getError || !getDisplay || !initialize || !bindAPI || !chooseConfig || !createWindowSurface || !createContext || !makeCurrent)
    return GL_TRUE;

  pBuffer = 0;
  display = EGL_NO_DISPLAY;
  if (queryDevices && getPlatformDisplay)
  {
    queryDevices(1, devices, &numDevices);
    if (numDevices==1)
    {
      /* Nvidia EGL doesn't need X11 for p-buffer surface */
      display = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[0], 0);
      configAttribs[1] = EGL_PBUFFER_BIT;
      pBuffer = 1;
    }
  }
  if (display==EGL_NO_DISPLAY)
  {
    /* Fall-back to X11 surface, works on Mesa */
    display = getDisplay(EGL_DEFAULT_DISPLAY);
  }
  if (display == EGL_NO_DISPLAY)
    return GL_TRUE;

  eglewInit(display);

  if (bindAPI(EGL_OPENGL_API) != EGL_TRUE)
    return GL_TRUE;

  if (chooseConfig(display, configAttribs, &config, 1, &numConfig) != EGL_TRUE || (numConfig != 1))
    return GL_TRUE;

  ctx = createContext(display, config, EGL_NO_CONTEXT, pBuffer ? contextAttribs : NULL);
  if (NULL == ctx)
    return GL_TRUE;

  surface = EGL_NO_SURFACE;
  /* Create a p-buffer surface if possible */
  if (pBuffer && createPbufferSurface)
  {
    surface = createPbufferSurface(display, config, pBufferAttribs);
  }
  /* Create a generic surface without a native window, if necessary */
  if (surface==EGL_NO_SURFACE)
  {
    surface = createWindowSurface(display, config, (EGLNativeWindowType) NULL, NULL);
  }
#if 0
  if (surface == EGL_NO_SURFACE)
    return GL_TRUE;
#endif

  if (makeCurrent(display, surface, surface, ctx) != EGL_TRUE)
    return GL_TRUE;

  return GL_FALSE;
}

void glewDestroyContext ()
{
  if (NULL != ctx) eglDestroyContext(display, ctx);
}

#elif defined(GLEW_OSMESA)
OSMesaContext ctx;

static const GLint osmFormat = GL_UNSIGNED_BYTE;
static const GLint osmWidth = 640;
static const GLint osmHeight = 480;
static GLubyte *osmPixels = NULL;

GLboolean glewCreateContext (struct createParams *params)
{
  ctx = OSMesaCreateContext(OSMESA_RGBA, NULL);
  if (NULL == ctx) return GL_TRUE;
  if (NULL == osmPixels)
  {
    osmPixels = (GLubyte *) calloc(osmWidth*osmHeight*4, 1);
  }
  if (!OSMesaMakeCurrent(ctx, osmPixels, GL_UNSIGNED_BYTE, osmWidth, osmHeight))
  {
      return GL_TRUE;
  }
  return GL_FALSE;
}

void glewDestroyContext ()
{
  if (NULL != ctx) OSMesaDestroyContext(ctx);
}

#elif defined(_WIN32)

HWND wnd = NULL;
HDC dc = NULL;
HGLRC rc = NULL;

GLboolean glewCreateContext (struct createParams* params)
{
  WNDCLASS wc;
  PIXELFORMATDESCRIPTOR pfd;
  /* register window class */
  ZeroMemory(&wc, sizeof(WNDCLASS));
  wc.hInstance = GetModuleHandle(NULL);
  wc.lpfnWndProc = DefWindowProc;
  wc.lpszClassName = "GLEW";
  if (0 == RegisterClass(&wc)) return GL_TRUE;
  /* create window */
  wnd = CreateWindow("GLEW", "GLEW", 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                     CW_USEDEFAULT, NULL, NULL, GetModuleHandle(NULL), NULL);
  if (NULL == wnd) return GL_TRUE;
  /* get the device context */
  dc = GetDC(wnd);
  if (NULL == dc) return GL_TRUE;
  /* find pixel format */
  ZeroMemory(&pfd, sizeof(PIXELFORMATDESCRIPTOR));
  if (params->pixelformat == -1) /* find default */
  {
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    params->pixelformat = ChoosePixelFormat(dc, &pfd);
    if (params->pixelformat == 0) return GL_TRUE;
  }
  /* set the pixel format for the dc */
  if (FALSE == SetPixelFormat(dc, params->pixelformat, &pfd)) return GL_TRUE;
  /* create rendering context */
  rc = wglCreateContext(dc);
  if (NULL == rc) return GL_TRUE;
  if (FALSE == wglMakeCurrent(dc, rc)) return GL_TRUE;
  if (params->major || params->profile || params->flags)
  {
    HGLRC oldRC = rc;
    int contextAttrs[20];
    int i;

    wglewInit();

    /* Intel HD 3000 has WGL_ARB_create_context, but not WGL_ARB_create_context_profile */
    if (!wglewGetExtension("WGL_ARB_create_context"))
      return GL_TRUE;

    i = 0;
    if (params->major)
    {
      contextAttrs[i++] = WGL_CONTEXT_MAJOR_VERSION_ARB;
      contextAttrs[i++] = params->major;
      contextAttrs[i++] = WGL_CONTEXT_MINOR_VERSION_ARB;
      contextAttrs[i++] = params->minor;
    }
    if (params->profile)
    {
      contextAttrs[i++] = WGL_CONTEXT_PROFILE_MASK_ARB;
      contextAttrs[i++] = params->profile;
    }
    if (params->flags)
    {
      contextAttrs[i++] = WGL_CONTEXT_FLAGS_ARB;
      contextAttrs[i++] = params->flags;
    }
    contextAttrs[i++] = 0;
    rc = wglCreateContextAttribsARB(dc, 0, contextAttrs);

    if (NULL == rc) return GL_TRUE;
    if (!wglMakeCurrent(dc, rc)) return GL_TRUE;

    wglDeleteContext(oldRC);
  }
  return GL_FALSE;
}

void glewDestroyContext ()
{
  if (NULL != rc) wglMakeCurrent(NULL, NULL);
  if (NULL != rc) wglDeleteContext(rc);
  if (NULL != wnd && NULL != dc) ReleaseDC(wnd, dc);
  if (NULL != wnd) DestroyWindow(wnd);
  UnregisterClass("GLEW", GetModuleHandle(NULL));
}

/* ------------------------------------------------------------------------ */

#elif defined(__APPLE__) && !defined(GLEW_APPLE_GLX)

#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLTypes.h>

CGLContextObj ctx, octx;

GLboolean glewCreateContext (struct createParams *params)
{
  CGLPixelFormatAttribute contextAttrs[20];
  int i;
  CGLPixelFormatObj pf;
  GLint npix;
  CGLError error;

  i = 0;
  contextAttrs[i++] = kCGLPFAAccelerated; /* No software rendering */

  /* MAC_OS_X_VERSION_10_7  == 1070 */
  #if MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
  if (params->profile & GL_CONTEXT_CORE_PROFILE_BIT)
  {
    if ((params->major==3 && params->minor>=2) || params->major>3)
    {
      contextAttrs[i++] = kCGLPFAOpenGLProfile;                                /* OSX 10.7 Lion onwards */
      contextAttrs[i++] = (CGLPixelFormatAttribute) kCGLOGLPVersion_3_2_Core;  /* 3.2 Core Context      */
    }
  }
  #endif

  contextAttrs[i++] = 0;

  error = CGLChoosePixelFormat(contextAttrs, &pf, &npix);
  if (error) return GL_TRUE;
  error = CGLCreateContext(pf, NULL, &ctx);
  if (error) return GL_TRUE;
  CGLReleasePixelFormat(pf);
  octx = CGLGetCurrentContext();
  error = CGLSetCurrentContext(ctx);
  if (error) return GL_TRUE;
  /* Needed for Regal on the Mac */
  #if defined(GLEW_REGAL) && defined(__APPLE__)
  RegalMakeCurrent(ctx);
  #endif
  return GL_FALSE;
}

void glewDestroyContext ()
{
  CGLSetCurrentContext(octx);
  CGLReleaseContext(ctx);
}

/* ------------------------------------------------------------------------ */

#elif defined(__HAIKU__)

GLboolean glewCreateContext (struct createParams *params)
{
  /* TODO: Haiku: We need to call C++ code here */
  return GL_FALSE;
}

void glewDestroyContext ()
{
  /* TODO: Haiku: We need to call C++ code here */
}

/* ------------------------------------------------------------------------ */

#else /* __UNIX || (__APPLE__ && GLEW_APPLE_GLX) */

Display* dpy = NULL;
XVisualInfo* vi = NULL;
XVisualInfo* vis = NULL;
GLXContext ctx = NULL;
Window wnd = 0;
Colormap cmap = 0;

GLboolean glewCreateContext (struct createParams *params)
{
  int attrib[] = { GLX_RGBA, GLX_DOUBLEBUFFER, None };
  int erb, evb;
  XSetWindowAttributes swa;
  /* open display */
  dpy = XOpenDisplay(params->display);
  if (NULL == dpy) return GL_TRUE;
  /* query for glx */
  if (!glXQueryExtension(dpy, &erb, &evb)) return GL_TRUE;
  /* choose visual */
  if (params->visual == -1)
  {
    vi = glXChooseVisual(dpy, DefaultScreen(dpy), attrib);
    if (NULL == vi) return GL_TRUE;
    params->visual = (int)XVisualIDFromVisual(vi->visual);
  }
  else
  {
    int n_vis, i;
    vis = XGetVisualInfo(dpy, 0, NULL, &n_vis);
    for (i=0; i<n_vis; i++)
    {
      if ((int)XVisualIDFromVisual(vis[i].visual) == params->visual)
        vi = &vis[i];
    }
    if (vi == NULL) return GL_TRUE;
  }
  /* create context */
  ctx = glXCreateContext(dpy, vi, None, True);
  if (NULL == ctx) return GL_TRUE;
  /* create window */
  /*wnd = XCreateSimpleWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 1, 1, 1, 0, 0);*/
  cmap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
  swa.border_pixel = 0;
  swa.colormap = cmap;
  wnd = XCreateWindow(dpy, RootWindow(dpy, vi->screen),
                      0, 0, 1, 1, 0, vi->depth, InputOutput, vi->visual,
                      CWBorderPixel | CWColormap, &swa);
  /* make context current */
  if (!glXMakeCurrent(dpy, wnd, ctx)) return GL_TRUE;
  if (params->major || params->profile || params->flags)
  {
    GLXContext oldCtx = ctx;
    GLXFBConfig *FBConfigs;
    int FBConfigAttrs[] = { GLX_FBCONFIG_ID, 0, None };
    int contextAttrs[20];
    int nelems, i;

    glxewInit();

    if (!glxewGetExtension("GLX_ARB_create_context"))
      return GL_TRUE;

    if (glXQueryContext(dpy, oldCtx, GLX_FBCONFIG_ID, &FBConfigAttrs[1]))
      return GL_TRUE;
    FBConfigs = glXChooseFBConfig(dpy, vi->screen, FBConfigAttrs, &nelems);

    if (nelems < 1)
      return GL_TRUE;

    i = 0;
    if (params->major)
    {
      contextAttrs[i++] = GLX_CONTEXT_MAJOR_VERSION_ARB;
      contextAttrs[i++] = params->major;
      contextAttrs[i++] = GLX_CONTEXT_MINOR_VERSION_ARB;
      contextAttrs[i++] = params->minor;
    }
    if (params->profile)
    {
      contextAttrs[i++] = GLX_CONTEXT_PROFILE_MASK_ARB;
      contextAttrs[i++] = params->profile;
    }
    if (params->flags)
    {
      contextAttrs[i++] = GLX_CONTEXT_FLAGS_ARB;
      contextAttrs[i++] = params->flags;
    }
    contextAttrs[i++] = None;
    ctx = glXCreateContextAttribsARB(dpy, *FBConfigs, NULL, True, contextAttrs);

    if (NULL == ctx) return GL_TRUE;
    if (!glXMakeCurrent(dpy, wnd, ctx)) return GL_TRUE;

    glXDestroyContext(dpy, oldCtx);

    XFree(FBConfigs);
  }
  return GL_FALSE;
}

void glewDestroyContext ()
{
  if (NULL != dpy && NULL != ctx) glXDestroyContext(dpy, ctx);
  if (NULL != dpy && 0 != wnd) XDestroyWindow(dpy, wnd);
  if (NULL != dpy && 0 != cmap) XFreeColormap(dpy, cmap);
  if (NULL != vis)
    XFree(vis);
  else if (NULL != vi)
    XFree(vi);
  if (NULL != dpy) XCloseDisplay(dpy);
}

#endif /* __UNIX || (__APPLE__ && GLEW_APPLE_GLX) */
