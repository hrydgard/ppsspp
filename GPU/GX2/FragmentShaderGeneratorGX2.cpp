// Copyright (c) 2017- PPSSPP Project.

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

#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/ge_constants.h"

#include "GPU/GX2/FragmentShaderGeneratorGX2.h"
#include "GPU/GX2/ShaderManagerGX2.h"

#include <wiiu/gx2/shader_emitter.hpp>
#include <wiiu/gx2/shader_disasm.hpp>
#include <wiiu/gx2/shader_info.h>

#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include <wiiu/os/debug.h>

using namespace GX2Gen;

class FragmentShaderGeneratorGX2 : private GX2PixelShaderEmitter {
public:
	FragmentShaderGeneratorGX2() {}
	bool Supported(const FShaderID &id);
	void Emit(const FShaderID &id, GX2PixelShader *ps);
};

bool FragmentShaderGeneratorGX2::Supported(const FShaderID &id) {
	FShaderID unsupported;
	unsupported.SetBit(FS_BIT_SHADER_DEPAL);
	unsupported.SetBit(FS_BIT_SHADER_TEX_CLAMP);
	unsupported.SetBit(FS_BIT_CLAMP_S);
	unsupported.SetBit(FS_BIT_CLAMP_T);
	unsupported.SetBit(FS_BIT_TEXTURE_AT_OFFSET);
	unsupported.SetBit(FS_BIT_LMODE);
	unsupported.SetBit(FS_BIT_COLOR_TEST);
	unsupported.SetBits(FS_BIT_COLOR_TEST_FUNC, 2, -1);
	unsupported.SetBit(FS_BIT_COLOR_AGAINST_ZERO);
	unsupported.SetBit(FS_BIT_ENABLE_FOG);
	unsupported.SetBit(FS_BIT_DO_TEXTURE_PROJ);
	unsupported.SetBit(FS_BIT_COLOR_DOUBLE);
	//	unsupported.SetBits(FS_BIT_STENCIL_TO_ALPHA, 2, -1);
	//	unsupported.SetBits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4, -1);
	unsupported.SetBits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2, -1);
	unsupported.SetBits(FS_BIT_REPLACE_BLEND, 3, -1);
	unsupported.SetBits(FS_BIT_BLENDEQ, 3, -1);
	unsupported.SetBits(FS_BIT_BLENDFUNC_A, 4, -1);
	unsupported.SetBits(FS_BIT_BLENDFUNC_B, 4, -1);
	unsupported.SetBit(FS_BIT_FLATSHADE);
	unsupported.SetBit(FS_BIT_TEST_DISCARD_TO_ZERO);
	unsupported.SetBit(FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL);

	return !(unsupported.d[0] & id.d[0]) && !(unsupported.d[1] & id.d[1]);
}

void FragmentShaderGeneratorGX2::Emit(const FShaderID &id, GX2PixelShader *ps) {
	GEComparison alphaTestFunc = (GEComparison)id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3);
	//	GEComparison colorTestFunc = (GEComparison)id.Bits(FS_BIT_COLOR_TEST_FUNC, 2);
	GETexFunc texFunc = (GETexFunc)id.Bits(FS_BIT_TEXFUNC, 3);
	//	ReplaceBlendType replaceBlend = (ReplaceBlendType)(id.Bits(FS_BIT_REPLACE_BLEND, 3));
	ReplaceAlphaType stencilToAlpha = (ReplaceAlphaType)(id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2));
	//	GEBlendSrcFactor replaceBlendFuncA = (GEBlendSrcFactor)id.Bits(FS_BIT_BLENDFUNC_A, 4);
	//	GEBlendDstFactor replaceBlendFuncB = (GEBlendDstFactor)id.Bits(FS_BIT_BLENDFUNC_B, 4);
	//	GEBlendMode replaceBlendEq = (GEBlendMode)id.Bits(FS_BIT_BLENDEQ, 3);
	StencilValueType replaceAlphaWithStencilType = (StencilValueType)id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);

	Reg color = allocImportReg(PSInput::COLOR0);

	if (!id.Bit(FS_BIT_CLEARMODE)) {
		if (id.Bit(FS_BIT_DO_TEXTURE)) {
			Reg coords = allocImportReg(PSInput::COORDS);
			Reg sample = coords;
			SAMPLE(sample, coords(x, y, _0_, _0_), 0, 0);
			//	if (id.Bit(FS_BIT_BGRA_TEXTURE))
			//		sample = sample(b, g, r, a);

			switch (texFunc) {
			case GE_TEXFUNC_REPLACE:
				if (id.Bit(FS_BIT_TEXALPHA))
					color = sample;
				else {
					MOV(color(r), sample(r));
					MOV(color(g), sample(g));
					MOV(color(b), sample(b));
					ALU_LAST();
				}
				break;
			case GE_TEXFUNC_DECAL:
				// TODO
				ADD(color(r), color(r), sample(r));
				ADD(color(g), color(g), sample(g));
				ADD(color(b), color(b), sample(b));
				if (id.Bit(FS_BIT_TEXALPHA))
					ADD(color(a), color(a), sample(a));
				ALU_LAST();
				break;
			case GE_TEXFUNC_MODULATE:
				MUL(color(r), color(r), sample(r));
				MUL(color(g), color(g), sample(g));
				MUL(color(b), color(b), sample(b));
				if (id.Bit(FS_BIT_TEXALPHA))
					MUL(color(a), color(a), sample(a));
				ALU_LAST();
				break;
			default:
			case GE_TEXFUNC_ADD:
				ADD(color(r), color(r), sample(r));
				ADD(color(g), color(g), sample(g));
				ADD(color(b), color(b), sample(b));
				if (id.Bit(FS_BIT_TEXALPHA))
					ADD(color(a), color(a), sample(a));
				ALU_LAST();
				break;
			}
		}

		if (id.Bit(FS_BIT_ALPHA_TEST)) {
			if (id.Bit(FS_BIT_ALPHA_AGAINST_ZERO)) {
				if (alphaTestFunc == GE_COMP_NOTEQUAL || alphaTestFunc == GE_COMP_GREATER)
					KILLGT(x, C(0.002f), color(a));
				else if (alphaTestFunc != GE_COMP_NEVER)
					KILLGT(x, color(a), C(0.002f));
				else
					KILLE(x, color(a), color(a));
			} else {
				// TODO
			}
			ALU_LAST();
		}
	}

	SrcChannel replacedAlpha = C(0.0f);
	if (stencilToAlpha != REPLACE_ALPHA_NO) {
		switch (replaceAlphaWithStencilType) {
		case STENCIL_VALUE_UNIFORM: {
			replacedAlpha = KCacheChannel(UB_Bindings::Base, offsetof(UB_VS_FS_Base, stencil), this);
			break;
		}

		case STENCIL_VALUE_ZERO: replacedAlpha = C(0.0f); break;

		case STENCIL_VALUE_ONE:
		case STENCIL_VALUE_INVERT:
			// In invert, we subtract by one, but we want to output one here.
			replacedAlpha = C(1.0f);
			break;

		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_DECR_4:
			// We're adding/subtracting, just by the smallest value in 4-bit.
			replacedAlpha = C(1.0f / 15.0f);
			break;

		case STENCIL_VALUE_INCR_8:
		case STENCIL_VALUE_DECR_8:
			// We're adding/subtracting, just by the smallest value in 8-bit.
			replacedAlpha = C(1.0f / 255.0f);
			break;

		case STENCIL_VALUE_KEEP:
			// Do nothing. We'll mask out the alpha using color mask.
			break;
		}
	}

	switch (stencilToAlpha) {
	case REPLACE_ALPHA_DUALSOURCE: {
		Reg temp = allocReg();
		MOV(temp(r), color(r));
		MOV(temp(g), color(g));
		MOV(temp(b), color(b));
		MOV(temp(a), replacedAlpha);
		ALU_LAST();
		EXP(PIX0, temp);
		EXP(PIX1, color(_0_, _0_, _0_, a));
		break;
	}

	case REPLACE_ALPHA_YES:
		MOV(color(a), replacedAlpha);
		ALU_LAST();
		EXP(PIX0, color);
		break;

	case REPLACE_ALPHA_NO: EXP(PIX0, color); break;

	default:
		ERROR_LOG(G3D, "Bad stencil-to-alpha type, corrupt ID?");
		EXP(PIX0, color);
		break;
	}

	END_OF_PROGRAM(ps);
}

void GenerateFragmentShaderGX2(const FShaderID &id, GX2PixelShader *ps) {
	FragmentShaderGeneratorGX2 fsGen;
	if (fsGen.Supported(id)) {
		fsGen.Emit(id, ps);
#if 0
		char buffer[0x20000];
		printf("\n### GPU Regs ###\n");
		GX2PixelShaderInfo(ps, buffer);
		puts(buffer);

		printf("\n### ASM ###\n%s\n", FragmentShaderDesc(id).c_str());
		DisassembleGX2Shader(ps->program, ps->size, buffer);
		puts(buffer);

		printf("\n### glsl ###\n");
		GenerateVulkanGLSLFragmentShader(id, buffer, 0);
		puts(buffer);
#endif
	} else {
		WARN_LOG(G3D, "unsupported FShaderID: \"%s\"", FragmentShaderDesc(id).c_str());
		*ps = PShaderAllGX2;
	}
}
