#pragma once

#include "Common/GPU/ShaderWriter.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/Draw2D.h"

enum class ReinterpretOutput {
	COLOR,
	// The stencil from the reinterpreted alpha (the PSP's stencil is the alpha bits). The bit to write is in
	// stencilValue (as in the stencil upload shader), and 0 writes everywhere, to clear.
	STENCIL_BITS,
	// The whole stencil in one draw, through GL_ARB_shader_stencil_export.
	STENCIL_EXPORT,
};

// The extension the writer needs for STENCIL_EXPORT.
extern const char *const g_stencilExportExtension;

Draw2DPipelineInfo GenerateReinterpretFragmentShader(ShaderWriter &writer, GEBufferFormat from, GEBufferFormat to, ReinterpretOutput output = ReinterpretOutput::COLOR, bool discardNeedsDepthWrite = false);
