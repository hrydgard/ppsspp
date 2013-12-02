#include <queue>
#include <algorithm>
#include "base/display.h"
#include "base/mutex.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx/texture.h"
#include "gfx/texture_atlas.h"
#include "ui/ui.h"
#include "ui/view.h"
#include "ui/ui_context.h"

namespace UI {

static View *focusedView;
static bool focusMovementEnabled;

static recursive_mutex mutex_;

const float ITEM_HEIGHT = 64.f;


struct DispatchQueueItem {
	Event *e;
	EventParams params;
};

std::deque<DispatchQueueItem> g_dispatchQueue;


void EventTriggered(Event *e, EventParams params) {
	lock_guard guard(mutex_);

	DispatchQueueItem item;
	item.e = e;
	item.params = params;
	g_dispatchQueue.push_front(item);
}

void DispatchEvents() {
	lock_guard guard(mutex_);

	while (!g_dispatchQueue.empty()) {
		DispatchQueueItem item = g_dispatchQueue.back();
		g_dispatchQueue.pop_back();
		if (item.e) {
			item.e->Dispatch(item.params);
		}
	}
}

void RemoveQueuedEvents(View *v) {
	for (size_t i = 0; i < g_dispatchQueue.size(); i++) {
		if (g_dispatchQueue[i].params.v == v)
			g_dispatchQueue.erase(g_dispatchQueue.begin() + i);
	}
}

View *GetFocusedView() {
	return focusedView;
}

void SetFocusedView(View *view) {
	if (focusedView) {
		focusedView->FocusChanged(FF_LOSTFOCUS);
	}
	focusedView = view;
	if (focusedView) {
		focusedView->FocusChanged(FF_GOTFOCUS);
	}
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
	EventTriggered(this, e);	
}

// Call this from UI thread
EventReturn Event::Dispatch(EventParams &e) {
	bool eventHandled = false;
	for (auto iter = handlers_.begin(); iter != handlers_.end(); ++iter) {
		if ((iter->func)(e) == UI::EVENT_DONE) {
			// Event is handled, stop looping immediately. This event might even have gotten deleted.
			return UI::EVENT_DONE;
		}
	}
	return UI::EVENT_SKIPPED;
}

View::~View() {
	if (HasFocus())
		SetFocusedView(0);
	RemoveQueuedEvents(this);
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

Point View::GetFocusPosition(FocusDirection dir) {
	// The +2/-2 is some extra fudge factor to cover for views sitting right next to each other.
	// Distance zero yields strange results otherwise.
	switch (dir) {
	case FOCUS_LEFT: return Point(bounds_.x + 2, bounds_.centerY());
	case FOCUS_RIGHT: return Point(bounds_.x2() - 2, bounds_.centerY());
	case FOCUS_UP: return Point(bounds_.centerX(), bounds_.y + 2);
	case FOCUS_DOWN: return Point(bounds_.centerX(), bounds_.y2() - 2);

	default:
		return bounds_.Center();
	}
}


void Clickable::Click() {
	UI::EventParams e;
	e.v = this;
	OnClick.Trigger(e);
};

void Clickable::FocusChanged(int focusFlags) {
	if (focusFlags & FF_LOSTFOCUS) {
		down_ = false;
		dragging_ = false;
	}
}

void Clickable::Touch(const TouchInput &input) {
	if (!IsEnabled()) {
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
		down_ = false;
		downCountDown_ = 0;
		dragging_ = false;
	}
}

// TODO: O/X confirm preference for xperia play?

bool IsAcceptKeyCode(int keyCode) {
	return std::find(confirmKeys.begin(), confirmKeys.end(), (keycode_t)keyCode) != confirmKeys.end();
}

bool IsEscapeKeyCode(int keyCode) {
	return std::find(cancelKeys.begin(), cancelKeys.end(), (keycode_t)keyCode) != cancelKeys.end();
}

void Clickable::Key(const KeyInput &key) {
	if (!HasFocus() && key.deviceId != DEVICE_ID_MOUSE) {
		down_ = false;
		return;
	}
	// TODO: Replace most of Update with this.
	if (key.flags & KEY_DOWN) {
		if (IsAcceptKeyCode(key.keyCode)) {
			down_ = true;
		}
	}
	if (key.flags & KEY_UP) {
		if (IsAcceptKeyCode(key.keyCode)) {
			if (down_) {
				Click();
				down_ = false;
			}
		} else if (IsEscapeKeyCode(key.keyCode)) {
			down_ = false;
		}
	}
}

void StickyChoice::Touch(const TouchInput &input) {
	dragging_ = false;
	if (!IsEnabled()) {
		down_ = false;
		return;
	}

	if (input.flags & TOUCH_DOWN) {
		if (bounds_.Contains(input.x, input.y)) {
			if (IsFocusMovementEnabled())
				SetFocusedView(this);
			down_ = true;
			Click();
		}
	}
}

void StickyChoice::Key(const KeyInput &key) {
	if (!HasFocus()) {
		return;
	}

	// TODO: Replace most of Update with this.
	if (key.flags & KEY_DOWN) {
		if (IsAcceptKeyCode(key.keyCode)) {
			down_ = true;
			Click();
		}
	}
}

void StickyChoice::FocusChanged(int focusFlags) {
	// Override Clickable's FocusChanged to do nothing.
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
	Style style =	dc.theme->itemStyle;

	if (HasFocus()) {
		style = dc.theme->itemFocusedStyle;
	}
	if (down_) {
		style = dc.theme->itemDownStyle;
	}

	dc.FillRect(style.background, bounds_);
}

void Choice::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	if (atlasImage_ != -1) {
		const AtlasImage &img = dc.Draw()->GetAtlas()->images[atlasImage_];
		w = img.w;
		h = img.h;
	} else {
		dc.MeasureText(dc.theme->uiFont, text_.c_str(), &w, &h);
	}
	w += 24;
	h += 16;
}

void Choice::HighlightChanged(bool highlighted){
	highlighted_ = highlighted;
}

void Choice::Draw(UIContext &dc) {
	if (!IsSticky()) {
		ClickableItem::Draw(dc);
	} else {
		Style style =	dc.theme->itemStyle;
		if (highlighted_) {
			style = dc.theme->itemHighlightedStyle;
		}
		if (down_) {
			style = dc.theme->itemDownStyle;
		}
		if (HasFocus()) {
			style = dc.theme->itemFocusedStyle;
		}
		dc.FillRect(style.background, bounds_);
	}

	Style style = dc.theme->itemStyle;
	if (!IsEnabled())
		style = dc.theme->itemDisabledStyle;

	if (atlasImage_ != -1) {
		dc.Draw()->DrawImage(atlasImage_, bounds_.centerX(), bounds_.centerY(), 1.0f, style.fgColor, ALIGN_CENTER);
	} else {
		int paddingX = 12;
		dc.SetFontStyle(dc.theme->uiFont);
		if (centered_) {
			dc.DrawText(text_.c_str(), bounds_.centerX(), bounds_.centerY(), style.fgColor, ALIGN_CENTER);
		} else {
			if (iconImage_ != -1) {
				dc.Draw()->DrawImage(iconImage_, bounds_.x2() - 32 - paddingX, bounds_.centerY(), 0.5f, style.fgColor, ALIGN_CENTER);
			}
			dc.DrawText(text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
		}
	}

	if (selected_) {
		dc.Draw()->DrawImage(dc.theme->checkOn, bounds_.x2() - 40, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_CENTER);
	}
}

void InfoItem::Draw(UIContext &dc) {
	Item::Draw(dc);
	int paddingX = 12;
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), 0xFFFFFFFF, ALIGN_VCENTER);
	dc.DrawText(rightText_.c_str(), bounds_.x2() - paddingX, bounds_.centerY(), 0xFFFFFFFF, ALIGN_VCENTER | ALIGN_RIGHT);
// 	dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y + 2, dc.theme->itemDownStyle.bgColor);
}

ItemHeader::ItemHeader(const std::string &text, LayoutParams *layoutParams)
	: Item(layoutParams), text_(text) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = 40;
}

void ItemHeader::Draw(UIContext &dc) {
	dc.SetFontStyle(dc.theme->uiFontSmall);
	dc.DrawText(text_.c_str(), bounds_.x + 4, bounds_.centerY(), 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);
	dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y2()-2, bounds_.x2(), bounds_.y2(), 0xFFFFFFFF);
}

void PopupHeader::Draw(UIContext &dc) {
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(text_.c_str(), bounds_.x + 12, bounds_.centerY(), dc.theme->popupTitle.fgColor, ALIGN_LEFT | ALIGN_VCENTER);
	dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y2()-2, bounds_.x2(), bounds_.y2(), dc.theme->popupTitle.fgColor);
}

void CheckBox::Toggle(){
	if (toggle_)
		*toggle_ = !(*toggle_);
};

EventReturn CheckBox::OnClicked(EventParams &e) {
	Toggle();
	return EVENT_CONTINUE;  // It's safe to keep processing events.
}

void CheckBox::Draw(UIContext &dc) {
	ClickableItem::Draw(dc);
	int paddingX = 12;
	int paddingY = 8;

	int image = *toggle_ ? dc.theme->checkOn : dc.theme->checkOff;

	Style style = dc.theme->itemStyle;
	if (!IsEnabled())
		style = dc.theme->itemDisabledStyle;

	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
	dc.Draw()->DrawImage(image, bounds_.x2() - paddingX, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}

void Button::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.MeasureText(dc.theme->uiFont, text_.c_str(), &w, &h);
	// Add some internal padding to not look totally ugly
	w += 16;
	h += 8;
}

void Button::Draw(UIContext &dc) {
	Style style = dc.theme->buttonStyle;

	if (HasFocus()) style = dc.theme->buttonFocusedStyle;
	if (down_) style = dc.theme->buttonDownStyle;
	if (!IsEnabled()) style = dc.theme->buttonDisabledStyle;

	// dc.Draw()->DrawImage4Grid(style.image, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y2(), style.bgColor);
	dc.FillRect(style.background, bounds_);
	float tw, th;
	dc.MeasureText(dc.theme->uiFont, text_.c_str(), &tw, &th);
	if (tw > bounds_.w) {
		dc.PushScissor(bounds_);
	}
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(text_.c_str(), bounds_.centerX(), bounds_.centerY(), style.fgColor, ALIGN_CENTER);
	if (tw > bounds_.w) {
		dc.PopScissor();
	}
}

void ImageView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &img = dc.Draw()->GetAtlas()->images[atlasImage_];
	// TODO: involve sizemode
	w = img.w;
	h = img.h;
}

void ImageView::Draw(UIContext &dc) {
	const AtlasImage &img = dc.Draw()->GetAtlas()->images[atlasImage_];
	// TODO: involve sizemode
	float scale = bounds_.w / img.w;
	dc.Draw()->DrawImage(atlasImage_, bounds_.x, bounds_.y, scale, 0xFFFFFFFF, ALIGN_TOPLEFT);
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

ImageFileView::ImageFileView(std::string filename, ImageSizeMode sizeMode, LayoutParams *layoutParams)
	: InertView(layoutParams), color_(0xFFFFFFFF), sizeMode_(sizeMode) {
	texture_ = new Texture();
	if (!texture_->Load(filename.c_str())) {
		ELOG("Failed to load texture %s", filename.c_str());
	}
}

ImageFileView::~ImageFileView() {
	delete texture_;
}

void ImageFileView::Draw(UIContext &dc) {
	// TODO: involve sizemode
	if (texture_) {
		dc.Flush();
		texture_->Bind(0);
		dc.Draw()->Rect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, color_);
		dc.Flush();
		dc.RebindTexture();
	}
}

void ImageFileView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// TODO: involve sizemode
	if (texture_) {
		w = (float)texture_->Width();
		h = (float)texture_->Height();
	} else {
		w = 16;
		h = 16;
	}
}

void TextView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.MeasureText(small_ ? dc.theme->uiFontSmall : dc.theme->uiFont, text_.c_str(), &w, &h);
}

void TextView::Draw(UIContext &dc) {
	dc.SetFontStyle(small_ ? dc.theme->uiFontSmall : dc.theme->uiFont);
	dc.DrawTextRect(text_.c_str(), bounds_, 0xFFFFFFFF, textAlign_);
}

void ProgressBar::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.MeasureText(dc.theme->uiFont, "  100%  ", &w, &h);
}

void ProgressBar::Draw(UIContext &dc) {
	char temp[32];
	sprintf(temp, "%i%%", (int)(progress_ * 100.0f));
	dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y, bounds_.x + bounds_.w * progress_, bounds_.y2(), 0xc0c0c0c0);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawTextRect(temp, bounds_, 0xFFFFFFFF, ALIGN_CENTER);
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

void Slider::Key(const KeyInput &input) {
	if (HasFocus() && input.flags & KEY_DOWN) {
		switch (input.keyCode) {
		case NKCODE_DPAD_LEFT:
		case NKCODE_MINUS:
		case NKCODE_NUMPAD_SUBTRACT:
			*value_ -= 1;
			break;
		case NKCODE_DPAD_RIGHT:
		case NKCODE_PLUS:
		case NKCODE_NUMPAD_ADD:
			*value_ += 1;
			break;
		}
		Clamp();
	}
}

void Slider::Touch(const TouchInput &input) {
	if (dragging_ || bounds_.Contains(input.x, input.y)) {
		float relativeX = (input.x - (bounds_.x + paddingLeft_)) / (bounds_.w - paddingLeft_ - paddingRight_);
		*value_ = floorf(relativeX * (maxValue_ - minValue_) + minValue_ + 0.5f);
		Clamp();
	}
}

void Slider::Clamp() {
	if (*value_ < minValue_) *value_ = minValue_;
	else if (*value_ > maxValue_) *value_ = maxValue_;
}

void Slider::Draw(UIContext &dc) {
	bool focus = HasFocus();
	float knobX = ((float)(*value_) - minValue_) / (maxValue_ - minValue_) * (bounds_.w - paddingLeft_ - paddingRight_) + (bounds_.x + paddingLeft_);
	dc.FillRect(Drawable(focus ? dc.theme->popupTitle.fgColor : 0xFFFFFFFF), Bounds(bounds_.x + paddingLeft_, bounds_.centerY() - 2, knobX - (bounds_.x + paddingLeft_), 4));
	dc.FillRect(Drawable(0xFF808080), Bounds(knobX, bounds_.centerY() - 2, (bounds_.x + bounds_.w - paddingRight_ - knobX), 4));
	dc.Draw()->DrawImage(dc.theme->sliderKnob, knobX, bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
	char temp[64];
	if (showPercent_)
		sprintf(temp, "%i%%", *value_);
	else
		sprintf(temp, "%i", *value_);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(temp, bounds_.x2() - 22, bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER);
}

void Slider::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// TODO
	w = 100;
	h = 50;
}

void SliderFloat::Key(const KeyInput &input) {
	if (HasFocus() && input.flags & KEY_DOWN) {
		switch (input.keyCode) {
		case NKCODE_DPAD_LEFT:
		case NKCODE_MINUS:
		case NKCODE_NUMPAD_SUBTRACT:
			*value_ -= (maxValue_ - minValue_) / 20.0f;
			break;
		case NKCODE_DPAD_RIGHT:
		case NKCODE_PLUS:
		case NKCODE_NUMPAD_ADD:
			*value_ += (maxValue_ - minValue_) / 20.0f;
			break;
		}
		Clamp();
	}
}

void SliderFloat::Touch(const TouchInput &input) {
	if (dragging_ || bounds_.Contains(input.x, input.y)) {
		float relativeX = (input.x - (bounds_.x + paddingLeft_)) / (bounds_.w - paddingLeft_ - paddingRight_);
		*value_ = (relativeX * (maxValue_ - minValue_) + minValue_);
		Clamp();
	}
}

void SliderFloat::Clamp() {
	if (*value_ < minValue_) *value_ = minValue_;
	else if (*value_ > maxValue_) *value_ = maxValue_;
}

void SliderFloat::Draw(UIContext &dc) {
	bool focus = HasFocus();
	float knobX = (*value_ - minValue_) / (maxValue_ - minValue_) * (bounds_.w - paddingLeft_ - paddingRight_) + (bounds_.x + paddingLeft_);
	dc.FillRect(Drawable(focus ? dc.theme->popupTitle.fgColor : 0xFFFFFFFF), Bounds(bounds_.x + paddingLeft_, bounds_.centerY() - 2, knobX - (bounds_.x + paddingLeft_), 4));
	dc.FillRect(Drawable(0xFF808080), Bounds(knobX, bounds_.centerY() - 2, (bounds_.x + bounds_.w - paddingRight_ - knobX), 4));
	dc.Draw()->DrawImage(dc.theme->sliderKnob, knobX, bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
	char temp[64];
	sprintf(temp, "%0.2f", *value_);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(temp, bounds_.x2() - 22, bounds_.centerY(), 0xFFFFFFFF, ALIGN_CENTER);
}

void SliderFloat::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// TODO
	w = 100;
	h = 50;
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
