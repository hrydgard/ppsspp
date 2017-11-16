#pragma once

#include <string>
#include <cstdint>

// Utility to be able to liberally sprinkle GL error checks around your code
// and easily disable them all in release builds - just undefine DEBUG_OPENGL.

// #define DEBUG_OPENGL

#if defined(DEBUG_OPENGL)

void CheckGLError(const char *file, int line);
#define CHECK_GL_ERROR_IF_DEBUG() CheckGLError(__FILE__, __LINE__)

#else

#define CHECK_GL_ERROR_IF_DEBUG()

#endif

std::string GLEnumToString(uint16_t value);
