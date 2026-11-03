#pragma once

#include "Common/GPU/ShaderWriter.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/Draw2D.h"

Draw2DPipelineInfo GenerateReinterpretFragmentShader(ShaderWriter &writer, GEBufferFormat from, GEBufferFormat to);
