#pragma once

#include <cstring>
#include <string_view>
#include <memory>

#include "Common/GPU/thin3d.h"
#include "Common/UI/View.h"
#include "Common/File/Path.h"

enum class ImageFileType {
	PNG,
	JPEG,
	ZIM,
	DETECT,
	UNKNOWN,
};

class TextureLoadTask;
class LimitedWaitable;

// For UI images loaded from disk, loaded into RAM, generally staged for upload.
// The reason for the separation is so that the image can be loaded and decompressed on a thread,
// and then only uploaded to the GPU on the main thread.
struct TempImage {
	~TempImage();
	Draw::DataFormat fmt = Draw::DataFormat::UNDEFINED;
	ImageFileType type = ImageFileType::UNKNOWN;
	uint8_t *levels[16]{};   // only free the first pointer, they all point to the same buffer.
	int zimFlags = 0;
	int width[16]{};
	int height[16]{};
	int numLevels = 0;

	bool LoadTextureLevelsFromFileData(const uint8_t *data, size_t size, ImageFileType typeSuggestion = ImageFileType::DETECT);
	void Free() {
		if (levels[0]) {
			free(levels[0]);
			memset(levels, 0, sizeof(levels));
		}
	}
};

// Managed (will auto-reload from file) and async. For use in UI.
class ManagedTexture {
public:
	ManagedTexture(Draw::DrawContext *draw, std::string_view filename, ImageFileType type = ImageFileType::DETECT, bool generateMips = false);
	~ManagedTexture();
	Draw::Texture *GetTexture();  // For immediate use, don't store.
	int Width() const { return texture_->Width(); }
	int Height() const { return texture_->Height(); }

	void DeviceLost();
	void DeviceRestored(Draw::DrawContext *draw);

	bool Failed() const {
		return state_ == LoadState::FAILED;
	}

	enum class LoadState {
		PENDING,
		FAILED,
		SUCCESS,
	};

private:
	void StartLoadTask();

	Draw::Texture *texture_ = nullptr;
	Draw::DrawContext *draw_;
	std::string filename_;  // Textures that are loaded from files can reload themselves automatically.
	bool generateMips_ = false;
	ImageFileType type_ = ImageFileType::DETECT;
	TextureLoadTask *loadTask_ = nullptr;
	LimitedWaitable *taskWaitable_ = nullptr;
	TempImage pendingImage_;
	LoadState state_ = LoadState::PENDING;
};

Draw::Texture *CreateTextureFromFileData(Draw::DrawContext *draw, const uint8_t *data, size_t dataSize, ImageFileType type, bool generateMips, const char *name);
Draw::Texture *CreateTextureFromFile(Draw::DrawContext *draw, const char *filename, ImageFileType type, bool generateMips);
Draw::Texture *CreateTextureFromTempImage(Draw::DrawContext *draw, const TempImage &image, bool generateMips, const char *name);

ImageFileType DetectImageFileType(const uint8_t *data, size_t size);
