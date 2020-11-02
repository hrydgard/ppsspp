#include <cstdarg>

#include "Common/GPU/ShaderWriter.h"
#include "GPU/Common/ReinterpretFramebuffer.h"

// TODO: We could have an option to preserve any extra color precision. But gonna start without it.
// Requires full size integer math.
bool GenerateReinterpretFragmentShader(char *buffer, GEBufferFormat from, GEBufferFormat to, const ShaderLanguageDesc &lang) {
	if (!lang.bitwiseOps) {
		return false;
	}

	ShaderWriter writer(buffer, lang, ShaderStage::Fragment, nullptr, 0);

	writer.DeclareSampler2D("samp", 0);
	writer.DeclareTexture2D("tex", 0);

	static const VaryingDef varyings[1] = {
		{ "vec4", "v_texcoord", "TEXCOORD0" },
	};

	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings);

	writer.C("  vec4 val = ").SampleTexture2D("tex", "samp", "v_texcoord.xy").C(";\n");

	switch (from) {
	case GE_FORMAT_4444:
		writer.C("  uint color = uint(val.r * 15.99) | (uint(val.g * 15.99) << 4) | (uint(val.b * 15.99) << 8) | (uint(val.a * 15.99) << 12);\n");
		break;
	case GE_FORMAT_5551:
		writer.C("  uint color = uint(val.r * 31.99) | (uint(val.g * 31.99) << 5) | (uint(val.b * 31.99) << 10);\n");
		writer.C("  if (val.a > 128.0) color |= 0x8000U;\n");
		break;
	case GE_FORMAT_565:
		writer.C("  uint color = uint(val.r * 31.99) | (uint(val.g * 63.99) << 5) | (uint(val.b * 31.99) << 11);\n");
		break;
	default:
		_assert_(false);
		break;
	}

	switch (to) {
	case GE_FORMAT_4444:
		writer.C("  vec4 outColor = vec4(float(color & 0xFU), float((color >> 4) & 0xFU), float((color >> 8) & 0xFU), float((color >> 12) & 0xFU));\n");
		writer.C("  outColor *= 1.0 / 15.0;\n");
		break;
	case GE_FORMAT_5551:
		writer.C("  vec4 outColor = vec4(float(color & 0x1FU), float((color >> 5) & 0x1FU), float((color >> 10) & 0x1FU), 0.0);\n");
		writer.C("  outColor.rgb *= 1.0 / 31.0;\n");
		writer.C("  outColor.a = float(color >> 15);\n");
		break;
	case GE_FORMAT_565:
		writer.C("  vec4 outColor = vec4(float(color & 0x1FU), float((color >> 5) & 0x3FU), float((color >> 11) & 0x1FU), 1.0);\n");
		writer.C("  outColor.rb *= 1.0 / 31.0;\n");
		writer.C("  outColor.g *= 1.0 / 63.0;\n");
		break;
	default:
		_assert_(false);
		break;
	}

	writer.EndFSMain("outColor");
	return true;
}
