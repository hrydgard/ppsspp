#include <set>

#include "base/functional.h"
#include "base/logging.h"
#include "base/mutex.h"
#include "base/stringutil.h"
#include "base/timeutil.h"
#include "input/keycodes.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "gfx_es2/draw_buffer.h"

#include <algorithm>

namespace UI {

const float ITEM_HEIGHT = 64.f;

static recursive_mutex focusLock;
static std::vector<int> focusMoves;
extern bool focusForced;
bool dragCaptured[MAX_POINTERS];

void CaptureDrag(int id) {
	dragCaptured[id] = true;
}

void ReleaseDrag(int id) {
	dragCaptured[id] = false;
}

bool IsDragCaptured(int id) {
	return dragCaptured[id];
}

void ApplyGravity(const Bounds outer, const Margins &margins, float w, float h, int gravity, Bounds &inner) {
	inner.w = w;
	inner.h = h;

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
	Clear();
}

void ViewGroup::RemoveSubview(View *view) {
	lock_guard guard(modifyLock_);
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view) {
			views_.erase(views_.begin() + i);
			delete view;
			return;
		}
	}
}

void ViewGroup::Clear() {
	lock_guard guard(modifyLock_);
	for (size_t i = 0; i < views_.size(); i++) {
		delete views_[i];
		views_[i] = nullptr;
	}
	views_.clear();
}

void ViewGroup::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	lock_guard guard(modifyLock_);

	std::string tag = Tag();
	if (tag.empty()) {
		tag = anonId;
	}

	ITOA stringify;
	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->PersistData(status, tag + "/" + stringify.p((int)i), storage);
	}
}

void ViewGroup::Touch(const TouchInput &input) {
	lock_guard guard(modifyLock_);
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() == V_VISIBLE)
			(*iter)->Touch(input);
	}
}

void ViewGroup::Query(float x, float y, std::vector<View *> &list) {
	if (bounds_.Contains(x, y)) {
		list.push_back(this);
		for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
			(*iter)->Query(x, y, list);
		}
	}
}

bool ViewGroup::Key(const KeyInput &input) {
	lock_guard guard(modifyLock_);
	bool ret = false;
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() == V_VISIBLE)
			ret = ret || (*iter)->Key(input);
	}
	return ret;
}

void ViewGroup::Axis(const AxisInput &input) {
	lock_guard guard(modifyLock_);
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() == V_VISIBLE)
			(*iter)->Axis(input);
	}
}

void ViewGroup::Draw(UIContext &dc) {
	if (hasDropShadow_) {
		// Darken things behind.
		dc.FillRect(UI::Drawable(0x60000000), dc.GetBounds());
		float dropsize = 30;
		dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid,
			bounds_.x - dropsize, bounds_.y,
			bounds_.x2() + dropsize, bounds_.y2()+dropsize*1.5, 0xDF000000, 3.0f);
	}

	if (clip_) {
		dc.PushScissor(bounds_);
	}

	dc.FillRect(bg_, bounds_);
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() == V_VISIBLE) {
			// Check if bounds are in current scissor rectangle.
			if (dc.GetScissorBounds().Intersects((*iter)->GetBounds()))
				(*iter)->Draw(dc);
		}
	}
	if (clip_) {
		dc.PopScissor();
	}
}

void ViewGroup::Update(const InputState &input_state) {
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() != V_GONE)
			(*iter)->Update(input_state);
	}
}

bool ViewGroup::SetFocus() {
	lock_guard guard(modifyLock_);
	if (!CanBeFocused() && !views_.empty()) {
		for (size_t i = 0; i < views_.size(); i++) {
			if (views_[i]->SetFocus())
				return true;
		}
	}
	return false;
}

bool ViewGroup::SubviewFocused(View *view) {
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view)
			return true;
		if (views_[i]->SubviewFocused(view))
			return true;
	}
	return false;
}

static float HorizontalOverlap(const Bounds &a, const Bounds &b) {
	if (a.x2() < b.x || b.x2() < a.x)
		return 0.0f;
	// okay they do overlap. Let's clip.
	float maxMin = std::max(a.x, b.x);
	float minMax = std::min(a.x2(), b.x2());
	float overlap = minMax - maxMin;
	if (overlap < 0.0f)
		return 0.0f;
	else
		return std::min(1.0f, overlap / std::min(a.w, b.w));
}

// Returns the percentage the smaller one overlaps the bigger one.
static float VerticalOverlap(const Bounds &a, const Bounds &b) {
	if (a.y2() < b.y || b.y2() < a.y)
		return 0.0f;
	// okay they do overlap. Let's clip.
	float maxMin = std::max(a.y, b.y);
	float minMax = std::min(a.y2(), b.y2());
	float overlap = minMax - maxMin;
	if (overlap < 0.0f)
		return 0.0f;
	else
		return std::min(1.0f, overlap / std::min(a.h, b.h));
}

float GetDirectionScore(View *origin, View *destination, FocusDirection direction) {
	// Skip labels and things like that.
	if (!destination->CanBeFocused())
		return 0.0f;
	if (destination->IsEnabled() == false)
		return 0.0f;
	if (destination->GetVisibility() != V_VISIBLE)
		return 0.0f;

	Point originPos = origin->GetFocusPosition(direction);
	Point destPos = destination->GetFocusPosition(Opposite(direction));

	float dx = destPos.x - originPos.x;
	float dy = destPos.y - originPos.y;

	float distance = sqrtf(dx*dx + dy*dy);
	float overlap = 0.0f;
	float dirX = dx / distance;
	float dirY = dy / distance;

	bool wrongDirection = false;
	bool vertical = false;
	float horizOverlap = HorizontalOverlap(origin->GetBounds(), destination->GetBounds());
	float vertOverlap = VerticalOverlap(origin->GetBounds(), destination->GetBounds());
	if (horizOverlap == 1.0f && vertOverlap == 1.0f) {
		ILOG("Contain overlap");
		return 0.0;
	}
	float originSize = 0.0f;
	switch (direction) {
	case FOCUS_LEFT:
		overlap = vertOverlap;
		originSize = origin->GetBounds().w;
		if (dirX > 0.0f) {
			wrongDirection = true;
		}
		break;
	case FOCUS_UP:
		overlap = horizOverlap;
		originSize = origin->GetBounds().h;
		if (dirY > 0.0f) {
			wrongDirection = true;
		}
		vertical = true;
		break;
	case FOCUS_RIGHT:
		overlap = vertOverlap;
		originSize = origin->GetBounds().w;
		if (dirX < 0.0f) {
			wrongDirection = true;
		}
		break;
	case FOCUS_DOWN:
		overlap = horizOverlap;
		originSize = origin->GetBounds().h;
		if (dirY < 0.0f) {
			wrongDirection = true;
		}
		vertical = true;
		break;
	case FOCUS_PREV:
	case FOCUS_NEXT:
		ELOG("Invalid focus direction");
		break;
	}

	// Add a small bonus if the views are the same size. This prioritizes moving to the next item
	// upwards in a scroll view instead of moving up to the top bar.
	float distanceBonus = 0.0f;
	if (vertical) {
		float widthDifference = origin->GetBounds().w - destination->GetBounds().w;
		if (widthDifference == 0) {
			distanceBonus = 40;
		}
	} else {
		float heightDifference = origin->GetBounds().h - destination->GetBounds().h;
		if (heightDifference == 0) {
			distanceBonus = 40;
		}
	}

	// At large distances, ignore overlap.
	if (distance > 2 * originSize)
		overlap = 0;

	if (wrongDirection)
		return 0.0f;
	else
		return 10.0f / std::max(1.0f, distance - distanceBonus) + overlap;
}

NeighborResult ViewGroup::FindNeighbor(View *view, FocusDirection direction, NeighborResult result) {
	if (!IsEnabled())
		return result;
	if (GetVisibility() != V_VISIBLE)
		return result;

	// First, find the position of the view in the list.
	int num = -1;
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view) {
			num = (int)i;
			break;
		}
	}

	// TODO: Do the cardinal directions right. Now we just map to
	// prev/next.

	switch (direction) {
	case FOCUS_PREV:
		// If view not found, no neighbor to find.
		if (num == -1)
			return NeighborResult(0, 0.0f);
		return NeighborResult(views_[(num + views_.size() - 1) % views_.size()], 0.0f);

	case FOCUS_NEXT:
		// If view not found, no neighbor to find.
		if (num == -1)
			return NeighborResult(0, 0.0f);
		return NeighborResult(views_[(num + 1) % views_.size()], 0.0f);

	case FOCUS_UP:
	case FOCUS_LEFT:
	case FOCUS_RIGHT:
	case FOCUS_DOWN:
		{
			// First, try the child views themselves as candidates
			for (size_t i = 0; i < views_.size(); i++) {
				if (views_[i] == view)
					continue;

				float score = GetDirectionScore(view, views_[i], direction);
				if (score > result.score) {
					result.score = score;
					result.view = views_[i];
				}
			}

			// Then go right ahead and see if any of the children contain any better candidates.
			for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
				if ((*iter)->IsViewGroup()) {
					ViewGroup *vg = static_cast<ViewGroup *>(*iter);
					if (vg)
						result = vg->FindNeighbor(view, direction, result);
				}
			}

			// Boost neighbors with the same parent
			if (num != -1) {
				//result.score += 100.0f;
			}

			return result;
		}

	default:
		return result;
	}
}

void MoveFocus(ViewGroup *root, FocusDirection direction) {
	if (!GetFocusedView()) {
		// Nothing was focused when we got in here. Focus the first non-group in the hierarchy.
		root->SetFocus();
		return;
	}

	NeighborResult neigh(0, 0);
	neigh = root->FindNeighbor(GetFocusedView(), direction, neigh);

	if (neigh.view) {
		neigh.view->SetFocus();
		root->SubviewFocused(neigh.view);
	}
}

// TODO: This code needs some cleanup/restructuring...
void LinearLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);

	if (views_.empty())
		return;

	float sum = 0.0f;
	float maxOther = 0.0f;
	float totalWeight = 0.0f;
	float weightSum = 0.0f;
	float weightZeroSum = 0.0f;

	int numVisible = 0;

	for (View *view : views_) {
		if (view->GetVisibility() == V_GONE)
			continue;
		numVisible++;

		const LinearLayoutParams *linLayoutParams = view->GetLayoutParams()->As<LinearLayoutParams>();

		Margins margins = defaultMargins_;

		if (linLayoutParams) {
			totalWeight += linLayoutParams->weight;
			if (linLayoutParams->HasMargins())
				margins = linLayoutParams->margins;
		}

		if (orientation_ == ORIENT_HORIZONTAL) {
			MeasureSpec v = vert;
			if (v.type == UNSPECIFIED && measuredHeight_ != 0.0f)
				v = MeasureSpec(AT_MOST, measuredHeight_);
			view->Measure(dc, MeasureSpec(UNSPECIFIED, measuredWidth_), v - (float)margins.vert());
		} else if (orientation_ == ORIENT_VERTICAL) {
			MeasureSpec h = horiz;
			if (h.type == UNSPECIFIED && measuredWidth_ != 0.0f)
				h = MeasureSpec(AT_MOST, measuredWidth_);
			view->Measure(dc, h - (float)margins.horiz(), MeasureSpec(UNSPECIFIED, measuredHeight_));
		}

		float amount;
		if (orientation_ == ORIENT_HORIZONTAL) {
			amount = view->GetMeasuredWidth() + margins.horiz();
			maxOther = std::max(maxOther, view->GetMeasuredHeight() + margins.vert());
		} else {
			amount = view->GetMeasuredHeight() + margins.vert();
			maxOther = std::max(maxOther, view->GetMeasuredWidth() + margins.horiz());
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

	weightZeroSum += spacing_ * (numVisible - 1);

	// Alright, got the sum. Let's take the remaining space after the fixed-size views,
	// and distribute among the weighted ones.
	if (orientation_ == ORIENT_HORIZONTAL) {
		MeasureBySpec(layoutParams_->width, weightZeroSum, horiz, &measuredWidth_);

		// If we've got stretch, allow growing to fill the parent.
		float allowedWidth = measuredWidth_;
		if (horiz.type == AT_MOST && measuredWidth_ < horiz.size) {
			allowedWidth = horiz.size;
		}

		float unit = (allowedWidth - weightZeroSum) / weightSum;
		float usedWidth = 0.0f;

		// Redistribute the stretchy ones! and remeasure the children!
		for (View *view : views_) {
			if (view->GetVisibility() == V_GONE)
				continue;
			const LinearLayoutParams *linLayoutParams = view->GetLayoutParams()->As<LinearLayoutParams>();

			if (linLayoutParams && linLayoutParams->weight > 0.0f) {
				Margins margins = defaultMargins_;
				if (linLayoutParams->HasMargins())
					margins = linLayoutParams->margins;
				MeasureSpec v = vert;
				if (v.type == UNSPECIFIED && measuredHeight_ != 0.0f)
					v = MeasureSpec(AT_MOST, measuredHeight_);
				MeasureSpec h(AT_MOST, unit * linLayoutParams->weight - margins.horiz());
				if (horiz.type == EXACTLY) {
					h.type = EXACTLY;
				}
				view->Measure(dc, h, v - (float)margins.vert());
				usedWidth += view->GetMeasuredWidth();
				maxOther = std::max(maxOther, view->GetMeasuredHeight() + margins.vert());
			}
		}

		if (horiz.type == AT_MOST && measuredWidth_ < horiz.size) {
			measuredWidth_ += usedWidth;
		}

		// Measure here in case maxOther moved (can happen due to word wrap.)
		MeasureBySpec(layoutParams_->height, maxOther, vert, &measuredHeight_);
	} else {
		MeasureBySpec(layoutParams_->height, weightZeroSum, vert, &measuredHeight_);

		// If we've got stretch, allow growing to fill the parent.
		float allowedHeight = measuredHeight_;
		if (vert.type == AT_MOST && measuredHeight_ < vert.size) {
			allowedHeight = vert.size;
		}

		float unit = (allowedHeight - weightZeroSum) / weightSum;
		float usedHeight = 0.0f;

		// Redistribute the stretchy ones! and remeasure the children!
		for (View *view : views_) {
			if (view->GetVisibility() == V_GONE)
				continue;
			const LinearLayoutParams *linLayoutParams = view->GetLayoutParams()->As<LinearLayoutParams>();

			if (linLayoutParams && linLayoutParams->weight > 0.0f) {
				Margins margins = defaultMargins_;
				if (linLayoutParams->HasMargins())
					margins = linLayoutParams->margins;
				MeasureSpec h = horiz;
				if (h.type == UNSPECIFIED && measuredWidth_ != 0.0f)
					h = MeasureSpec(AT_MOST, measuredWidth_);
				MeasureSpec v(AT_MOST, unit * linLayoutParams->weight - margins.vert());
				if (vert.type == EXACTLY) {
					v.type = EXACTLY;
				}
				view->Measure(dc, h - (float)margins.horiz(), v);
				usedHeight += view->GetMeasuredHeight();
				maxOther = std::max(maxOther, view->GetMeasuredWidth() + margins.horiz());
			}
		}

		if (vert.type == AT_MOST && measuredHeight_ < vert.size) {
			measuredHeight_ += usedHeight;
		}

		// Measure here in case maxOther moved (can happen due to word wrap.)
		MeasureBySpec(layoutParams_->width, maxOther, horiz, &measuredWidth_);
	}
}

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
		if (views_[i]->GetVisibility() == V_GONE)
			continue;

		const LinearLayoutParams *linLayoutParams = views_[i]->GetLayoutParams()->As<LinearLayoutParams>();

		Gravity gravity = G_TOPLEFT;
		Margins margins = defaultMargins_;
		if (linLayoutParams) {
			if (linLayoutParams->HasMargins())
				margins = linLayoutParams->margins;
			gravity = linLayoutParams->gravity;
		}

		if (orientation_ == ORIENT_HORIZONTAL) {
			itemBounds.x = pos;
			itemBounds.w = views_[i]->GetMeasuredWidth() + margins.horiz();
		} else {
			itemBounds.y = pos;
			itemBounds.h = views_[i]->GetMeasuredHeight() + margins.vert();
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

void FrameLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	if (views_.empty()) {
		MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
		MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);
		return;
	}

	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i]->GetVisibility() == V_GONE)
			continue;
		views_[i]->Measure(dc, horiz, vert);
	}
}

void FrameLayout::Layout() {
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i]->GetVisibility() == V_GONE)
			continue;
		float w = views_[i]->GetMeasuredWidth();
		float h = views_[i]->GetMeasuredHeight();

		Bounds bounds;
		bounds.w = w;
		bounds.h = h;

		bounds.x = bounds_.x + (measuredWidth_ - w) / 2;
		bounds.y = bounds_.y + (measuredWidth_ - h) / 2;
		views_[i]->SetBounds(bounds);
	}
}

void ScrollView::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	// Respect margins
	Margins margins;
	if (views_.size()) {
		const LinearLayoutParams *linLayoutParams = views_[0]->GetLayoutParams()->As<LinearLayoutParams>();
		if (linLayoutParams) {
			margins = linLayoutParams->margins;
		}
	}

	// The scroll view itself simply obeys its parent - but also tries to fit the child if possible.
	MeasureBySpec(layoutParams_->width, horiz.size, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, vert.size, vert, &measuredHeight_);

	if (views_.size()) {
		if (orientation_ == ORIENT_HORIZONTAL) {
			MeasureSpec v = MeasureSpec(AT_MOST, measuredHeight_ - margins.vert());
			if (measuredHeight_ == 0.0f && layoutParams_->height == WRAP_CONTENT) {
				v.type = UNSPECIFIED;
			}
			views_[0]->Measure(dc, MeasureSpec(UNSPECIFIED, measuredWidth_), v);
			MeasureBySpec(layoutParams_->height, views_[0]->GetMeasuredHeight(), vert, &measuredHeight_);
		} else {
			MeasureSpec h = MeasureSpec(AT_MOST, measuredWidth_ - margins.horiz());
			if (measuredWidth_ == 0.0f && layoutParams_->width == WRAP_CONTENT) {
				h.type = UNSPECIFIED;
			}
			views_[0]->Measure(dc, h, MeasureSpec(UNSPECIFIED, measuredHeight_));
			MeasureBySpec(layoutParams_->width, views_[0]->GetMeasuredWidth(), horiz, &measuredWidth_);
		}
		if (orientation_ == ORIENT_VERTICAL && vert.type != EXACTLY) {
			if (measuredHeight_ < views_[0]->GetMeasuredHeight()) {
				measuredHeight_ = views_[0]->GetMeasuredHeight();
			}
			if (measuredHeight_ < views_[0]->GetBounds().h) {
				measuredHeight_ = views_[0]->GetBounds().h;
			}
			if (vert.type == AT_MOST && measuredHeight_ > vert.size) {
				measuredHeight_ = vert.size;
			}
		}
	}
}

void ScrollView::Layout() {
	if (!views_.size())
		return;
	Bounds scrolled;

	// Respect margins
	Margins margins;
	const LinearLayoutParams *linLayoutParams = views_[0]->GetLayoutParams()->As<LinearLayoutParams>();
	if (linLayoutParams) {
		margins = linLayoutParams->margins;
	}

	scrolled.w = views_[0]->GetMeasuredWidth() - margins.horiz();
	scrolled.h = views_[0]->GetMeasuredHeight() - margins.vert();

	float layoutScrollPos = ClampedScrollPos(scrollPos_);

	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		if (scrolled.w != lastViewSize_) {
			ScrollTo(0.0f);
			lastViewSize_ = scrolled.w;
		}
		scrolled.x = bounds_.x - layoutScrollPos;
		scrolled.y = bounds_.y + margins.top;
		break;
	case ORIENT_VERTICAL:
		if (scrolled.h != lastViewSize_ && scrollToTopOnSizeChange_) {
			ScrollTo(0.0f);
			lastViewSize_ = scrolled.h;
		}
		scrolled.x = bounds_.x + margins.left;
		scrolled.y = bounds_.y - layoutScrollPos;
		break;
	}

	views_[0]->SetBounds(scrolled);
	views_[0]->Layout();
}

bool ScrollView::Key(const KeyInput &input) {
	if (visibility_ != V_VISIBLE)
		return ViewGroup::Key(input);

	if (input.flags & KEY_DOWN) {
		switch (input.keyCode) {
		case NKCODE_EXT_MOUSEWHEEL_UP:
			ScrollRelative(-250);
			break;
		case NKCODE_EXT_MOUSEWHEEL_DOWN:
			ScrollRelative(250);
			break;
		case NKCODE_PAGE_DOWN:
			ScrollRelative((orientation_ == ORIENT_VERTICAL ? bounds_.h : bounds_.w) - 50);
			break;
		case NKCODE_PAGE_UP:
			ScrollRelative(-(orientation_ == ORIENT_VERTICAL ? bounds_.h : bounds_.w) + 50);
			break;
		case NKCODE_MOVE_HOME:
			ScrollTo(0);
			break;
		case NKCODE_MOVE_END:
			if (views_.size())
				ScrollTo(orientation_ == ORIENT_VERTICAL ? views_[0]->GetBounds().h : views_[0]->GetBounds().w);
			break;
		}
	}
	return ViewGroup::Key(input);
}

const float friction = 0.92f;
const float stop_threshold = 0.1f;

void ScrollView::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && input.id == 0) {
		scrollStart_ = scrollPos_;
		inertia_ = 0.0f;
	}

	Gesture gesture = orientation_ == ORIENT_VERTICAL ? GESTURE_DRAG_VERTICAL : GESTURE_DRAG_HORIZONTAL;

	if (input.flags & TOUCH_UP) {
		float info[4];
		if (!IsDragCaptured(input.id) && gesture_.GetGestureInfo(gesture, info)) {
			inertia_ = info[1];
		}
	}

	TouchInput input2;
	if (CanScroll() && !IsDragCaptured(input.id)) {
		input2 = gesture_.Update(input, bounds_);
		float info[4];
		if (gesture_.GetGestureInfo(gesture, info) && !(input.flags & TOUCH_DOWN)) {
			float pos = scrollStart_ - info[0];
			scrollPos_ = pos;
			scrollTarget_ = pos;
			scrollToTarget_ = false;
		}
	} else {
		input2 = input;
		scrollTarget_ = scrollPos_;
		scrollToTarget_ = false;
	}

	if (!(input.flags & TOUCH_DOWN) || bounds_.Contains(input.x, input.y)) {
		ViewGroup::Touch(input2);
	}
}

void ScrollView::Draw(UIContext &dc) {
	if (!views_.size()) {
		ViewGroup::Draw(dc);
		return;
	}

	dc.PushScissor(bounds_);
	// For debugging layout issues, this can be useful.
	// dc.FillRect(Drawable(0x60FF00FF), bounds_);
	views_[0]->Draw(dc);
	dc.PopScissor();

	float childHeight = views_[0]->GetBounds().h;
	float scrollMax = std::max(0.0f, childHeight - bounds_.h);

	float ratio = bounds_.h / views_[0]->GetBounds().h;

	float bobWidth = 5;
	if (ratio < 1.0f && scrollMax > 0.0f) {
		float bobHeight = ratio * bounds_.h;
		float bobOffset = (ClampedScrollPos(scrollPos_) / scrollMax) * (bounds_.h - bobHeight);

		Bounds bob(bounds_.x2() - bobWidth, bounds_.y + bobOffset, bobWidth, bobHeight);
		dc.FillRect(Drawable(0x80FFFFFF), bob);
	}
}

bool ScrollView::SubviewFocused(View *view) {
	if (!ViewGroup::SubviewFocused(view))
		return false;

	const Bounds &vBounds = view->GetBounds();

	// Scroll so that the focused view is visible, and a bit more so that headers etc gets visible too, in most cases.
	const float overscroll = std::min(view->GetBounds().h / 1.5f, GetBounds().h / 4.0f);

	float pos = ClampedScrollPos(scrollPos_);
	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		if (vBounds.x2() > bounds_.x2()) {
			ScrollTo(pos + vBounds.x2() - bounds_.x2() + overscroll);
		}
		if (vBounds.x < bounds_.x) {
			ScrollTo(pos + (vBounds.x - bounds_.x) - overscroll);
		}
		break;
	case ORIENT_VERTICAL:
		if (vBounds.y2() > bounds_.y2()) {
			ScrollTo(pos + vBounds.y2() - bounds_.y2() + overscroll);
		}
		if (vBounds.y < bounds_.y) {
			ScrollTo(pos + (vBounds.y - bounds_.y) - overscroll);
		}
		break;
	}

	return true;
}

void ScrollView::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	ViewGroup::PersistData(status, anonId, storage);

	std::string tag = Tag();
	if (tag.empty()) {
		tag = anonId;
	}

	PersistBuffer &buffer = storage["ScrollView::" + tag];
	switch (status) {
	case PERSIST_SAVE:
		{
			buffer.resize(1);
			float pos = scrollToTarget_ ? scrollTarget_ : scrollPos_;
			// Hmm, ugly... better buffer?
			buffer[0] = *(int *)&pos;
		}
		break;

	case PERSIST_RESTORE:
		if (buffer.size() == 1) {
			float pos = *(float *)&buffer[0];
			scrollPos_ = pos;
			scrollTarget_ = pos;
			scrollToTarget_ = false;
		}
		break;
	}
}

void ScrollView::SetVisibility(Visibility visibility) {
	ViewGroup::SetVisibility(visibility);

	if (visibility == V_GONE) {
		// Since this is no longer shown, forget the scroll position.
		// For example, this happens when switching tabs.
		ScrollTo(0.0f);
	}
}

void ScrollView::ScrollTo(float newScrollPos) {
	scrollTarget_ = newScrollPos;
	scrollToTarget_ = true;
}

void ScrollView::ScrollRelative(float distance) {
	scrollTarget_ = scrollPos_ + distance;
	scrollToTarget_ = true;
}

float ScrollView::ClampedScrollPos(float pos) {
	if (!views_.size()) {
		return 0.0f;
	}

	float childSize = orientation_ == ORIENT_VERTICAL ? views_[0]->GetBounds().h : views_[0]->GetBounds().w;
	float scrollMax = std::max(0.0f, childSize - (orientation_ == ORIENT_VERTICAL ? bounds_.h : bounds_.w));

	Gesture gesture = orientation_ == ORIENT_VERTICAL ? GESTURE_DRAG_VERTICAL : GESTURE_DRAG_HORIZONTAL;

	if (gesture_.IsGestureActive(gesture)) {
		float maxPull = bounds_.h * 0.1f;
		if (pos < 0.0f) {
			float dist = std::min(-pos * (1.0f / bounds_.h), 1.0f);
			pull_ = -(sqrt(dist) * maxPull);
		} else if (pos > scrollMax) {
			float dist = std::min((pos - scrollMax) * (1.0f / bounds_.h), 1.0f);
			pull_ = sqrt(dist) * maxPull;
		} else {
			pull_ = 0.0f;
		}
	}

	if (pos < 0.0f && pos < pull_) {
		pos = pull_;
	}
	if (pos > scrollMax && pos > scrollMax + pull_) {
		pos = scrollMax + pull_;
	}

	return pos;
}

void ScrollView::ScrollToBottom() {
	float childHeight = views_[0]->GetBounds().h;
	float scrollMax = std::max(0.0f, childHeight - bounds_.h);
	scrollPos_ = scrollMax;
	scrollTarget_ = scrollMax;
}

bool ScrollView::CanScroll() const {
	if (!views_.size())
		return false;
	switch (orientation_) {
	case ORIENT_VERTICAL:
		return views_[0]->GetBounds().h > bounds_.h;
	case ORIENT_HORIZONTAL:
		return views_[0]->GetBounds().w > bounds_.w;
	default:
		return false;
	}
}

void ScrollView::Update(const InputState &input_state) {
	if (visibility_ != V_VISIBLE) {
		inertia_ = 0.0f;
	}
	ViewGroup::Update(input_state);

	Gesture gesture = orientation_ == ORIENT_VERTICAL ? GESTURE_DRAG_VERTICAL : GESTURE_DRAG_HORIZONTAL;
	gesture_.UpdateFrame();
	if (scrollToTarget_) {
		float target = ClampedScrollPos(scrollTarget_);

		inertia_ = 0.0f;
		if (fabsf(target - scrollPos_) < 0.5f) {
			scrollPos_ = target;
			scrollToTarget_ = false;
		} else {
			scrollPos_ += (target - scrollPos_) * 0.3f;
		}
	} else if (inertia_ != 0.0f && !gesture_.IsGestureActive(gesture)) {
		scrollPos_ -= inertia_;
		inertia_ *= friction;
		if (fabsf(inertia_) < stop_threshold)
			inertia_ = 0.0f;
	}

	if (!gesture_.IsGestureActive(gesture)) {
		scrollPos_ = ClampedScrollPos(scrollPos_);

		pull_ *= friction;
		if (fabsf(pull_) < 0.01f) {
			pull_ = 0.0f;
		}
	}
}

void AnchorLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);

	for (size_t i = 0; i < views_.size(); i++) {
		Size width = WRAP_CONTENT;
		Size height = WRAP_CONTENT;

		MeasureSpec specW(UNSPECIFIED, measuredWidth_);
		MeasureSpec specH(UNSPECIFIED, measuredHeight_);

		if (!overflow_) {
			if (horiz.type != UNSPECIFIED) {
				specW = MeasureSpec(AT_MOST, horiz.size);
			}
			if (vert.type != UNSPECIFIED) {
				specH = MeasureSpec(AT_MOST, vert.size);
			}
		}

		const AnchorLayoutParams *params = views_[i]->GetLayoutParams()->As<AnchorLayoutParams>();
		if (params) {
			width = params->width;
			height = params->height;

			if (!params->center) {
				if (params->left >= 0 && params->right >= 0) {
					width = measuredWidth_ - params->left - params->right;
				}
				if (params->top >= 0 && params->bottom >= 0) {
					height = measuredHeight_ - params->top - params->bottom;
				}
			}
			if (width >= 0) {
				specW = MeasureSpec(EXACTLY, width);
			}
			if (height >= 0) {
				specH = MeasureSpec(EXACTLY, height);
			}
		}

		views_[i]->Measure(dc, specW, specH);
	}
}

void AnchorLayout::Layout() {
	for (size_t i = 0; i < views_.size(); i++) {
		const AnchorLayoutParams *params = views_[i]->GetLayoutParams()->As<AnchorLayoutParams>();

		Bounds vBounds;
		vBounds.w = views_[i]->GetMeasuredWidth();
		vBounds.h = views_[i]->GetMeasuredHeight();

		// Clamp width/height to our own
		if (vBounds.w > bounds_.w) vBounds.w = bounds_.w;
		if (vBounds.h > bounds_.h) vBounds.h = bounds_.h;

		float left = 0, top = 0, right = 0, bottom = 0, center = false;
		if (params) {
			left = params->left;
			top = params->top;
			right = params->right;
			bottom = params->bottom;
			center = params->center;
		}

		if (left >= 0) {
			vBounds.x = bounds_.x + left;
			if (center)
				vBounds.x -= vBounds.w * 0.5f;
		} else if (right >= 0) {
			vBounds.x = bounds_.x2() - right - vBounds.w;
			if (center) {
				vBounds.x += vBounds.w * 0.5f;
			}
		}

		if (top >= 0) {
			vBounds.y = bounds_.y + top;
			if (center)
				vBounds.y -= vBounds.h * 0.5f;
		} else if (bottom >= 0) {
			vBounds.y = bounds_.y2() - bottom - vBounds.h;
			if (center)
				vBounds.y += vBounds.h * 0.5f;
		}

		views_[i]->SetBounds(vBounds);
		views_[i]->Layout();
	}
}

void GridLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureSpecType measureType = settings_.fillCells ? EXACTLY : AT_MOST;

	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->Measure(dc, MeasureSpec(measureType, settings_.columnWidth), MeasureSpec(measureType, settings_.rowHeight));
	}

	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);

	// Okay, got the width we are supposed to adjust to. Now we can calculate the number of columns.
	numColumns_ = (measuredWidth_ - settings_.spacing) / (settings_.columnWidth + settings_.spacing);
	if (!numColumns_) numColumns_ = 1;
	int numRows = (int)(views_.size() + (numColumns_ - 1)) / numColumns_;

	float estimatedHeight = (settings_.rowHeight + settings_.spacing) * numRows;

	MeasureBySpec(layoutParams_->height, estimatedHeight, vert, &measuredHeight_);
}

void GridLayout::Layout() {
	int y = 0;
	int x = 0;
	int count = 0;
	for (size_t i = 0; i < views_.size(); i++) {
		Bounds itemBounds, innerBounds;

		itemBounds.x = bounds_.x + x;
		itemBounds.y = bounds_.y + y;
		itemBounds.w = settings_.columnWidth;
		itemBounds.h = settings_.rowHeight;

		ApplyGravity(itemBounds, Margins(0.0f),
			views_[i]->GetMeasuredWidth(), views_[i]->GetMeasuredHeight(),
			G_HCENTER | G_VCENTER, innerBounds);

		views_[i]->SetBounds(innerBounds);
		views_[i]->Layout();

		count++;
		if (count == numColumns_) {
			count = 0;
			x = 0;
			y += itemBounds.h + settings_.spacing;
		} else {
			x += itemBounds.w + settings_.spacing;
		}
	}
}

TabHolder::TabHolder(Orientation orientation, float stripSize, LayoutParams *layoutParams)
	: LinearLayout(Opposite(orientation), layoutParams),
		tabStrip_(nullptr), tabScroll_(nullptr),
		stripSize_(stripSize),
		currentTab_(0) {
	SetSpacing(0.0f);
	if (orientation == ORIENT_HORIZONTAL) {
		tabStrip_ = new ChoiceStrip(orientation, new LayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		tabStrip_->SetTopTabs(true);
		tabScroll_ = new ScrollView(orientation, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
		tabScroll_->Add(tabStrip_);
		Add(tabScroll_);
	} else {
		tabStrip_ = new ChoiceStrip(orientation, new LayoutParams(stripSize, WRAP_CONTENT));
		tabStrip_->SetTopTabs(true);
		Add(tabStrip_);
		tabScroll_ = nullptr;
	}
	tabStrip_->OnChoice.Handle(this, &TabHolder::OnTabClick);
}

EventReturn TabHolder::OnTabClick(EventParams &e) {
	// We have e.b set when it was an explicit click action.
	// In that case, we make the view gone and then visible - this scrolls scrollviews to the top.
	if (currentTab_ != (int)e.a || e.b != 0) {
		tabs_[currentTab_]->SetVisibility(V_GONE);
		currentTab_ = e.a;
		tabs_[currentTab_]->SetVisibility(V_VISIBLE);
	}
	return EVENT_DONE;
}

void TabHolder::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	ViewGroup::PersistData(status, anonId, storage);

	std::string tag = Tag();
	if (tag.empty()) {
		tag = anonId;
	}

	PersistBuffer &buffer = storage["TabHolder::" + tag];
	switch (status) {
	case PERSIST_SAVE:
		buffer.resize(1);
		buffer[0] = currentTab_;
		break;

	case PERSIST_RESTORE:
		if (buffer.size() == 1) {
			SetCurrentTab(buffer[0]);
		}
		break;
	}
}

ChoiceStrip::ChoiceStrip(Orientation orientation, LayoutParams *layoutParams)
		: LinearLayout(orientation, layoutParams), selected_(0), topTabs_(false) {
	SetSpacing(0.0f);
}

void ChoiceStrip::AddChoice(const std::string &title) {
	StickyChoice *c = new StickyChoice(title, "",
			orientation_ == ORIENT_HORIZONTAL ?
			nullptr :
			new LinearLayoutParams(FILL_PARENT, ITEM_HEIGHT));
	c->OnClick.Handle(this, &ChoiceStrip::OnChoiceClick);
	Add(c);
	if (selected_ == (int)views_.size() - 1)
		c->Press();
}

void ChoiceStrip::AddChoice(ImageID buttonImage) {
	StickyChoice *c = new StickyChoice(buttonImage,
			orientation_ == ORIENT_HORIZONTAL ?
			nullptr :
			new LinearLayoutParams(FILL_PARENT, ITEM_HEIGHT));
	c->OnClick.Handle(this, &ChoiceStrip::OnChoiceClick);
	Add(c);
	if (selected_ == (int)views_.size() - 1)
		c->Press();
}

EventReturn ChoiceStrip::OnChoiceClick(EventParams &e) {
	// Unstick the other choices that weren't clicked.
	for (int i = 0; i < (int)views_.size(); i++) {
		if (views_[i] != e.v) {
			Choice(i)->Release();
		} else {
			selected_ = i;
		}
	}

	EventParams e2;
	e2.v = views_[selected_];
	e2.a = selected_;
	// Set to 1 to indicate an explicit click.
	e2.b = 1;
	// Dispatch immediately (we're already on the UI thread as we're in an event handler).
	return OnChoice.Dispatch(e2);
}

void ChoiceStrip::SetSelection(int sel) {
	int prevSelected = selected_;
	StickyChoice *prevChoice = Choice(selected_);
	if (prevChoice)
		prevChoice->Release();
	selected_ = sel;
	StickyChoice *newChoice = Choice(selected_);
	if (newChoice) {
		newChoice->Press();

		if (topTabs_ && prevSelected != selected_) {
			EventParams e;
			e.v = views_[selected_];
			e.a = selected_;
			// Set to 0 to indicate a selection change (not a click.)
			e.b = 0;
			OnChoice.Trigger(e);
		}
	}
}

void ChoiceStrip::HighlightChoice(unsigned int choice){
	if (choice < (unsigned int)views_.size()){
		Choice(choice)->HighlightChanged(true);
	}
};

bool ChoiceStrip::Key(const KeyInput &input) {
	bool ret = false;
	if (input.flags & KEY_DOWN) {
		if (IsTabLeftKey(input) && selected_ > 0) {
			SetSelection(selected_ - 1);
			ret = true;
		} else if (IsTabRightKey(input) && selected_ < (int)views_.size() - 1) {
			SetSelection(selected_ + 1);
			ret = true;
		}
	}
	return ret || ViewGroup::Key(input);
}

void ChoiceStrip::Draw(UIContext &dc) {
	ViewGroup::Draw(dc);
	if (topTabs_) {
		if (orientation_ == ORIENT_HORIZONTAL)
			dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x, bounds_.y2() - 4, bounds_.x2(), bounds_.y2(), dc.theme->itemDownStyle.background.color );
		else if (orientation_ == ORIENT_VERTICAL)
			dc.Draw()->DrawImageStretch(dc.theme->whiteImage, bounds_.x2() - 4, bounds_.y, bounds_.x2(), bounds_.y2(), dc.theme->itemDownStyle.background.color );
	}
}

StickyChoice *ChoiceStrip::Choice(int index) {
	if ((size_t)index < views_.size())
		return static_cast<StickyChoice *>(views_[index]);
	return nullptr;
}
ListView::ListView(ListAdaptor *a, std::set<int> hidden, LayoutParams *layoutParams)
	: ScrollView(ORIENT_VERTICAL, layoutParams), adaptor_(a), maxHeight_(0), hidden_(hidden) {

	linLayout_ = new LinearLayout(ORIENT_VERTICAL);
	linLayout_->SetSpacing(0.0f);
	Add(linLayout_);
	CreateAllItems();
}

void ListView::CreateAllItems() {
	linLayout_->Clear();
	// Let's not be clever yet, we'll just create them all up front and add them all in.
	for (int i = 0; i < adaptor_->GetNumItems(); i++) {
		if (hidden_.find(i) == hidden_.end()) {
			View * v = linLayout_->Add(adaptor_->CreateItemView(i));
			adaptor_->AddEventCallback(v, std::bind(&ListView::OnItemCallback, this, i, placeholder::_1));
		}
	}
}

void ListView::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	ScrollView::Measure(dc, horiz, vert);
	if (maxHeight_ > 0 && measuredHeight_ > maxHeight_) {
		measuredHeight_ = maxHeight_;
	}
}

EventReturn ListView::OnItemCallback(int num, EventParams &e) {
	EventParams ev;
	ev.v = 0;
	ev.a = num;
	adaptor_->SetSelected(num);
	View *focused = GetFocusedView();
	OnChoice.Trigger(ev);
	CreateAllItems();
	if (focused)
		SetFocusedView(linLayout_->GetViewByIndex(num));
	return EVENT_DONE;
}

View *ChoiceListAdaptor::CreateItemView(int index) {
	return new Choice(items_[index]);
}

bool ChoiceListAdaptor::AddEventCallback(View *view, std::function<EventReturn(EventParams&)> callback) {
	Choice *choice = (Choice *)view;
	choice->OnClick.Add(callback);
	return EVENT_DONE;
}


View *StringVectorListAdaptor::CreateItemView(int index) {
	return new Choice(items_[index], "", index == selected_);
}

bool StringVectorListAdaptor::AddEventCallback(View *view, std::function<EventReturn(EventParams&)> callback) {
	Choice *choice = (Choice *)view;
	choice->OnClick.Add(callback);
	return EVENT_DONE;
}

void LayoutViewHierarchy(const UIContext &dc, ViewGroup *root) {
	if (!root) {
		ELOG("Tried to layout a view hierarchy from a zero pointer root");
		return;
	}
	const Bounds &rootBounds = dc.GetBounds();

	MeasureSpec horiz(EXACTLY, rootBounds.w);
	MeasureSpec vert(EXACTLY, rootBounds.h);

	// Two phases - measure contents, layout.
	root->Measure(dc, horiz, vert);
	// Root has a specified size. Set it, then let root layout all its children.
	root->SetBounds(rootBounds);
	root->Layout();
}

// TODO: Figure out where this should really live.
// Simple simulation of key repeat on platforms and for gamepads where we don't
// automatically get it.

static int frameCount;

// Ignore deviceId when checking for matches. Turns out that Ouya for example sends
// completely broken input where the original keypresses have deviceId = 10 and the repeats
// have deviceId = 0.
struct HeldKey {
	int key;
	int deviceId;
	double triggerTime;

	// Ignores startTime
	bool operator <(const HeldKey &other) const {
		if (key < other.key) return true;
		return false;
	}
	bool operator ==(const HeldKey &other) const { return key == other.key; }
};

static std::set<HeldKey> heldKeys;

const double repeatDelay = 15 * (1.0 / 60.0f);  // 15 frames like before.
const double repeatInterval = 5 * (1.0 / 60.0f);  // 5 frames like before.

bool KeyEvent(const KeyInput &key, ViewGroup *root) {
	bool retval = false;
	// Ignore repeats for focus moves.
	if ((key.flags & (KEY_DOWN | KEY_IS_REPEAT)) == KEY_DOWN) {
		if (IsDPadKey(key)) {
			// Let's only repeat DPAD initially.
			HeldKey hk;
			hk.key = key.keyCode;
			hk.deviceId = key.deviceId;
			hk.triggerTime = time_now_d() + repeatDelay;

			// Check if the key is already held. If it is, ignore it. This is to avoid
			// multiple key repeat mechanisms colliding.
			if (heldKeys.find(hk) != heldKeys.end()) {
				return false;
			}

			heldKeys.insert(hk);
			lock_guard lock(focusLock);
			focusMoves.push_back(key.keyCode);
			retval = true;
		}
	}
	if (key.flags & KEY_UP) {
		// We ignore the device ID here (in the comparator for HeldKey), due to the Ouya quirk mentioned above.
		if (!heldKeys.empty()) {
			HeldKey hk;
			hk.key = key.keyCode;
			hk.deviceId = key.deviceId;
			hk.triggerTime = 0.0; // irrelevant
			if (heldKeys.find(hk) != heldKeys.end()) {
				heldKeys.erase(hk);
				retval = true;
			}
		}
	}

	retval = root->Key(key);

	// Ignore volume keys and stuff here. Not elegant but need to propagate bools through the view hierarchy as well...
	switch (key.keyCode) {
	case NKCODE_VOLUME_DOWN:
	case NKCODE_VOLUME_UP:
	case NKCODE_VOLUME_MUTE:
		retval = false;
		break;
	}

	return retval;
}

static void ProcessHeldKeys(ViewGroup *root) {
	double now = time_now_d();

restart:

	for (std::set<HeldKey>::iterator iter = heldKeys.begin(); iter != heldKeys.end(); ++iter) {
		if (iter->triggerTime < now) {
			KeyInput key;
			key.keyCode = iter->key;
			key.deviceId = iter->deviceId;
			key.flags = KEY_DOWN;
			KeyEvent(key, root);

			lock_guard lock(focusLock);
			focusMoves.push_back(key.keyCode);

			// Cannot modify the current item when looping over a set, so let's do this instead.
			HeldKey hk = *iter;
			heldKeys.erase(hk);
			hk.triggerTime = now + repeatInterval;
			heldKeys.insert(hk);
			goto restart;
		}
	}
}

bool TouchEvent(const TouchInput &touch, ViewGroup *root) {
	focusForced = false;
	root->Touch(touch);
	if ((touch.flags & TOUCH_DOWN) && !focusForced) {
		EnableFocusMovement(false);
	}
	return true;
}

bool AxisEvent(const AxisInput &axis, ViewGroup *root) {
	root->Axis(axis);
	return true;
}

void UpdateViewHierarchy(const InputState &input_state, ViewGroup *root) {
	ProcessHeldKeys(root);
	frameCount++;

	if (!root) {
		ELOG("Tried to update a view hierarchy from a zero pointer root");
		return;
	}

	if (focusMoves.size()) {
		lock_guard lock(focusLock);
		EnableFocusMovement(true);
		if (!GetFocusedView()) {
			if (root->GetDefaultFocusView()) {
				root->GetDefaultFocusView()->SetFocus();
			} else {
				root->SetFocus();
			}
			root->SubviewFocused(GetFocusedView());
		} else {
			for (size_t i = 0; i < focusMoves.size(); i++) {
				switch (focusMoves[i]) {
					case NKCODE_DPAD_LEFT: MoveFocus(root, FOCUS_LEFT); break;
					case NKCODE_DPAD_RIGHT: MoveFocus(root, FOCUS_RIGHT); break;
					case NKCODE_DPAD_UP: MoveFocus(root, FOCUS_UP); break;
					case NKCODE_DPAD_DOWN: MoveFocus(root, FOCUS_DOWN); break;
				}
			}
		}
		focusMoves.clear();
	}

	root->Update(input_state);
	DispatchEvents();
}

}  // namespace UI
