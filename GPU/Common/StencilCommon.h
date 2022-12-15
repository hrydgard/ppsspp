#pragma once

#include "Common/CommonTypes.h"

#include "Common/GPU/thin3d.h"

// Exposed for automated tests
void GenerateStencilFs(char *buffer, const ShaderLanguageDesc &lang, const Draw::Bugs &bugs, bool useExport);
void GenerateStencilVs(char *buffer, const ShaderLanguageDesc &lang);
