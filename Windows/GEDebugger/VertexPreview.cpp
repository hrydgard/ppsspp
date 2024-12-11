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

#include "Common/Math/lin/matrix4x4.h"
#include "Common/GPU/OpenGL/GLSLProgram.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "GPU/GPUCommon.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Debugger/State.h"
#include "GPU/GPUState.h"
#include "Common/Log.h"
#include "Common/MemoryUtil.h"

static const char preview_fs[] =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"void main() {\n"
	"	gl_FragColor = vec4(1.0, 0.0, 0.0, 0.6);\n"
	"}\n";

static const char preview_vs[] =
	"#version 120\n"
	"attribute vec4 a_position;\n"
	"uniform mat4 u_viewproj;\n"
	"void main() {\n"
	"  gl_Position = u_viewproj * a_position;\n"
	"  gl_Position.z = 1.0;\n"
	"}\n";

static GLSLProgram *previewProgram = nullptr;
static GLSLProgram *texPreviewProgram = nullptr;

static GLuint previewVao = 0;
static GLuint texPreviewVao = 0;
static GLuint vbuf = 0;
static GLuint ibuf = 0;

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
	if (prog == nullptr) {
		prog = glsl_create_source(preview_vs, preview_fs);
	}

	glsl_bind(prog);
}

u32 CGEDebugger::PrimPreviewOp() {
	DisplayList list;
	if (gpuDebug != nullptr && gpuDebug->GetCurrentDisplayList(list)) {
		const u32 op = Memory::Read_U32(list.pc);
		const u32 cmd = op >> 24;
		if (cmd == GE_CMD_PRIM || cmd == GE_CMD_BEZIER || cmd == GE_CMD_SPLINE) {
			return op;
		}
	}
	return 0;
}

bool GetPrimPreview(u32 op, int which, GEPrimitiveType &prim, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices, int &count) {
	u32 prim_type = GE_PRIM_INVALID;
	int count_u = 0;
	int count_v = 0;

	const u32 cmd = op >> 24;
	if (cmd == GE_CMD_PRIM) {
		prim_type = (op >> 16) & 0x7;
		count = op & 0xFFFF;
	} else {
		const GEPrimitiveType primLookup[] = { GE_PRIM_TRIANGLES, GE_PRIM_LINES, GE_PRIM_POINTS, GE_PRIM_POINTS };
		if (gstate.getPatchPrimitiveType() < ARRAY_SIZE(primLookup))
			prim_type = primLookup[gstate.getPatchPrimitiveType()];
		count_u = (op & 0x00FF) >> 0;
		count_v = (op & 0xFF00) >> 8;
		count = count_u * count_v;
	}

	if (prim_type >= 7) {
		ERROR_LOG(Log::G3D, "Unsupported prim type: %x", op);
		return false;
	}
	if (!gpuDebug) {
		ERROR_LOG(Log::G3D, "Invalid debugging environment, shutting down?");
		return false;
	}
	if (count == 0 || which == 0) {
		return false;
	}

	prim = static_cast<GEPrimitiveType>(prim_type);

	if (!gpuDebug->GetCurrentSimpleVertices(count, vertices, indices)) {
		ERROR_LOG(Log::G3D, "Vertex preview not yet supported");
		return false;
	}

	if (cmd != GE_CMD_PRIM) {
		static std::vector<SimpleVertex> generatedVerts;
		static std::vector<u16> generatedInds;

		static std::vector<SimpleVertex> simpleVerts;
		simpleVerts.resize(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			// For now, let's just copy back so we can use TessellateBezierPatch/TessellateSplinePatch...
			simpleVerts[i].uv[0] = vertices[i].u;
			simpleVerts[i].uv[1] = vertices[i].v;
			simpleVerts[i].pos = Vec3Packedf(vertices[i].x, vertices[i].y, vertices[i].z);
		}

		if (cmd == GE_CMD_BEZIER) {
			ExpandBezier(count, op, simpleVerts, indices, generatedVerts, generatedInds);
		} else if (cmd == GE_CMD_SPLINE) {
			ExpandSpline(count, op, simpleVerts, indices, generatedVerts, generatedInds);
		}

		vertices.resize(generatedVerts.size());
		for (size_t i = 0; i < vertices.size(); ++i) {
			vertices[i].u = generatedVerts[i].uv[0];
			vertices[i].v = generatedVerts[i].uv[1];
			vertices[i].x = generatedVerts[i].pos.x;
			vertices[i].y = generatedVerts[i].pos.y;
			vertices[i].z = generatedVerts[i].pos.z;
		}
		indices = generatedInds;
	}

	if (prim == GE_PRIM_RECTANGLES) {
		ExpandRectangles(vertices, indices, count, gpuDebug->GetGState().isModeThrough());
	}

	// TODO: Probably there's a better way and place to do this.
	u16 minIndex = 0;
	u16 maxIndex = count - 1;
	if (!indices.empty()) {
		_dbg_assert_(count <= indices.size());
		minIndex = 0xFFFF;
		maxIndex = 0;
		for (int i = 0; i < count; ++i) {
			if (minIndex > indices[i]) {
				minIndex = indices[i];
			}
			if (maxIndex < indices[i]) {
				maxIndex = indices[i];
			}
		}
	}

	auto wrapCoord = [](float &coord) {
		if (coord < 0.0f) {
			coord += ceilf(-coord);
		}
		if (coord > 1.0f) {
			coord -= floorf(coord);
		}
	};

	const float invTexWidth = 1.0f / gpuDebug->GetGState().getTextureWidth(0);
	const float invTexHeight = 1.0f / gpuDebug->GetGState().getTextureHeight(0);
	bool clampS = gpuDebug->GetGState().isTexCoordClampedS();
	bool clampT = gpuDebug->GetGState().isTexCoordClampedT();
	for (u16 i = minIndex; i <= maxIndex; ++i) {
		vertices[i].u *= invTexWidth;
		vertices[i].v *= invTexHeight;
		if (!clampS)
			wrapCoord(vertices[i].u);
		if (!clampT)
			wrapCoord(vertices[i].v);
	}

	return true;
}

void CGEDebugger::UpdatePrimPreview(u32 op, int which) {
	which &= previewsEnabled_;

	static std::vector<GPUDebugVertex> vertices;
	static std::vector<u16> indices;

	int count = 0;
	GEPrimitiveType prim;
	if (!GetPrimPreview(op, which, prim, vertices, indices, count)) {
		return;
	}

	float fw, fh;
	float x, y;

	const float invRealTexWidth = 1.0f / gstate_c.curTextureWidth;
	const float invRealTexHeight = 1.0f / gstate_c.curTextureHeight;

	// Preview positions on the framebuffer
	if (which & 1) {
		primaryWindow->Begin();
		primaryWindow->GetContentSize(x, y, fw, fh);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquation(GL_FUNC_ADD);
		glBindTexture(GL_TEXTURE_2D, 0);
		// The surface is upside down, so vertical offsets are negated.
		glViewport((GLint)x, (GLint)-(y + fh - primaryWindow->Height()), (GLsizei)fw, (GLsizei)fh);
		glScissor((GLint)x, (GLint)-(y + fh - primaryWindow->Height()), (GLsizei)fw, (GLsizei)fh);
		BindPreviewProgram(previewProgram);

		if (previewVao == 0 && gl_extensions.ARB_vertex_array_object) {
			glGenVertexArrays(1, &previewVao);
			glBindVertexArray(previewVao);
			glEnableVertexAttribArray(previewProgram->a_position);

			if (ibuf == 0)
				glGenBuffers(1, &ibuf);
			if (vbuf == 0)
				glGenBuffers(1, &vbuf);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf);
			glBindBuffer(GL_ARRAY_BUFFER, vbuf);

			glVertexAttribPointer(previewProgram->a_position, 3, GL_FLOAT, GL_FALSE, sizeof(GPUDebugVertex), (void *)(2 * sizeof(float)));
		}

		if (vbuf != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, vbuf);
			glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GPUDebugVertex), vertices.data(), GL_STREAM_DRAW);
		}

		if (ibuf != 0 && !indices.empty()) {
			glBindVertexArray(previewVao);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(u16), indices.data(), GL_STREAM_DRAW);
		}

		float scale[] = {
			480.0f / (float)PSP_CoreParameter().renderWidth,
			272.0f / (float)PSP_CoreParameter().renderHeight,
		};

		Lin::Matrix4x4 ortho;
		ortho.setOrtho(-(float)gstate_c.curRTOffsetX, (primaryWindow->TexWidth() - (int)gstate_c.curRTOffsetX) * scale[0], primaryWindow->TexHeight() * scale[1], 0, -1, 1);
		glUniformMatrix4fv(previewProgram->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
		if (previewVao != 0) {
			glBindVertexArray(previewVao);
		} else {
			glEnableVertexAttribArray(previewProgram->a_position);
			glVertexAttribPointer(previewProgram->a_position, 3, GL_FLOAT, GL_FALSE, sizeof(GPUDebugVertex), (float *)vertices.data() + 2);
		}

		if (indices.empty()) {
			glDrawArrays(glprim[prim], 0, count);
		} else {
			glDrawElements(glprim[prim], count, GL_UNSIGNED_SHORT, previewVao != 0 ? 0 : indices.data());
		}

		if (previewVao == 0) {
			glDisableVertexAttribArray(previewProgram->a_position);
		}

		primaryWindow->End();
	}

	// Preview UVs on the texture
	if (which & 2) {
		secondWindow->Begin();
		secondWindow->GetContentSize(x, y, fw, fh);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquation(GL_FUNC_ADD);
		glBindTexture(GL_TEXTURE_2D, 0);
		// The surface is upside down, so vertical offsets are flipped.
		glViewport((GLint)x, (GLint)-(y + fh - secondWindow->Height()), (GLsizei)fw, (GLsizei)fh);
		glScissor((GLint)x, (GLint)-(y + fh - secondWindow->Height()), (GLsizei)fw, (GLsizei)fh);
		BindPreviewProgram(texPreviewProgram);

		if (texPreviewVao == 0 && vbuf != 0 && ibuf != 0 && gl_extensions.ARB_vertex_array_object) {
			glGenVertexArrays(1, &texPreviewVao);
			glBindVertexArray(texPreviewVao);
			glEnableVertexAttribArray(texPreviewProgram->a_position);

			if (ibuf == 0)
				glGenBuffers(1, &ibuf);
			if (vbuf == 0)
				glGenBuffers(1, &vbuf);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf);
			glBindBuffer(GL_ARRAY_BUFFER, vbuf);

			glVertexAttribPointer(texPreviewProgram->a_position, 2, GL_FLOAT, GL_FALSE, sizeof(GPUDebugVertex), 0);
		}

		// TODO: For some reason we have to re-upload the data?
		if (vbuf != 0) {
			glBindBuffer(GL_ARRAY_BUFFER, vbuf);
			glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GPUDebugVertex), vertices.data(), GL_STREAM_DRAW);
		}

		if (ibuf != 0 && !indices.empty()) {
			glBindVertexArray(texPreviewVao);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(u16), indices.data(), GL_STREAM_DRAW);
		}

		Lin::Matrix4x4 ortho;
		ortho.setOrtho(0.0f - (float)gstate_c.curTextureXOffset * invRealTexWidth, 1.0f - (float)gstate_c.curTextureXOffset * invRealTexWidth, 1.0f - (float)gstate_c.curTextureYOffset * invRealTexHeight, 0.0f - (float)gstate_c.curTextureYOffset * invRealTexHeight, -1.0f, 1.0f);
		glUniformMatrix4fv(texPreviewProgram->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
		if (texPreviewVao != 0) {
			glBindVertexArray(texPreviewVao);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf);
			glBindBuffer(GL_ARRAY_BUFFER, vbuf);
			glEnableVertexAttribArray(texPreviewProgram->a_position);
			glVertexAttribPointer(texPreviewProgram->a_position, 2, GL_FLOAT, GL_FALSE, sizeof(GPUDebugVertex), 0);
		} else {
			glEnableVertexAttribArray(texPreviewProgram->a_position);
			glVertexAttribPointer(texPreviewProgram->a_position, 2, GL_FLOAT, GL_FALSE, sizeof(GPUDebugVertex), (float *)vertices.data());
		}

		if (indices.empty()) {
			glDrawArrays(glprim[prim], 0, count);
		} else {
			glDrawElements(glprim[prim], count, GL_UNSIGNED_SHORT, texPreviewVao != 0 ? 0 : indices.data());
		}

		if (texPreviewVao == 0) {
			glDisableVertexAttribArray(texPreviewProgram->a_position);
		}

		secondWindow->End();
	}
}

void CGEDebugger::CleanupPrimPreview() {
	if (previewProgram) {
		glsl_destroy(previewProgram);
	}
	if (texPreviewProgram) {
		glsl_destroy(texPreviewProgram);
	}
}

void CGEDebugger::HandleRedraw(int which) {
	if (updating_) {
		return;
	}

	u32 op = PrimPreviewOp();
	if (op && !showClut_) {
		UpdatePrimPreview(op, which);
	}
}
