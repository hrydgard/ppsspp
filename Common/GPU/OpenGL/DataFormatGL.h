#pragma once

#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/DataFormat.h"

namespace Draw {

bool Thin3DFormatToGLFormatAndType(DataFormat fmt, GLuint &internalFormat, GLuint &format, GLuint &type, int &alignment);

}
