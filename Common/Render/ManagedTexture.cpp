#include <algorithm>
#include <cstring>

#include "Common/GPU/thin3d.h"
#include "ext/jpge/jpgd.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Format/ZIMLoad.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Math/math_util.h"
#include "Common/Math/curves.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Thread/Waitable.h"

// TODO: It really feels like we should be able to simplify this.
class TextureLoadTask : public Task {
public:
	TextureLoadTask(std::string_view filename, ImageFileType type, bool generateMips, TempImage *tempImage, ManagedTexture::LoadState *state, LimitedWaitable *waitable)
		: filename_(filename), type_(type), generateMips_(generateMips), tempImage_(tempImage), state_(state), waitable_(waitable) {}

	TaskType Type() const override { return TaskType::IO_BLOCKING; }
	TaskPriority Priority() const override { return TaskPriority::NORMAL; }

	void Run() override {
		size_t fileSize;
		uint8_t *buffer = g_VFS.ReadFile(filename_.c_str(), &fileSize);
		if (!buffer) {
			WARN_LOG(Log::IO, "Failed to read file '%s'", filename_.c_str());
			filename_.clear();
			*state_ = ManagedTexture::LoadState::FAILED;
			waitable_->Notify();
			return;
		}

		if (!tempImage_->LoadTextureLevelsFromFileData(buffer, fileSize, type_)) {
			*state_ = ManagedTexture::LoadState::FAILED;
			waitable_->Notify();
			return;
		}
		delete[] buffer;
		*state_ = ManagedTexture::LoadState::SUCCESS;
		waitable_->Notify();
	}

private:
	LimitedWaitable *waitable_;
	std::string filename_;
	TempImage *tempImage_;
	ImageFileType type_;
	bool generateMips_;
	ManagedTexture::LoadState *state_;
};

TempImage::~TempImage() {
	// Make sure you haven't forgotten to call Free.
	_dbg_assert_(levels[0] == nullptr);
}

static Draw::DataFormat ZimToT3DFormat(int zim) {
	switch (zim) {
	case ZIM_RGBA8888: return Draw::DataFormat::R8G8B8A8_UNORM;
	default: return Draw::DataFormat::R8G8B8A8_UNORM;
	}
}

ImageFileType DetectImageFileType(const uint8_t *data, size_t size) {
	if (size < 4) {
		return ImageFileType::UNKNOWN;
	}
	if (!memcmp(data, "ZIMG", 4)) {
		return ImageFileType::ZIM;
	} else if (!memcmp(data, "\x89\x50\x4E\x47", 4)) {
		return ImageFileType::PNG;
	} else if (!memcmp(data, "\xff\xd8\xff\xe0", 4) || !memcmp(data, "\xff\xd8\xff\xe1", 4)) {
		return ImageFileType::JPEG;
	} else {
		return ImageFileType::UNKNOWN;
	}
}

bool TempImage::LoadTextureLevelsFromFileData(const uint8_t *data, size_t size, ImageFileType typeSuggestion) {
	if (typeSuggestion == ImageFileType::DETECT) {
		typeSuggestion = DetectImageFileType(data, size);
	}
	if (typeSuggestion == ImageFileType::UNKNOWN) {
		ERROR_LOG(Log::G3D, "File (size: %d) has unknown format", (int)size);
		return false;
	}

	type = typeSuggestion;
	numLevels = 0;
	zimFlags = 0;

	switch (typeSuggestion) {
	case ImageFileType::ZIM:
		numLevels = LoadZIMPtr((const uint8_t *)data, size, width, height, &zimFlags, levels);
		fmt = ZimToT3DFormat(zimFlags & ZIM_FORMAT_MASK);
		break;

	case ImageFileType::PNG:
		if (1 == pngLoadPtr((const unsigned char *)data, size, &width[0], &height[0], &levels[0])) {
			numLevels = 1;
			fmt = Draw::DataFormat::R8G8B8A8_UNORM;
			if (!levels[0]) {
				ERROR_LOG(Log::IO, "pngLoadPtr failed (input size = %d)", (int)size);
				return false;
			}
		} else {
			ERROR_LOG(Log::IO, "PNG load failed");
			return false;
		}
		break;

	case ImageFileType::JPEG:
	{
		int actual_components = 0;
		unsigned char *jpegBuf = jpgd::decompress_jpeg_image_from_memory(data, (int)size, &width[0], &height[0], &actual_components, 4);
		if (jpegBuf) {
			numLevels = 1;
			fmt = Draw::DataFormat::R8G8B8A8_UNORM;
			levels[0] = (uint8_t *)jpegBuf;
		}
		break;
	}

	default:
		ERROR_LOG(Log::IO, "Unsupported image format %d", (int)type);
		return false;
	}

	return numLevels > 0;
}

Draw::Texture *CreateTextureFromTempImage(Draw::DrawContext *draw, const TempImage &image, bool generateMips, const char *name) {
	using namespace Draw;
	_assert_(image.levels[0] != nullptr && image.width[0] > 0 && image.height[0] > 0);

	int numLevels = image.numLevels;
	if (numLevels < 0 || numLevels >= 16) {
		ERROR_LOG(Log::IO, "Invalid num_levels: %d. Falling back to one. Image: %dx%d", numLevels, image.width[0], image.height[0]);
		numLevels = 1;
	}

	int potentialLevels = std::min(log2i(image.width[0]), log2i(image.height[0]));
	TextureDesc desc{};
	desc.type = TextureType::LINEAR2D;
	desc.format = image.fmt;
	desc.width = image.width[0];
	desc.height = image.height[0];
	desc.depth = 1;
	desc.mipLevels = generateMips ? potentialLevels : image.numLevels;
	desc.generateMips = generateMips && potentialLevels > image.numLevels;
	desc.tag = name;
	for (int i = 0; i < image.numLevels; i++) {
		desc.initData.push_back(image.levels[i]);
	}
	return draw->CreateTexture(desc);
}

Draw::Texture *CreateTextureFromFileData(Draw::DrawContext *draw, const uint8_t *data, size_t dataSize, ImageFileType type, bool generateMips, const char *name) {
	TempImage image;
	if (!image.LoadTextureLevelsFromFileData(data, dataSize, type)) {
		return nullptr;
	}
	Draw::Texture *texture = CreateTextureFromTempImage(draw, image, generateMips, name);
	image.Free();
	return texture;
}

Draw::Texture *CreateTextureFromFile(Draw::DrawContext *draw, const char *filename, ImageFileType type, bool generateMips) {
	size_t fileSize;
	uint8_t *buffer = g_VFS.ReadFile(filename, &fileSize);
	if (!buffer) {
		ERROR_LOG(Log::IO, "Failed to read file '%s'", filename);
		return nullptr;
	}
	Draw::Texture *texture = CreateTextureFromFileData(draw, buffer, fileSize, type, generateMips, filename);
	delete[] buffer;
	return texture;
}

Draw::Texture *ManagedTexture::GetTexture() {
	if (texture_) {
		return texture_;
	} else if (state_ == LoadState::SUCCESS) {
		if (taskWaitable_) {
			taskWaitable_->WaitAndRelease();
			taskWaitable_ = nullptr;
		}
		// Image load is done, texture creation is not.
		texture_ = CreateTextureFromTempImage(draw_, pendingImage_, generateMips_, filename_.c_str());
		if (!texture_) {
			// Failed to create the texture for whatever reason, like dimensions. Don't retry next time.
			state_ = LoadState::FAILED;
		}
		pendingImage_.Free();
	}
	return texture_;
}

ManagedTexture::ManagedTexture(Draw::DrawContext *draw, std::string_view filename, ImageFileType type, bool generateMips) 
	: draw_(draw), filename_(filename), type_(type), generateMips_(generateMips)
{
	StartLoadTask();
}

ManagedTexture::~ManagedTexture() {
	// Stop any pending loads.
	if (taskWaitable_) {
		taskWaitable_->WaitAndRelease();
		pendingImage_.Free();
	}
	if (texture_)
		texture_->Release();
}

void ManagedTexture::StartLoadTask() {
	_dbg_assert_(!taskWaitable_);
	taskWaitable_ = new LimitedWaitable();
	g_threadManager.EnqueueTask(new TextureLoadTask(filename_, type_, generateMips_, &pendingImage_, &state_, taskWaitable_));
}

void ManagedTexture::DeviceLost() {
	INFO_LOG(Log::G3D, "ManagedTexture::DeviceLost(%s)", filename_.c_str());
	if (taskWaitable_) {
		taskWaitable_->WaitAndRelease();
		taskWaitable_ = nullptr;
		pendingImage_.Free();
	}
	if (texture_)
		texture_->Release();
	texture_ = nullptr;
	if (state_ == LoadState::SUCCESS) {
		state_ = LoadState::PENDING;
	}
}

void ManagedTexture::DeviceRestored(Draw::DrawContext *draw) {
    INFO_LOG(Log::G3D, "ManagedTexture::DeviceRestored(%s)", filename_.c_str());

	draw_ = draw;

	_dbg_assert_(!texture_);
	if (texture_) {
		ERROR_LOG(Log::G3D, "ManagedTexture: Unexpected - texture already present: %s", filename_.c_str());
		return;
	}

	if (state_ == LoadState::PENDING) {
		// Kick off a new load task.
		StartLoadTask();
	}
}
