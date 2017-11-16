#include <cstdlib>

#include "base/logging.h"
#include "gfx/gl_common.h"
#include "gfx/gl_debug_log.h"

// This we can expand as needed.
std::string GLEnumToString(uint16_t value) {
	char str[64];
	switch (value) {
	case GL_UNSIGNED_SHORT_4_4_4_4: return "GL_UNSIGNED_SHORT_4_4_4_4";
	case GL_UNSIGNED_SHORT_5_5_5_1: return "GL_UNSIGNED_SHORT_5_5_5_1";
	case GL_UNSIGNED_SHORT_5_6_5: return "GL_UNSIGNED_SHORT_5_6_5";
	case GL_UNSIGNED_BYTE: return "GL_UNSIGNED_BYTE";
	case GL_RGBA: return "GL_RGBA";
	case GL_RGB: return "GL_RGB";
#if !defined(USING_GLES2)
	case GL_BGRA: return "GL_BGRA";
	case GL_UNSIGNED_SHORT_4_4_4_4_REV: return "GL_UNSIGNED_SHORT_4_4_4_4_REV";
	case GL_UNSIGNED_SHORT_5_6_5_REV: return "GL_UNSIGNED_SHORT_5_6_5_REV";
	case GL_UNSIGNED_SHORT_1_5_5_5_REV: return "GL_UNSIGNED_SHORT_1_5_5_5_REV";
	case GL_UNSIGNED_INT_8_8_8_8_REV: return "GL_UNSIGNED_INT_8_8_8_8_REV";
#endif
	case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
	case GL_PACK_ALIGNMENT: return "GL_PACK_ALIGNMENT";
	case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
	default: {
		snprintf(str, sizeof(str), "(unk:%04x)", value);
		return str;
	}
	}
}

void CheckGLError(const char *file, int line) {
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		ELOG("GL error %s on %s:%d", GLEnumToString(err).c_str(), file, line);
	}
}
