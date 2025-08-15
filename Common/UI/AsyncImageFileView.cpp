#include "Common/UI/View.h"
#include "Common/UI/AsyncImageFileView.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/ManagedTexture.h"

AsyncImageFileView::AsyncImageFileView(const Path &filename, UI::ImageSizeMode sizeMode, UI::LayoutParams *layoutParams)
	: UI::Clickable(layoutParams), canFocus_(true), filename_(filename), color_(0xFFFFFFFF), sizeMode_(sizeMode), textureFailed_(false), fixedSizeW_(0.0f), fixedSizeH_(0.0f) {}

AsyncImageFileView::~AsyncImageFileView() {}

static float DesiredSize(float sz, float contentSize, UI::MeasureSpec spec) {
	float measured;
	UI::MeasureBySpec(sz, contentSize, spec, &measured);
	return measured;
}

void AsyncImageFileView::GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const {
	if (texture_ && texture_->GetTexture()) {
		float texw = (float)texture_->Width();
		float texh = (float)texture_->Height();
		float desiredW = DesiredSize(layoutParams_->width, w, horiz);
		float desiredH = DesiredSize(layoutParams_->height, h, vert);
		switch (sizeMode_) {
		case UI::IS_FIXED:
			w = fixedSizeW_;
			h = fixedSizeH_;
			break;
		case UI::IS_KEEP_ASPECT:
			w = texw;
			h = texh;
			if (desiredW != w || desiredH != h) {
				float aspect = w / h;
				// We need the other dimension based on the desired scale to find the best aspect.
				float desiredWOther = DesiredSize(layoutParams_->height, h * (desiredW / w), vert);
				float desiredHOther = DesiredSize(layoutParams_->width, w * (desiredH / h), horiz);

				float diffW = fabsf(aspect - desiredW / desiredWOther);
				float diffH = fabsf(aspect - desiredH / desiredHOther);
				if (diffW < diffH) {
					w = desiredW;
					h = desiredWOther;
				} else {
					w = desiredHOther;
					h = desiredH;
				}
			}
			break;
		case UI::IS_DEFAULT:
		default:
			w = texw;
			h = texh;
			break;
		}
	} else {
		w = 16;
		h = 16;
	}
}

void AsyncImageFileView::SetFilename(const Path &filename) {
	if (filename_ != filename) {
		textureFailed_ = false;
		filename_ = filename;
		texture_.reset(nullptr);
	}
}

void AsyncImageFileView::DeviceLost() {
	if (texture_.get())
		texture_->DeviceLost();
}

void AsyncImageFileView::DeviceRestored(Draw::DrawContext *draw) {
	if (texture_.get())
		texture_->DeviceRestored(draw);
}

void AsyncImageFileView::Draw(UIContext &dc) {
	using namespace Draw;
	if (!texture_ && !textureFailed_ && !filename_.empty()) {
		texture_ = std::make_unique<ManagedTexture>(dc.GetDrawContext(), filename_.c_str(), ImageFileType::DETECT, true);
		if (!texture_.get())
			textureFailed_ = true;
	}

	if (HasFocus()) {
		dc.FillRect(dc.theme->itemFocusedStyle.background, bounds_.Expand(3));
	}

	// TODO: involve sizemode
	if (texture_ && texture_->GetTexture()) {
		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, texture_->GetTexture());
		dc.Draw()->Rect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, color_);
		dc.Flush();
		dc.RebindTexture();
		if (!text_.empty()) {
			dc.DrawText(text_, bounds_.centerX() + 1, bounds_.centerY() + 1, 0x80000000, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
			dc.DrawText(text_, bounds_.centerX(), bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
		}
	} else {
		if (!texture_ || texture_->Failed()) {
			if (!filename_.empty()) {
				// draw a black rectangle to represent the missing screenshot.
				dc.FillRect(UI::Drawable(0xFF000000), GetBounds());
			} else {
				// draw a dark gray rectangle to represent no save state.
				dc.FillRect(UI::Drawable(0x50202020), GetBounds());
			}
		}
		if (!text_.empty()) {
			dc.DrawText(text_, bounds_.centerX(), bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
		}
	}
}
