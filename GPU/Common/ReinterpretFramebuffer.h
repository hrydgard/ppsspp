#pragma once

#include "Common/Log.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUCommon.h"
#include "Common/GPU/ShaderWriter.h"

bool GenerateReinterpretFragmentShader(char *buffer, GEBufferFormat from, GEBufferFormat to, const ShaderLanguageDesc &lang);
