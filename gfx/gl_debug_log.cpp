#if defined(ANDROID)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
typedef char GLchar;
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include "base/logging.h"

void glCheckzor(const char *file, int line) {
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    ELOG("GL error on line %i in %s: %i (%04x)", line, file, (int)err, (int)err);
  }
}

#ifndef ANDROID
#if 0
void log_callback(GLenum source, GLenum type,
                  GLuint id,
                  GLenum severity,
                  GLsizei length,
                  const GLchar* message,
                  GLvoid* userParam) {
  const char *src = "unknown";
  switch (source) {
    case GL_DEBUG_SOURCE_API_GL_ARB:
      src = "GL";
      break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:
      src = "GLSL";
      break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:
      src = "X";
      break;
    default:
      break;
  }
  switch (type) {
    case GL_DEBUG_TYPE_ERROR_ARB:
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
      ELOG("%s: %s", src, message);
      break;
    default:
      ILOG("%s: %s", src, message);
      break;
  }
}
#endif
#endif

void gl_log_enable() {
#ifndef ANDROID
#if 0
  glEnable(DEBUG_OUTPUT_SYNCHRONOUS_ARB);  // TODO: Look into disabling, for more perf
  glDebugMessageCallback(&log_callback, 0);
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
#endif
#endif
}
