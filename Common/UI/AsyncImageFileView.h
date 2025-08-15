#pragma once

#include "Common/UI/View.h"
#include "Common/File/Path.h"

class UIContext;
class ManagedTexture;

// AsyncImageFileView loads a texture from a file, and reloads it as necessary.
// TODO: Actually make async, doh.
class AsyncImageFileView : public UI::Clickable {
public:
	AsyncImageFileView(const Path &filename, UI::ImageSizeMode sizeMode, UI::LayoutParams *layoutParams = 0);
	~AsyncImageFileView();

	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return text_; }

	void DeviceLost() override;
	void DeviceRestored(Draw::DrawContext *draw) override;

	void SetFilename(const Path &filename);
	void SetColor(uint32_t color) { color_ = color; }
	void SetOverlayText(const std::string &text) { text_ = text; }
	void SetFixedSize(float fixW, float fixH) { fixedSizeW_ = fixW; fixedSizeH_ = fixH; }
	void SetCanBeFocused(bool can) { canFocus_ = can; }

	bool CanBeFocused() const override { return canFocus_; }

	const Path &GetFilename() const { return filename_; }

private:
	bool canFocus_;
	Path filename_;
	std::string text_;
	uint32_t color_;
	UI::ImageSizeMode sizeMode_;

	std::unique_ptr<ManagedTexture> texture_;
	bool textureFailed_;
	float fixedSizeW_;
	float fixedSizeH_;
};
