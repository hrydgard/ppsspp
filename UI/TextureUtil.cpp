#include "thin3d/thin3d.h"
#include "image/zim_load.h"
#include "image/png_load.h"
#include "file/vfs.h"
#include "ext/jpge/jpgd.h"
#include "UI/TextureUtil.h"

static Draw::DataFormat ZimToT3DFormat(int zim) {
	switch (zim) {
	case ZIM_ETC1: return Draw::DataFormat::ETC1;
	case ZIM_RGBA8888: return Draw::DataFormat::R8G8B8A8_UNORM;
	case ZIM_LUMINANCE: return Draw::DataFormat::R8_UNORM;
	default: return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

static ImageFileType DetectImageFileType(const uint8_t *data, size_t size) {
	if (!memcmp(data, "ZIMG", 4)) {
		return ZIM;
	}
	else if (!memcmp(data, "\x89\x50\x4E\x47", 4)) {
		return PNG;
	}
	else if (!memcmp(data, "\xff\xd8\xff\xe0", 4)) {
		return JPEG;
	}
	else {
		return TYPE_UNKNOWN;
	}
}

static bool LoadTextureLevels(const uint8_t *data, size_t size, ImageFileType type, int width[16], int height[16], int *num_levels, Draw::DataFormat *fmt, uint8_t *image[16], int *zim_flags) {
	if (type == DETECT) {
		type = DetectImageFileType(data, size);
	}
	if (type == TYPE_UNKNOWN) {
		ELOG("File has unknown format");
		return false;
	}

	*num_levels = 0;
	*zim_flags = 0;

	switch (type) {
	case ZIM:
	{
		*num_levels = LoadZIMPtr((const uint8_t *)data, size, width, height, zim_flags, image);
		*fmt = ZimToT3DFormat(*zim_flags & ZIM_FORMAT_MASK);
	}
	break;

	case PNG:
		if (1 == pngLoadPtr((const unsigned char *)data, size, &width[0], &height[0], &image[0], false)) {
			*num_levels = 1;
			*fmt = Draw::DataFormat::R8G8B8A8_UNORM;
		}
		break;

	case JPEG:
	{
		int actual_components = 0;
		unsigned char *jpegBuf = jpgd::decompress_jpeg_image_from_memory(data, (int)size, &width[0], &height[0], &actual_components, 4);
		if (jpegBuf) {
			*num_levels = 1;
			*fmt = Draw::DataFormat::R8G8B8A8_UNORM;
			image[0] = (uint8_t *)jpegBuf;
		}
	}
	break;

	default:
		ELOG("Unknown image format");
		return false;
	}

	return *num_levels > 0;
}

bool ManagedTexture::LoadFromFileData(const uint8_t *data, size_t dataSize, ImageFileType type) {
	using namespace Draw;

	int width[16]{}, height[16]{};
	uint8_t *image[16]{};

	int num_levels = 0;
	int zim_flags = 0;
	DataFormat fmt;
	if (!LoadTextureLevels(data, dataSize, type, width, height, &num_levels, &fmt, image, &zim_flags)) {
		return false;
	}

	if (num_levels < 0 || num_levels >= 16) {
		ELOG("Invalid num_levels: %d. Falling back to one. Image: %dx%d", num_levels, width[0], height[0]);
		num_levels = 1;
	}

	if (texture_) {
		delete texture_;
		texture_ = nullptr;
	}

	TextureDesc desc{};
	desc.type = TextureType::LINEAR2D;
	desc.format = fmt;
	desc.width = width[0];
	desc.height = height[0];
	desc.depth = 1;
	desc.mipLevels = num_levels;
	for (int i = 0; i < num_levels; i++) {
		desc.initData.push_back(image[i]);
	}
	texture_ = draw_->CreateTexture(desc);
	for (int i = 0; i < num_levels; i++) {
		if (image[i])
			free(image[i]);
	}
	return true;
}

bool ManagedTexture::LoadFromFile(const std::string &filename, ImageFileType type) {
	filename_ = "";
	size_t fileSize;
	uint8_t *buffer = VFSReadFile(filename.c_str(), &fileSize);
	if (!buffer) {
		ELOG("Failed to read file %s", filename.c_str());
		return false;
	}
	bool retval = LoadFromFileData(buffer, fileSize, type);
	if (retval) {
		filename_ = filename;
	} else {
		ELOG("Failed to load texture %s", filename.c_str());
	}
	delete[] buffer;
	return retval;
}

ManagedTexture *CreateTextureFromFile(Draw::DrawContext *draw, const char *filename, ImageFileType type) {
	if (!draw)
		return nullptr;
	ManagedTexture *mtex = new ManagedTexture(draw);
	if (!mtex->LoadFromFile(filename, type)) {
		delete mtex;
		return nullptr;
	}
	return mtex;
}

// TODO: Remove the code duplication between this and LoadFromFileData
ManagedTexture *CreateTextureFromFileData(Draw::DrawContext *draw, const uint8_t *data, int size, ImageFileType type) {
	if (!draw)
		return nullptr;
	ManagedTexture *mtex = new ManagedTexture(draw);
	mtex->LoadFromFileData(data, size, type);
	return mtex;
}