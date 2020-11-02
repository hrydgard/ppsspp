#pragma once

#include "Common/Log.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/ShaderCommon.h"

// Helps generate a shader compatible with all backends.
// Using #defines and magic in this class, we partially define our own shader language that basically looks
// like GLSL, but has a few little oddities like splat3.

class ShaderWriter {
public:
	ShaderWriter(char *buffer, const ShaderLanguageDesc &lang, ShaderStage stage, const char **gl_extensions, size_t num_gl_extensions) : p_(buffer), lang_(lang), stage_(stage) {
		Preamble(gl_extensions, num_gl_extensions);
	}

	// Assumes the input is zero-terminated.
	template<size_t T>
	void W(const char(&text)[T]) {
		memcpy(p_, text, T);
		p_ += T;
	}
	void W(const char *text) {
		size_t len = strlen(text);
		memcpy(p_, text, len + 1);
		p_ += len;
	}

	// Formats into the buffer.
	void W(char *format, ...);

	// void BeginMain();
	// void EndMain();

	char *GetPos() {
		return p_;
	}

private:
	void Preamble(const char **gl_extensions, size_t num_gl_extensions);

	char *p_;
	const ShaderLanguageDesc &lang_;
	const ShaderStage stage_;
};
