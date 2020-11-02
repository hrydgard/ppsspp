#include <cstdarg>

#include "GPU/Common/ColorReinterpret.h"
#include "GPU/Common/ShaderWriter.h"

// TODO: We could have an option to preserve any extra color precision. But gonna start without it.
// Requires full size integer math.
bool GenerateReinterpretFragmentShader(char *buffer, GEBufferFormat from, GEBufferFormat to, const ShaderLanguageDesc &lang) {
	if (!lang.bitwiseOps) {
		return false;
	}
	ShaderWriter writer(buffer, lang, ShaderStage::Fragment);

	switch (from) {
	case GE_FORMAT_4444:
		writer.W("  uint color = uint(in.r * 15.99) | (uint(in.g * 15.99) << 4) | (uint(in.b * 15.99) << 8) | (uint(in.a * 15.99) << 12);\n");
		break;
	case GE_FORMAT_5551:
		writer.W("  uint color = uint(in.r * 31.99) | (uint(in.g * 31.99) << 5) | (uint(in.b * 31.99) << 10);\n");
		writer.W("  if (in.a > 128.0) color |= 0x8000;\n");
		break;
	case GE_FORMAT_565:
		writer.W("  uint color = uint(in.r * 31.99) | (uint(in.g * 63.99) << 5) | (uint(in.b * 31.99) << 11);\n");
		break;
	default: _assert_(false);
	}

	switch (to) {
	case GE_FORMAT_4444:
		writer.W("  vec4 output = vec4(float(color & 0xF), float((color >> 4) & 0xF), float((color >> 8) & 0xF), float((color >> 12) & 0xF));\n");
		writer.W("  output *= 1.0 / 15.0;\n");
		break;
	case GE_FORMAT_5551:
		writer.W("  vec4 output = vec4(float(color & 0x1F), float((color >> 5) & 0x1F), float((color >> 10) & 0x1F), 0.0);\n");
		writer.W("  output.rgb *= 1.0 / 31.0;\n");
		writer.W("  output.a = float(color >> 15);\n");
		break;
	case GE_FORMAT_565:
		writer.W("  vec4 output = vec4(float(color & 0x1F), float((color >> 5) & 0x3F), float((color >> 11) & 0x1F), 1.0);\n");
		writer.W("  output.rb *= 1.0 / 31.0;\n");
		writer.W("  output.g *= 1.0 / 63.0;\n");
		break;
	default: _assert_(false);
	}

	writer.W("}");

	return true;
}
