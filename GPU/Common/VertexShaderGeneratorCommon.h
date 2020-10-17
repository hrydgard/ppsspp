#pragma once

#include "GPU/Common/ShaderId.h"
#include <cstdarg>
#include <cstdlib>

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

enum class CodeBufLang {
	GLSL_ES,
	GLSL_VK,
};


struct CodeBuf {
	char *p;
	CodeBufLang lang;

	CodeBuf &F(const char *format, ...) {
		va_list args;
		va_start(args, format);
		size_t written = vsprintf(p, format, args);
		va_end(args);
		p += written;
		return *this;
	}
	// TODO: calculate the length at compile time with a template.
	CodeBuf &W(const char *str) {
		size_t len = strlen(str);
		memcpy(p, str, len + 1);
		p += len;
		return *this;
	}
};

// If we can know from the lights that there's no specular, sets specularIsZero to true.
char *WriteLights(char *p, const VShaderID &id, DoLightComputation doLight[4], bool *specularIsZero);
