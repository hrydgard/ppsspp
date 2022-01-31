#include <cstdarg>

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/Log.h"
#include "GPU/Common/ReinterpretFramebuffer.h"

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", "TEXCOORD0", 0, "highp" },
};

// TODO: We could possibly have an option to preserve any extra color precision? But gonna start without it.
// Requires full size integer math. It would be possible to make a floating point-only version with lots of
// modulo and stuff, might do it one day.
bool GenerateReinterpretFragmentShader(char *buffer, GEBufferFormat from, GEBufferFormat to, const ShaderLanguageDesc &lang) {
	if (!lang.bitwiseOps) {
		return false;
	}

	ShaderWriter writer(buffer, lang, ShaderStage::Fragment, nullptr, 0);

	writer.HighPrecisionFloat();

	writer.DeclareSampler2D("samp", 0);
	writer.DeclareTexture2D("tex", 0);

	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings);

	writer.C("  vec4 val = ").SampleTexture2D("tex", "samp", "v_texcoord.xy").C(";\n");

	switch (from) {
	case GE_FORMAT_4444:
		writer.C("  uint color = uint(val.r * 15.99) | (uint(val.g * 15.99) << 4u) | (uint(val.b * 15.99) << 8u) | (uint(val.a * 15.99) << 12u);\n");
		break;
	case GE_FORMAT_5551:
		writer.C("  uint color = uint(val.r * 31.99) | (uint(val.g * 31.99) << 5u) | (uint(val.b * 31.99) << 10u);\n");
		writer.C("  if (val.a >= 0.5) color |= 0x8000U;\n");
		break;
	case GE_FORMAT_565:
		writer.C("  uint color = uint(val.r * 31.99) | (uint(val.g * 63.99) << 5u) | (uint(val.b * 31.99) << 11u);\n");
		break;
	default:
		_assert_(false);
		break;
	}

	switch (to) {
	case GE_FORMAT_4444:
		writer.C("  vec4 outColor = vec4(float(color & 0xFU), float((color >> 4u) & 0xFU), float((color >> 8u) & 0xFU), float((color >> 12u) & 0xFU));\n");
		writer.C("  outColor *= 1.0 / 15.0;\n");
		break;
	case GE_FORMAT_5551:
		writer.C("  vec4 outColor = vec4(float(color & 0x1FU), float((color >> 5u) & 0x1FU), float((color >> 10u) & 0x1FU), 0.0);\n");
		writer.C("  outColor.rgb *= 1.0 / 31.0;\n");
		writer.C("  outColor.a = float(color >> 15);\n");
		break;
	case GE_FORMAT_565:
		writer.C("  vec4 outColor = vec4(float(color & 0x1FU), float((color >> 5u) & 0x3FU), float((color >> 11u) & 0x1FU), 1.0);\n");
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

bool GenerateReinterpretVertexShader(char *buffer, const ShaderLanguageDesc &lang) {
	if (!lang.bitwiseOps) {
		return false;
	}
	ShaderWriter writer(buffer, lang, ShaderStage::Vertex, nullptr, 0);

	writer.BeginVSMain(Slice<InputDef>::empty(), Slice<UniformDef>::empty(), varyings);

	writer.C("  float x = -1.0 + float((gl_VertexIndex & 1) << 2);\n");
	writer.C("  float y = -1.0 + float((gl_VertexIndex & 2) << 1);\n");
	writer.C("  v_texcoord = (vec2(x, y) + vec2(1.0, 1.0)) * 0.5;\n");
	writer.F("  y *= %s1.0;\n", lang.viewportYSign);
	writer.C("  gl_Position = vec4(x, y, 0.0, 1.0);\n");

	writer.EndVSMain(varyings);
	return true;
}
