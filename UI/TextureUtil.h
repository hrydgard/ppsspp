#pragma once

#include <memory>

#include "Common/GPU/thin3d.h"
#include "Common/UI/View.h"
#include "Common/File/Path.h"

enum ImageFileType {
	PNG,
	JPEG,
	ZIM,
	DETECT,
	TYPE_UNKNOWN,
};

class ManagedTexture {
public:
	ManagedTexture(Draw::DrawContext *draw) : draw_(draw) {
	}
	~ManagedTexture() {
		if (texture_)
			texture_->Release();
	}

	bool LoadFromFile(const std::string &filename, ImageFileType type = ImageFileType::DETECT, bool generateMips = false);
	bool LoadFromFileData(const uint8_t *data, size_t dataSize, ImageFileType type, bool generateMips, const char *name);
	Draw::Texture *GetTexture();  // For immediate use, don't store.
	int Width() const { return texture_->Width(); }
	int Height() const { return texture_->Height(); }

	void DeviceLost();
	void DeviceRestored(Draw::DrawContext *draw);

private:
	Draw::Texture *texture_ = nullptr;
	Draw::DrawContext *draw_;
	std::string filename_;  // Textures that are loaded from files can reload themselves automatically.
	bool generateMips_ = false;
	bool loadPending_ = false;
};

std::unique_ptr<ManagedTexture> CreateTextureFromFile(Draw::DrawContext *draw, const char *filename, ImageFileType fileType, bool generateMips);
std::unique_ptr<ManagedTexture> CreateTextureFromFileData(Draw::DrawContext *draw, const uint8_t *data, int size, ImageFileType fileType, bool generateMips, const char *name);

class GameIconView : public UI::InertView {
public:
	GameIconView(const Path &gamePath, float scale, UI::LayoutParams *layoutParams = 0)
		: InertView(layoutParams), gamePath_(gamePath), scale_(scale) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return ""; }

private:
	Path gamePath_;
	float scale_ = 1.0f;
	int textureWidth_ = 0;
	int textureHeight_ = 0;
};
