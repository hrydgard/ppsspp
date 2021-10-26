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

	ShaderWriter p(buffer, compat, ShaderStage::Vertex, gl_exts.data(), gl_exts.size());
	p.C(R"(

	layout(triangles) in;
	layout(triangle_strip, max_vertices = 3) out;

	)");

	std::vector<VaryingDef> varyings;

	

	/*
	void main()
	{
		for (int i = 0; i < gl_in.length(); i++)
		{
			// copy attributes
			gl_Position = gl_in[i].gl_Position;
			VertexOut.normal = VertexIn[i].normal;
			VertexOut.texCoord = VertexIn[i].texCoord;

			// done with the vertex
			EmitVertex();
		}
	}
	*/
	return false;
}
