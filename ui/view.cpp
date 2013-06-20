#include "base/display.h"
#include "base/mutex.h"
#include "input/input_state.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx/texture.h"
#include "gfx/texture_atlas.h"
#include "ui/ui.h"
#include "ui/view.h"
#include "ui/ui_context.h"

namespace UI {

static View *focusedView;
static bool focusMovementEnabled;

const float ITEM_HEIGHT = 64.f;

View *GetFocusedView() {
	return focusedView;
}

void SetFocusedView(View *view) {
	focusedView = view;
}

void EnableFocusMovement(bool enable) {
	focusMovementEnabled = enable;
	if (!enable)
		focusedView = 0;
}

bool IsFocusMovementEnabled() {
	return focusMovementEnabled;
}

void MeasureBySpec(Size sz, float contentWidth, MeasureSpec spec, float *measured) {
	*measured = sz;
	if (sz == WRAP_CONTENT) {
		if (spec.type == UNSPECIFIED || spec.type == AT_MOST)
			*measured = contentWidth;
		else if (spec.type == EXACTLY)
			*measured = spec.size;
	} else if (sz == FILL_PARENT)	{
		if (spec.type == UNSPECIFIED)
			*measured = contentWidth;  // We have no value to set
		else
			*measured = spec.size;
	} else if (spec.type == EXACTLY || (spec.type == AT_MOST && *measured > spec.size)) {
		*measured = spec.size;
	}
}

void Event::Add(std::function<EventReturn(EventParams&)> func) {
	HandlerRegistration reg;
	reg.func = func;
	handlers_.push_back(reg);
}

// Call this from input thread or whatever, it doesn't matter
void Event::Trigger(EventParams &e) {
	lock_guard guard(mutex_);
	if (!triggered_) {
		triggered_ = true;
		eventParams_ = e;
	}
}

// Call this from UI thread
void Event::Update() {
	lock_guard guard(mutex_);
	if (triggered_) {
		for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter) {
			(iter->func)(eventParams_);
		}
		triggered_ = false;
	}
}

void View::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	float contentW = 0.0f, contentH = 0.0f;
	GetContentDimensions(dc, contentW, contentH);
	MeasureBySpec(layoutParams_->width, contentW, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, contentH, vert, &measuredHeight_);
}

// Default values

void View::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 10.0f;
	h = 10.0f;
}

void Clickable::Click() {
	UI::EventParams e;
	e.v = this;
	OnClick.Trigger(e);
};

void Clickable::Touch(const TouchInput &input) {
	if (!enabled_) {
		dragging_ = false;
		down_ = false;
		return;
	}

	if (input.flags & TOUCH_DOWN) {
		if (bounds_.Contains(input.x, input.y)) {
			if (IsFocusMovementEnabled())
				SetFocusedView(this);
			dragging_ = true;
			down_ = true;
		} else {
			down_ = false;
			dragging_ = false;
		}
	} else if (input.flags & TOUCH_MOVE) {
		if (dragging_)
			down_ = bounds_.Contains(input.x, input.y);
	} 
	if (input.flags & TOUCH_UP) {
		if ((input.flags & TOUCH_CANCEL) == 0 && dragging_ && bounds_.Contains(input.x, input.y)) {
			Click();
		}	
		downCountDown_ = 0;
		down_ = false;	
		dragging_ = false;
	}
}

void Clickable::Update(const InputState &input_state) {
	OnClick.Update();
	if (!HasFocus())
		return;

	if (!enabled_) {
		down_ = false;
		return;
	}

	if (input_state.pad_buttons_down & PAD_BUTTON_A) {
		down_ = true;
	} else if (input_state.pad_buttons_up & PAD_BUTTON_A) {
		if (down_) {
			UI::EventParams e;
			e.v = this;
			OnClick.Trigger(e);
		}
		down_ = false;
	}
}

Item::Item(LayoutParams *layoutParams) : InertView(layoutParams) {
	layoutParams_->width = FILL_PARENT;
	layoutParams_->height = ITEM_HEIGHT;
}

void Item::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 0.0f;
	h = 0.0f;
}

void ClickableItem::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 0.0f;
	h = 0.0f;
}

ClickableItem::ClickableItem(LayoutParams *layoutParams) : Clickable(layoutParams) {
	if (layoutParams_->width == WRAP_CONTENT) 
		layoutParams_->width = FILL_PARENT;
	layoutParams_->height = ITEM_HEIGHT;
}

void ClickableItem::Draw(UIContext &dc) {
	Style style = dc.theme->itemStyle;
	if (down_) {
		style = dc.theme->itemDownStyle;
	} else if (HasFocus()) {
		style = dc.theme->itemFocusedStyle;
	}
	dc.FillRect(style.background, bounds_);
}

void Choice::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->MeasureText(dc.theme->uiFont, text_.c_str(), &w, &h);
}

void Choice::Draw(UIContext &dc) {
	ClickableItem::Draw(dc);

	int paddingX = 4;
	int paddingY = 4;
	dc.Draw()->DrawText(dc.theme->uiFont, text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), 0xFFFFFFFF, ALIGN_VCENTER);
	// dc.draw->DrawText(dc.theme->uiFontSmaller, text_.c_str(), paddingX, paddingY, 0xFFFFFFFF, ALIGN_TOPLEFT);
}

void InfoItem::Draw(UIContext &dc) {
	Item::Draw(dc);
	int paddingX = 4;
	int paddingY = 4;
	dc.Draw()->DrawText(dc.theme->uiFont, text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), 0xFFFFFFFF, ALIGN_VCENTER);
	dc.Draw()->DrawText(dc.theme->uiFont, text_.c_str(), bounds_.x2() - paddingX, bounds_.centerY(), 0xFFFFFFFF, ALIGN_VCENTER | ALIGN_RIGHT);
// 	dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y + 2, dc.theme->itemDownStyle.bgColor);
}

void ItemHeader::Draw(UIContext &dc) {
	dc.Draw()->DrawText(dc.theme->uiFontSmaller, text_.c_str(), bounds_.x + 4, bounds_.y, 0xFF707070, ALIGN_LEFT);
	dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y2()-2, bounds_.x2(), bounds_.y2(), 0xFFFFFFFF);
}

void CheckBox::Draw(UIContext &dc) {
	ClickableItem::Draw(dc);
	int paddingX = 4;
	int paddingY = 4;

	int image = *toggle_ ? dc.theme->checkOn : dc.theme->checkOff;

	dc.Draw()->DrawText(dc.theme->uiFont, text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), 0xFFFFFFFF, ALIGN_VCENTER);
	dc.Draw()->DrawImage(image, bounds_.x2() - 4, bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

void Button::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->MeasureText(dc.theme->uiFont, text_.c_str(), &w, &h);
}

void Button::Draw(UIContext &dc) {
	Style style = dc.theme->buttonStyle;
	if (HasFocus()) style = dc.theme->buttonFocusedStyle;
	if (down_) style = dc.theme->buttonDownStyle;
	if (!enabled_) style = dc.theme->buttonDisabledStyle;

	// dc.Draw()->DrawImage4Grid(style.image, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y2(), style.bgColor);
	dc.FillRect(style.background, bounds_);
	dc.Draw()->DrawText(dc.theme->uiFont, text_.c_str(), bounds_.centerX(), bounds_.centerY(), style.fgColor, ALIGN_CENTER);
}

void ImageView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &img = dc.Draw()->GetAtlas()->images[atlasImage_];
	// TODO: involve sizemode
	w = img.w;
	h = img.h;
}

void ImageView::Draw(UIContext &dc) {
	// TODO: involve sizemode
	dc.Draw()->DrawImage(atlasImage_, bounds_.x, bounds_.y, bounds_.w, bounds_.h, 0xFFFFFFFF);
}

void TextureView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// TODO: involve sizemode
	if (texture_) {
		w = (float)texture_->Width();
		h = (float)texture_->Height();
	} else {
		w = 16;
		h = 16;
	}
}

void TextureView::Draw(UIContext &dc) {
	// TODO: involve sizemode
	if (texture_) {
		dc.Flush();
		texture_->Bind(0);
		dc.Draw()->Rect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, color_);
		dc.Flush();
		dc.RebindTexture();
	}
}

void TextView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->SetFontScale(textScaleX_, textScaleY_);
	dc.Draw()->MeasureText(dc.theme->uiFont, text_.c_str(), &w, &h);
	dc.Draw()->SetFontScale(1.0f, 1.0f);
}

void TextView::Draw(UIContext &dc) {
	dc.Draw()->SetFontScale(textScaleX_, textScaleY_);
	dc.Draw()->DrawTextRect(dc.theme->uiFont, text_.c_str(), bounds_.x, bounds_.y, bounds_.w, bounds_.h, 0xFFFFFFFF, textAlign_);
	dc.Draw()->SetFontScale(1.0f, 1.0f);
}

void ProgressBar::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->MeasureText(dc.theme->uiFont, "  100%  ", &w, &h);
}

void ProgressBar::Draw(UIContext &dc) {
	char temp[32];
	sprintf(temp, "%i%%", (int)(progress_ * 100.0f));
	dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y, bounds_.x + bounds_.w * progress_, bounds_.y2(), 0xc0c0c0c0);
	dc.Draw()->DrawTextRect(dc.theme->uiFont, temp, bounds_.x, bounds_.y, bounds_.w, bounds_.h, 0xFFFFFFFF, ALIGN_CENTER);
}

void TriggerButton::Touch(const TouchInput &input) {
	if (input.flags & TOUCH_DOWN) {
		if (bounds_.Contains(input.x, input.y)) {
			down_ |= 1 << input.id;
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (bounds_.Contains(input.x, input.y))
			down_ |= 1 << input.id;
		else
			down_ &= ~(1 << input.id);
	}

	if (input.flags & TOUCH_UP) {
		down_ &= ~(1 << input.id);
	}

	if (down_ != 0) {
		*bitField_ |= bit_;
	} else {
		*bitField_ &= ~bit_;
	}
}

void TriggerButton::Draw(UIContext &dc) {
	dc.Draw()->DrawImage(imageBackground_, bounds_.centerX(), bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
	dc.Draw()->DrawImage(imageForeground_, bounds_.centerX(), bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
}

void TriggerButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &image = dc.Draw()->GetAtlas()->images[imageBackground_];
	w = image.w;
	h = image.h;
}


/*
TabStrip::TabStrip()
	: selected_(0) {
}

void TabStrip::Touch(const TouchInput &touch) {
	int tabw = bounds_.w / tabs_.size();
	int h = 20;
	if (touch.flags & TOUCH_DOWN) {
		for (int i = 0; i < MAX_POINTERS; i++) {
			if (UIRegionHit(i, bounds_.x, bounds_.y, bounds_.w, h, 8)) {
				selected_ = (touch.x - bounds_.x) / tabw;
			}
		}
		if (selected_ < 0) selected_ = 0;
		if (selected_ >= (int)tabs_.size()) selected_ = (int)tabs_.size() - 1;
	}
}

void TabStrip::Draw(DrawContext &dc) {
	int tabw = bounds_.w / tabs_.size();
	int h = 20;
	for (int i = 0; i < numTabs; i++) {
		dc.draw->DrawImageStretch(WHITE, x + 1, y + 2, x + tabw - 1, y + h, 0xFF202020);
		dc.draw->DrawText(font, tabs_[i].title.c_str(), x + tabw/2, y + h/2, 0xFFFFFFFF, ALIGN_VCENTER | ALIGN_HCENTER);
		if (selected_ == i) {
			float tw, th;
			ui_draw2d.MeasureText(font, names[i], &tw, &th);
			// TODO: better image
			ui_draw2d.DrawImageStretch(WHITE, x + 1, y + h - 6, x + tabw - 1, y + h, tabColors ? tabColors[i] : 0xFFFFFFFF);
		} else {
			ui_draw2d.DrawImageStretch(WHITE, x + 1, y + h - 1, x + tabw - 1, y + h, tabColors ? tabColors[i] : 0xFFFFFFFF);
		}
		x += tabw;
	}
}*/

void Fill(UIContext &dc, const Bounds &bounds, const Drawable &drawable) {
	if (drawable.type == DRAW_SOLID_COLOR) {
		
	}
}

}  // namespace
