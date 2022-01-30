#pragma once

#include <cstring>

#include "Common/GPU/Shader.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUCommon.h"
#include "Common/Data/Collections/Slice.h"

#include "Common/GPU/thin3d.h"

// Helps generate a shader compatible with all backends.
// 
// Can use the uniform buffer support in thin3d.
//
// Using #defines and magic in this class, we partially define our own shader language that basically looks
// like GLSL, but has a few little oddities like splat3.

struct InputDef {
	const char *type;
	const char *name;
};

struct UniformDef {
	const char *type;
	const char *name;
	int index;
};

struct VaryingDef {
	const char *type;
	const char *name;
	const char *semantic;
	int index;
	const char *precision;
};

class ShaderWriter {
public:
	ShaderWriter(char *buffer, const ShaderLanguageDesc &lang, ShaderStage stage, const char **gl_extensions, size_t num_gl_extensions) : p_(buffer), lang_(lang), stage_(stage) {
		Preamble(gl_extensions, num_gl_extensions);
	}

	// I tried to call all three write functions "W", but only MSVC
	// managed to disentangle the ambiguities, so had to give up on that.

	// Assumes the input is zero-terminated.
	// C : Copies a buffer directly to the stream.
	template<size_t T>
	ShaderWriter &C(const char(&text)[T]) {
		memcpy(p_, text, T);
		p_ += T - 1;
		return *this;
	}
	// W: Writes a zero-terminated string to the stream.
	ShaderWriter &W(const char *text) {
		size_t len = strlen(text);
		memcpy(p_, text, len + 1);
		p_ += len;
		return *this;
	}

	// F: Formats into the buffer.
	ShaderWriter &F(const char *format, ...);

	// Useful for fragment shaders in GLES.
	// We always default integers to high precision.
	void HighPrecisionFloat();

	// Several of the shader languages ignore samplers, beware of that.
	void DeclareSampler2D(const char *name, int binding);
	void DeclareTexture2D(const char *name, int binding);

	ShaderWriter &SampleTexture2D(const char *texName, const char *samplerName, const char *uv);

	// Simple shaders with no special tricks.
	void BeginVSMain(Slice<InputDef> inputs, Slice<UniformDef> uniforms, Slice<VaryingDef> varyings);
	void BeginFSMain(Slice<UniformDef> uniforms, Slice<VaryingDef> varyings);

	// For simple shaders that output a single color, we can deal with this generically.
	void EndVSMain(Slice<VaryingDef> varyings);
	void EndFSMain(const char *vec4_color_variable);


	void Rewind(size_t offset) {
		p_ -= offset;
	}

	// Can probably remove this
	char *GetPos() {
		return p_;
	}

private:
	void Preamble(const char **gl_extensions, size_t num_gl_extensions);

	char *p_;
	const ShaderLanguageDesc &lang_;
	const ShaderStage stage_;
};
