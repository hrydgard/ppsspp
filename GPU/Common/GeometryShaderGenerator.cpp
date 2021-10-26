// Copyright (c) 2012- PPSSPP Project.

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
#include <cstdlib>
#include <locale.h>

#include "Common/StringUtils.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/GPU/thin3d.h"
#include "Core/Config.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderUniforms.h"
#include "GPU/Common/GeometryShaderGenerator.h"

#undef WRITE

#define WRITE(p, ...) p.F(__VA_ARGS__)

bool GenerateGeometryShader(const GShaderID &id, char *buffer, const ShaderLanguageDesc &compat, const Draw::Bugs bugs, std::string *errorString) {

	std::vector<const char*> gl_exts;
	if (ShaderLanguageIsOpenGL(compat.shaderLanguage)) {
		if (gl_extensions.EXT_gpu_shader4) {
			gl_exts.push_back("#extension GL_EXT_gpu_shader4 : enable");
		}
	}

	ShaderWriter p(buffer, compat, ShaderStage::Geometry, gl_exts.data(), gl_exts.size());
	p.C("layout(triangles) in;\n");
	p.C("layout(triangle_strip, max_vertices = 3) out;\n");

	std::vector<VaryingDef> varyings;

	varyings.push_back(VaryingDef{ "vec4", "v_color0", "COLOR0", 1, "highp" });
	if (id.Bit(GS_BIT_LMODE)) {
		varyings.push_back(VaryingDef{ "vec4", "v_color1", "COLOR1", 2, "highp" });
	}
	if (id.Bit(GS_BIT_ENABLE_FOG)) {
		varyings.push_back(VaryingDef{ "float", "v_fogdepth", "TEXCOORD0", 3, "highp" });
	}
	if (id.Bit(GS_BIT_DO_TEXTURE)) {
		varyings.push_back(VaryingDef{ "vec3", "v_texcoord", "TEXCOORD0", 0, "highp" });
	}

	p.BeginGSMain(varyings);

	p.C("  for (int i = 0; i < gl_in.length(); i++) {\n");
	p.C("    gl_Position = gl_in[i].gl_Position;\n");  // copy attributes
	p.C("    EmitVertex();\n");
	p.C("  }\n");

	p.EndGSMain(varyings);

	return true;
}
