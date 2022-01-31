// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "UI/DisplayLayoutScreen.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "DisplayLayoutEditor.h"
#include "GPU/Common/FramebufferManagerCommon.h"

static const int leftColumnWidth = 200;
static const float orgRatio = 1.764706f;

static const float UI_DISPLAY_SCALE = 8.0f;
static float ScaleSettingToUI() {
	float scale = g_Config.fSmallDisplayZoomLevel * UI_DISPLAY_SCALE;
	// Account for 1x display doubling dps.
	if (g_dpi_scale_x > 1.0f) {
		scale *= g_dpi_scale_x;
	}
	return scale;
}

static void UpdateScaleSetting(float scale) {
	// Account for 1x display doubling dps.
	if (g_dpi_scale_x > 1.0f) {
		scale /= g_dpi_scale_x;
	}
	g_Config.fSmallDisplayZoomLevel = scale;
}

static void UpdateScaleSettingFromUI(float scale) {
	UpdateScaleSetting(scale / UI_DISPLAY_SCALE);
}

class DragDropDisplay : public MultiTouchDisplay {
public:
	DragDropDisplay(float &x, float &y, ImageID img, float scale, const Bounds &screenBounds)
		: MultiTouchDisplay(img, scale, new UI::AnchorLayoutParams(x * screenBounds.w, y * screenBounds.h, UI::NONE, UI::NONE, true)),
		x_(x), y_(y), screenBounds_(screenBounds) {
		UpdateScale(scale);
	}

	std::string DescribeText() const override;

	void SaveDisplayPosition() {
		x_ = bounds_.centerX() / screenBounds_.w;
		y_ = bounds_.centerY() / screenBounds_.h;
	}

	void UpdateScale(float s) {
		scale_ = s;
	}
	float Scale() {
		return scale_;
	}

private:
	float &x_, &y_;
	const Bounds &screenBounds_;
};

std::string DragDropDisplay::DescribeText() const {
	auto u = GetI18NCategory("UI Elements");
	return u->T("Screen representation");
}

DisplayLayoutScreen::DisplayLayoutScreen() {
	// Ignore insets - just couldn't get the logic to work.
	ignoreInsets_ = true;
}

bool DisplayLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	int mode = mode_ ? mode_->GetSelection() : 0;
	if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
		mode = -1;
	}

	const Bounds &screen_bounds = screenManager()->getUIContext()->GetBounds();
	if ((touch.flags & TOUCH_MOVE) != 0 && dragging_) {
		int touchX = touch.x - offsetTouchX_;
		int touchY = touch.y - offsetTouchY_;
		if (mode == 0) {
			const auto &prevParams = displayRepresentation_->GetLayoutParams()->As<AnchorLayoutParams>();
			Point newPos(prevParams->left, prevParams->top);

			int limitX = g_Config.fSmallDisplayZoomLevel * 120;
			int limitY = g_Config.fSmallDisplayZoomLevel * 68;

			const int quarterResX = screen_bounds.w / 4;
			const int quarterResY = screen_bounds.h / 4;

			if (bRotated_) {
				//swap X/Y limit for rotated display
				std::swap(limitX, limitY);
			}

			// Check where each edge of the screen is
			const int windowLeftEdge = quarterResX;
			const int windowRightEdge = windowLeftEdge * 3;
			const int windowUpperEdge = quarterResY;
			const int windowLowerEdge = windowUpperEdge * 3;
			// And stick display when close to any edge
			stickToEdgeX_ = false;
			stickToEdgeY_ = false;
			if (touchX > windowLeftEdge - 8 + limitX && touchX < windowLeftEdge + 8 + limitX) { touchX = windowLeftEdge + limitX; stickToEdgeX_ = true; }
			if (touchX > windowRightEdge - 8 - limitX && touchX < windowRightEdge + 8 - limitX) { touchX = windowRightEdge - limitX; stickToEdgeX_ = true; }
			if (touchY > windowUpperEdge - 8 + limitY && touchY < windowUpperEdge + 8 + limitY) { touchY = windowUpperEdge + limitY; stickToEdgeY_ = true; }
			if (touchY > windowLowerEdge - 8 - limitY && touchY < windowLowerEdge + 8 - limitY) { touchY = windowLowerEdge - limitY; stickToEdgeY_ = true; }

			const int minX = screen_bounds.w / 2;
			const int maxX = screen_bounds.w + minX;
			const int minY = screen_bounds.h / 2;
			const int maxY = screen_bounds.h + minY;
			// Display visualization disappear outside of those bounds, so we have to limit
			if (touchX < -minX) touchX = -minX;
			if (touchX >  maxX) touchX =  maxX;
			if (touchY < -minY) touchY = -minY;
			if (touchY >  maxY) touchY =  maxY;

			// Limit small display on much larger output a bit differently
			if (quarterResX > limitX) limitX = quarterResX;
			if (quarterResY > limitY) limitY = quarterResY;

			// Allow moving zoomed in display freely as long as at least noticeable portion of the screen is occupied
			if (touchX > minX - limitX - 10 && touchX < minX + limitX + 10) {
				newPos.x = touchX;
			}
			if (touchY > minY - limitY - 10 && touchY < minY + limitY + 10) {
				newPos.y = touchY;
			}
			displayRepresentation_->ReplaceLayoutParams(new AnchorLayoutParams(newPos.x, newPos.y, NONE, NONE, true));
		} else if (mode == 1) {
			// Resize. Vertical = scaling; Up should be bigger so let's negate in that direction
			float diffY = -(touchY - startY_);

			float movementScale = 0.5f;
			float newScale = startScale_ + diffY * movementScale;
			// Desired scale * 8.0 since the visualization is tiny size and multiplied by 8.
			newScale = clamp_value(newScale, UI_DISPLAY_SCALE, UI_DISPLAY_SCALE * 10.0f);
			displayRepresentation_->UpdateScale(newScale);
			UpdateScaleSettingFromUI(newScale);
		}
	}
	if ((touch.flags & TOUCH_DOWN) != 0 && !dragging_) {
		dragging_ = true;
		const Bounds &bounds = displayRepresentation_->GetBounds();
		startY_ = bounds.centerY();
		offsetTouchX_ = touch.x - bounds.centerX();
		offsetTouchY_ = touch.y - bounds.centerY();
		startScale_ = displayRepresentation_->Scale();
	}
	if ((touch.flags & TOUCH_UP) != 0 && dragging_) {
		displayRepresentation_->SaveDisplayPosition();
		dragging_ = false;
	}
	return true;
}

void DisplayLayoutScreen::resized() {
	RecreateViews();
}

void DisplayLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save("DisplayLayoutScreen::onFinish");
}

UI::EventReturn DisplayLayoutScreen::OnCenter(UI::EventParams &e) {
	if (!stickToEdgeX_ || (stickToEdgeX_ && stickToEdgeY_))
		g_Config.fSmallDisplayOffsetX = 0.5f;
	if (!stickToEdgeY_ || (stickToEdgeX_ && stickToEdgeY_))
		g_Config.fSmallDisplayOffsetY = 0.5f;
	RecreateViews();
	return UI::EVENT_DONE;
};

UI::EventReturn DisplayLayoutScreen::OnZoomTypeChange(UI::EventParams &e) {
	if (g_Config.iSmallDisplayZoomType < (int)SmallDisplayZoom::MANUAL) {
		const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
		float autoBound = bounds.w / 480.0f;
		UpdateScaleSetting(autoBound);
		displayRepresentation_->UpdateScale(ScaleSettingToUI());
		g_Config.fSmallDisplayOffsetX = 0.5f;
		g_Config.fSmallDisplayOffsetY = 0.5f;
	}
	RecreateViews();
	return UI::EVENT_DONE;
};

void DisplayLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

class Boundary : public UI::View {
public:
	Boundary(UI::LayoutParams *layoutParams) : UI::View(layoutParams) {
	}

	std::string DescribeText() const override {
		return "";
	}

	void Draw(UIContext &dc) override {
		dc.Draw()->DrawImageCenterTexel(dc.theme->whiteImage, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y2(), dc.theme->itemDownStyle.background.color);
	}
};

// Stealing StickyChoice's layout and text rendering.
class HighlightLabel : public UI::StickyChoice {
public:
	HighlightLabel(const std::string &text, UI::LayoutParams *layoutParams)
		: UI::StickyChoice(text, "", layoutParams) {
		Press();
	}

	bool CanBeFocused() const override { return false; }
};

void DisplayLayoutScreen::CreateViews() {
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();

	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto gr = GetI18NCategory("Graphics");
	auto co = GetI18NCategory("Controls");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	const float previewWidth = bounds.w / 2.0f;
	const float previewHeight = bounds.h / 2.0f;

	// Just visual boundaries of the screen, should be easier to use than imagination
	const float horizPreviewPadding = bounds.w / 4.0f;
	const float vertPreviewPadding = bounds.h / 4.0f;
	const float horizBoundariesWidth = 4.0f;
	// This makes it have at least 10.0f padding below at 1x.
	const float vertBoundariesHeight = 52.0f;

	// We manually implement insets here for the buttons. This file defied refactoring :(
	float leftInset = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT);

	// Left side, right, top, bottom.
	root_->Add(new Boundary(new AnchorLayoutParams(horizBoundariesWidth, FILL_PARENT, horizPreviewPadding - horizBoundariesWidth, 0, NONE, 0)));
	root_->Add(new Boundary(new AnchorLayoutParams(horizBoundariesWidth, FILL_PARENT, horizPreviewPadding + previewWidth, 0, NONE, 0)));
	root_->Add(new Boundary(new AnchorLayoutParams(previewWidth, vertBoundariesHeight, horizPreviewPadding, vertPreviewPadding - vertBoundariesHeight, NONE, NONE)));
	root_->Add(new Boundary(new AnchorLayoutParams(previewWidth, vertBoundariesHeight, horizPreviewPadding, vertPreviewPadding + previewHeight, NONE, NONE)));

	bool displayRotEnable = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) || g_Config.bSoftwareRendering;
	bRotated_ = false;
	if (displayRotEnable && (g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180)) {
		bRotated_ = true;
	}

	HighlightLabel *label = nullptr;
	mode_ = nullptr;
	if (g_Config.iSmallDisplayZoomType >= (int)SmallDisplayZoom::AUTO) { // Scaling
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
			label = new HighlightLabel(gr->T("Auto Scaling"), new AnchorLayoutParams(WRAP_CONTENT, 64.0f, bounds.w / 2.0f, bounds.h / 2.0f, NONE, NONE, true));
			float autoBound = bounds.h / 270.0f;
			// Case of screen rotated ~ only works with buffered rendering
			if (bRotated_) {
				autoBound = bounds.h / 480.0f;
			} else { // Without rotation in common cases like 1080p we cut off 2 pixels of height, this reflects other cases
				float resCommonWidescreen = autoBound - floor(autoBound);
				if (resCommonWidescreen != 0.0f) {
					float ratio = bounds.w / bounds.h;
					if (ratio < orgRatio) {
						autoBound = bounds.w / 480.0f;
					}
					else {
						autoBound = bounds.h / 272.0f;
					}
				}
			}
			UpdateScaleSetting(autoBound);
			g_Config.fSmallDisplayOffsetX = 0.5f;
			g_Config.fSmallDisplayOffsetY = 0.5f;
		} else { // Manual Scaling
			Choice *center = new Choice(di->T("Center"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10 + leftInset, NONE, NONE, 74));
			center->OnClick.Handle(this, &DisplayLayoutScreen::OnCenter);
			root_->Add(center);
			float minZoom = 1.0f;
			if (g_dpi_scale_x > 1.0f) {
				minZoom /= g_dpi_scale_x;
			}
			PopupSliderChoiceFloat *zoomlvl = new PopupSliderChoiceFloat(&g_Config.fSmallDisplayZoomLevel, minZoom, 10.0f, di->T("Zoom"), 1.0f, screenManager(), di->T("* PSP res"), new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10 + leftInset, NONE, NONE, 10 + 64 + 64));
			root_->Add(zoomlvl);
			mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10 + leftInset, NONE, NONE, 158 + 64 + 10));
			mode_->AddChoice(di->T("Move"));
			mode_->AddChoice(di->T("Resize"));
			mode_->SetSelection(0, false);
		}
		displayRepresentation_ = new DragDropDisplay(g_Config.fSmallDisplayOffsetX, g_Config.fSmallDisplayOffsetY, ImageID("I_PSP_DISPLAY"), ScaleSettingToUI(), bounds);
		displayRepresentation_->SetVisibility(V_VISIBLE);
	} else { // Stretching
		label = new HighlightLabel(gr->T("Stretching"), new AnchorLayoutParams(WRAP_CONTENT, 64.0f, bounds.w / 2.0f, bounds.h / 2.0f, NONE, NONE, true));
		displayRepresentation_ = new DragDropDisplay(g_Config.fSmallDisplayOffsetX, g_Config.fSmallDisplayOffsetY, ImageID("I_PSP_DISPLAY"), ScaleSettingToUI(), bounds);
		displayRepresentation_->SetVisibility(V_INVISIBLE);
		float width = previewWidth;
		float height = previewHeight;
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::STRETCH) {
			Choice *stretched = new Choice("", "", false, new AnchorLayoutParams(width, height, width - width / 2.0f, NONE, NONE, height - height / 2.0f));
			stretched->SetEnabled(false);
			root_->Add(stretched);
		} else { // Partially stretched
			float origRatio = !bRotated_ ? 480.0f / 272.0f : 272.0f / 480.0f;
			float frameRatio = width / height;
			if (origRatio > frameRatio) {
				height = width / origRatio;
				if (!bRotated_ && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH) {
					height = (272.0f + height) / 2.0f;
				}
			} else {
				width = height * origRatio;
				if (bRotated_ && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH) {
					width = (272.0f + height) / 2.0f;
				}
			}
			Choice *stretched = new Choice("", "", false, new AnchorLayoutParams(width, height, previewWidth - width / 2.0f, NONE, NONE, previewHeight - height / 2.0f));
			stretched->SetEnabled(false);
			root_->Add(stretched);
		}
	}
	if (bRotated_) {
		displayRepresentation_->SetAngle(90.0f);
	}

	root_->Add(displayRepresentation_);
	if (mode_) {
		root_->Add(mode_);
	}
	if (label) {
		root_->Add(label);
	}

	static const char *zoomLevels[] = { "Stretching", "Partial Stretch", "Auto Scaling", "Manual Scaling" };
	auto zoom = new PopupMultiChoice(&g_Config.iSmallDisplayZoomType, di->T("Options"), zoomLevels, 0, ARRAY_SIZE(zoomLevels), gr->GetName(), screenManager(), new AnchorLayoutParams(400, WRAP_CONTENT, previewWidth - 200.0f, NONE, NONE, 10));
	zoom->OnChoice.Handle(this, &DisplayLayoutScreen::OnZoomTypeChange);
	root_->Add(zoom);

	static const char *displayRotation[] = { "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed" };
	auto rotation = new PopupMultiChoice(&g_Config.iInternalScreenRotation, gr->T("Rotation"), displayRotation, 1, ARRAY_SIZE(displayRotation), co->GetName(), screenManager(), new AnchorLayoutParams(400, WRAP_CONTENT, previewWidth - 200.0f, 10, NONE, bounds.h - 64 - 10));
	rotation->SetEnabledFunc([] {
		return g_Config.iRenderingMode != FB_NON_BUFFERED_MODE || g_Config.bSoftwareRendering;
	});
	root_->Add(rotation);

	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10 + leftInset, NONE, NONE, 10));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(back);
}
