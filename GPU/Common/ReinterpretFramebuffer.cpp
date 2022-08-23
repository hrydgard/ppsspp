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
	{ "tex" }
};

// Requires full size integer math. It would be possible to make a floating point-only version with lots of
// modulo and stuff, might do it one day.
Draw2DPipelineInfo GenerateReinterpretFragmentShader(ShaderWriter &writer, GEBufferFormat from, GEBufferFormat to) {
	writer.HighPrecisionFloat();

	writer.DeclareSamplers(samplers);

	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings, FSFLAG_NONE);

	writer.C("  vec4 val = ").SampleTexture2D("tex", "v_texcoord.xy").C(";\n");

	if (writer.Lang().bitwiseOps) {
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
	} else {
		// Floating point can comfortably represent integers up to 16 million, we only need 65536 since these textures are 16-bit.
		switch (from) {
		case GE_FORMAT_4444:
			writer.C("  float color = (floor(val.r * 15.99) + floor(val.g * 15.99) * 16.0) + (floor(val.b * 15.99) * 256.0 + floor(val.a * 15.99) * 4096.0);\n");
			break;
		case GE_FORMAT_5551:
			writer.C("  float color = floor(val.r * 31.99) + floor(val.g * 31.99) * 32.0 + floor(val.b * 31.99) * 1024.0;\n");
			writer.C("  if (val.a >= 0.5) color += 32768.0;\n");
			break;
		case GE_FORMAT_565:
			writer.C("  float color = floor(val.r * 31.99) + floor(val.g * 63.99) * 32.0 + floor(val.b * 31.99) * 2048.0;\n");
			break;
		default:
			_assert_(false);
			break;
		}

		switch (to) {
		case GE_FORMAT_4444:
			writer.C("  vec4 outColor = vec4(mod(floor(color), 16.0), mod(floor(color / 16.0), 16.0),");
			writer.C("                       mod(floor(color / 256.0), 16.0), mod(floor(color / 4096.0), 16.0)); \n");
			writer.C("  outColor *= 1.0 / 15.0;\n");
			break;
		case GE_FORMAT_5551:
			writer.C("  vec4 outColor = vec4(mod(floor(color), 32.0), mod(floor(color / 32.0), 32.0), mod(floor(color / 1024.0), 32.0), 0.0);\n");
			writer.C("  outColor.rgb *= 1.0 / 31.0;\n");
			writer.C("  outColor.a = floor(color / 32768.0);\n");
			break;
		case GE_FORMAT_565:
			writer.C("  vec4 outColor = vec4(mod(floor(color), 32.0), mod(floor(color / 32.0), 64.0), mod(floor(color / 2048.0), 32.0), 0.0);\n");
			writer.C("  outColor.rb *= 1.0 / 31.0;\n");
			writer.C("  outColor.g *= 1.0 / 63.0;\n");
			writer.C("  outColor.a = 1.0;\n");
			break;
		default:
			_assert_(false);
			break;
		}
	}

	writer.EndFSMain("outColor", FSFLAG_NONE);

	return Draw2DPipelineInfo{
		RASTER_COLOR,
		RASTER_COLOR,
	};
}

