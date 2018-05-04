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
#include "GPU/ge_constants.h"

#include "GPU/GX2/VertexShaderGeneratorGX2.h"
#include "GPU/GX2/ShaderManagerGX2.h"

#include <wiiu/gx2/shader_emitter.hpp>
#include <wiiu/gx2/shader_disasm.hpp>
#include <wiiu/gx2/shader_info.h>

#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"
#include <wiiu/os/debug.h>

using namespace GX2Gen;

class VertexShaderGeneratorGX2 : private GX2VertexShaderEmitter {
public:
	VertexShaderGeneratorGX2() {}
	bool Supported(const VShaderID &id);
	void Emit(const VShaderID &id, GX2VertexShader *vs);
};

bool VertexShaderGeneratorGX2::Supported(const VShaderID &id) {
	VShaderID unsupported;
	unsupported.SetBit(VS_BIT_LMODE);
	unsupported.SetBit(VS_BIT_ENABLE_FOG);
	unsupported.SetBit(VS_BIT_DO_TEXTURE_TRANSFORM);
	unsupported.SetBit(VS_BIT_USE_HW_TRANSFORM);
	unsupported.SetBit(VS_BIT_HAS_NORMAL);
	unsupported.SetBit(VS_BIT_NORM_REVERSE);
	unsupported.SetBit(VS_BIT_HAS_TEXCOORD);
	unsupported.SetBit(VS_BIT_HAS_COLOR_TESS);
	unsupported.SetBit(VS_BIT_HAS_TEXCOORD_TESS);
	unsupported.SetBit(VS_BIT_NORM_REVERSE_TESS);
	unsupported.SetBit(VS_BIT_HAS_NORMAL_TESS);
	unsupported.SetBits(VS_BIT_UVGEN_MODE, 2, -1);
	unsupported.SetBits(VS_BIT_UVPROJ_MODE, 2, -1);
	unsupported.SetBits(VS_BIT_LS0, 2, -1);
	unsupported.SetBits(VS_BIT_LS1, 2, -1);
	unsupported.SetBits(VS_BIT_BONES, 3, -1);
	unsupported.SetBit(VS_BIT_ENABLE_BONES);
	unsupported.SetBits(VS_BIT_LIGHT0_COMP, 2, -1);
	unsupported.SetBits(VS_BIT_LIGHT0_TYPE, 2, -1);
	unsupported.SetBits(VS_BIT_LIGHT1_COMP, 2, -1);
	unsupported.SetBits(VS_BIT_LIGHT1_TYPE, 2, -1);
	unsupported.SetBits(VS_BIT_LIGHT2_COMP, 2, -1);
	unsupported.SetBits(VS_BIT_LIGHT2_TYPE, 2, -1);
	unsupported.SetBits(VS_BIT_LIGHT3_COMP, 2, -1);
	unsupported.SetBits(VS_BIT_LIGHT3_TYPE, 2, -1);
	unsupported.SetBits(VS_BIT_MATERIAL_UPDATE, 3, -1);
	unsupported.SetBit(VS_BIT_SPLINE);
	unsupported.SetBit(VS_BIT_LIGHT0_ENABLE);
	unsupported.SetBit(VS_BIT_LIGHT1_ENABLE);
	unsupported.SetBit(VS_BIT_LIGHT2_ENABLE);
	unsupported.SetBit(VS_BIT_LIGHT3_ENABLE);
	unsupported.SetBit(VS_BIT_LIGHTING_ENABLE);
	unsupported.SetBits(VS_BIT_WEIGHT_FMTSCALE, 2, -1);
	unsupported.SetBit(VS_BIT_FLATSHADE);
	unsupported.SetBit(VS_BIT_BEZIER);

	return !(unsupported.d[0] & id.d[0]) && !(unsupported.d[1] & id.d[1]);
}

void VertexShaderGeneratorGX2::Emit(const VShaderID &id, GX2VertexShader *vs) {
	Reg pos = allocImportReg(VSInput::POSITION);

	GX2Emitter::KCacheRegs proj = KCacheRegs(UB_Bindings::Base, offsetof(UB_VS_FS_Base, proj), this);
	if (id.Bit(VS_BIT_IS_THROUGH))
		proj = KCacheRegs(UB_Bindings::Base, offsetof(UB_VS_FS_Base, proj_through), this);

	MUL(___(x), pos(x), proj[0](x));
	MUL(___(y), pos(x), proj[0](y));
	MUL(___(z), pos(x), proj[0](z));
	MUL(___(w), pos(x), proj[0](w));
	ALU_LAST();
	MULADD(___(x), pos(y), proj[1](x), PV(x));
	MULADD(___(y), pos(y), proj[1](y), PV(y));
	MULADD(___(z), pos(y), proj[1](z), PV(z));
	MULADD(___(w), pos(y), proj[1](w), PV(w));
	ALU_LAST();
	MULADD(___(x), pos(z), proj[2](x), PV(x));
	MULADD(___(y), pos(z), proj[2](y), PV(y));
	MULADD(___(z), pos(z), proj[2](z), PV(z));
	MULADD(___(w), pos(z), proj[2](w), PV(w));
	ALU_LAST();
	ADD(pos(x), proj[3](x), PV(x));
	ADD(pos(y), proj[3](y), PV(y));
	ADD(pos(z), proj[3](z), PV(z));
	ADD(pos(w), proj[3](w), PV(w));
	ALU_LAST();

	EXP_POS(pos);

	if (id.Bit(VS_BIT_DO_TEXTURE))
		EXP_PARAM(PSInput::COORDS, allocImportReg(VSInput::COORDS)(x, y, _1_, __), NO_BARRIER);

	if (id.Bit(VS_BIT_HAS_COLOR))
		EXP_PARAM(PSInput::COLOR0, allocImportReg(VSInput::COLOR0), NO_BARRIER);

	END_OF_PROGRAM(vs);
}
void GenerateVertexShaderGX2(const VShaderID &id, GX2VertexShader *vs) {
	VertexShaderGeneratorGX2 vsGen;
	if (vsGen.Supported(id)) {
		vsGen.Emit(id, vs);
#if 0
		char buffer[0x20000];
		printf("\n### GPU Regs ###\n");
		GX2VertexShaderInfo(vs, buffer);
		puts(buffer);

		printf("\n### ASM ###\n%s\n", VertexShaderDesc(id).c_str());
		DisassembleGX2Shader(vs->program, vs->size, buffer);
		puts(buffer);

		printf("\n### glsl ###\n");
		GenerateVulkanGLSLVertexShader(id, buffer);
		puts(buffer);
#endif
	} else {
		WARN_LOG(G3D, "unsupported VShaderID: \"%s\"", VertexShaderDesc(id).c_str());
		*vs = id.Bit(VS_BIT_USE_HW_TRANSFORM) ? id.Bit(VS_BIT_ENABLE_BONES) ? VShaderHWSkinGX2 : VShaderHWNoSkinGX2
														  : VShaderSWGX2;
	}
}
