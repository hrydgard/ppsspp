#include <cstdarg>

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Core/System.h"
#include "GPU/Common/ReinterpretFramebuffer.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const SamplerDef samplers[1] = {
	{ 0, "tex", SamplerFlags::ARRAY_ON_VULKAN }
};

// Requires full size integer math. It would be possible to make a floating point-only version with lots of
// modulo and stuff, might do it one day.
Draw2DPipelineInfo GenerateReinterpretFragmentShader(ShaderWriter &writer, GEBufferFormat from, GEBufferFormat to) {
	writer.HighPrecisionFloat();
	writer.DeclareSamplers(samplers);

	if (writer.Lang().bitwiseOps) {
		switch (from) {
		case GE_FORMAT_4444:
			writer.C("uint packColor(vec4 val) {\n");
			writer.C("  return uint(val.r * 15.99) | (uint(val.g * 15.99) << 0x4u) | (uint(val.b * 15.99) << 0x8u) | (uint(val.a * 15.99) << 0xCu);\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_5551:
			writer.C("uint packColor(vec4 val) {\n");
			writer.C("  uint color = uint(val.r * 31.99) | (uint(val.g * 31.99) << 0x5u) | (uint(val.b * 31.99) << 0xAu);\n");
			writer.C("  if (val.a >= 0.5) color |= 0x8000U;\n");
			writer.C("  return color;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_565:
			writer.C("uint packColor(vec4 val) {\n");
			writer.C("  return uint(val.r * 31.99) | (uint(val.g * 63.99) << 0x5u) | (uint(val.b * 31.99) << 0xBu);\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_8888:
			writer.C("uint packColor(vec2 val) {\n");
			writer.C("  return uint(val.r * 255.99) | (uint(val.g * 255.99) << 8u);\n");
			writer.C("}\n");
			break;
		default:
			_assert_(false);
			break;
		}
	} else {
		// Floating point can comfortably represent integers up to 16 million, we only need 65536 since these textures are 16-bit.
		switch (from) {
		case GE_FORMAT_4444:
			writer.C("float packColor(vec4 val) {\n");
			writer.C("  return (floor(val.r * 15.99) + floor(val.g * 15.99) * 16.0) + (floor(val.b * 15.99) * 256.0 + floor(val.a * 15.99) * 4096.0);\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_5551:
			writer.C("float packColor(vec4 val) {\n");
			writer.C("  float color = floor(val.r * 31.99) + floor(val.g * 31.99) * 32.0 + floor(val.b * 31.99) * 1024.0;\n");
			writer.C("  if (val.a >= 0.5) color += 32768.0;\n");
			writer.C("  return color;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_565:
			writer.C("float packColor(vec4 val) {\n");
			writer.C("  return floor(val.r * 31.99) + floor(val.g * 63.99) * 32.0 + floor(val.b * 31.99) * 2048.0;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_8888:
			writer.C("float packColor(vec2 val) {\n");
			writer.C("  return floor(val.r * 255.99) + floor(val.g * 255.99) * 256.0;\n");
			writer.C("}\n");
			break;
		default:
			_assert_(false);
			break;
		}
	}

	if (writer.Lang().bitwiseOps) {
		switch (to) {
		case GE_FORMAT_4444:
			writer.C("vec4 unpackColor(uint color) {\n");
			writer.C("  vec4 outColor = vec4(float(color & 0xFu), float((color >> 0x4u) & 0xFu), float((color >> 0x8u) & 0xFu), float((color >> 0xCu) & 0xFu));\n");
			writer.C("  outColor *= 1.0 / 15.0;\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_5551:
			writer.C("vec4 unpackColor(uint color) {\n");
			writer.C("  vec4 outColor = vec4(float(color & 0x1Fu), float((color >> 0x5u) & 0x1Fu), float((color >> 0xAu) & 0x1Fu), 0.0);\n");
			writer.C("  outColor.rgb *= 1.0 / 31.0;\n");
			writer.C("  outColor.a = float(color >> 0xFu);\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_565:
			writer.C("vec4 unpackColor(uint color) {\n");
			writer.C("  vec4 outColor = vec4(float(color & 0x1Fu), float((color >> 0x5u) & 0x3Fu), float((color >> 0xBu) & 0x1Fu), 1.0);\n");
			writer.C("  outColor.rb *= 1.0 / 31.0;\n");
			writer.C("  outColor.g *= 1.0 / 63.0;\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_8888:
			writer.C("vec4 unpackColor(uint colorLeft, uint colorRight) {\n");
			writer.C("  vec4 outColor = vec4(float(colorLeft & 0xFFu),  float((colorLeft >> 0x8u)  & 0xFFu),\n");
			writer.C("                       float(colorRight & 0xFFu), float((colorRight >> 0x8u) & 0xFFu));\n");
			writer.C("  outColor *= 1.0 / 255.0;\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		default:
			_assert_(false);
			break;
		}
	} else {
		switch (to) {
		case GE_FORMAT_4444:
			writer.C("vec4 unpackColor(float color) {\n");
			writer.C("  vec4 outColor = vec4(mod(floor(color), 16.0), mod(floor(color / 16.0), 16.0),");
			writer.C("                       mod(floor(color / 256.0), 16.0), mod(floor(color / 4096.0), 16.0)); \n");
			writer.C("  outColor *= 1.0 / 15.0;\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_5551:
			writer.C("vec4 unpackColor(float color) {\n");
			writer.C("  vec4 outColor = vec4(mod(floor(color), 32.0), mod(floor(color / 32.0), 32.0), mod(floor(color / 1024.0), 32.0), 0.0);\n");
			writer.C("  outColor.rgb *= 1.0 / 31.0;\n");
			writer.C("  outColor.a = floor(color / 32768.0);\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_565:
			writer.C("vec4 unpackColor(float color) {\n");
			writer.C("  vec4 outColor = vec4(mod(floor(color), 32.0), mod(floor(color / 32.0), 64.0), mod(floor(color / 2048.0), 32.0), 0.0);\n");
			writer.C("  outColor.rb *= 1.0 / 31.0;\n");
			writer.C("  outColor.g *= 1.0 / 63.0;\n");
			writer.C("  outColor.a = 1.0;\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		case GE_FORMAT_8888:
			writer.C("vec4 unpackColor(float colorLeft, float colorRight) {\n");
			writer.C("  vec4 outColor = vec4(mod(floor(colorLeft), 256.0), mod(floor(colorLeft / 256.0), 256.0),\n");
			writer.C("                       mod(floor(colorRight), 256.0), mod(floor(colorRight / 256.0), 256.0));\n");
			writer.C("  outColor *= 1.0 / 255.0;\n");
			writer.C("  return outColor;\n");
			writer.C("}\n");
			break;
		default:
			_assert_(false);
			break;
		}
	}

	writer.BeginFSMain(g_draw2Duniforms, varyings);

	if (IsBufferFormat16Bit(from) && IsBufferFormat16Bit(to)) {
		writer.C("  vec4 val = ").SampleTexture2D("tex", "v_texcoord.xy").C(";\n");
		writer.C("  vec4 outColor = unpackColor(packColor(val));\n");
	} else if (IsBufferFormat16Bit(from) && !IsBufferFormat16Bit(to)) {
		// 16-to-32-bit (two pixels, draw size is halved)

		writer.C("  vec4 valLeft = ").SampleTexture2D("tex", "v_texcoord.xy + vec2(-0.25 / texSize.x, 0.0)").C(";\n");
		writer.C("  vec4 valRight = ").SampleTexture2D("tex", "v_texcoord.xy + vec2(0.25 / texSize.x, 0.0)").C(";\n");
		writer.C("  vec4 outColor = unpackColor(packColor(valLeft), packColor(valRight));\n");

		_assert_("not yet implemented");
	} else if (!IsBufferFormat16Bit(from) && IsBufferFormat16Bit(to)) {
		// 32-to-16-bit (half of the pixel, draw size is doubled).

		writer.C("  vec4 val = ").SampleTexture2D("tex", "v_texcoord.xy").C(";\n");
		writer.C("  float u = mod(floor(v_texcoord.x * texSize.x * 2.0), 2.0);\n");
		writer.C("  vec4 outColor = unpackColor(u == 0.0 ? packColor(val.rg) : packColor(val.ba));\n");
	}

	writer.EndFSMain("outColor");

	return Draw2DPipelineInfo{
		"reinterpret",
		RASTER_COLOR,
		RASTER_COLOR,
	};
}

