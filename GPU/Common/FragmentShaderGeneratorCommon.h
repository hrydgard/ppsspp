#pragma once

#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"

struct ShaderCompat {
	const char *varying;
	const char *fragColor0;
	const char *fragColor1;
	const char *texture;
	const char *texelFetch;
	const char *lastFragData;
	bool glslES30;
	bool bitwiseOps;
	ShaderLanguage shaderLanguage;
};

char *WriteReplaceBlend(char *p, const FShaderID &id, const ShaderCompat &compat);
