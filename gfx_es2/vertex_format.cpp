#include "base/logging.h"
#include "gfx_es2/glsl_program.h"
#include "gfx_es2/vertex_format.h"

static const GLuint formatLookup[16] = {
	GL_FLOAT,
	0,	//GL_HALF_FLOAT_EXT,
	GL_UNSIGNED_SHORT,
	GL_UNSIGNED_BYTE,
	0, //GL_UNSIGNED_INT_10_10_10_2,
	0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

void SetVertexFormat(const GLSLProgram *program, uint32_t vertexFormat) {
	// First special case our favorites
	if (vertexFormat == (POS_FLOAT | NRM_FLOAT | UV0_FLOAT)) {
		const int vertexSize = 3*4 + 3*4 + 2*4;
		glUniform1i(program->sampler0, 0);
		glEnableVertexAttribArray(program->a_position);
		glEnableVertexAttribArray(program->a_normal);
		glEnableVertexAttribArray(program->a_texcoord0);
		glVertexAttribPointer(program->a_position,  3, GL_FLOAT, GL_FALSE, vertexSize, (void *)0);
		glVertexAttribPointer(program->a_normal,    3, GL_FLOAT, GL_FALSE, vertexSize, (void *)12);
		glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, vertexSize, (void *)24);
		return;
	}

	// Then have generic code here.


	int vertexSize = 0;

	FLOG("TODO: Write generic code.");

	if (vertexFormat & UV0_MASK) {
		glUniform1i(program->sampler0, 0);
	}

	glEnableVertexAttribArray(program->a_position);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, vertexSize, (void *)0);
	if (vertexFormat & NRM_MASK) {
		glEnableVertexAttribArray(program->a_normal);
		glVertexAttribPointer(program->a_normal, 3, GL_FLOAT, GL_FALSE, vertexSize, (void *)12);
	}
	if (vertexFormat & UV0_MASK) {
		glEnableVertexAttribArray(program->a_texcoord0);
		glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, vertexSize, (void *)24);
	}
	if (vertexFormat & UV1_MASK) {
		glEnableVertexAttribArray(program->a_texcoord1);
		glVertexAttribPointer(program->a_texcoord1, 2, GL_FLOAT, GL_FALSE, vertexSize, (void *)24);
	}
	if (vertexFormat & RGBA_MASK) {
		glEnableVertexAttribArray(program->a_color);
		glVertexAttribPointer(program->a_color, 4, GL_FLOAT, GL_FALSE, vertexSize, (void *)28);
	}
}

// TODO: Save state so that we can get rid of this.
void UnsetVertexFormat(const GLSLProgram *program, uint32 vertexFormat) {
	glDisableVertexAttribArray(program->a_position);
	if (vertexFormat & NRM_MASK)
		glDisableVertexAttribArray(program->a_normal);
	if (vertexFormat & UV0_MASK)
		glDisableVertexAttribArray(program->a_texcoord0);
	if (vertexFormat & UV1_MASK)
		glDisableVertexAttribArray(program->a_texcoord1);
	if (vertexFormat & RGBA_MASK)
		glDisableVertexAttribArray(program->a_color);
}
