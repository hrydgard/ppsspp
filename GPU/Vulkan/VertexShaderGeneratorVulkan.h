#pragma once

#include "GPU/Common/ShaderId.h"

bool GenerateVulkanGLSLVertexShader(const ShaderID &id, char *buffer, bool *usesLighting);