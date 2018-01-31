#pragma once

#include "gfx/gl_common.h"
#include "thin3d/DataFormat.h"

namespace Draw {

bool Thin3DFormatToFormatAndType(DataFormat fmt, GLuint &internalFormat, GLuint &format, GLuint &type, int &alignment);

}
