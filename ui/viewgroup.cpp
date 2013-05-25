#include "base/display.h"
#include "base/logging.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

namespace UI {

void ApplyGravity(const Bounds outer, const Margins &margins, float w, float h, int gravity, Bounds &inner) {
	inner.w = w - (margins.left + margins.right);
	inner.h = h - (margins.right + margins.left); 

	switch (gravity & G_HORIZMASK) {
	case G_LEFT: inner.x = outer.x + margins.left; break;
	case G_RIGHT: inner.x = outer.x + outer.w - w - margins.right; break;
	case G_HCENTER: inner.x = outer.x + (outer.w - w) / 2; break;
	}

	switch (gravity & G_VERTMASK) {
	case G_TOP: inner.y = outer.y + margins.top; break;
	case G_BOTTOM: inner.y = outer.y + outer.h - h - margins.bottom; break;
	case G_VCENTER: inner.y = outer.y + (outer.h - h) / 2; break;
	}
}

ViewGroup::~ViewGroup() {
	// Tear down the contents recursively.
	for (auto iter = views_.begin(); iter != views_.end(); ++iter)
		delete *iter;
}

void ViewGroup::Touch(const TouchInput &input) {
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		(*iter)->Touch(input);
	}
}

void ViewGroup::Draw(DrawContext &dc) {
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		(*iter)->Draw(dc);
	}
}

void ViewGroup::Update(const InputState &input_state) {
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		(*iter)->Update(input_state);
	}
}

bool ViewGroup::SetFocus() {
	if (!CanBeFocused() && !views_.empty()) {
		for (size_t i = 0; i < views_.size(); i++) {
			if (views_[i]->SetFocus())
				return true;
		}
	}
	return false;
}

void ViewGroup::MoveFocus(FocusDirection direction) {
	if (!GetFocusedView()) {
		SetFocus();
		return;
	}

	View *neighbor = FindNeighbor(GetFocusedView(), direction);
	if (neighbor) {
		neighbor->SetFocus();
	} else {
		for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
			(*iter)->MoveFocus(direction);
		}
	}
}

View *ViewGroup::FindNeighbor(View *view, FocusDirection direction) {
	// First, find the position of the view in the list.
	size_t num = -1;
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view) {
			num = i;
			break;
		}
	}

	// If view not found, no neighbor to find.
	if (num == -1)
		return 0;

	// TODO: Do the cardinal directions right. Now we just map to
	// prev/next.

	switch (direction) {
	case FOCUS_UP:
	case FOCUS_LEFT:
	case FOCUS_PREV:
		return views_[(num + views_.size() - 1) % views_.size()];
	case FOCUS_RIGHT:
	case FOCUS_DOWN:
	case FOCUS_NEXT:
		return views_[(num + 1) % views_.size()];

	default:
		return view;
	} 
}


void LinearLayout::Measure(const DrawContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	if (views_.empty()) {
		MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
		MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);
		return; 
	}

	float sum = 0.0f;
	float maxOther = 0.0f;
	float totalWeight = 0.0f;
	float weightSum = 0.0f;
	float weightZeroSum = 0.0f;

	for (size_t i = 0; i < views_.size(); i++) {
		const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
		const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);
		Margins margins = defaultMargins_;

		if (linLayoutParams) {
			totalWeight += linLayoutParams->weight;
			if (linLayoutParams->HasMargins())
				margins = linLayoutParams->margins;
		}

		if (orientation_ == ORIENT_HORIZONTAL) {
			views_[i]->Measure(dc, MeasureSpec(UNSPECIFIED), vert - (float)(margins.top + margins.bottom));
		} else if (orientation_ == ORIENT_VERTICAL) {
			views_[i]->Measure(dc, horiz - (float)(margins.left + margins.right), MeasureSpec(UNSPECIFIED));
		}

		float amount;
		if (orientation_ == ORIENT_HORIZONTAL) {
			amount = views_[i]->GetMeasuredWidth() + margins.left + margins.right;
			maxOther = std::max(maxOther, views_[i]->GetMeasuredHeight() + margins.top + margins.bottom);
		} else {
			amount = views_[i]->GetMeasuredHeight() + margins.top + margins.bottom;
			maxOther = std::max(maxOther, views_[i]->GetMeasuredWidth() + margins.left + margins.right);
		}

		sum += amount;
		if (linLayoutParams) {
			if (linLayoutParams->weight == 0.0f)
				weightZeroSum += amount;

			weightSum += linLayoutParams->weight;
		} else {
			weightZeroSum += amount;
		}
	}

	weightZeroSum += spacing_ * (views_.size() - 1);
	
	// Awright, got the sum. Let's take the remaining space after the fixed-size views,
	// and distribute among the weighted ones.
	if (orientation_ == ORIENT_HORIZONTAL) {
		MeasureBySpec(layoutParams_->width, weightZeroSum, horiz, &measuredWidth_);
		MeasureBySpec(layoutParams_->height, maxOther, vert, &measuredHeight_);

		float unit = (measuredWidth_ - weightZeroSum) / weightSum;
		// Redistribute the stretchy ones! and remeasure the children!
		for (size_t i = 0; i < views_.size(); i++) {
			const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
			const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);

			if (linLayoutParams && linLayoutParams->weight > 0.0)
				views_[i]->Measure(dc, MeasureSpec(EXACTLY, unit * linLayoutParams->weight), vert);
		}
	} else {
		MeasureBySpec(layoutParams_->height, weightZeroSum, vert, &measuredHeight_);
		MeasureBySpec(layoutParams_->width, maxOther, horiz, &measuredWidth_);

		float unit = (measuredHeight_ - weightZeroSum) / weightSum;

		// Redistribute! and remeasure children!
		for (size_t i = 0; i < views_.size(); i++) {
			const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
			const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);

			if (linLayoutParams && linLayoutParams->weight)
				views_[i]->Measure(dc, horiz, MeasureSpec(EXACTLY, unit * linLayoutParams->weight));
		}
	}
}

// TODO: Stretch and squeeze!
// weight != 0 = fill remaining space.
void LinearLayout::Layout() {
	const Bounds &bounds = bounds_;

	Bounds itemBounds;
	float pos;
	
	if (orientation_ == ORIENT_HORIZONTAL) {
		pos = bounds.x;
		itemBounds.y = bounds.y;
		itemBounds.h = measuredHeight_;
	} else {
		pos = bounds.y;
		itemBounds.x = bounds.x;
		itemBounds.w = measuredWidth_;
	}

	for (size_t i = 0; i < views_.size(); i++) {
		const LayoutParams *layoutParams = views_[i]->GetLayoutParams();
		const LinearLayoutParams *linLayoutParams = dynamic_cast<const LinearLayoutParams *>(layoutParams);

		Gravity gravity = G_TOPLEFT; 
		Margins margins = defaultMargins_;
		if (linLayoutParams) {
			if (linLayoutParams->HasMargins())
				margins = linLayoutParams->margins;
			gravity = linLayoutParams->gravity;
		}

		if (orientation_ == ORIENT_HORIZONTAL) {
			itemBounds.x = pos;
			itemBounds.w = views_[i]->GetMeasuredWidth() + margins.left + margins.right;
		} else {
			itemBounds.y = pos;
			itemBounds.h = views_[i]->GetMeasuredHeight() + margins.top + margins.bottom;
		}

		Bounds innerBounds;
		ApplyGravity(itemBounds, margins,
			views_[i]->GetMeasuredWidth(), views_[i]->GetMeasuredHeight(),
			gravity, innerBounds);

		views_[i]->SetBounds(innerBounds);
		views_[i]->Layout();

		pos += spacing_ + (orientation_ == ORIENT_HORIZONTAL ? itemBounds.w : itemBounds.h);
	}
}

void FrameLayout::Measure(const DrawContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	if (views_.empty()) {
		MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
		MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);
		return; 
	}

	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->Measure(dc, horiz, vert);
	}
}

void FrameLayout::Layout() {

}

void ScrollView::Measure(const DrawContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	// The scroll view itself simply obeys its parent.
	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);

	if (orientation_ == ORIENT_HORIZONTAL) {
		views_[0]->Measure(dc, MeasureSpec(UNSPECIFIED), vert);
	} else {
		views_[0]->Measure(dc, horiz, MeasureSpec(UNSPECIFIED));
	}
}

void ScrollView::Layout() {
	Bounds scrolled;
	scrolled.w = views_[0]->GetMeasuredWidth();
	scrolled.h = views_[0]->GetMeasuredHeight();

	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		scrolled.x = bounds_.x - scrollPos_;
		scrolled.y = bounds_.y;
		break;
	case ORIENT_VERTICAL:
		scrolled.x = bounds_.x;
		scrolled.y = bounds_.y - scrollPos_;
		break;
	}
	views_[0]->SetBounds(scrolled);
	views_[0]->Layout();
}

void ScrollView::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && input.id == 0) {
		scrollStart_ = scrollPos_;
	}

	gesture_.Update(input);

	if (gesture_.IsGestureActive(GESTURE_DRAG_VERTICAL)) {
		float info[4];
		gesture_.GetGestureInfo(GESTURE_DRAG_VERTICAL, info);
		scrollPos_ = scrollStart_ - info[0];
	} else {
		ViewGroup::Touch(input);
	}

	// Clamp scrollPos_. TODO: flinging, bouncing, etc.
	if (scrollPos_ < 0.0f) {
		scrollPos_ = 0.0f;
	}
	float childHeight = views_[0]->GetBounds().h;
	float scrollMax = std::max(0.0f, childHeight - bounds_.h);
	if (scrollPos_ > scrollMax) {
		scrollPos_ = scrollMax;
	}
}

void ScrollView::Draw(DrawContext &dc) {
	dc.PushStencil(bounds_);
	views_[0]->Draw(dc);
	dc.PopStencil();
}

void GridLayout::Layout() {

}

void RelativeLayout::Layout() {

}

void LayoutViewHierarchy(const DrawContext &dc, ViewGroup *root) {
	Bounds rootBounds;
	rootBounds.x = 0;
	rootBounds.y = 0;
	rootBounds.w = dp_xres;
	rootBounds.h = dp_yres;

	MeasureSpec horiz(EXACTLY, rootBounds.w);
	MeasureSpec vert(EXACTLY, rootBounds.h);

	// Two phases - measure contents, layout.
	root->Measure(dc, horiz, vert);
	// Root has a specified size. Set it, then let root layout all its children.
	root->SetBounds(rootBounds);
	root->Layout();
}

void UpdateViewHierarchy(const InputState &input_state, ViewGroup *root) {
	if (input_state.pad_buttons_down & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN))
		EnableFocusMovement(true);

	if (input_state.pad_last_buttons == 0) {
		if (input_state.pad_buttons_down & PAD_BUTTON_RIGHT)
			root->MoveFocus(FOCUS_RIGHT);
		if (input_state.pad_buttons_down & PAD_BUTTON_UP)
			root->MoveFocus(FOCUS_UP);
		if (input_state.pad_buttons_down & PAD_BUTTON_LEFT)
			root->MoveFocus(FOCUS_LEFT);
		if (input_state.pad_buttons_down & PAD_BUTTON_DOWN)
			root->MoveFocus(FOCUS_DOWN);
	}

	root->Update(input_state);
}

}  // namespace UI