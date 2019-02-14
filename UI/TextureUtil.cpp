#include <algorithm>

#include "thin3d/thin3d.h"
#include "image/zim_load.h"
#include "image/png_load.h"
#include "math/math_util.h"
#include "file/vfs.h"
#include "ext/jpge/jpgd.h"
#include "UI/TextureUtil.h"
#include "Common/Log.h"

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
		ELOG("File (size: %d) has unknown format", (int)size);
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
			if (!image[0]) {
				ELOG("WTF");
				return false;
			}
		} else {
			ELOG("PNG load failed");
			return false;
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
		ELOG("Unsupported image format %d", (int)type);
		return false;
	}

	return *num_levels > 0;
}

bool ManagedTexture::LoadFromFileData(const uint8_t *data, size_t dataSize, ImageFileType type, bool generateMips) {
	generateMips_ = generateMips;
	using namespace Draw;

	int width[16]{}, height[16]{};
	uint8_t *image[16]{};

	int num_levels = 0;
	int zim_flags = 0;
	DataFormat fmt;
	if (!LoadTextureLevels(data, dataSize, type, width, height, &num_levels, &fmt, image, &zim_flags)) {
		return false;
	}

	if (!image[0]) {
		Crash();
	}

	if (num_levels < 0 || num_levels >= 16) {
		ELOG("Invalid num_levels: %d. Falling back to one. Image: %dx%d", num_levels, width[0], height[0]);
		num_levels = 1;
	}

	// Free the old texture, if any.
	if (texture_) {
		delete texture_;
		texture_ = nullptr;
	}

	int potentialLevels = std::min(log2i(width[0]), log2i(height[0]));

	TextureDesc desc{};
	desc.type = TextureType::LINEAR2D;
	desc.format = fmt;
	desc.width = width[0];
	desc.height = height[0];
	desc.depth = 1;
	desc.mipLevels = generateMips ? potentialLevels : num_levels;
	desc.generateMips = generateMips && potentialLevels > num_levels;
	desc.tag = "LoadedFileData";
	for (int i = 0; i < num_levels; i++) {
		desc.initData.push_back(image[i]);
	}
	texture_ = draw_->CreateTexture(desc);
	for (int i = 0; i < num_levels; i++) {
		if (image[i])
			free(image[i]);
	}
	return texture_;
}

bool ManagedTexture::LoadFromFile(const std::string &filename, ImageFileType type, bool generateMips) {
	generateMips_ = generateMips;
	size_t fileSize;
	uint8_t *buffer = VFSReadFile(filename.c_str(), &fileSize);
	if (!buffer) {
		filename_ = "";
		ELOG("Failed to read file '%s'", filename.c_str());
		return false;
	}
	bool retval = LoadFromFileData(buffer, fileSize, type, generateMips);
	if (retval) {
		filename_ = filename;
	} else {
		filename_ = "";
		ELOG("Failed to load texture '%s'", filename.c_str());
	}
	delete[] buffer;
	return retval;
}

std::unique_ptr<ManagedTexture> CreateTextureFromFile(Draw::DrawContext *draw, const char *filename, ImageFileType type, bool generateMips) {
	if (!draw)
		return std::unique_ptr<ManagedTexture>();
	// TODO: Load the texture on a background thread.
	ManagedTexture *mtex = new ManagedTexture(draw);
	if (!mtex->LoadFromFile(filename, type, generateMips)) {
		delete mtex;
		return std::unique_ptr<ManagedTexture>();
	}
	return std::unique_ptr<ManagedTexture>(mtex);
}

void ManagedTexture::DeviceLost() {
	ILOG("ManagedTexture::DeviceLost(%s)", filename_.c_str());
	if (texture_)
		texture_->Release();
	texture_ = nullptr;
}

void ManagedTexture::DeviceRestored(Draw::DrawContext *draw) {
	ILOG("ManagedTexture::DeviceRestored(%s)", filename_.c_str());
	_assert_(!texture_);
	draw_ = draw;
	// Vulkan: Can't load textures before the first frame has started.
	// Should probably try to lift that restriction again someday..
	loadPending_ = true;
}

Draw::Texture *ManagedTexture::GetTexture() {
	if (loadPending_) {
		if (!LoadFromFile(filename_, ImageFileType::DETECT, generateMips_)) {
			ELOG("ManagedTexture failed: '%s'", filename_.c_str());
		}
		loadPending_ = false;
	}
	return texture_;
}

// TODO: Remove the code duplication between this and LoadFromFileData
std::unique_ptr<ManagedTexture> CreateTextureFromFileData(Draw::DrawContext *draw, const uint8_t *data, int size, ImageFileType type, bool generateMips) {
	if (!draw)
		return std::unique_ptr<ManagedTexture>();
	ManagedTexture *mtex = new ManagedTexture(draw);
	if (mtex->LoadFromFileData(data, size, type, generateMips)) {
		return std::unique_ptr<ManagedTexture>(mtex);
	} else {
		// Best to return a null pointer if we fail!
		delete mtex;
		return std::unique_ptr<ManagedTexture>();
	}
}
