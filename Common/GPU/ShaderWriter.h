#pragma once

#include <cstring>

#include "Common/GPU/Shader.h"
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
	int semantic;
};

struct VaryingDef {
	const char *type;
	const char *name;
	int semantic;
	int index;
	const char *precision;
};

enum class ShaderWriterFlags {
	NONE = 0,
	FS_WRITE_DEPTH = 1,
	FS_AUTO_STEREO = 2,  // Automatically indexes makes samplers tagged with `array` by gl_ViewIndex. Useful for stereo rendering.
};
ENUM_CLASS_BITOPS(ShaderWriterFlags);

class ShaderWriter {
public:
	// Extensions are supported for both OpenGL ES and Vulkan (though of course, they're different).
	ShaderWriter(char *buffer, const ShaderLanguageDesc &lang, ShaderStage stage, Slice<const char *> extensions = Slice<const char *>(), ShaderWriterFlags flags = ShaderWriterFlags::NONE) : p_(buffer), lang_(lang), stage_(stage), flags_(flags) {
		buffer[0] = '\0';
		Preamble(extensions);
	}
	ShaderWriter(const ShaderWriter &) = delete;

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

	ShaderWriter &endl() {
		return C("\n");
	}

	// Useful for fragment shaders in GLES.
	// We always default integers to high precision.
	void HighPrecisionFloat();
	void LowPrecisionFloat();

	// NOTE: samplers must live for the rest of ShaderWriter's lifetime. No way to express that in C++ though :(
	void DeclareSamplers(Slice<SamplerDef> samplers);

	// Same as DeclareSamplers, but doesn't actually declare them.
	// This is currently only required by FragmentShaderGenerator.
	void ApplySamplerMetadata(Slice<SamplerDef> samplers);

	void ConstFloat(const char *name, float value);
	void SetFlags(ShaderWriterFlags flags) { flags_ |= flags; }
	ShaderWriterFlags Flags() const { return flags_; }
	void SetTexBindingBase(int base) { texBindingBase_ = base; }

	ShaderWriter &SampleTexture2D(const char *texName, const char *uv);
	ShaderWriter &SampleTexture2DOffset(const char *texName, const char *uv, int offX, int offY);
	ShaderWriter &LoadTexture2D(const char *texName, const char *integer_uv, int level);
	ShaderWriter &GetTextureSize(const char *szVariable, const char *texName);

	// Simple shaders with no special tricks.
	void BeginVSMain(Slice<InputDef> inputs, Slice<UniformDef> uniforms, Slice<VaryingDef> varyings);
	void BeginFSMain(Slice<UniformDef> uniforms, Slice<VaryingDef> varyings);
	void BeginGSMain(Slice<VaryingDef> varyings, Slice<VaryingDef> outVaryings);

	// For simple shaders that output a single color, we can deal with this generically.
	void EndVSMain(Slice<VaryingDef> varyings);
	void EndFSMain(const char *vec4_color_variable);
	void EndGSMain();

	const ShaderLanguageDesc &Lang() const {
		return lang_;
	}

	void Rewind(size_t offset) {
		p_ -= offset;
	}

	// Can probably remove this
	char *GetPos() {
		return p_;
	}

private:
	// Several of the shader languages ignore samplers, beware of that.
	void DeclareSampler2D(const SamplerDef &def);
	void DeclareTexture2D(const SamplerDef &def);
	const SamplerDef *GetSamplerDef(const char *name) const;

	void Preamble(Slice<const char *> extensions);

	char *p_;
	const ShaderLanguageDesc &lang_;
	const ShaderStage stage_;
	Slice<SamplerDef> samplers_;
	ShaderWriterFlags flags_ = ShaderWriterFlags::NONE;
	Slice<SamplerDef> samplerDefs_;
	int texBindingBase_ = 1;
};
