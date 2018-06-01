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

#include "gfx_es2/gpu_features.h"

#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

#define WRITE p+=sprintf

// TODO: Add a compute shader path. Complete waste of time to set up a graphics state.

// Uses integer instructions available since OpenGL 3.0. Suitable for ES 3.0 as well.
void GenerateDepalShader300(char *buffer, GEBufferFormat pixelFormat, ShaderLanguage language) {
	char *p = buffer;
	if (language == HLSL_D3D11) {
		WRITE(p, "SamplerState texSamp : register(s0);\n");
		WRITE(p, "Texture2D<float4> tex : register(t0);\n");
		WRITE(p, "Texture2D<float4> pal : register(t3);\n");
	} else if (language == GLSL_VULKAN) {
		WRITE(p, "#version 450\n");
		WRITE(p, "#extension GL_ARB_separate_shader_objects : enable\n");
		WRITE(p, "#extension GL_ARB_shading_language_420pack : enable\n");
		WRITE(p, "layout(set = 0, binding = 0) uniform sampler2D tex;\n");
		WRITE(p, "layout(set = 0, binding = 1) uniform sampler2D pal;\n");
		WRITE(p, "layout(location = 0) in vec2 v_texcoord0;\n");
		WRITE(p, "layout(location = 0) out vec4 fragColor0;\n");
	} else {
		if (gl_extensions.IsGLES) {
			WRITE(p, "#version 300 es\n");
			WRITE(p, "precision mediump float;\n");
			WRITE(p, "precision highp int;\n");
		} else {
			WRITE(p, "#version 330\n");
		}
		WRITE(p, "in vec2 v_texcoord0;\n");
		WRITE(p, "out vec4 fragColor0;\n");
		WRITE(p, "uniform sampler2D tex;\n");
		WRITE(p, "uniform sampler2D pal;\n");
	}

	if (language == HLSL_D3D11) {
		WRITE(p, "float4 main(in float2 v_texcoord0 : TEXCOORD0) : SV_Target {\n");
		WRITE(p, "  float4 color = tex.Sample(texSamp, v_texcoord0);\n");
	} else {
		// TODO: Add support for integer textures. Though it hardly matters.
		WRITE(p, "void main() {\n");
		WRITE(p, "  vec4 color = texture(tex, v_texcoord0);\n");
	}

	int mask = gstate.getClutIndexMask();
	int shift = gstate.getClutIndexShift();
	int offset = gstate.getClutIndexStartPos();
	GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	// Unfortunately sampling turned our texture into floating point. To avoid this, might be able
	// to declare them as isampler2D objects, but these require integer textures, which needs more work.
	// Anyhow, we simply work around this by converting back to integer. Hopefully there will be no loss of precision.
	// Use the mask to skip reading some components.
	int shiftedMask = mask << shift;
	switch (pixelFormat) {
	case GE_FORMAT_8888:
		if (shiftedMask & 0xFF) WRITE(p, "  int r = int(color.r * 255.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0xFF00) WRITE(p, "  int g = int(color.g * 255.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0xFF0000) WRITE(p, "  int b = int(color.b * 255.99);\n"); else WRITE(p, "  int b = 0;\n");
		if (shiftedMask & 0xFF000000) WRITE(p, "  int a = int(color.a * 255.99);\n"); else WRITE(p, "  int a = 0;\n");
		WRITE(p, "  int index = (a << 24) | (b << 16) | (g << 8) | (r);\n");
		break;
	case GE_FORMAT_4444:
		if (shiftedMask & 0xF) WRITE(p, "  int r = int(color.r * 15.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0xF0) WRITE(p, "  int g = int(color.g * 15.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0xF00) WRITE(p, "  int b = int(color.b * 15.99);\n"); else WRITE(p, "  int b = 0;\n");
		if (shiftedMask & 0xF000) WRITE(p, "  int a = int(color.a * 15.99);\n"); else WRITE(p, "  int a = 0;\n");
		WRITE(p, "  int index = (a << 12) | (b << 8) | (g << 4) | (r);\n");
		break;
	case GE_FORMAT_565:
		if (shiftedMask & 0x1F) WRITE(p, "  int r = int(color.r * 31.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0x7E0) WRITE(p, "  int g = int(color.g * 63.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0xF800) WRITE(p, "  int b = int(color.b * 31.99);\n"); else WRITE(p, "  int b = 0;\n");
		WRITE(p, "  int index = (b << 11) | (g << 5) | (r);\n");
		break;
	case GE_FORMAT_5551:
		if (shiftedMask & 0x1F) WRITE(p, "  int r = int(color.r * 31.99);\n"); else WRITE(p, "  int r = 0;\n");
		if (shiftedMask & 0x3E0) WRITE(p, "  int g = int(color.g * 31.99);\n"); else WRITE(p, "  int g = 0;\n");
		if (shiftedMask & 0x7C00) WRITE(p, "  int b = int(color.b * 31.99);\n"); else WRITE(p, "  int b = 0;\n");
		if (shiftedMask & 0x8000) WRITE(p, "  int a = int(color.a);\n"); else WRITE(p, "  int a = 0;\n");
		WRITE(p, "  int index = (a << 15) | (b << 10) | (g << 5) | (r);\n");
		break;
	default:
		break;
	}

	float texturePixels = 256;
	if (clutFormat != GE_CMODE_32BIT_ABGR8888)
		texturePixels = 512;

	if (shift) {
		WRITE(p, "  index = (int(uint(index) >> %i) & 0x%02x)", shift, mask);
	} else {
		WRITE(p, "  index = (index & 0x%02x)", mask);
	}
	if (offset) {
		WRITE(p, " | %i;\n", offset);  // '|' matches what we have in gstate.h
	} else {
		WRITE(p, ";\n");
	}

	if (language == HLSL_D3D11) {
		WRITE(p, "  return pal.Load(int3(index, 0, 0)).bgra;\n");
	} else {
		WRITE(p, "  fragColor0 = texture(pal, vec2((float(index) + 0.5) * (1.0 / %f), 0.0));\n", texturePixels);
	}
	WRITE(p, "}\n");
}

// FP only, to suit GL(ES) 2.0
void GenerateDepalShaderFloat(char *buffer, GEBufferFormat pixelFormat, ShaderLanguage lang) {
	char *p = buffer;

	const char *modFunc = lang == HLSL_DX9 ? "fmod" : "mod";

	char lookupMethod[128] = "index.r";
	char offset[128] = "";

	const GEPaletteFormat clutFormat = gstate.getClutPaletteFormat();
	const u32 clutBase = gstate.getClutIndexStartPos();

	const int shift = gstate.getClutIndexShift();
	const int mask = gstate.getClutIndexMask();

	float index_multiplier = 1.0f;
	// pixelformat is the format of the texture we are sampling.
	bool formatOK = true;
	switch (pixelFormat) {
	case GE_FORMAT_8888:
		if ((mask & (mask + 1)) == 0) {
			// If the value has all bits contiguous (bitmask check above), we can mod by it + 1.
			const char *rgba = "rrrrrrrrggggggggbbbbbbbbaaaaaaaa";
			const u8 rgba_shift = shift & 7;
			if (rgba_shift == 0 && mask == 0xFF) {
				sprintf(lookupMethod, "index.%c", rgba[shift]);
			} else {
				sprintf(lookupMethod, "%s(index.%c * %f, %d.0)", modFunc, rgba[shift], 255.99f / (1 << rgba_shift), mask + 1);
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
				sprintf(lookupMethod, "%s(index.%c * %f, %d.0)", modFunc, rgba[shift], 15.99f / (1 << rgba_shift), mask + 1);
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
				sprintf(lookupMethod, "%s(index.%c * %f, %d.0)", modFunc, rgba[shift], ((float)multipliers[shift] + 0.99f) / (1 << rgba_shift), mask + 1);
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
				sprintf(lookupMethod, "%s(index.%c * %f, %d.0)", modFunc, rgba[shift], 31.99f / (1 << rgba_shift), mask + 1);
				index_multiplier = 1.0f / 256.0f;
				formatOK = mask <= 31 - (1 << rgba_shift);
			}
		} else {
			formatOK = false;
		}
		break;
	default:
		break;
	}

	float texturePixels = 256.f;
	if (clutFormat != GE_CMODE_32BIT_ABGR8888) {
		texturePixels = 512.f;
		index_multiplier *= 0.5f;
	}

	// Adjust index_multiplier, similar to the use of 15.99 instead of 16 in the ES 3 path.
	// index_multiplier -= 0.01f / texturePixels;

	if (!formatOK) {
		ERROR_LOG_REPORT_ONCE(depal, G3D, "%i depal unsupported: shift=%i mask=%02x offset=%d", pixelFormat, shift, mask, clutBase);
	}

	// Offset by half a texel (plus clutBase) to turn NEAREST filtering into FLOOR.
	// Technically, the clutBase should be |'d, not added, but that's hard with floats.
	float texel_offset = ((float)clutBase + 0.5f) / texturePixels;
	sprintf(offset, " + %f", texel_offset);

	if (lang == GLSL_140) {
		if (gl_extensions.IsGLES) {
			WRITE(p, "#version 100\n");
			WRITE(p, "precision mediump float;\n");
		} else {
			WRITE(p, "#version 110\n");
		}
		WRITE(p, "varying vec2 v_texcoord0;\n");
		WRITE(p, "uniform sampler2D tex;\n");
		WRITE(p, "uniform sampler2D pal;\n");
		WRITE(p, "void main() {\n");
		WRITE(p, "  vec4 index = texture2D(tex, v_texcoord0);\n");
		WRITE(p, "  float coord = (%s * %f)%s;\n", lookupMethod, index_multiplier, offset);
		WRITE(p, "  gl_FragColor = texture2D(pal, vec2(coord, 0.0));\n");
		WRITE(p, "}\n");
	} else if (lang == HLSL_DX9) {
		WRITE(p, "sampler tex: register(s0);\n");
		WRITE(p, "sampler pal: register(s1);\n");
		WRITE(p, "float4 main(float2 v_texcoord0 : TEXCOORD0) : COLOR0 {\n");
		WRITE(p, "  float4 index = tex2D(tex, v_texcoord0);\n");
		WRITE(p, "  float coord = (%s * %f)%s;\n", lookupMethod, index_multiplier, offset);
		WRITE(p, "  return tex2D(pal, float2(coord, 0.0)).bgra;\n");
		WRITE(p, "}\n");
	}
}

void GenerateDepalShader(char *buffer, GEBufferFormat pixelFormat, ShaderLanguage language) {
	switch (language) {
	case GLSL_140:
		GenerateDepalShaderFloat(buffer, pixelFormat, language);
		break;
	case GLSL_300:
	case GLSL_VULKAN:
	case HLSL_D3D11:
		GenerateDepalShader300(buffer, pixelFormat, language);
		break;
	case HLSL_DX9:
		GenerateDepalShaderFloat(buffer, pixelFormat, language);
		break;
	}
}

uint32_t DepalShaderCacheCommon::GenerateShaderID(uint32_t clutMode, GEBufferFormat pixelFormat) const {
	return (clutMode & 0xFFFFFF) | (pixelFormat << 24);
}

uint32_t DepalShaderCacheCommon::GetClutID(GEPaletteFormat clutFormat, uint32_t clutHash) const {
	// Simplistic.
	return clutHash ^ (uint32_t)clutFormat;
}

#undef WRITE
