// Copyright (c) 2014- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <cstdio>

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"

#include "GPU/Common/ShaderCommon.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

#define WRITE p+=sprintf

static const InputDef vsInputs[2] = {
	{ "vec2", "a_position", Draw::SEM_POSITION, },
	{ "vec2", "a_texcoord0", Draw::SEM_TEXCOORD0, },
};

// TODO: Deduplicate with DepalettizeCommon.cpp
static const SamplerDef samplers[2] = {
	{ "tex" },
	{ "pal" },
};

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

// Uses integer instructions available since OpenGL 3.0, ES 3.0 (and 2.0 with extensions), and of course Vulkan and D3D11.
void GenerateDepalShader300(ShaderWriter &writer, const DepalConfig &config, const ShaderLanguageDesc &lang) {
	const int shift = config.shift;
	const int mask = config.mask;

	if (config.bufferFormat == GE_FORMAT_DEPTH16) {
		DepthScaleFactors factors = GetDepthScaleFactors();
		writer.ConstFloat("z_scale", factors.scale);
		writer.ConstFloat("z_offset", factors.offset);
	}

	// Sampling turns our texture into floating point. To avoid this, might be able
	// to declare them as isampler2D objects, but these require integer textures, which needs more work.
	// Anyhow, we simply work around this by converting back to integer, which is fine.
	// Use the mask to skip reading some components.

	// TODO: Since we actually have higher precision color data here, we might want to apply a dithering pattern here
	// in the 5551, 565 and 4444 modes. This would benefit Test Drive which renders at 16-bit on the real hardware
	// and dithers immediately, while we render at higher color depth and thus don't dither resulting in banding
	// when we sample it at low color depth like this.

	// An alternative would be to have a special mode where we keep some extra precision here and sample the CLUT linearly - works for ramps such
	// as those that Test Drive uses for its color remapping. But would need game specific flagging.

	writer.C("  vec4 color = ").SampleTexture2D("tex", "v_texcoord").C(";\n");

	int shiftedMask = mask << shift;
	switch (config.bufferFormat) {
	case GE_FORMAT_8888:
		if (shiftedMask & 0xFF) writer.C("  int r = int(color.r * 255.99);\n"); else writer.C("  int r = 0;\n");
		if (shiftedMask & 0xFF00) writer.C("  int g = int(color.g * 255.99);\n"); else writer.C("  int g = 0;\n");
		if (shiftedMask & 0xFF0000) writer.C("  int b = int(color.b * 255.99);\n"); else writer.C("  int b = 0;\n");
		if (shiftedMask & 0xFF000000) writer.C("  int a = int(color.a * 255.99);\n"); else writer.C("  int a = 0;\n");
		writer.C("  int index = (a << 24) | (b << 16) | (g << 8) | (r);\n");
		break;
	case GE_FORMAT_4444:
		if (shiftedMask & 0xF) writer.C("  int r = int(color.r * 15.99);\n"); else writer.C("  int r = 0;\n");
		if (shiftedMask & 0xF0) writer.C("  int g = int(color.g * 15.99);\n"); else writer.C("  int g = 0;\n");
		if (shiftedMask & 0xF00) writer.C("  int b = int(color.b * 15.99);\n"); else writer.C("  int b = 0;\n");
		if (shiftedMask & 0xF000) writer.C("  int a = int(color.a * 15.99);\n"); else writer.C("  int a = 0;\n");
		writer.C("  int index = (a << 12) | (b << 8) | (g << 4) | (r);\n");
		break;
	case GE_FORMAT_565:
		if (shiftedMask & 0x1F) writer.C("  int r = int(color.r * 31.99);\n"); else writer.C("  int r = 0;\n");
		if (shiftedMask & 0x7E0) writer.C("  int g = int(color.g * 63.99);\n"); else writer.C("  int g = 0;\n");
		if (shiftedMask & 0xF800) writer.C("  int b = int(color.b * 31.99);\n"); else writer.C("  int b = 0;\n");
		writer.C("  int index = (b << 11) | (g << 5) | (r);\n");
		break;
	case GE_FORMAT_5551:
		if (shiftedMask & 0x1F) writer.C("  int r = int(color.r * 31.99);\n"); else writer.C("  int r = 0;\n");
		if (shiftedMask & 0x3E0) writer.C("  int g = int(color.g * 31.99);\n"); else writer.C("  int g = 0;\n");
		if (shiftedMask & 0x7C00) writer.C("  int b = int(color.b * 31.99);\n"); else writer.C("  int b = 0;\n");
		if (shiftedMask & 0x8000) writer.C("  int a = int(color.a);\n"); else writer.C("  int a = 0;\n");
		writer.C("  int index = (a << 15) | (b << 10) | (g << 5) | (r);\n");
		break;
	case GE_FORMAT_DEPTH16:
		// Remap depth buffer.
		writer.C("  float depth = (color.x - z_offset) * z_scale;\n");

		if (config.bufferFormat == GE_FORMAT_DEPTH16 && config.textureFormat == GE_TFMT_5650) {
			// Convert depth to 565, without going through a CLUT.
			writer.C("  int idepth = int(clamp(depth, 0.0, 65535.0));\n");
			writer.C("  float r = (idepth & 31) / 31.0f;\n");
			writer.C("  float g = ((idepth >> 5) & 63) / 63.0f;\n");
			writer.C("  float b = ((idepth >> 11) & 31) / 31.0f;\n");
			writer.C("  vec4 outColor = vec4(r, g, b, 1.0);\n");
			return;
		}

		writer.C("  int index = int(clamp(depth, 0.0, 65535.0));\n");
		break;
	default:
		break;
	}

	float texturePixels = 256.0f;
	if (config.clutFormat != GE_CMODE_32BIT_ABGR8888) {
		texturePixels = 512.0f;
	}

	if (shift) {
		writer.F("  index = (int(uint(index) >> uint(%d)) & 0x%02x)", shift, mask);
	} else {
		writer.F("  index = (index & 0x%02x)", mask);
	}
	if (config.startPos) {
		writer.F(" | %d;\n", config.startPos);  // '|' matches what we have in gstate.h
	} else {
		writer.F(";\n");
	}

	writer.F("  vec2 uv = vec2((float(index) + 0.5) * %f, 0.0);\n", 1.0f / texturePixels);
	writer.C("  vec4 outColor = ").SampleTexture2D("pal", "uv").C(";\n");
}

// FP only, to suit GL(ES) 2.0 and DX9
void GenerateDepalShaderFloat(ShaderWriter &writer, const DepalConfig &config, const ShaderLanguageDesc &lang) {
	char lookupMethod[128] = "index.r";

	const int shift = config.shift;
	const int mask = config.mask;

	if (config.bufferFormat == GE_FORMAT_DEPTH16) {
		DepthScaleFactors factors = GetDepthScaleFactors();
		writer.ConstFloat("z_scale", factors.scale);
		writer.ConstFloat("z_offset", factors.offset);
	}

	float index_multiplier = 1.0f;
	// pixelformat is the format of the texture we are sampling.
	bool formatOK = true;
	switch (config.bufferFormat) {
	case GE_FORMAT_8888:
		if ((mask & (mask + 1)) == 0) {
			// If the value has all bits contiguous (bitmask check above), we can mod by it + 1.
			const char *rgba = "rrrrrrrrggggggggbbbbbbbbaaaaaaaa";
			const u8 rgba_shift = shift & 7;
			if (rgba_shift == 0 && mask == 0xFF) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
			} else {
				sprintf(lookupMethod, "fmod(index.%c * %f, %d.0)", rgba[shift], 255.99f / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				// Format was OK if there weren't bits from another component.
				formatOK = mask <= 255 - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	case GE_FORMAT_4444:
		if ((mask & (mask + 1)) == 0 && shift < 16) {
			const char *rgba = "rrrrggggbbbbaaaa";
			const u8 rgba_shift = shift & 3;
			if (rgba_shift == 0 && mask == 0xF) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = 15.0f / 256.0f;
			} else {
				// Let's divide and mod to get the right bits.  A common case is shift=0, mask=01.
				sprintf(lookupMethod, "fmod(index.%c * %f, %d.0)", rgba[shift], 15.99f / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				formatOK = mask <= 15 - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	case GE_FORMAT_565:
		if ((mask & (mask + 1)) == 0 && shift < 16) {
			const u8 shifts[16] = { 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4 };
			const int multipliers[16] = { 31, 31, 31, 31, 31, 63, 63, 63, 63, 63, 63, 31, 31, 31, 31, 31 };
			const char *rgba = "rrrrrggggggbbbbb";
			const u8 rgba_shift = shifts[shift];
			if (rgba_shift == 0 && mask == multipliers[shift]) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = multipliers[shift] / 256.0f;
			} else {
				// We just need to divide the right component by the right value, and then mod against the mask.
				// A common case is shift=1, mask=0f.
				sprintf(lookupMethod, "fmod(index.%c * %f, %d.0)", rgba[shift], ((float)multipliers[shift] + 0.99f) / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				formatOK = mask <= multipliers[shift] - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	case GE_FORMAT_5551:
		if ((mask & (mask + 1)) == 0 && shift < 16) {
			const char *rgba = "rrrrrgggggbbbbba";
			const u8 rgba_shift = shift % 5;
			if (rgba_shift == 0 && mask == 0x1F) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = 31.0f / 256.0f;
			} else if (shift == 15 && mask == 1) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
				index_multiplier = 1.0f / 256.0f;
			} else {
				// A isn't possible here.
				sprintf(lookupMethod, "fmod(index.%c * %f, %d.0)", rgba[shift], 31.99f / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				formatOK = mask <= 31 - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	case GE_FORMAT_DEPTH16:
	{
		// TODO: I think we can handle most scenarios here, but texturing from depth buffers requires an extension on ES 2.0 anyway.
		if (shift < 16) {
			index_multiplier = 1.0f / (float)(1 << shift);
			truncate_cpy(lookupMethod, "((index.x - z_offset) * z_scale)");

			if ((mask & (mask + 1)) != 0) {
				// But we'll try with the above anyway.
				formatOK = false;
			}
		} else {
			formatOK = false;
		}
		break;
	}
	default:
		break;
	}

	float texturePixels = 256.f;
	if (config.clutFormat != GE_CMODE_32BIT_ABGR8888) {
		texturePixels = 512.f;
		index_multiplier *= 0.5f;
	}

	// Adjust index_multiplier, similar to the use of 15.99 instead of 16 in the ES 3 path.
	// index_multiplier -= 0.01f / texturePixels;

	if (!formatOK) {
		ERROR_LOG_REPORT_ONCE(depal, G3D, "%s depal unsupported: shift=%d mask=%02x offset=%d", GeBufferFormatToString(config.bufferFormat), shift, mask, config.startPos);
	}

	// Offset by half a texel (plus clutBase) to turn NEAREST filtering into FLOOR.
	// Technically, the clutBase should be |'d, not added, but that's hard with floats.
	float texel_offset = ((float)config.startPos + 0.5f) / texturePixels;
	char offset[128] = "";
	sprintf(offset, " + %f", texel_offset);

	writer.C("  vec4 index = ").SampleTexture2D("tex", "v_texcoord").C(";\n");
	writer.F("  float coord = (%s * %f)%s;\n", lookupMethod, index_multiplier, offset);
	writer.C("  vec4 outColor = ").SampleTexture2D("pal", "vec2(coord, 0.0)").C(";\n");
}

void GenerateDepalFs(char *buffer, const DepalConfig &config, const ShaderLanguageDesc &lang) {
	ShaderWriter writer(buffer, lang, ShaderStage::Fragment);
	writer.DeclareSamplers(samplers);
	writer.HighPrecisionFloat();
	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings, FSFLAG_NONE);
	switch (lang.shaderLanguage) {
	case HLSL_D3D9:
	case GLSL_1xx:
		GenerateDepalShaderFloat(writer, config, lang);
		break;
	case GLSL_VULKAN:
	case GLSL_3xx:
	case HLSL_D3D11:
		GenerateDepalShader300(writer, config, lang);
		break;
	default:
		_assert_msg_(false, "Depal shader language not supported: %d", (int)lang.shaderLanguage);
	}
	writer.EndFSMain("outColor", FSFLAG_NONE);
}

void GenerateDepalVs(char *buffer, const ShaderLanguageDesc &lang) {
	ShaderWriter writer(buffer, lang, ShaderStage::Vertex, nullptr, 0);
	writer.BeginVSMain(vsInputs, Slice<UniformDef>::empty(), varyings);
	writer.C("  v_texcoord = a_texcoord0;\n");
	writer.C("  gl_Position = vec4(a_position, 0.0, 1.0);\n");
	writer.EndVSMain(varyings);
}

#undef WRITE
