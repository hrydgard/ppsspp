#include <algorithm>
#include <mutex>

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/UI/Tween.h"
#include "Common/UI/Root.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"

namespace UI {

static constexpr float MIN_TEXT_SCALE = 0.8f;
static constexpr float MAX_ITEM_SIZE = 65535.0f;

void MeasureBySpec(Size sz, float contentDim, MeasureSpec spec, float *measured) {
	if (sz == WRAP_CONTENT) {
		if (spec.type == UNSPECIFIED)
			*measured = contentDim;
		else if (spec.type == AT_MOST)
			*measured = contentDim < spec.size ? contentDim : spec.size;
		else if (spec.type == EXACTLY)
			*measured = spec.size;
	} else if (sz == FILL_PARENT) {
		// UNSPECIFIED may have a minimum size of the parent.  Let's use it to fill.
		if (spec.type == UNSPECIFIED)
			*measured = std::max(spec.size, contentDim);
		else
			*measured = spec.size;
	} else if (spec.type == EXACTLY || (spec.type == AT_MOST && *measured > spec.size)) {
		*measured = spec.size;
	} else {
		*measured = sz;
	}
}

void ApplyBoundBySpec(float &bound, MeasureSpec spec) {
	switch (spec.type) {
	case AT_MOST:
		bound = bound < spec.size ? bound : spec.size;
		break;
	case EXACTLY:
		bound = spec.size;
		break;
	case UNSPECIFIED:
		break;
	}
}

void ApplyBoundsBySpec(Bounds &bounds, MeasureSpec horiz, MeasureSpec vert) {
	ApplyBoundBySpec(bounds.w, horiz);
	ApplyBoundBySpec(bounds.h, vert);
}

void Event::Add(std::function<void(EventParams&)> func) {
	_dbg_assert_(!func_);
	func_ = func;
}

void Event::Trigger(EventParams &e) {
	if (!func_) {
		return;
	}
	EventTriggered(this, e);
}

void Event::Dispatch(EventParams &e) {
	if (func_)
		func_(e);
}

Event::~Event() {
	RemoveQueuedEventsByEvent(this);
}

View::~View() {
	if (HasFocus())
		SetFocusedView(0);
	RemoveQueuedEventsByView(this);

	// Could use unique_ptr, but then we have to include tween everywhere.
	for (UI::Tween *tween : tweens_)
		delete tween;
}

void View::Update() {
	for (size_t i = 0; i < tweens_.size(); ++i) {
		Tween *tween = tweens_[i];
		if (!tween->Finished()) {
			tween->Apply(this);
		} else if (!tween->Persists()) {
			tweens_.erase(tweens_.begin() + i);
			i--;
			delete tween;
		}
	}
}

void View::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	float contentW = 0.0f, contentH = 0.0f;
	GetContentDimensionsBySpec(dc, horiz, vert, contentW, contentH);
	MeasureBySpec(layoutParams_->width, contentW, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, contentH, vert, &measuredHeight_);
}

void View::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// Default values
	w = 10.0f;
	h = 10.0f;
}

void View::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	GetContentDimensions(dc, w, h);
}

void View::Query(float x, float y, std::vector<View *> &list) {
	if (bounds_.Contains(x, y)) {
		list.push_back(this);
	}
}

std::string View::DescribeLog() const {
	return StringFromFormat("%0.1f,%0.1f %0.1fx%0.1f", bounds_.x, bounds_.y, bounds_.w, bounds_.h);
}

void View::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	// Remember if this view was a focused view.
	std::string tag = Tag();
	if (tag.empty()) {
		tag = anonId;
	}

	const std::string focusedKey = "ViewFocused::" + tag;
	switch (status) {
	case UI::PERSIST_SAVE:
		if (HasFocus()) {
			storage[focusedKey].resize(1);
		}
		break;
	case UI::PERSIST_RESTORE:
		if (storage.find(focusedKey) != storage.end()) {
			SetFocus();
		}
		break;
	}

	for (int i = 0; i < (int)tweens_.size(); ++i) {
		tweens_[i]->PersistData(status, tag + "/" + StringFromInt(i), storage);
	}
}

Point2D View::GetFocusPosition(FocusDirection dir) const {
	// The +2/-2 is some extra fudge factor to cover for views sitting right next to each other.
	// Distance zero yields strange results otherwise.
	switch (dir) {
	case FOCUS_LEFT: return Point2D(bounds_.x + 2, bounds_.centerY());
	case FOCUS_RIGHT: return Point2D(bounds_.x2() - 2, bounds_.centerY());
	case FOCUS_UP: return Point2D(bounds_.centerX(), bounds_.y + 2);
	case FOCUS_DOWN: return Point2D(bounds_.centerX(), bounds_.y2() - 2);

	default:
		return bounds_.Center();
	}
}

Point2D CollapsibleHeader::GetFocusPosition(FocusDirection dir) const {
	// Bias the focus position to the left.
	switch (dir) {
	case FOCUS_UP: return Point2D(bounds_.x + 50, bounds_.y + 2);
	case FOCUS_DOWN: return Point2D(bounds_.x + 50, bounds_.y2() - 2);
	default:
		return View::GetFocusPosition(dir);
	}
}

bool View::SetFocus() {
	if (IsFocusMovementEnabled()) {
		if (CanBeFocused()) {
			SetFocusedView(this);
			return true;
		}
	}
	return false;
}

Clickable::Clickable(LayoutParams *layoutParams)
	: View(layoutParams) {
	// We set the colors later once we have a UIContext.
	bgColor_ = AddTween(new CallbackColorTween(0.1f));
	bgColor_->Persist();
}

void Clickable::DrawBG(UIContext &dc, const Style &style) {
	if (style.background.type == DRAW_SOLID_COLOR) {
		if (time_now_d() - bgColorLast_ >= 0.25f) {
			bgColor_->Reset(style.background.color);
		} else if (bgColor_->CurrentValue() != style.background.color) {
			bgColor_->Divert(style.background.color, down_ ? 0.05f : 0.1f);
		}
		bgColorLast_ = time_now_d();

		dc.FillRect(Drawable(bgColor_->CurrentValue()), bounds_);
	} else {
		dc.FillRect(style.background, bounds_);
	}
}

void Clickable::ClickInternal() {
	UI::EventParams e{};
	e.v = this;
	if (hasAutoResult_) {
		e.bubbleResult = autoResult_;
	}
	OnClick.Trigger(e);
};

void Clickable::FocusChanged(int focusFlags) {
	if (focusFlags & FF_LOSTFOCUS) {
		down_ = false;
		dragging_ = false;
	}
}

bool Clickable::Touch(const TouchInput &input) {
	bool contains = bounds_.Contains(input.x, input.y);

	if (!IsEnabled()) {
		dragging_ = false;
		down_ = false;
		return contains;
	}

	// Ignore buttons other than the left one.
	if ((input.flags & TouchInputFlags::MOUSE) && (input.buttons & 1) == 0) {
		return contains;
	}

	if (input.flags & TouchInputFlags::DOWN) {
		if (bounds_.Contains(input.x, input.y)) {
			if (IsFocusMovementEnabled())
				SetFocusedView(this);
			dragging_ = true;
			down_ = true;
		} else {
			down_ = false;
			dragging_ = false;
		}
	} else if (input.flags & TouchInputFlags::MOVE) {
		if (dragging_)
			down_ = bounds_.Contains(input.x, input.y);
	}
	if (input.flags & TouchInputFlags::UP) {
		if ((input.flags & TouchInputFlags::CANCEL) == 0 && dragging_ && bounds_.Contains(input.x, input.y)) {
			ClickInternal();
		}
		down_ = false;
		downCountDown_ = 0;
		dragging_ = false;
	}
	return contains;
}

static bool MatchesKeyDef(const std::vector<InputMapping> &defs, const KeyInput &key) {
	// In addition to the actual search, we need to do another search where we replace the device ID with "ANY".
	return
		std::find(defs.begin(), defs.end(), InputMapping(key.deviceId, key.keyCode)) != defs.end() ||
		std::find(defs.begin(), defs.end(), InputMapping(DEVICE_ID_ANY, key.keyCode)) != defs.end();
}

// TODO: O/X confirm preference for xperia play?

bool IsDPadKey(const KeyInput &key) {
	if (dpadKeys.empty()) {
		return key.keyCode >= NKCODE_DPAD_UP && key.keyCode <= NKCODE_DPAD_RIGHT;
	} else {
		return MatchesKeyDef(dpadKeys, key);
	}
}

bool IsAcceptKey(const KeyInput &key) {
	if (confirmKeys.empty()) {
		// This path is pretty much not used, confirmKeys should be set.
		// TODO: Get rid of this stuff?
		if (key.deviceId == DEVICE_ID_KEYBOARD) {
			return key.keyCode == NKCODE_SPACE || key.keyCode == NKCODE_ENTER || key.keyCode == NKCODE_Z || key.keyCode == NKCODE_NUMPAD_ENTER;
		} else {
			return key.keyCode == NKCODE_BUTTON_A || key.keyCode == NKCODE_BUTTON_CROSS || key.keyCode == NKCODE_BUTTON_1 || key.keyCode == NKCODE_DPAD_CENTER;
		}
	} else {
		return MatchesKeyDef(confirmKeys, key);
	}
}

bool IsEscapeKey(const KeyInput &key) {
	if (cancelKeys.empty()) {
		// This path is pretty much not used, cancelKeys should be set.
		// TODO: Get rid of this stuff?
		if (key.deviceId == DEVICE_ID_KEYBOARD) {
			return key.keyCode == NKCODE_ESCAPE || key.keyCode == NKCODE_BACK;
		} else {
			return key.keyCode == NKCODE_BUTTON_CIRCLE || key.keyCode == NKCODE_BUTTON_B || key.keyCode == NKCODE_BUTTON_2;
		}
	} else {
		return MatchesKeyDef(cancelKeys, key);
	}
}

// Corresponds to Triangle
bool IsInfoKey(const KeyInput &key) {
	if (infoKeys.empty()) {
		// This path is pretty much not used, infoKeys should be set.
		// TODO: Get rid of this stuff?
		if (key.deviceId == DEVICE_ID_KEYBOARD) {
			return key.keyCode == NKCODE_S || key.keyCode == NKCODE_NUMPAD_ADD;
		} else {
			return key.keyCode == NKCODE_BUTTON_Y || key.keyCode == NKCODE_BUTTON_3;
		}
	} else {
		return MatchesKeyDef(infoKeys, key);
	}
}

bool IsTabLeftKey(const KeyInput &key) {
	if (tabLeftKeys.empty()) {
		// This path is pretty much not used, tabLeftKeys should be set.
		// TODO: Get rid of this stuff?
		return key.keyCode == NKCODE_BUTTON_L1;
	} else {
		return MatchesKeyDef(tabLeftKeys, key);
	}
}

bool IsTabRightKey(const KeyInput &key) {
	if (tabRightKeys.empty()) {
		// This path is pretty much not used, tabRightKeys should be set.
		// TODO: Get rid of this stuff?
		return key.keyCode == NKCODE_BUTTON_R1;
	} else {
		return MatchesKeyDef(tabRightKeys, key);
	}
}

bool Clickable::Key(const KeyInput &key) {
	if (!HasFocus() && key.deviceId != DEVICE_ID_MOUSE) {
		down_ = false;
		return false;
	}
	// TODO: Replace most of Update with this.

	bool ret = false;
	if (key.flags & KeyInputFlags::DOWN) {
		if (IsAcceptKey(key)) {
			down_ = true;
			ret = true;
		}
	}
	if (key.flags & KeyInputFlags::UP) {
		if (IsAcceptKey(key)) {
			if (down_) {
				ClickInternal();
				down_ = false;
				ret = true;
			}
		} else if (down_ && IsEscapeKey(key)) {
			down_ = false;
		}
	}
	return ret;
}

bool StickyChoice::Touch(const TouchInput &touch) {
	bool contains = bounds_.Contains(touch.x, touch.y);
	dragging_ = false;
	if (!IsEnabled()) {
		down_ = false;
		return contains;
	}

	if (touch.flags & TouchInputFlags::DOWN) {
		if (contains) {
			if (IsFocusMovementEnabled())
				SetFocusedView(this);
			down_ = true;
			ClickInternal();
			return true;
		}
	}
	return false;
}

bool StickyChoice::Key(const KeyInput &key) {
	if (!HasFocus()) {
		return false;
	}

	// TODO: Replace most of Update with this.
	if (key.flags & KeyInputFlags::DOWN) {
		if (IsAcceptKey(key)) {
			down_ = true;
			UI::PlayUISound(UI::UISound::TOGGLE_ON);
			ClickInternal();
			return true;
		}
	}
	return false;
}

void StickyChoice::FocusChanged(int focusFlags) {
	// Override Clickable's FocusChanged to do nothing.
}

Item::Item(LayoutParams *layoutParams) : InertView(layoutParams) {
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
}

void Item::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 0.0f;
	h = 0.0f;
}

void ClickableItem::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 0.0f;
	h = ITEM_HEIGHT;
}

ClickableItem::ClickableItem(LayoutParams *layoutParams) : Clickable(layoutParams) {
	if (!layoutParams) {
		// The default LayoutParams assigned by View::View defaults to WRAP_CONTENT/WRAP_CONTENT.
		if (layoutParams_->width == WRAP_CONTENT)
			layoutParams_->width = FILL_PARENT;
	}
}

void ClickableItem::Draw(UIContext &dc) {
	Style style = dc.GetTheme().itemStyle;

	if (!IsEnabled()) {
		style = dc.GetTheme().itemDisabledStyle;
	}
	if (HasFocus()) {
		style = dc.GetTheme().itemFocusedStyle;
	}
	if (down_) {
		style = dc.GetTheme().itemDownStyle;
	}

	DrawBG(dc, style);
}

void Choice::ClickInternal() {
	ClickableItem::ClickInternal();
	UI::PlayUISound(UI::UISound::CONFIRM);
}

void Choice::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	float totalW = 0.0f;
	float totalH = 0.0f;
	if (!text_.empty() && !hideTitle_) {
		int paddingLeft = 12;
		int paddingRight = 12;
		if (rightIconImage_.isValid()) {
			float imgW, imgH;
			dc.Draw()->GetAtlas()->measureImage(rightIconImage_, &imgW, &imgH);
			paddingRight = std::max(ITEM_HEIGHT, imgW);
		}

		float availWidth = horiz.size - paddingLeft - paddingRight - textPadding_.horiz() - totalW;
		if (availWidth < 0.0f) {
			// Let it have as much space as it needs.
			availWidth = MAX_ITEM_SIZE;
		}
		if (horiz.type != EXACTLY && layoutParams_->width > 0.0f && availWidth > layoutParams_->width)
			availWidth = layoutParams_->width;
		float scale = dc.CalculateTextScale(text_, availWidth);
		float textW = 0.0f, textH = 0.0f;
		dc.MeasureTextRect(dc.GetTheme().uiFont, scale, scale, text_, availWidth, &textW, &textH, FLAG_WRAP_TEXT);
		totalH = std::max(totalH, textH);
		totalW += textW;
		if (image_.isValid()) {
			totalW += 12;
			totalW += totalH;
		}
	} else {
		if (image_.isValid()) {
			dc.Draw()->GetAtlas()->measureImage(image_, &w, &h);
			totalW = w * imgScale_ + 6;
			totalH = h * imgScale_;
		}
	}

	w = totalW + (text_.empty() ? 16 : 24);
	h = totalH + 16;
	h = std::max(h, ITEM_HEIGHT);
}

void Choice::Draw(UIContext &dc) {
	Style style = dc.GetTheme().itemStyle;
	if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

	DrawBG(dc, style);

	if (image_.isValid() && text_.empty()) {
		dc.Draw()->DrawImageRotated(image_, bounds_.centerX(), bounds_.centerY(), imgScale_, imgRot_, style.fgColor, imgFlipH_);
		if (shine_) {
			Bounds b = Bounds::FromCenter(bounds_.centerX(), bounds_.centerY(), bounds_.h * 0.4f);
			DrawIconShine(dc, b.Inset(5.0f, 5.0f), 0.65f, false);
		}
	} else if (!text_.empty() && !hideTitle_) {
		dc.SetFontStyle(dc.GetTheme().uiFont);

		int paddingLeft = 12;
		int paddingRight = 12;

		if (image_.isValid()) {
			paddingLeft = 0;
			const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(image_);
			if (image) {
				_dbg_assert_(image);
				// TODO: Use scale rotation and flip here as well (DrawImageRotated is always ALIGN_CENTER for now)
				dc.Draw()->DrawImage(image_, bounds_.x + bounds_.h * 0.5f + paddingLeft, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_CENTER);

				paddingLeft += bounds_.h;
			}
		}

		if (rightIconImage_.isValid()) {
			float imgW, imgH;
			dc.Draw()->GetAtlas()->measureImage(rightIconImage_, &imgW, &imgH);
			paddingRight = std::max(ITEM_HEIGHT, imgW);
		}

		float availWidth = bounds_.w - (paddingLeft + paddingRight + textPadding_.horiz());

		if (centered_) {
			dc.DrawTextRectSqueeze(text_, bounds_, style.fgColor, ALIGN_CENTER | FLAG_WRAP_TEXT | drawTextFlags_);
		} else {
			if (rightIconImage_.isValid()) {
				uint32_t col = rightIconKeepColor_ ? 0xffffffff : style.fgColor; // Don't apply theme to gold icon

				float iconX = bounds_.x2() - paddingRight + ITEM_HEIGHT * 0.5f;

				if (shine_) {
					Bounds b = Bounds::FromCenter(iconX, bounds_.centerY(), bounds_.h * 0.4f);
					DrawIconShine(dc, b.Inset(5.0f, 5.0f), 0.65f, false);
				}
				dc.Draw()->DrawImageRotated(rightIconImage_, iconX, bounds_.centerY(), rightIconScale_, rightIconRot_, col, rightIconFlipH_);
			}
			Bounds textBounds(bounds_.x + paddingLeft + textPadding_.left, bounds_.y, availWidth, bounds_.h);
			dc.DrawTextRectSqueeze(text_, textBounds, style.fgColor, ALIGN_VCENTER | FLAG_WRAP_TEXT | drawTextFlags_);
		}
		dc.SetFontScale(1.0f, 1.0f);
	}

	if (selected_) {
		dc.Draw()->DrawImage(dc.GetTheme().checkOn, bounds_.x2() - 40, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_CENTER);
	}
}

std::string Choice::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 choice"), text_);
}

InfoItem::InfoItem(std::string_view text, std::string_view rightText, LayoutParams *layoutParams)
	: Item(layoutParams), text_(text), rightText_(rightText) {
	// We set the colors later once we have a UIContext.
}

void InfoItem::Draw(UIContext &dc) {
	Item::Draw(dc);

	UI::Style style = HasFocus() ? dc.GetTheme().itemFocusedStyle : dc.GetTheme().infoStyle;

	dc.FillRect(style.background, bounds_);

	int paddingX = 12;
	Bounds padBounds = bounds_.Expand(-paddingX, 0);

	float leftWidth, leftHeight;
	dc.MeasureTextRect(dc.GetTheme().uiFont, 1.0f, 1.0f, text_, padBounds.w, &leftWidth, &leftHeight, ALIGN_VCENTER);

	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawTextRect(text_, padBounds, style.fgColor, ALIGN_VCENTER);

	Bounds rightBounds(padBounds.x + leftWidth, padBounds.y, padBounds.w - leftWidth, padBounds.h);
	dc.DrawTextRect(rightText_, rightBounds, style.fgColor, ALIGN_VCENTER | ALIGN_RIGHT | FLAG_WRAP_TEXT);
}

std::string InfoItem::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1: %2"), text_, rightText_);
}

ItemHeader::ItemHeader(std::string_view text, LayoutParams *layoutParams)
	: Item(layoutParams), text_(text) {
	layoutParams_->width = FILL_PARENT;
	layoutParams_->height = 40;
}

void ItemHeader::Draw(UIContext &dc) {
	dc.SetFontStyle(large_ ? dc.GetTheme().uiFont : dc.GetTheme().uiFontSmall);

	const UI::Style &style = popupStyle_ ? dc.GetTheme().popupStyle : dc.GetTheme().headerStyle;
	dc.FillRect(style.background, bounds_);
	dc.DrawText(text_, bounds_.x + 8, bounds_.centerY(), style.fgColor, ALIGN_LEFT | ALIGN_VCENTER);
	dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y2()-2, bounds_.x2(), bounds_.y2(), style.fgColor);
}

void ItemHeader::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	float availWidth = layoutParams_->width;
	if (availWidth < 0) {
		// If there's no size, let's grow as big as we want.
		availWidth = horiz.size == 0 ? MAX_ITEM_SIZE : horiz.size;
	}
	ApplyBoundBySpec(availWidth, horiz);
	dc.MeasureTextRect(dc.GetTheme().uiFontSmall, 1.0f, 1.0f, text_, availWidth, &w, &h, ALIGN_LEFT | ALIGN_VCENTER);
}

std::string ItemHeader::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 heading"), text_);
}

CollapsibleHeader::CollapsibleHeader(bool *toggle, std::string_view text, LayoutParams *layoutParams)
	: CheckBox(toggle, text, "", layoutParams) {
	layoutParams_->width = FILL_PARENT;
	layoutParams_->height = 40;
}

void CollapsibleHeader::Draw(UIContext &dc) {
	Style style = dc.GetTheme().collapsibleHeaderStyle;
	if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

	DrawBG(dc, style);

	float xoff = 37.0f;

	dc.SetFontStyle(dc.GetTheme().uiFontSmall);
	dc.DrawText(text_, bounds_.x + 6 + xoff, bounds_.centerY(), style.fgColor, ALIGN_LEFT | ALIGN_VCENTER);
	dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y2() - 2, bounds_.x2(), bounds_.y2(), style.fgColor);
	if (hasSubItems_) {
		dc.Draw()->DrawImageRotated(ImageID("I_ARROW"), bounds_.x + 20.0f, bounds_.y + 20.0f, 1.0f, *toggle_ ? -M_PI / 2 : M_PI, style.fgColor);
	}
}

void CollapsibleHeader::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	float availWidth = layoutParams_->width;
	if (availWidth < 0) {
		// If there's no size, let's grow as big as we want.
		availWidth = horiz.size == 0 ? MAX_ITEM_SIZE : horiz.size;
	}
	ApplyBoundBySpec(availWidth, horiz);
	dc.MeasureTextRect(dc.GetTheme().uiFontSmall, 1.0f, 1.0f, text_, availWidth, &w, &h, ALIGN_LEFT | ALIGN_VCENTER);
}

void CollapsibleHeader::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	View::GetContentDimensions(dc, w, h);
}

void BorderView::Draw(UIContext &dc) {
	Color color = 0xFFFFFFFF;
	if (style_ == BorderStyle::HEADER_FG)
		color = dc.GetTheme().headerStyle.fgColor;
	else if (style_ == BorderStyle::ITEM_DOWN_BG)
		color = dc.GetTheme().itemDownStyle.background.color;

	if (borderFlags_ & BORDER_TOP)
		dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y + size_, color);
	if (borderFlags_ & BORDER_LEFT)
		dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y, bounds_.x + size_, bounds_.y2(), color);
	if (borderFlags_ & BORDER_BOTTOM)
		dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y2() - size_, bounds_.x2(), bounds_.y2(), color);
	if (borderFlags_ & BORDER_RIGHT)
		dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x2() - size_, bounds_.y, bounds_.x2(), bounds_.y2(), color);
}

void BorderView::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	Bounds bounds(0, 0, layoutParams_->width, layoutParams_->height);
	if (bounds.w < 0) {
		// If there's no size, let's grow as big as we want.
		bounds.w = horiz.size == 0 ? MAX_ITEM_SIZE : horiz.size;
	}
	if (bounds.h < 0) {
		bounds.h = vert.size == 0 ? MAX_ITEM_SIZE : vert.size;
	}
	ApplyBoundsBySpec(bounds, horiz, vert);
	// If we have vertical borders, grow to width so they're spaced apart.
	w = (borderFlags_ & BORDER_VERT) != 0 ? bounds.w : 0;
	h = (borderFlags_ & BORDER_HORIZ) != 0 ? bounds.h : 0;
}

void PopupHeader::Draw(UIContext &dc) {
	const float paddingHorizontal = 12;
	const float availableWidth = bounds_.w - paddingHorizontal * 2;

	float tw, th;
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, text_, &tw, &th, 0);

	float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;

	float tx = paddingHorizontal;
	if (availableWidth < tw) {
		float overageRatio = 1.5f * availableWidth * 1.0f / tw;
		tx -= (1.0f + sin(time_now_d() * overageRatio)) * sineWidth;
		Bounds tb = bounds_;
		tb.x = bounds_.x + paddingHorizontal;
		tb.w = bounds_.w - paddingHorizontal * 2;
		dc.PushScissor(tb);
	}

	// Header background
	dc.FillRect(dc.GetTheme().popupTitleStyle.background, bounds_);
	// Header title text
	dc.DrawText(text_, bounds_.x + tx, bounds_.centerY(), dc.GetTheme().popupTitleStyle.fgColor, ALIGN_LEFT | ALIGN_VCENTER);
	// Underline
	dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y2()-2, bounds_.x2(), bounds_.y2(), dc.GetTheme().popupTitleStyle.fgColor);

	if (availableWidth < tw) {
		dc.PopScissor();
	}
}

std::string PopupHeader::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 heading"), text_);
}

void CheckBox::Toggle() {
	if (toggle_) {
		*toggle_ = !(*toggle_);
		UI::PlayUISound(*toggle_ ? UI::UISound::TOGGLE_ON : UI::UISound::TOGGLE_OFF);
	}
}

bool CheckBox::Toggled() const {
	if (toggle_)
		return *toggle_;
	return false;
}

void CheckBox::ClickInternal() {
	Clickable::ClickInternal();
	Toggle();
}

void CheckBox::Draw(UIContext &dc) {
	Style style = dc.GetTheme().itemStyle;
	if (!IsEnabled()) {
		style = dc.GetTheme().itemDisabledStyle;
	}
	ImageID image = Toggled() ? dc.GetTheme().checkOn : dc.GetTheme().checkOff;

	// In image mode, light up instead of showing a checkbox.
	if (imageID_.isValid()) {
		image = imageID_;
		if (Toggled()) {
			if (HasFocus()) {
				style = dc.GetTheme().itemDownStyle;
			} else {
				style = dc.GetTheme().itemFocusedStyle;
			}
		} else {
			if (HasFocus()) {
				style = dc.GetTheme().itemDownStyle;
			} else {
				style = dc.GetTheme().itemStyle;
			}
		}

		if (down_) {
			style.background.color = lightenColor(style.background.color);
		}

	} else {
		if (HasFocus()) {
			style = dc.GetTheme().itemFocusedStyle;
		}
		if (down_) {
			style = dc.GetTheme().itemDownStyle;
		}
	}

	dc.SetFontStyle(dc.GetTheme().uiFont);

	DrawBG(dc, style);

	float imageW, imageH;
	dc.Draw()->MeasureImage(image, &imageW, &imageH);

	const int paddingX = 12;
	// Padding right of the checkbox image too.
	const float availWidth = bounds_.w - paddingX * 2 - imageW - paddingX;

	if (!text_.empty()) {
		Bounds textBounds(bounds_.x + paddingX, bounds_.y, availWidth, bounds_.h);
		dc.DrawTextRectSqueeze(text_, textBounds, style.fgColor, ALIGN_VCENTER | FLAG_WRAP_TEXT);
	}
	dc.Draw()->DrawImage(image, bounds_.x2() - paddingX, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
	dc.SetFontScale(1.0f, 1.0f);
}

std::string CheckBox::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	std::string text = ApplySafeSubstitutions(u->T("%1 checkbox"), text_);
	if (!smallText_.empty()) {
		text += "\n" + smallText_;
	}
	return text;
}

void CheckBox::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	ImageID image = Toggled() ? dc.GetTheme().checkOn : dc.GetTheme().checkOff;
	if (imageID_.isValid()) {
		image = imageID_;
	}

	float imageW, imageH;
	dc.Draw()->MeasureImage(image, &imageW, &imageH);

	const int paddingX = 12;

	if (imageID_.isValid()) {
		w = imageW + paddingX * 2;
		h = std::max(imageH, ITEM_HEIGHT);
		return;
	}

	// The below code is kinda wacky, we shouldn't involve bounds_ here.

	// Padding right of the checkbox image too.
	float availWidth = bounds_.w - paddingX * 2 - imageW - paddingX;
	if (availWidth < 0.0f) {
		// Let it have as much space as it needs.
		availWidth = MAX_ITEM_SIZE;
	}

	if (!text_.empty()) {
		float scale = dc.CalculateTextScale(text_, availWidth);

		float actualWidth, actualHeight;
		dc.MeasureTextRect(dc.GetTheme().uiFont, scale, scale, text_, availWidth, &actualWidth, &actualHeight, ALIGN_VCENTER | FLAG_WRAP_TEXT);
		h = std::max(actualHeight, ITEM_HEIGHT);
	} else {
		h = std::max(imageH, ITEM_HEIGHT);
	}
	w = bounds_.w;
}

BitCheckBox::BitCheckBox(uint32_t *bitfield, uint32_t bit, std::string_view text, std::string_view smallText, LayoutParams *layoutParams)
	: CheckBox(nullptr, text, smallText, layoutParams), bitfield_(bitfield), bit_(bit) {
	_dbg_assert_msg_(bit != 0, "bit is a mask, not a bit index");
}

void BitCheckBox::Toggle() {
	if (bitfield_) {
		*bitfield_ = *bitfield_ ^ bit_;
		if (*bitfield_ & bit_) {
			UI::PlayUISound(UI::UISound::TOGGLE_ON);
		} else {
			UI::PlayUISound(UI::UISound::TOGGLE_OFF);
		}
	}
}

bool BitCheckBox::Toggled() const {
	if (bitfield_)
		return (bit_ & *bitfield_) == bit_;
	return false;
}

void Button::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	if (imageID_.isValid()) {
		dc.Draw()->GetAtlas()->measureImage(imageID_, &w, &h);
	} else {
		w = 0.0f;
		h = 0.0f;
	}

	if (!text_.empty() && !ignoreText_) {
		float width = 0.0f;
		float height = 0.0f;
		dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, text_, &width, &height);

		w += width;
		if (imageID_.isValid()) {
			w += paddingW_;
		}
		h = std::max(h, height);
	}

	// Add some internal padding to not look totally ugly
	w += paddingW_;
	h += paddingH_;

	w *= scale_;
	h *= scale_;
}

std::string Button::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 button"), GetText());
}

void Button::ClickInternal() {
	Clickable::ClickInternal();
	UI::PlayUISound(UI::UISound::CONFIRM);
}

void Button::Draw(UIContext &dc) {
	Style style = dc.GetTheme().itemStyle;

	if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

	// dc.Draw()->DrawImage4Grid(style.image, bounds_.x, bounds_.y, bounds_.x2(), bounds_.y2(), style.bgColor);
	DrawBG(dc, style);
	float tw, th;
	dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, text_, &tw, &th);
	tw *= scale_;
	th *= scale_;

	if (tw > bounds_.w || imageID_.isValid()) {
		dc.PushScissor(bounds_);
	}
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.SetFontScale(scale_, scale_);
	if (imageID_.isValid() && (ignoreText_ || text_.empty())) {
		dc.Draw()->DrawImage(imageID_, bounds_.centerX(), bounds_.centerY(), scale_, style.fgColor, ALIGN_CENTER);
	} else if (!text_.empty()) {
		float textX = bounds_.centerX();
		if (imageID_.isValid()) {
			const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(imageID_);
			if (img) {
				dc.Draw()->DrawImage(imageID_, bounds_.centerX() - tw / 2 - 5, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_CENTER);
				textX += img->w / 2.0f;
			}
		}
		dc.DrawText(text_, textX, bounds_.centerY(), style.fgColor, ALIGN_CENTER);
	}
	dc.SetFontScale(1.0f, 1.0f);

	if (tw > bounds_.w || imageID_.isValid()) {
		dc.PopScissor();
	}
}

void RadioButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 0.0f;
	h = 0.0f;

	if (!text_.empty()) {
		dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, text_, &w, &h);
	}

	// Add some internal padding to not look totally ugly
	w += paddingW_ * 3.0f + radioRadius_ * 2.0f;
	h = std::max(h, radioRadius_ * 2) + paddingH_ * 2;
}

std::string RadioButton::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 radio button"), text_);
}

void RadioButton::ClickInternal() {
	Clickable::ClickInternal();
	UI::PlayUISound(UI::UISound::CONFIRM);
	*value_ = thisButtonValue_;
}

void RadioButton::Draw(UIContext &dc) {
	Style style = dc.GetTheme().itemStyle;

	bool checked = *value_ == thisButtonValue_;

	if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

	DrawBG(dc, style);

	dc.Flush();
	dc.BeginNoTex();
	dc.Draw()->Circle(bounds_.x + paddingW_ + radioRadius_, bounds_.centerY(), radioRadius_, 2.5f, 36, 0, style.fgColor, 1.0f);
	if (checked) {
		dc.Draw()->FillCircle(bounds_.x + paddingW_ + radioRadius_, bounds_.centerY(), radioInnerRadius_, 36, style.fgColor);
	}
	dc.Flush();
	dc.Begin();

	float tw, th;
	dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, text_, &tw, &th);

	if (tw > bounds_.w) {
		dc.PushScissor(bounds_);
	}

	dc.SetFontStyle(dc.GetTheme().uiFont);

	if (!text_.empty()) {
		float textX = bounds_.x + paddingW_ * 2.0f + radioRadius_ * 2.0f;
		dc.DrawText(text_, textX, bounds_.centerY(), style.fgColor, ALIGN_LEFT | ALIGN_VCENTER);
	}

	if (tw > bounds_.w) {
		dc.PopScissor();
	}
}

ImageView::ImageView(ImageID atlasImage, const std::string &text, ImageSizeMode sizeMode, LayoutParams *layoutParams)
	: InertView(layoutParams), text_(text), atlasImage_(atlasImage), sizeMode_(sizeMode) {}

void ImageView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->GetAtlas()->measureImage(atlasImage_, &w, &h);
	w *= scale_;
	h *= scale_;
	// TODO: involve sizemode
}

void ImageView::Draw(UIContext &dc) {
	const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(atlasImage_);
	if (img) {
		// TODO: involve sizemode
		float scale = bounds_.w / img->w;
		dc.Draw()->DrawImage(atlasImage_, bounds_.x, bounds_.y, scale, 0xFFFFFFFF, ALIGN_TOPLEFT);
	}
}

const float bulletOffset = 25;

void SimpleTextView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.MeasureText(*ComputeStyle(dc), 1.0f, 1.0f, text_, &w, &h, 0);
}

void SimpleTextView::Draw(UIContext &dc) {
	uint32_t textColor = dc.GetTheme().itemStyle.fgColor;
	dc.SetFontStyle(*ComputeStyle(dc));
	dc.DrawText(text_, bounds_.x, bounds_.y, textColor, 0);
}

const FontStyle *SimpleTextView::ComputeStyle(const UIContext &dc) const {
	if (small_) {
		return &dc.GetTheme().uiFontSmall;
	} else if (big_) {
		return &dc.GetTheme().uiFontBig;
	} else {
		return &dc.GetTheme().uiFont;
	}
}

const FontStyle *GetTextStyle(const UIContext &dc, TextSize size) {
	const FontStyle *style = &dc.GetTheme().uiFont;
	switch (size) {
	case TextSize::Tiny:
		style = &dc.GetTheme().uiFontTiny;
		break;
	case TextSize::Small:
		style = &dc.GetTheme().uiFontSmall;
		break;
	case TextSize::Big:
		style = &dc.GetTheme().uiFontBig;
		break;
	default:
		break;
	}
	return style;
}

void TextView::GetContentDimensionsBySpec(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert, float &w, float &h) const {
	float availWidth = layoutParams_->width;
	if (availWidth < 0) {
		// If there's no size, let's grow as big as we want.
		availWidth = horiz.size == 0 ? MAX_ITEM_SIZE : horiz.size;
	}
	ApplyBoundBySpec(availWidth, horiz);
	if (bullet_) {
		availWidth -= bulletOffset;
	}
	const FontStyle *style = GetTextStyle(dc, textSize_);
	float measuredW;
	float measuredH;
	dc.MeasureTextRect(*style, 1.0f, 1.0f, text_, availWidth, &measuredW, &measuredH, textAlign_);
	w = measuredW + pad_.horiz();
	h = measuredH + pad_.vert();
	if (bullet_) {
		w += bulletOffset;
	}
}

void TextView::Draw(UIContext &dc) {
	uint32_t textColor = hasTextColor_ ? textColor_ : (popupStyle_ ? dc.GetTheme().popupStyle.fgColor : dc.GetTheme().infoStyle.fgColor);
	if (!(textColor & 0xFF000000))
		return;

	bool clip = false;
	if (measuredWidth_ > bounds_.w || measuredHeight_ > bounds_.h)
		clip = true;
	if (bounds_.w < 0 || bounds_.h < 0 || !clip_) {
		// We have a layout but, but try not to screw up rendering.
		// TODO: Fix properly.
		clip = false;
	}
	if (clip) {
		dc.Flush();
		dc.PushScissor(bounds_);
	}
	// In case it's been made focusable.
	if (HasFocus()) {
		UI::Style style = dc.GetTheme().itemFocusedStyle;
		style.background.color &= 0x7fffffff;
		dc.FillRect(style.background, bounds_);
	}

	const FontStyle *style = GetTextStyle(dc, textSize_);
	dc.SetFontStyle(*style);

	Bounds textBounds = bounds_;

	if (bullet_) {
		float radius = 7.0f;
		dc.Flush();
		dc.BeginNoTex();
		dc.Draw()->FillCircle(textBounds.x + radius, textBounds.centerY(), radius, 20, textColor);
		dc.Flush();
		dc.Begin();
		textBounds.x += bulletOffset;
		textBounds.w -= bulletOffset;
	}

	if (shadow_) {
		uint32_t shadowColor = 0x80000000;
		dc.DrawTextRect(text_, textBounds.Offset(1.0f + pad_.left, 1.0f + pad_.top), shadowColor, textAlign_);
	}
	dc.DrawTextRect(text_, textBounds.Offset(pad_.left, pad_.top), textColor, textAlign_);
	if (textSize_ != TextSize::Normal) {
		// If we changed font style, reset it.
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
	if (clip) {
		dc.PopScissor();
	}
}

bool ClickableTextView::Touch(const TouchInput &input) {
	bool contains = bounds_.Contains(input.x, input.y);

	// Ignore buttons other than the left one.
	if ((input.flags & TouchInputFlags::MOUSE) && (input.buttons & 1) == 0) {
		return contains;
	}

	if (input.flags & TouchInputFlags::DOWN) {
		if (bounds_.Contains(input.x, input.y)) {
			if (IsFocusMovementEnabled())
				SetFocusedView(this);
			dragging_ = true;
			down_ = true;
		} else {
			down_ = false;
			dragging_ = false;
		}
	} else if (input.flags & TouchInputFlags::MOVE) {
		if (dragging_)
			down_ = bounds_.Contains(input.x, input.y);
	}
	if (input.flags & TouchInputFlags::UP) {
		if ((input.flags & TouchInputFlags::CANCEL) == 0 && dragging_ && bounds_.Contains(input.x, input.y)) {
			EventParams e{};
			e.v = this;
			OnClick.Trigger(e);
		}
		down_ = false;
		dragging_ = false;
	}
	return contains;
}

TextEdit::TextEdit(std::string_view text, std::string_view title, std::string_view placeholderText, LayoutParams *layoutParams)
  : View(layoutParams), text_(text), title_(title), undo_(text), placeholderText_(placeholderText),
    textColor_(0xFFFFFFFF), maxLen_(255) {
	caret_ = (int)text_.size();
}

void TextEdit::FocusChanged(int focusFlags) {
	if (focusFlags == FF_GOTFOCUS) {
		System_NotifyUIEvent(UIEventNotification::TEXT_GOTFOCUS);
	}
	else {
		System_NotifyUIEvent(UIEventNotification::TEXT_LOSTFOCUS);
	}
}

void TextEdit::Draw(UIContext &dc) {
	dc.PushScissor(bounds_);
	dc.SetFontStyle(dc.GetTheme().uiFont);

	// TODO: make background themeable?
	dc.FillRect(HasFocus() ? UI::Drawable(0x80000000) : UI::Drawable(0x30000000), bounds_);

	uint32_t textColor = popupStyle_ ? dc.GetTheme().popupStyle.fgColor : dc.GetTheme().infoStyle.fgColor;
	float textX = bounds_.x;
	float w, h;

	Bounds textBounds = bounds_;
	textBounds.x = textX - scrollPos_;

	std::string textToDisplay = text_;
	if (passwordMasking_) {
		for (int i = 0; i < textToDisplay.size(); i++) {
			textToDisplay[i] = '*';
		}
	}

	if (text_.empty()) {
		if (placeholderText_.size()) {
			uint32_t c = textColor & 0x50FFFFFF;
			dc.DrawTextRect(placeholderText_, bounds_, c, ALIGN_CENTER);
		}
	} else {
		dc.DrawTextRect(textToDisplay, textBounds, textColor, ALIGN_VCENTER | ALIGN_LEFT | align_);
	}

	if (HasFocus()) {
		// Hack to find the caret position. Might want to find a better way...
		dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, textToDisplay.substr(0, caret_), &w, &h, ALIGN_VCENTER | ALIGN_LEFT | align_);
		float caretX = w - scrollPos_;
		if (caretX > bounds_.w) {
			scrollPos_ += caretX - bounds_.w;
		}
		if (caretX < 0) {
			scrollPos_ += caretX;
		}
		caretX += textX;
		dc.FillRect(UI::Drawable(textColor), Bounds(caretX - 1, bounds_.y + 2, 3, bounds_.h - 4));
	}
	dc.PopScissor();
}

void TextEdit::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, !text_.empty() ? text_ : "Wj", &w, &h, align_);
	w += 2;
	h += 2;
}

std::string TextEdit::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 text field"), GetText());
}

// Handles both windows and unix line endings.
static std::string FirstLine(const std::string &text) {
	size_t pos = text.find("\r\n");
	if (pos != std::string::npos) {
		return text.substr(0, pos);
	}
	pos = text.find('\n');
	if (pos != std::string::npos) {
		return text.substr(0, pos);
	}
	return text;
}

bool TextEdit::Touch(const TouchInput &touch) {
	if (touch.flags & TouchInputFlags::DOWN) {
		if (bounds_.Contains(touch.x, touch.y)) {
			SetFocusedView(this, true);
			return true;
		}
	}
	return false;
}

bool TextEdit::Key(const KeyInput &input) {
	if (!HasFocus())
		return false;
	bool textChanged = false;
	// Process hardcoded navigation keys. These aren't chars.
	if (input.flags & KeyInputFlags::DOWN) {
		switch (input.keyCode) {
		case NKCODE_CTRL_LEFT:
		case NKCODE_CTRL_RIGHT:
			ctrlDown_ = true;
			break;
		case NKCODE_DPAD_LEFT:  // ASCII left arrow
			u8_dec(text_.c_str(), &caret_);
			break;
		case NKCODE_DPAD_RIGHT: // ASCII right arrow
			u8_inc(text_.c_str(), &caret_);
			break;
		case NKCODE_MOVE_HOME:
		case NKCODE_PAGE_UP:
			caret_ = 0;
			break;
		case NKCODE_MOVE_END:
		case NKCODE_PAGE_DOWN:
			caret_ = (int)text_.size();
			break;
		case NKCODE_FORWARD_DEL:
			if (caret_ < (int)text_.size()) {
				int endCaret = caret_;
				u8_inc(text_.c_str(), &endCaret);
				undo_ = text_;
				text_.erase(text_.begin() + caret_, text_.begin() + endCaret);
				textChanged = true;
			}
			break;
		case NKCODE_DEL:
			if (caret_ > 0) {
				int begCaret = caret_;
				u8_dec(text_.c_str(), &begCaret);
				undo_ = text_;
				text_.erase(text_.begin() + begCaret, text_.begin() + caret_);
				caret_--;
				textChanged = true;
			}
			break;
		case NKCODE_ENTER:
		case NKCODE_NUMPAD_ENTER:
			{
				EventParams e{};
				e.v = this;
				e.s = text_;
				OnEnter.Trigger(e);
				break;
			}
		case NKCODE_BACK:
		case NKCODE_ESCAPE:
			return false;
		default:
			break;
		}

		if (ctrlDown_) {
			switch (input.keyCode) {
			case NKCODE_C:
				// Just copy the entire text contents, until we get selection support.
				System_CopyStringToClipboard(text_);
				break;
			case NKCODE_V:
				{
					std::string clipText = System_GetProperty(SYSPROP_CLIPBOARD_TEXT);
					clipText = FirstLine(clipText);
					if (clipText.size()) {
						// Until we get selection, replace the whole text
						undo_ = text_;
						text_.clear();
						caret_ = 0;

						size_t maxPaste = maxLen_ - text_.size();
						if (clipText.size() > maxPaste) {
							int end = 0;
							while ((size_t)end < maxPaste) {
								u8_inc(clipText.c_str(), &end);
							}
							if (end > 0) {
								u8_dec(clipText.c_str(), &end);
							}
							clipText = clipText.substr(0, end);
						}
						InsertAtCaret(clipText.c_str());
						textChanged = true;
					}
				}
				break;
			case NKCODE_Z:
				text_ = undo_;
				break;
			default:
				break;
			}
		}

		if (caret_ < 0) {
			caret_ = 0;
		}
		if (caret_ > (int)text_.size()) {
			caret_ = (int)text_.size();
		}
	}

	if (input.flags & KeyInputFlags::UP) {
		switch (input.keyCode) {
		case NKCODE_CTRL_LEFT:
		case NKCODE_CTRL_RIGHT:
			ctrlDown_ = false;
			break;
		default:
			break;
		}
	}

	// Process chars.
	if (input.flags & KeyInputFlags::CHAR) {
		const int unichar = input.keyCode;
		if (unichar >= 0x20 && !ctrlDown_) {  // Ignore control characters.
			// Insert it! (todo: do it with a string insert)
			char buf[8];
			buf[u8_wc_toutf8(buf, unichar)] = '\0';
			if (strlen(buf) + text_.size() <= maxLen_) {
				undo_ = text_;
				InsertAtCaret(buf);
				textChanged = true;
			}
		}
	}

	if (textChanged) {
		UI::EventParams e{};
		e.v = this;
		OnTextChange.Trigger(e);
	}
	return true;
}

void TextEdit::InsertAtCaret(const char *text) {
	size_t len = strlen(text);
	for (size_t i = 0; i < len; i++) {
		text_.insert(text_.begin() + caret_, text[i]);
		caret_++;
	}
}

void ProgressBar::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, "  100%  ", &w, &h);
}

void ProgressBar::Draw(UIContext &dc) {
	char temp[32];
	snprintf(temp, sizeof(temp), "%d%%", (int)(progress_ * 100.0f));
	dc.Draw()->DrawImageCenterTexel(dc.GetTheme().whiteImage, bounds_.x, bounds_.y, bounds_.x + bounds_.w * progress_, bounds_.y2(), 0xc0c0c0c0);
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawTextRect(temp, bounds_, 0xFFFFFFFF, ALIGN_CENTER);
}

std::string ProgressBar::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	float percent = progress_ * 100.0f;
	return ApplySafeSubstitutions(u->T("Progress: %1%"), StringFromInt((int)percent));
}

void Spinner::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 48;
	h = 48;
}

void Spinner::Draw(UIContext &dc) {
	if (!(color_ & 0xFF000000))
		return;
	double t = time_now_d() * 1.3f;
	double angle = fmod(t, M_PI * 2.0);

	if (!images_) {
		// Simple.
		dc.Draw()->CircleSegment(bounds_.centerX(), bounds_.centerY(), bounds_.radius(), 3.0f, 20.0f, angle, angle + PI * 3.0 / 2.0, dc.GetTheme().itemStyle.fgColor, 0.0f);
		return;
	}

	float r = bounds_.w * 0.5f;
	double da = M_PI * 2.0 / numImages_;
	for (int i = 0; i < numImages_; i++) {
		double a = angle + i * da;
		float x = (float)cos(a) * r;
		float y = (float)sin(a) * r;
		dc.Draw()->DrawImage(images_[i], bounds_.centerX() + x, bounds_.centerY() + y, 1.0f, color_, ALIGN_CENTER);
	}
}

bool TriggerButton::Touch(const TouchInput &input) {
	bool contains = bounds_.Contains(input.x, input.y);
	if (input.flags & TouchInputFlags::DOWN) {
		if (contains) {
			down_ |= 1 << input.id;
		}
	}

	if (input.flags & TouchInputFlags::MOVE) {
		if (contains)
			down_ |= 1 << input.id;
		else
			down_ &= ~(1 << input.id);
	}

	if (input.flags & TouchInputFlags::UP) {
		down_ &= ~(1 << input.id);
	}

	if (down_ != 0) {
		*bitField_ |= bit_;
	} else {
		*bitField_ &= ~bit_;
	}

	return contains;
}

void TriggerButton::Draw(UIContext &dc) {
	dc.Draw()->DrawImage(imageBackground_, bounds_.centerX(), bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
	dc.Draw()->DrawImage(imageForeground_, bounds_.centerX(), bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
}

void TriggerButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->GetAtlas()->measureImage(imageBackground_, &w, &h);
}

bool Slider::Key(const KeyInput &input) {
	if (HasFocus() && (input.flags & KeyInputFlags::DOWN) && !(input.flags & KeyInputFlags::IS_REPEAT)) {
		if (ApplyKey(input.keyCode)) {
			Clamp();
			repeat_ = 0;
			repeatCode_ = input.keyCode;
			return true;
		}
		return false;
	} else if ((input.flags & KeyInputFlags::UP) && input.keyCode == repeatCode_) {
		repeat_ = -1;
		return false;
	} else {
		return false;
	}
}

bool Slider::ApplyKey(InputKeyCode keyCode) {
	SnapToFixed();
	
	if (numFixedChoices_) {
		// Find the current one.
		int curIndex = -1;
		for (int i = 0; i < numFixedChoices_; i++) {
			if (*value_ == fixedChoices_[i]) {
				curIndex = i;
			}
		}
		switch (keyCode) {
		case NKCODE_DPAD_LEFT:
		case NKCODE_MINUS:
		case NKCODE_NUMPAD_SUBTRACT:
		case NKCODE_PAGE_DOWN:
			if (curIndex >= 1) {
				*value_ = fixedChoices_[curIndex - 1];
			}
			break;
		case NKCODE_DPAD_RIGHT:
		case NKCODE_PLUS:
		case NKCODE_NUMPAD_ADD:
		case NKCODE_PAGE_UP:
			if (curIndex < numFixedChoices_ - 1) {
				*value_ = fixedChoices_[curIndex + 1];
			}
			break;
		case NKCODE_MOVE_HOME:
			*value_ = fixedChoices_[0];
			break;
		case NKCODE_MOVE_END:
			*value_ = fixedChoices_[numFixedChoices_ - 1];
			break;
		default:
			return false;
		}
	} else {
		switch (keyCode) {
		case NKCODE_DPAD_LEFT:
		case NKCODE_MINUS:
		case NKCODE_NUMPAD_SUBTRACT:
			*value_ -= step_;
			break;
		case NKCODE_DPAD_RIGHT:
		case NKCODE_PLUS:
		case NKCODE_NUMPAD_ADD:
			*value_ += step_;
			break;
		case NKCODE_PAGE_UP:
			*value_ -= step_ * 10;
			break;
		case NKCODE_PAGE_DOWN:
			*value_ += step_ * 10;
			break;
		case NKCODE_MOVE_HOME:
			*value_ = minValue_;
			break;
		case NKCODE_MOVE_END:
			*value_ = maxValue_;
			break;
		default:
			return false;
		}
	}

	EventParams params{};
	params.v = this;
	params.a = (uint32_t)(*value_);
	params.f = (float)(*value_);
	OnChange.Trigger(params);
	return true;
}

bool Slider::Touch(const TouchInput &input) {
	// Calling it afterwards, so dragging_ hasn't been set false yet when checking it above.
	bool contains = Clickable::Touch(input);

	if (dragging_) {
		float relativeX = (input.x - (bounds_.x + paddingLeft_)) / (bounds_.w - paddingLeft_ - paddingRight_);
		*value_ = floorf(relativeX * (maxValue_ - minValue_) + minValue_ + 0.5f);
		Clamp();
		EventParams params{};
		params.v = this;
		params.a = (uint32_t)(*value_);
		params.f = (float)(*value_);
		OnChange.Trigger(params);
	}

	// Cancel any key repeat.
	repeat_ = -1;
	return contains;
}

void Slider::Clamp() {
	if (*value_ < minValue_) *value_ = minValue_;
	else if (*value_ > maxValue_) *value_ = maxValue_;

	// Clamp the value to be a multiple of the nearest step (e.g. if step == 5, value == 293, it'll round down to 290).
	*value_ = *value_ - fmodf(*value_, step_);

	// If it's a fixed set, snap it.
	SnapToFixed();
}

void Slider::Draw(UIContext &dc) {
	bool focus = HasFocus();
	uint32_t sliderColor;

	if (dragging_) {
		sliderColor = popupStyle_ ? dc.GetTheme().popupSliderFocusedColor : dc.GetTheme().itemDownStyle.fgColor;
	} else if (focus) {
		sliderColor = popupStyle_ ? dc.GetTheme().popupSliderFocusedColor : dc.GetTheme().itemFocusedStyle.fgColor;
	} else {
		sliderColor = popupStyle_ ? dc.GetTheme().popupSliderColor : dc.GetTheme().itemStyle.fgColor;
	}

	float knobX = ((float)(*value_) - minValue_) / (maxValue_ - minValue_) * (bounds_.w - paddingLeft_ - paddingRight_) + (bounds_.x + paddingLeft_);
	dc.FillRect(Drawable(sliderColor), Bounds(bounds_.x + paddingLeft_, bounds_.centerY() - 2, knobX - (bounds_.x + paddingLeft_), 4));
	dc.FillRect(Drawable(0xFF808080), Bounds(knobX, bounds_.centerY() - 2, (bounds_.x + bounds_.w - paddingRight_ - knobX), 4));
	dc.Draw()->DrawImage(dc.GetTheme().sliderKnob, knobX, bounds_.centerY(), 1.0f, sliderColor, ALIGN_CENTER);
	char temp[64];
	if (showPercent_)
		snprintf(temp, sizeof(temp), "%d%%", *value_);
	else
		snprintf(temp, sizeof(temp), "%d", *value_);
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawText(temp, bounds_.x2() - 22, bounds_.centerY(), sliderColor, ALIGN_CENTER | FLAG_DYNAMIC_ASCII);
}

std::string Slider::DescribeText() const {
	if (showPercent_)
		return StringFromFormat("%d%% / %d%%", *value_, maxValue_);
	return StringFromFormat("%d / %d", *value_, maxValue_);
}

void Slider::Update() {
	View::Update();
	if (repeat_ >= 0) {
		repeat_++;
	}

	if (repeat_ >= 47) {
		ApplyKey(repeatCode_);
		if ((maxValue_ - minValue_) / step_ >= 300) {
			ApplyKey(repeatCode_);
		}
		Clamp();
	} else if (repeat_ >= 12 && (repeat_ & 1) == 1) {
		ApplyKey(repeatCode_);
		Clamp();
	}
}

void Slider::SnapToFixed() {
	if (!numFixedChoices_) {
		return;
	}

	int val = *value_;
	// Find the closest value.
	int minDist = 999999999;
	int best = -1;
	for (int i = 0; i < numFixedChoices_; i++) {
		int dist = val - fixedChoices_[i];
		if (dist < 0)
			dist = -dist;
		if (dist < minDist) {
			minDist = dist;
			best = i;
		}
	}

	if (best >= 0) {
		*value_ = fixedChoices_[best];
	}
}

void Slider::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// TODO
	w = 100;
	h = 50;
}

bool SliderFloat::Key(const KeyInput &input) {
	if (HasFocus() && (input.flags & KeyInputFlags::DOWN) && !(input.flags & KeyInputFlags::IS_REPEAT)) {
		if (ApplyKey(input.keyCode)) {
			Clamp();
			repeat_ = 0;
			repeatCode_ = input.keyCode;
			return true;
		}
		return false;
	} else if ((input.flags & KeyInputFlags::UP) && input.keyCode == repeatCode_) {
		repeat_ = -1;
		return false;
	} else {
		return false;
	}
}

bool SliderFloat::ApplyKey(InputKeyCode keyCode) {
	switch (keyCode) {
	case NKCODE_DPAD_LEFT:
	case NKCODE_MINUS:
	case NKCODE_NUMPAD_SUBTRACT:
		*value_ -= (maxValue_ - minValue_) / 50.0f;
		break;
	case NKCODE_DPAD_RIGHT:
	case NKCODE_PLUS:
	case NKCODE_NUMPAD_ADD:
		*value_ += (maxValue_ - minValue_) / 50.0f;
		break;
	case NKCODE_PAGE_UP:
		*value_ -= (maxValue_ - minValue_) / 5.0f;
		break;
	case NKCODE_PAGE_DOWN:
		*value_ += (maxValue_ - minValue_) / 5.0f;
		break;
	case NKCODE_MOVE_HOME:
		*value_ = minValue_;
		break;
	case NKCODE_MOVE_END:
		*value_ = maxValue_;
		break;
	default:
		return false;
	}

	_dbg_assert_(!my_isnanorinf(*value_));

	EventParams params{};
	params.v = this;
	params.a = (uint32_t)(*value_);
	params.f = (float)(*value_);
	OnChange.Trigger(params);
	return true;
}

bool SliderFloat::Touch(const TouchInput &input) {
	bool contains = Clickable::Touch(input);
	if (dragging_) {
		float relativeX = (input.x - (bounds_.x + paddingLeft_)) / (bounds_.w - paddingLeft_ - paddingRight_);
		*value_ = (relativeX * (maxValue_ - minValue_) + minValue_);
		Clamp();
		EventParams params{};
		params.v = this;
		params.a = (uint32_t)(*value_);
		params.f = (float)(*value_);
		OnChange.Trigger(params);
	}

	// Cancel any key repeat.
	repeat_ = -1;
	return contains;
}

void SliderFloat::Clamp() {
	_dbg_assert_(!my_isnanorinf(*value_));
	if (*value_ < minValue_)
		*value_ = minValue_;
	else if (*value_ > maxValue_)
		*value_ = maxValue_;
}

void SliderFloat::Draw(UIContext &dc) {
	bool focus = HasFocus();
	uint32_t sliderColor;

	if (down_) {
		sliderColor = popupStyle_ ? dc.GetTheme().popupSliderFocusedColor : dc.GetTheme().itemDownStyle.fgColor;
	} else if (focus) {
		sliderColor = popupStyle_ ? dc.GetTheme().popupSliderFocusedColor : dc.GetTheme().itemFocusedStyle.fgColor;
	} else {
		sliderColor = popupStyle_ ? dc.GetTheme().popupSliderColor : dc.GetTheme().itemStyle.fgColor;
	}

	float knobX = (*value_ - minValue_) / (maxValue_ - minValue_) * (bounds_.w - paddingLeft_ - paddingRight_) + (bounds_.x + paddingLeft_);
	dc.FillRect(Drawable(sliderColor), Bounds(bounds_.x + paddingLeft_, bounds_.centerY() - 2, knobX - (bounds_.x + paddingLeft_), 4));
	dc.FillRect(Drawable(0xFF808080), Bounds(knobX, bounds_.centerY() - 2, (bounds_.x + bounds_.w - paddingRight_ - knobX), 4));
	dc.Draw()->DrawImage(dc.GetTheme().sliderKnob, knobX, bounds_.centerY(), 1.0f, sliderColor, ALIGN_CENTER);
	char temp[64];
	snprintf(temp, sizeof(temp), "%0.2f", *value_);
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawText(temp, bounds_.x2() - 22, bounds_.centerY(), sliderColor, ALIGN_CENTER);
}

std::string SliderFloat::DescribeText() const {
	return StringFromFormat("%0.2f / %0.2f", *value_, maxValue_);
}

void SliderFloat::Update() {
	View::Update();
	if (repeat_ >= 0) {
		repeat_++;
	}

	if (repeat_ >= 47) {
		ApplyKey(repeatCode_);
		Clamp();
	} else if (repeat_ >= 12 && (repeat_ & 1) == 1) {
		ApplyKey(repeatCode_);
		Clamp();
	}
}

void SliderFloat::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// TODO
	w = 100;
	h = 50;
}

void Spacer::Draw(UIContext &dc) {
	View::Draw(dc);
	if (drawAsSeparator_) {
		dc.FillRect(UI::Drawable(dc.GetTheme().itemDownStyle.background.color), bounds_);
	}
}

void DrawIconShine(UIContext &dc, const Bounds &bounds, float shine, bool animated) {
	static const float radius[6] = { 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f };
	static const float startAngle[6] = { 0.3f, 0.5f, 0.1f, 0.9f, 0.7f, 0.4f };
	static const float arcLength[6] = { 0.45f, 0.2f, 0.6f, 0.7f, 0.3f, 0.5f };
	static const float speed[6] = { 0.4f, -0.9f, 0.2f, -0.6f, 0.7f, -0.1f };
	if (animated) {
		dc.Flush();
		dc.BeginNoTex();

		const double t = time_now_d();
		const int x = bounds.centerX();
		const int y = bounds.centerY();
		for (int i = 0; i < 6; i++) {
			float radius = (i * 0.1f + 0.4f) * 1.3f;
			float alpha = (5 - i) * (1.0f / 9.0f);

			float angle = fmod(startAngle[i] + t * speed[i] * 0.7f, 1.0) * 2 * PI;
			dc.Draw()->CircleSegment(x, y, radius * bounds.w, 4.0f, 64.0f, angle, angle + arcLength[i] * 2 * PI, colorAlpha(0xFF3EC5FF, alpha * shine), 0.0f);
		}

		dc.Flush();
		dc.Begin();
	}
	const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(ImageID("I_DROP_SHADOW"));
	if (img) {
		float scale = bounds.w / img->w;
		dc.Draw()->DrawImage(ImageID("I_DROP_SHADOW"), bounds.centerX(), bounds.centerY(), scale * 1.7f, colorAlpha(0xFF3EC5FF, 0.75f * shine), ALIGN_CENTER);
	}
	dc.Flush();
}

}  // namespace
