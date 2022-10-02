#pragma once

#include "GPU/Common/ShaderId.h"

bool GenerateGeometryShader(const GShaderID &id, char *buffer, const ShaderLanguageDesc &compat, const Draw::Bugs bugs, std::string *errorString);
