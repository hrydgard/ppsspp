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

void FrameLayout::Measure(const DrawContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	if (views_.empty()) {
		ELOG("A FrameLayout must have a child view");
		return;
	}
}

void LinearLayout::Measure(const DrawContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	if (!views_.size()) {
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
		MeasureBySpec(layoutParams_->width, sum, horiz, &measuredWidth_);
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
		MeasureBySpec(layoutParams_->height, sum, vert, &measuredHeight_);
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

void ScrollView::Measure(const DrawContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	
}

void ScrollView::Layout() {
	Bounds scrolled = bounds_;

	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		scrolled.x -= scrollPos_;
		break;
	case ORIENT_VERTICAL:
		scrolled.y -= scrollPos_;
		break;
	}
	views_[0]->SetBounds(scrolled);
}

void ScrollView::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && input.id == 0) {
		scrollStart_ = orientation_ == ORIENT_HORIZONTAL ? input.x : input.y;
	}

	gesture_.Update(input);

	if (gesture_.IsGestureActive(GESTURE_DRAG_VERTICAL)) {
		float info[4];
		gesture_.GetGestureInfo(GESTURE_DRAG_VERTICAL, info);
	}
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

}  // namespace UI