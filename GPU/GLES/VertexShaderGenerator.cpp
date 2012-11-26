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

// TODO: We should transition from doing the transform in software, as seen in TransformPipeline.cpp,
// into doing the transform in the vertex shader - except for Rectangles, there we really need to do
// the transforms ourselves.

#include <stdio.h>

#include "../ge_constants.h"
#include "../GPUState.h"

#include "VertexShaderGenerator.h"

// SDL 1.2 on Apple does not have support for OpenGL 3 and hence needs
// special treatment in the shader generator.
#ifdef __APPLE__
#define FORCE_OPENGL_2_0
#endif

#undef WRITE

static char buffer[16384];

#define WRITE(x, ...) p+=sprintf(p, x "\n" __VA_ARGS__)

void ComputeVertexShaderID(VertexShaderID *id)
{
	// There's currently only one vertex shader
	// as we do the transform in software.
	memset(id->d, 0, sizeof(id->d));
	id->d[0] = gstate.lmode & 1;
}

void WriteLight(char *p, int l) {
	// TODO
}

char *GenerateVertexShader()
{
	char *p = buffer;
#if defined(USING_GLES2)
	WRITE("precision highp float;");
#elif !defined(FORCE_OPENGL_2_0)
	WRITE("#version 130");
#endif

	int lmode = gstate.lmode & 1;

	WRITE("attribute vec4 a_position;");
	WRITE("attribute vec2 a_texcoord;");
	WRITE("attribute vec4 a_color0;");
	if (lmode)
		WRITE("attribute vec4 a_color1;");

	WRITE("uniform mat4 u_proj;");

	WRITE("varying vec4 v_color0;");
	if (lmode)
		WRITE("varying vec4 v_color1;");
	WRITE("varying vec2 v_texcoord;");

	WRITE("void main() {");
	WRITE("v_color0 = a_color0;");
	if (lmode)
		WRITE("v_color1 = a_color1;");
	WRITE("v_texcoord = a_texcoord;");
	WRITE("gl_Position = u_proj * a_position;");
	WRITE("}");

	return buffer;
}

