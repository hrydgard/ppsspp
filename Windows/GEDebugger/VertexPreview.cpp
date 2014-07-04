// Copyright (c) 2013- PPSSPP Project.

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

#include "math/lin/matrix4x4.h"
#include "gfx_es2/glsl_program.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"

static const char preview_fs[] =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"void main() {\n"
	"	gl_FragColor = vec4(1.0, 0.0, 0.0, 0.6);\n"
	"}\n";

static const char preview_vs[] =
#ifndef USING_GLES2
	"#version 120\n"
#endif
	"attribute vec4 a_position;\n"
	"uniform mat4 u_viewproj;\n"
	"void main() {\n"
	"  gl_Position = u_viewproj * a_position;\n"
	"  gl_Position.z = 1.0f;\n"
	"}\n";

static GLSLProgram *previewProgram = NULL;
static GLSLProgram *texPreviewProgram = NULL;

static const GLuint glprim[8] = {
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	// This is for RECTANGLES (see ExpandRectangles().)
	GL_TRIANGLES,
};

static void BindPreviewProgram(GLSLProgram *&prog) {
	if (prog == NULL) {
		prog = glsl_create_source(preview_vs, preview_fs);
	}

	glsl_bind(prog);
}

static void SwapUVs(GPUDebugVertex &a, GPUDebugVertex &b) {
	float tempu = a.u;
	float tempv = a.v;
	a.u = b.u;
	a.v = b.v;
	b.u = tempu;
	b.v = tempv;
}

static void RotateUVThrough(GPUDebugVertex v[4]) {
	float x1 = v[2].x;
	float x2 = v[0].x;
	float y1 = v[2].y;
	float y2 = v[0].y;

	if ((x1 < x2 && y1 > y2) || (x1 > x2 && y1 < y2))
		SwapUVs(v[1], v[3]);
}

static void ExpandRectangles(std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices, int &count, bool throughMode) {
	static std::vector<GPUDebugVertex> newVerts;
	static std::vector<u16> newInds;

	bool useInds = true;
	size_t numInds = indices.size();
	if (indices.empty()) {
		useInds = false;
		numInds = count;
	}

	// Will need 4 coords and 6 points per rectangle (currently 2 each.)
	newVerts.resize(numInds * 2);
	newInds.resize(numInds * 3);

	u16 v = 0;
	GPUDebugVertex *vert = &newVerts[0];
	u16 *ind = &newInds[0];
	for (size_t i = 0, end = numInds; i < end; i += 2) {
		const auto &orig_tl = useInds ? vertices[indices[i]] : vertices[i];
		const auto &orig_br = useInds ? vertices[indices[i + 1]] : vertices[i + 1];

		vert[0] = orig_br;

		// Top right.
		vert[1] = orig_br;
		vert[1].y = orig_tl.y;
		vert[1].v = orig_tl.v;

		vert[2] = orig_tl;

		// Bottom left.
		vert[3] = orig_br;
		vert[3].x = orig_tl.x;
		vert[3].u = orig_tl.u;

		// That's the four corners. Now process UV rotation.
		if (throughMode)
			RotateUVThrough(vert);

		// Build the two 3 point triangles from our 4 coordinates.
		*ind++ = v + 0;
		*ind++ = v + 1;
		*ind++ = v + 2;
		*ind++ = v + 3;
		*ind++ = v + 0;
		*ind++ = v + 2;

		vert += 4;
		v += 4;
	}

	std::swap(vertices, newVerts);
	std::swap(indices, newInds);
	count *= 3;
}

void CGEDebugger::UpdatePrimPreview(u32 op) {
	const u32 prim_type = (op >> 16) & 0xFF;
	int count = op & 0xFFFF;
	if (prim_type >= 7) {
		ERROR_LOG(COMMON, "Unsupported prim type: %x", op);
		return;
	}
	if (!gpuDebug) {
		ERROR_LOG(COMMON, "Invalid debugging environment, shutting down?");
		return;
	}
	if (count == 0) {
		return;
	}

	const GEPrimitiveType prim = static_cast<GEPrimitiveType>(prim_type);
	static std::vector<GPUDebugVertex> vertices;
	static std::vector<u16> indices;

	if (!gpuDebug->GetCurrentSimpleVertices(count, vertices, indices)) {
		ERROR_LOG(COMMON, "Vertex preview not yet supported");
		return;
	}

	if (prim == GE_PRIM_RECTANGLES) {
		ExpandRectangles(vertices, indices, count, gpuDebug->GetGState().isModeThrough());
	}

	float fw, fh;
	float x, y;

	frameWindow->Begin();
	frameWindow->GetContentSize(x, y, fw, fh);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendEquation(GL_FUNC_ADD);
	glBindTexture(GL_TEXTURE_2D, 0);
	glViewport((GLint)x, (GLint)y, (GLsizei)fw, (GLsizei)fh);
	glScissor((GLint)x, (GLint)y, (GLsizei)fw, (GLsizei)fh);
	BindPreviewProgram(previewProgram);

	float scale[] = {
		480.0f / (float)PSP_CoreParameter().renderWidth,
		272.0f / (float)PSP_CoreParameter().renderHeight,
	};

	Matrix4x4 ortho;
	ortho.setOrtho(-(float)gstate_c.cutRTOffsetX, (frameWindow->TexWidth() - (int)gstate_c.cutRTOffsetX) * scale[0], frameWindow->TexHeight() * scale[1], 0, -1, 1);
	glUniformMatrix4fv(previewProgram->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glEnableVertexAttribArray(previewProgram->a_position);
	glVertexAttribPointer(previewProgram->a_position, 3, GL_FLOAT, GL_FALSE, sizeof(GPUDebugVertex), (float *)vertices.data() + 2);

	if (indices.empty()) {
		glDrawArrays(glprim[prim], 0, count);
	} else {
		glDrawElements(glprim[prim], count, GL_UNSIGNED_SHORT, indices.data());
	}

	frameWindow->End();

	texWindow->Begin();
	texWindow->GetContentSize(x, y, fw, fh);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendEquation(GL_FUNC_ADD);
	glBindTexture(GL_TEXTURE_2D, 0);
	glViewport((GLint)x, (GLint)y, (GLsizei)fw, (GLsizei)fh);
	glScissor((GLint)x, (GLint)y, (GLsizei)fw, (GLsizei)fh);
	BindPreviewProgram(texPreviewProgram);

	// TODO: Probably there's a better way and place to do this.
	if (indices.empty()) {
		for (int i = 0; i < count; ++i) {
			vertices[i].u -= floor(vertices[i].u);
			vertices[i].v -= floor(vertices[i].v);
		}
	} else {
		for (int i = 0; i < count; ++i) {
			vertices[indices[i]].u -= floor(vertices[indices[i]].u);
			vertices[indices[i]].v -= floor(vertices[indices[i]].v);
		}
	}

	ortho.setOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
	glUniformMatrix4fv(texPreviewProgram->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glEnableVertexAttribArray(texPreviewProgram->a_position);
	glVertexAttribPointer(texPreviewProgram->a_position, 2, GL_FLOAT, GL_FALSE, sizeof(GPUDebugVertex), (float *)vertices.data());
	if (indices.empty()) {
		glDrawArrays(glprim[prim], 0, count);
	} else {
		glDrawElements(glprim[prim], count, GL_UNSIGNED_SHORT, indices.data());
	}

	texWindow->End();
}

void CGEDebugger::CleanupPrimPreview() {
	glsl_destroy(previewProgram);
	glsl_destroy(texPreviewProgram);
}
