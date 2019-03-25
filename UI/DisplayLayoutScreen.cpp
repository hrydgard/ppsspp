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

#include <vector>

#include "base/colorutil.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui_atlas.h"

#include "DisplayLayoutScreen.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "DisplayLayoutEditor.h"
#include "GPU/Common/FramebufferCommon.h"

static const int leftColumnWidth = 200;
static const float orgRatio = 1.764706f;

// Ugly hackery, need to rework some stuff to get around this
static float local_dp_xres;
static float local_dp_yres;

class DragDropDisplay : public MultiTouchDisplay {
public:
	DragDropDisplay(float &x, float &y, int img, float &scale)
		: MultiTouchDisplay(img, scale, new UI::AnchorLayoutParams(x*local_dp_xres, y*local_dp_yres, UI::NONE, UI::NONE, true)),
		x_(x), y_(y), theScale_(scale) {
		scale_ = theScale_;
	}	

	virtual void SaveDisplayPosition() {
		x_ = bounds_.centerX() / local_dp_xres;
		y_ = bounds_.centerY() / local_dp_yres;
		scale_ = theScale_;
	}

	virtual float GetScale() const { return theScale_; }
	virtual void SetScale(float s) { theScale_ = s; scale_ = s; }

	private:

	float &x_, &y_;
	float &theScale_;
};

DisplayLayoutScreen::DisplayLayoutScreen() {
	picked_ = 0;
	mode_ = nullptr;
};


bool DisplayLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	int mode = mode_ ? mode_->GetSelection() : 0;
	if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) { mode = -1; }

	const Bounds &screen_bounds = screenManager()->getUIContext()->GetBounds();
	if ((touch.flags & TOUCH_MOVE) && picked_ != 0) {
		int touchX = touch.x - offsetTouchX;
		int touchY = touch.y - offsetTouchY;
		if (mode == 0) {
			const Bounds &bounds = picked_->GetBounds();

			int limitX = g_Config.fSmallDisplayZoomLevel * 120;
			int limitY = g_Config.fSmallDisplayZoomLevel * 68;

			const int quarterResX = local_dp_xres / 4;
			const int quarterResY = local_dp_yres / 4;

			if (bRotated) {
				//swap X/Y limit for rotated display
				int limitTemp = limitX;
				limitX = limitY;
				limitY = limitTemp;
			}

			// Check where each edge of the screen is
			const int windowLeftEdge = quarterResX;
			const int windowRightEdge = windowLeftEdge * 3;
			const int windowUpperEdge = quarterResY;
			const int windowLowerEdge = windowUpperEdge * 3;
			// And stick display when close to any edge
			stickToEdgeX = false; stickToEdgeY = false;
			if (touchX > windowLeftEdge - 8 + limitX && touchX < windowLeftEdge + 8 + limitX) { touchX = windowLeftEdge + limitX; stickToEdgeX = true; }
			if (touchX > windowRightEdge - 8 - limitX && touchX < windowRightEdge + 8 - limitX) { touchX = windowRightEdge - limitX; stickToEdgeX = true; }
			if (touchY > windowUpperEdge - 8 + limitY && touchY < windowUpperEdge + 8 + limitY) { touchY = windowUpperEdge + limitY; stickToEdgeY = true; }
			if (touchY > windowLowerEdge - 8 - limitY && touchY < windowLowerEdge + 8 - limitY) { touchY = windowLowerEdge - limitY; stickToEdgeY = true; }

			const int minX = local_dp_xres / 2;
			const int maxX = local_dp_xres + minX;
			const int minY = local_dp_yres / 2;
			const int maxY = local_dp_yres + minY;
			// Display visualization disappear outside of those bounds, so we have to limit
			if (touchX < -minX) touchX = -minX;
			if (touchX >  maxX) touchX =  maxX;
			if (touchY < -minY) touchY = -minY;
			if (touchY >  maxY) touchY =  maxY;

			// Limit small display on much larger output a bit differently
			if (quarterResX > limitX) limitX = quarterResX;
			if (quarterResY > limitY) limitY = quarterResY;

			int newX = bounds.centerX(), newY = bounds.centerY();
			// Allow moving zoomed in display freely as long as at least noticeable portion of the screen is occupied
			if (touchX > minX - limitX - 10 && touchX < minX + limitX + 10) {
				newX = touchX;
			}
			if (touchY > minY - limitY - 10 && touchY < minY + limitY + 10) {
				newY = touchY;
			}
			picked_->ReplaceLayoutParams(new UI::AnchorLayoutParams(newX, newY, NONE, NONE, true));
		} else if (mode == 1) {
			// Resize. Vertical = scaling, horizontal = spacing;
			// Up should be bigger so let's negate in that direction
			float diffX = (touchX - startX_);
			float diffY = -(touchY - startY_);

			float movementScale = 0.5f;
			float newScale = startScale_ + diffY * movementScale;
			// Desired scale * 8.0 since the visualization is tiny size and multiplied by 8.
			if (newScale > 80.0f) newScale = 80.0f;
			if (newScale < 8.0f) newScale = 8.0f;
			picked_->SetScale(newScale);
			scaleUpdate_ = picked_->GetScale();
			g_Config.fSmallDisplayZoomLevel = scaleUpdate_ / 8.0f;
		}
	}
	if ((touch.flags & TOUCH_DOWN) && picked_ == 0) {
		picked_ = displayRepresentation_;
		if (picked_) {
			const Bounds &bounds = picked_->GetBounds();
			startX_ = bounds.centerX();
			startY_ = bounds.centerY();
			offsetTouchX = touch.x - startX_;
			offsetTouchY = touch.y - startY_;
			startScale_ = picked_->GetScale();
		}
	}
	if ((touch.flags & TOUCH_UP) && picked_ != 0) {
		const Bounds &bounds = picked_->GetBounds();
		float saveX_ = touch.x;
		float saveY_ = touch.y;
		startScale_ = picked_->GetScale();
		picked_->SaveDisplayPosition();
		picked_ = 0;
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
	if (!stickToEdgeX || (stickToEdgeX && stickToEdgeY))
		g_Config.fSmallDisplayOffsetX = 0.5f;
	if (!stickToEdgeY || (stickToEdgeX && stickToEdgeY))
		g_Config.fSmallDisplayOffsetY = 0.5f;
	RecreateViews();
	return UI::EVENT_DONE;
};

UI::EventReturn DisplayLayoutScreen::OnZoomTypeChange(UI::EventParams &e) {
	if (g_Config.iSmallDisplayZoomType < (int)SmallDisplayZoom::MANUAL) {
		const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
		float autoBound = bounds.w / 480.0f;
		g_Config.fSmallDisplayZoomLevel = autoBound;
		displayRepresentationScale_ = g_Config.fSmallDisplayZoomLevel * 8.0f;
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

	void Draw(UIContext &dc) override {
		dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y2(), dc.theme->itemDownStyle.background.color);
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

	local_dp_xres = bounds.w;
	local_dp_yres = bounds.h;

	using namespace UI;

	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *gr = GetI18NCategory("Graphics");
	I18NCategory *co = GetI18NCategory("Controls");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	const float previewWidth = local_dp_xres / 2.0f;
	const float previewHeight = local_dp_yres / 2.0f;

	// Just visual boundaries of the screen, should be easier to use than imagination
	const float horizPreviewPadding = local_dp_xres / 4.0f;
	const float vertPreviewPadding = local_dp_yres / 4.0f;
	const float horizBoundariesWidth = 4.0f;
	// This makes it have at least 10.0f padding below at 1x.
	const float vertBoundariesHeight = 52.0f;

	// Left side, right, top, bottom.
	root_->Add(new Boundary(new AnchorLayoutParams(horizBoundariesWidth, FILL_PARENT, NONE, 0, horizPreviewPadding + previewWidth, 0)));
	root_->Add(new Boundary(new AnchorLayoutParams(horizBoundariesWidth, FILL_PARENT, horizPreviewPadding + previewWidth, 0, NONE, 0)));
	root_->Add(new Boundary(new AnchorLayoutParams(previewWidth, vertBoundariesHeight, horizPreviewPadding, vertPreviewPadding - vertBoundariesHeight, NONE, NONE)));
	root_->Add(new Boundary(new AnchorLayoutParams(previewWidth, vertBoundariesHeight, horizPreviewPadding, NONE, NONE, vertPreviewPadding - vertBoundariesHeight)));

	static const char *zoomLevels[] = { "Stretching", "Partial Stretch", "Auto Scaling", "Manual Scaling" };
	zoom_ = new PopupMultiChoice(&g_Config.iSmallDisplayZoomType, di->T("Options"), zoomLevels, 0, ARRAY_SIZE(zoomLevels), gr->GetName(), screenManager(), new AnchorLayoutParams(400, WRAP_CONTENT, previewWidth - 200.0f, NONE, NONE, 10));
	zoom_->OnChoice.Handle(this, &DisplayLayoutScreen::OnZoomTypeChange);

	static const char *displayRotation[] = { "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed" };
	rotation_ = new PopupMultiChoice(&g_Config.iInternalScreenRotation, gr->T("Rotation"), displayRotation, 1, ARRAY_SIZE(displayRotation), co->GetName(), screenManager(), new AnchorLayoutParams(400, WRAP_CONTENT, previewWidth - 200.0f, 10, NONE, local_dp_yres - 64 - 10));
	rotation_->SetEnabledPtr(&displayRotEnable_);
	displayRotEnable_ = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	bRotated = false;
	if (displayRotEnable_ && (g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL || g_Config.iInternalScreenRotation == ROTATION_LOCKED_VERTICAL180)) {
		bRotated = true;
	}
	displayRepresentationScale_ = g_Config.fSmallDisplayZoomLevel * 8.0f; // Visual representation image is just icon size and have to be scaled 8 times to match PSP native resolution which is used as 1.0 for zoom

	HighlightLabel *label = nullptr;
	mode_ = nullptr;
	if (g_Config.iSmallDisplayZoomType >= (int)SmallDisplayZoom::AUTO) { // Scaling
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::AUTO) {
			label = new HighlightLabel(gr->T("Auto Scaling"), new AnchorLayoutParams(WRAP_CONTENT, 64.0f, local_dp_xres / 2.0f, local_dp_yres / 2.0f, NONE, NONE, true));
			float autoBound = local_dp_yres / 270.0f;
			// Case of screen rotated ~ only works with buffered rendering
			if (bRotated) {
				autoBound = local_dp_yres / 480.0f;
			}
			else { // Without rotation in common cases like 1080p we cut off 2 pixels of height, this reflects other cases
				float resCommonWidescreen = autoBound - floor(autoBound);
				if (resCommonWidescreen != 0.0f) {
					float ratio = local_dp_xres / local_dp_yres;
					if (ratio < orgRatio) {
						autoBound = local_dp_xres / 480.0f;
					}
					else {
						autoBound = local_dp_yres / 272.0f;
					}
				}
			}
			g_Config.fSmallDisplayZoomLevel = autoBound;
			displayRepresentationScale_ = g_Config.fSmallDisplayZoomLevel * 8.0f;
			g_Config.fSmallDisplayOffsetX = 0.5f;
			g_Config.fSmallDisplayOffsetY = 0.5f;
		} else { // Manual Scaling
			Choice *center = new Choice(di->T("Center"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 74));
			center->OnClick.Handle(this, &DisplayLayoutScreen::OnCenter);
			root_->Add(center);
			PopupSliderChoiceFloat *zoomlvl = new PopupSliderChoiceFloat(&g_Config.fSmallDisplayZoomLevel, 1.0f, 10.0f, di->T("Zoom"), 1.0f, screenManager(), di->T("* PSP res"), new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10 + 64 + 64));
			root_->Add(zoomlvl);
			mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 158 + 64 + 10));
			mode_->AddChoice(di->T("Move"));
			mode_->AddChoice(di->T("Resize"));
			mode_->SetSelection(0);
		}
		displayRepresentation_ = new DragDropDisplay(g_Config.fSmallDisplayOffsetX, g_Config.fSmallDisplayOffsetY, I_PSP_DISPLAY, displayRepresentationScale_);
		displayRepresentation_->SetVisibility(V_VISIBLE);
	} else { // Stretching
		label = new HighlightLabel(gr->T("Stretching"), new AnchorLayoutParams(WRAP_CONTENT, 64.0f, local_dp_xres / 2.0f, local_dp_yres / 2.0f, NONE, NONE, true));
		displayRepresentation_ = new DragDropDisplay(g_Config.fSmallDisplayOffsetX, g_Config.fSmallDisplayOffsetY, I_PSP_DISPLAY, displayRepresentationScale_);
		displayRepresentation_->SetVisibility(V_INVISIBLE);
		float width = previewWidth;
		float height = previewHeight;
		if (g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::STRETCH) {
			Choice *stretched = new Choice("", "", false, new AnchorLayoutParams(width, height, width - width / 2.0f, NONE, NONE, height - height / 2.0f));
			stretched->SetEnabled(false);
			root_->Add(stretched);
		} else { // Partially stretched
			float origRatio = !bRotated ? 480.0f / 272.0f : 272.0f / 480.0f;
			float frameRatio = width / height;
			if (origRatio > frameRatio) {
				height = width / origRatio;
				if (!bRotated && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH) {
					height = (272.0f + height) / 2.0f;
				}
			} else {
				width = height * origRatio;
				if (bRotated && g_Config.iSmallDisplayZoomType == (int)SmallDisplayZoom::PARTIAL_STRETCH) {
					width = (272.0f + height) / 2.0f;
				}
			}
			Choice *stretched = new Choice("", "", false, new AnchorLayoutParams(width, height, previewWidth - width / 2.0f, NONE, NONE, previewHeight - height / 2.0f));
			stretched->SetEnabled(false);
			root_->Add(stretched);
		}
	}
	if (bRotated) {
		displayRepresentation_->SetAngle(90.0f);
	}

	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10));
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(displayRepresentation_);
	if (mode_) {
		root_->Add(mode_);
	}
	if (label) {
		root_->Add(label);
	}
	root_->Add(zoom_);
	root_->Add(rotation_);
	root_->Add(back);
}
