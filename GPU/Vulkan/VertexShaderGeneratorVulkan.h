#pragma once

#include "GPU/Common/ShaderId.h"

bool GenerateVertexShaderVulkanGLSL(const VShaderID &id, char *buffer, std::string *errorString);
