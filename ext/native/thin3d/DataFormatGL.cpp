#include "thin3d/DataFormatGL.h"
#include "base/logging.h"
#include "Common/Log.h"

namespace Draw {

// TODO: Also output storage format (GL_RGBA8 etc) for modern GL usage.
bool Thin3DFormatToFormatAndType(DataFormat fmt, GLuint &internalFormat, GLuint &format, GLuint &type, int &alignment) {
	alignment = 4;
	switch (fmt) {
	case DataFormat::R8G8B8A8_UNORM:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
		break;

	case DataFormat::D32F:
		internalFormat = GL_DEPTH_COMPONENT;
		format = GL_DEPTH_COMPONENT;
		type = GL_FLOAT;
		break;

#ifndef USING_GLES2
	case DataFormat::S8:
		internalFormat = GL_STENCIL_INDEX;
		format = GL_STENCIL_INDEX;
		type = GL_UNSIGNED_BYTE;
		alignment = 1;
		break;
#endif

	case DataFormat::R8G8B8_UNORM:
		internalFormat = GL_RGB;
		format = GL_RGB;
		type = GL_UNSIGNED_BYTE;
		alignment = 1;
		break;

	case DataFormat::B4G4R4A4_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_4_4_4_4;
		alignment = 2;
		break;

	case DataFormat::B5G6R5_UNORM_PACK16:
		internalFormat = GL_RGB;
		format = GL_RGB;
		type = GL_UNSIGNED_SHORT_5_6_5;
		alignment = 2;
		break;

	case DataFormat::B5G5R5A1_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_5_5_5_1;
		alignment = 2;
		break;

#ifndef USING_GLES2
	case DataFormat::A4R4G4B4_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_4_4_4_4_REV;
		alignment = 2;
		break;

	case DataFormat::R5G6B5_UNORM_PACK16:
		internalFormat = GL_RGB;
		format = GL_RGB;
		type = GL_UNSIGNED_SHORT_5_6_5_REV;
		alignment = 2;
		break;

	case DataFormat::A1R5G5B5_UNORM_PACK16:
		internalFormat = GL_RGBA;
		format = GL_RGBA;
		type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
		alignment = 2;
		break;
#endif

	default:
		_assert_msg_(G3D, false, "Thin3d GL: Unsupported texture format %d", (int)fmt);
		return false;
	}
	return true;
}

}