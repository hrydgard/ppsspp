#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>

#include "Common/Data/Text/I18n.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Math/curves.h"
#include "Common/UI/Context.h"
#include "Common/UI/Tween.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"

namespace UI {

static constexpr Size ITEM_HEIGHT = 64.f;

void ApplyGravity(const Bounds outer, const Margins &margins, float w, float h, int gravity, Bounds &inner) {
	inner.w = w;
	inner.h = h;

	switch (gravity & G_HORIZMASK) {
	case G_LEFT: inner.x = outer.x + margins.left; break;
	case G_RIGHT: inner.x = outer.x + outer.w - w - margins.right; break;
	case G_HCENTER: inner.x = outer.x + (outer.w - w) * 0.5f; break;
	}

	switch (gravity & G_VERTMASK) {
	case G_TOP: inner.y = outer.y + margins.top; break;
	case G_BOTTOM: inner.y = outer.y + outer.h - h - margins.bottom; break;
	case G_VCENTER: inner.y = outer.y + (outer.h - h) * 0.5f; break;
	}
}

ViewGroup::~ViewGroup() {
	// Tear down the contents recursively.
	Clear();
}

void ViewGroup::RemoveSubview(View *view) {
	std::lock_guard<std::mutex> guard(modifyLock_);
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view) {
			views_.erase(views_.begin() + i);
			delete view;
			return;
		}
	}
}

bool ViewGroup::ContainsSubview(const View *view) const {
	for (const View *subview : views_) {
		if (subview == view || subview->ContainsSubview(view))
			return true;
	}
	return false;
}

void ViewGroup::Clear() {
	std::lock_guard<std::mutex> guard(modifyLock_);
	for (size_t i = 0; i < views_.size(); i++) {
		delete views_[i];
		views_[i] = nullptr;
	}
	views_.clear();
}

void ViewGroup::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	std::lock_guard<std::mutex> guard(modifyLock_);

	std::string tag = Tag();
	if (tag.empty()) {
		tag = anonId;
	}

	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->PersistData(status, tag + "/" + StringFromInt((int)i), storage);
	}
}

bool ViewGroup::Touch(const TouchInput &input) {
	std::lock_guard<std::mutex> guard(modifyLock_);
	bool any = false;
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() == V_VISIBLE) {
			bool touch = (*iter)->Touch(input);
			any = any || touch;
			if (exclusiveTouch_ && touch && (input.flags & TOUCH_DOWN)) {
				break;
			}
		}
	}
	if (clickableBackground_) {
		return any || bounds_.Contains(input.x, input.y);
	} else {
		return any;
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
	std::lock_guard<std::mutex> guard(modifyLock_);
	bool ret = false;
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() == V_VISIBLE)
			ret = ret || (*iter)->Key(input);
	}
	return ret;
}

void ViewGroup::Axis(const AxisInput &input) {
	std::lock_guard<std::mutex> guard(modifyLock_);
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if ((*iter)->GetVisibility() == V_VISIBLE)
			(*iter)->Axis(input);
	}
}

void ViewGroup::DeviceLost() {
	std::lock_guard<std::mutex> guard(modifyLock_);
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		(*iter)->DeviceLost();
	}
}

void ViewGroup::DeviceRestored(Draw::DrawContext *draw) {
	std::lock_guard<std::mutex> guard(modifyLock_);
	for (auto iter = views_.begin(); iter != views_.end(); ++iter) {
		(*iter)->DeviceRestored(draw);
	}
}

void ViewGroup::Draw(UIContext &dc) {
	if (hasDropShadow_) {
		// Darken things behind.
		dc.FillRect(UI::Drawable(0x60000000), dc.GetBounds().Expand(dropShadowExpand_));
		float dropsize = 30.0f;
		dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid,
			bounds_.x - dropsize, bounds_.y,
			bounds_.x2() + dropsize, bounds_.y2()+dropsize*1.5f, 0xDF000000, 3.0f);
	}

	if (clip_) {
		dc.PushScissor(bounds_);
	}

	dc.FillRect(bg_, bounds_);
	for (View *view : views_) {
		if (view->GetVisibility() == V_VISIBLE) {
			// Check if bounds are in current scissor rectangle.
			if (dc.GetScissorBounds().Intersects(dc.TransformBounds(view->GetBounds())))
				view->Draw(dc);
		}
	}
	if (clip_) {
		dc.PopScissor();
	}
}

std::string ViewGroup::DescribeText() const {
	std::stringstream ss;
	bool needNewline = false;
	for (View *view : views_) {
		if (view->GetVisibility() != V_VISIBLE)
			continue;
		std::string s = view->DescribeText();
		if (s.empty())
			continue;

		if (needNewline) {
			ss << "\n";
		}
		ss << s;
		needNewline = s[s.length() - 1] != '\n';
	}
	return ss.str();
}

std::string ViewGroup::DescribeListUnordered(const char *heading) const {
	std::stringstream ss;
	ss << heading << "\n";

	bool needNewline = false;
	for (View *view : views_) {
		if (view->GetVisibility() != V_VISIBLE)
			continue;
		std::string s = view->DescribeText();
		if (s.empty())
			continue;

		ss << " - " << IndentString(s, "   ", true);
	}
	return ss.str();
}

std::string ViewGroup::DescribeListOrdered(const char *heading) const {
	std::stringstream ss;
	ss << heading << "\n";

	// This is how much space we need for the highest number.
	int sz = (int)floorf(log10f((float)views_.size())) + 1;
	std::string indent = "  " + std::string(sz, ' ');

	bool needNewline = false;
	int n = 1;
	for (View *view : views_) {
		if (view->GetVisibility() != V_VISIBLE)
			continue;
		std::string s = view->DescribeText();
		if (s.empty())
			continue;

		ss << std::setw(sz) << n++ << ". " << IndentString(s, indent, true);
	}
	return ss.str();
}

void ViewGroup::Update() {
	View::Update();
	for (View *view : views_) {
		if (view->GetVisibility() != V_GONE)
			view->Update();
	}
}

bool ViewGroup::SetFocus() {
	std::lock_guard<std::mutex> guard(modifyLock_);
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

// Returns the percentage the smaller one overlaps the bigger one.
static float HorizontalOverlap(const Bounds &a, const Bounds &b) {
	if (a.x2() < b.x || b.x2() < a.x)
		return 0.0f;
	// okay they do overlap. Let's clip.
	float maxMin = std::max(a.x, b.x);
	float minMax = std::min(a.x2(), b.x2());
	float minW = std::min(a.w, b.w);
	float overlap = minMax - maxMin;
	if (overlap < 0.0f || minW <= 0.0f)
		return 0.0f;
	else
		return std::min(1.0f, overlap / minW);
}

// Returns the percentage the smaller one overlaps the bigger one.
static float VerticalOverlap(const Bounds &a, const Bounds &b) {
	if (a.y2() < b.y || b.y2() < a.y)
		return 0.0f;
	// okay they do overlap. Let's clip.
	float maxMin = std::max(a.y, b.y);
	float minMax = std::min(a.y2(), b.y2());
	float minH = std::min(a.h, b.h);
	float overlap = minMax - maxMin;
	if (overlap < 0.0f || minH <= 0.0f)
		return 0.0f;
	else
		return std::min(1.0f, overlap / minH);
}

float GetTargetScore(const Point &originPos, int originIndex, const View *origin, const View *destination, FocusDirection direction) {
	// Skip labels and things like that.
	if (!destination->CanBeFocused())
		return 0.0f;
	if (destination->IsEnabled() == false)
		return 0.0f;
	if (destination->GetVisibility() != V_VISIBLE)
		return 0.0f;

	Point destPos = destination->GetFocusPosition(Opposite(direction));

	float dx = destPos.x - originPos.x;
	float dy = destPos.y - originPos.y;

	float distance = sqrtf(dx*dx + dy*dy);
	if (distance == 0.0f) {
		distance = 0.001f;
	}
	float overlap = 0.0f;
	float dirX = dx / distance;
	float dirY = dy / distance;

	bool wrongDirection = false;
	bool vertical = false;
	float horizOverlap = HorizontalOverlap(origin->GetBounds(), destination->GetBounds());
	float vertOverlap = VerticalOverlap(origin->GetBounds(), destination->GetBounds());
	if (horizOverlap == 1.0f && vertOverlap == 1.0f) {
		if (direction != FOCUS_PREV_PAGE && direction != FOCUS_NEXT_PAGE) {
			INFO_LOG(SYSTEM, "Contain overlap");
			return 0.0;
		}
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
	case FOCUS_FIRST:
		if (originIndex == -1)
			return 0.0f;
		if (dirX > 0.0f || dirY > 0.0f)
			return 0.0f;
		// More distance is good.
		return distance;
	case FOCUS_LAST:
		if (originIndex == -1)
			return 0.0f;
		if (dirX < 0.0f || dirY < 0.0f)
			return 0.0f;
		// More distance is good.
		return distance;
	case FOCUS_PREV_PAGE:
	case FOCUS_NEXT_PAGE:
		// Not always, but let's go with the bonus on height.
		vertical = true;
		break;
	case FOCUS_PREV:
	case FOCUS_NEXT:
		ERROR_LOG(SYSTEM, "Invalid focus direction");
		break;
	}

	// At large distances, ignore overlap.
	if (distance > 2.0 * originSize)
		overlap = 0.0f;

	if (wrongDirection) {
		return 0.0f;
	} else {
		return 10.0f / std::max(1.0f, distance) + overlap * 2.0;
	}
}

static float GetDirectionScore(int originIndex, const View *origin, View *destination, FocusDirection direction) {
	Point originPos = origin->GetFocusPosition(direction);
	return GetTargetScore(originPos, originIndex, origin, destination, direction);
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

	if (direction == FOCUS_PREV || direction == FOCUS_NEXT) {
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
		default:
			return NeighborResult(nullptr, 0.0f);
		}
	}

	switch (direction) {
	case FOCUS_UP:
	case FOCUS_LEFT:
	case FOCUS_RIGHT:
	case FOCUS_DOWN:
	case FOCUS_FIRST:
	case FOCUS_LAST:
		{
			// First, try the child views themselves as candidates
			for (size_t i = 0; i < views_.size(); i++) {
				if (views_[i] == view)
					continue;

				float score = GetDirectionScore(num, view, views_[i], direction);
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

	case FOCUS_PREV_PAGE:
	case FOCUS_NEXT_PAGE:
		return FindScrollNeighbor(view, Point(INFINITY, INFINITY), direction, result);

	default:
		return result;
	}
}

NeighborResult ViewGroup::FindScrollNeighbor(View *view, const Point &target, FocusDirection direction, NeighborResult best) {
	if (!IsEnabled())
		return best;
	if (GetVisibility() != V_VISIBLE)
		return best;

	if (target.x < INFINITY && target.y < INFINITY) {
		for (auto v : views_) {
			// Note: we consider the origin itself, which might already be the best option.
			float score = GetTargetScore(target, -1, view, v, direction);
			if (score > best.score) {
				best.score = score;
				best.view = v;
			}
		}
	}
	for (auto v : views_) {
		if (v->IsViewGroup()) {
			ViewGroup *vg = static_cast<ViewGroup *>(v);
			if (vg)
				best = vg->FindScrollNeighbor(view, target, direction, best);
		}
	}
	return best;
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

	float weightSum = 0.0f;  // Total sum of weights
	float weightZeroSum = 0.0f;  // Sum of sizes of things with weight 0.0, a bit confusingly named

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
			view->Measure(dc, MeasureSpec(UNSPECIFIED, measuredWidth_), v - (float)margins.vert() - (float)padding.vert());
			if (horiz.type == AT_MOST && view->GetMeasuredWidth() + margins.horiz() + padding.horiz() > horiz.size - weightZeroSum) {
				// Try again, this time with AT_MOST.
				view->Measure(dc, horiz, v - (float)margins.vert() - (float)padding.vert());
			}
		} else if (orientation_ == ORIENT_VERTICAL) {
			MeasureSpec h = horiz;
			if (h.type == UNSPECIFIED && measuredWidth_ != 0.0f)
				h = MeasureSpec(AT_MOST, measuredWidth_);
			view->Measure(dc, h - (float)margins.horiz() - (float)padding.horiz(), MeasureSpec(UNSPECIFIED, measuredHeight_));
			if (vert.type == AT_MOST && view->GetMeasuredHeight() + margins.vert() + padding.horiz() > vert.size - weightZeroSum) {
				// Try again, this time with AT_MOST.
				view->Measure(dc, h - (float)margins.horiz() - (float)padding.horiz(), vert);
			}
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

	weightZeroSum += spacing_ * (numVisible - 1); // +(orientation_ == ORIENT_HORIZONTAL) ? padding.horiz() : padding.vert();

	// Alright, got the sum. Let's take the remaining space after the fixed-size views,
	// and distribute among the weighted ones.
	if (orientation_ == ORIENT_HORIZONTAL) {
		MeasureBySpec(layoutParams_->width, weightZeroSum + padding.horiz(), horiz, &measuredWidth_);

		// If we've got stretch, allow growing to fill the parent.
		float allowedWidth = measuredWidth_;
		if (horiz.type == AT_MOST && measuredWidth_ < horiz.size) {
			allowedWidth = horiz.size;
		}

		float usedWidth = 0.0f + padding.horiz();

		// Redistribute the stretchy ones! and remeasure the children!
		for (View *view : views_) {
			if (view->GetVisibility() == V_GONE)
				continue;
			// FILL_PARENT is not appropriate in this direction. It gets ignored though.
			// We have a bit too many of these due to the hack in the ClickableItem constructor.
			// _dbg_assert_(view->GetLayoutParams()->width != UI::FILL_PARENT);

			const LinearLayoutParams *linLayoutParams = view->GetLayoutParams()->As<LinearLayoutParams>();
			if (linLayoutParams && linLayoutParams->weight > 0.0f) {
				Margins margins = defaultMargins_;
				if (linLayoutParams->HasMargins())
					margins = linLayoutParams->margins;
				MeasureSpec v = vert;
				if (v.type == UNSPECIFIED && measuredHeight_ != 0.0f)
					v = MeasureSpec(AT_MOST, measuredHeight_);
				float unit = (allowedWidth - weightZeroSum) / weightSum;
				if (weightSum == 0.0f) {
					// We must have gotten an inf.
					unit = 1.0f;
				}
				MeasureSpec h(AT_MOST, unit * linLayoutParams->weight - margins.horiz());
				if (horiz.type == EXACTLY) {
					h.type = EXACTLY;
				}
				view->Measure(dc, h, v - (float)margins.vert() - (float)padding.vert());
				usedWidth += view->GetMeasuredWidth();
				maxOther = std::max(maxOther, view->GetMeasuredHeight() + margins.vert());
			}
		}

		if (horiz.type == AT_MOST && measuredWidth_ < horiz.size) {
			measuredWidth_ += usedWidth;
		}

		// Measure here in case maxOther moved (can happen due to word wrap.)
		MeasureBySpec(layoutParams_->height, maxOther + padding.vert(), vert, &measuredHeight_);
	} else {
		MeasureBySpec(layoutParams_->height, weightZeroSum + padding.vert(), vert, &measuredHeight_);

		// If we've got stretch, allow growing to fill the parent.
		float allowedHeight = measuredHeight_;
		if (vert.type == AT_MOST && measuredHeight_ < vert.size) {
			allowedHeight = vert.size;
		}

		float usedHeight = 0.0f + padding.vert();

		// Redistribute the stretchy ones! and remeasure the children!
		for (View *view : views_) {
			if (view->GetVisibility() == V_GONE)
				continue;
			// FILL_PARENT is not appropriate in this direction. It gets ignored though.
			// We have a bit too many of these due to the hack in the ClickableItem constructor.
			// _dbg_assert_(view->GetLayoutParams()->height != UI::FILL_PARENT);

			const LinearLayoutParams *linLayoutParams = view->GetLayoutParams()->As<LinearLayoutParams>();
			if (linLayoutParams && linLayoutParams->weight > 0.0f) {
				Margins margins = defaultMargins_;
				if (linLayoutParams->HasMargins())
					margins = linLayoutParams->margins;
				MeasureSpec h = horiz;
				if (h.type == UNSPECIFIED && measuredWidth_ != 0.0f)
					h = MeasureSpec(AT_MOST, measuredWidth_);
				float unit = (allowedHeight - weightZeroSum) / weightSum;
				if (weightSum == 0.0f) {
					// We must have gotten an inf.
					unit = 1.0f;
				}
				MeasureSpec v(AT_MOST, unit * linLayoutParams->weight - margins.vert());
				if (vert.type == EXACTLY) {
					v.type = EXACTLY;
				}
				view->Measure(dc, h - (float)margins.horiz() - (float)padding.horiz(), v);
				usedHeight += view->GetMeasuredHeight();
				maxOther = std::max(maxOther, view->GetMeasuredWidth() + margins.horiz());
			}
		}

		if (vert.type == AT_MOST && measuredHeight_ < vert.size) {
			measuredHeight_ += usedHeight;
		}

		// Measure here in case maxOther moved (can happen due to word wrap.)
		MeasureBySpec(layoutParams_->width, maxOther + padding.horiz(), horiz, &measuredWidth_);
	}
}

// weight != 0 = fill remaining space.
void LinearLayout::Layout() {
	const Bounds &bounds = bounds_;

	Bounds itemBounds;
	float pos;

	if (orientation_ == ORIENT_HORIZONTAL) {
		pos = bounds.x + padding.left;
		itemBounds.y = bounds.y + padding.top;
		itemBounds.h = measuredHeight_ - padding.vert();
	} else {
		pos = bounds.y + padding.top;
		itemBounds.x = bounds.x + padding.left;
		itemBounds.w = measuredWidth_ - padding.horiz();
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

std::string LinearLayoutList::DescribeText() const {
	auto u = GetI18NCategory("UI Elements");
	return DescribeListOrdered(u->T("List:"));
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

		bounds.x = bounds_.x + (measuredWidth_ - w) * 0.5f;
		bounds.y = bounds_.y + (measuredWidth_ - h) * 0.5f;
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
			if (measuredHeight_ == 0.0f && (vert.type == UNSPECIFIED || layoutParams_->height == WRAP_CONTENT)) {
				v.type = UNSPECIFIED;
			}
			views_[0]->Measure(dc, MeasureSpec(UNSPECIFIED, measuredWidth_), v);
			MeasureBySpec(layoutParams_->height, views_[0]->GetMeasuredHeight(), vert, &measuredHeight_);
			if (layoutParams_->width == WRAP_CONTENT)
				MeasureBySpec(layoutParams_->width, views_[0]->GetMeasuredWidth(), horiz, &measuredWidth_);
		} else {
			MeasureSpec h = MeasureSpec(AT_MOST, measuredWidth_ - margins.horiz());
			if (measuredWidth_ == 0.0f && (horiz.type == UNSPECIFIED || layoutParams_->width == WRAP_CONTENT)) {
				h.type = UNSPECIFIED;
			}
			views_[0]->Measure(dc, h, MeasureSpec(UNSPECIFIED, measuredHeight_));
			MeasureBySpec(layoutParams_->width, views_[0]->GetMeasuredWidth(), horiz, &measuredWidth_);
			if (layoutParams_->height == WRAP_CONTENT)
				MeasureBySpec(layoutParams_->height, views_[0]->GetMeasuredHeight(), vert, &measuredHeight_);
		}
		if (orientation_ == ORIENT_VERTICAL && vert.type != EXACTLY) {
			float bestHeight = std::max(views_[0]->GetMeasuredHeight(), views_[0]->GetBounds().h);
			if (vert.type == AT_MOST)
				bestHeight = std::min(bestHeight, vert.size);

			if (measuredHeight_ < bestHeight && layoutParams_->height < 0.0f) {
				measuredHeight_ = bestHeight;
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

	layoutScrollPos_ = ClampedScrollPos(scrollPos_);

	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		if (scrolled.w != lastViewSize_) {
			if (rememberPos_)
				scrollPos_ = *rememberPos_;
			lastViewSize_ = scrolled.w;
		}
		scrolled.x = bounds_.x - layoutScrollPos_;
		scrolled.y = bounds_.y + margins.top;
		break;
	case ORIENT_VERTICAL:
		if (scrolled.h != lastViewSize_) {
			if (rememberPos_)
				scrollPos_ = *rememberPos_;
			lastViewSize_ = scrolled.h;
		}
		scrolled.x = bounds_.x + margins.left;
		scrolled.y = bounds_.y - layoutScrollPos_;
		break;
	}

	views_[0]->SetBounds(scrolled);
	views_[0]->Layout();
}

bool ScrollView::Key(const KeyInput &input) {
	if (visibility_ != V_VISIBLE)
		return ViewGroup::Key(input);

	float scrollSpeed = 250;
	switch (input.deviceId) {
		case DEVICE_ID_XR_CONTROLLER_LEFT:
		case DEVICE_ID_XR_CONTROLLER_RIGHT:
			scrollSpeed = 50;
			break;
	}

	if (input.flags & KEY_DOWN) {
		switch (input.keyCode) {
		case NKCODE_EXT_MOUSEWHEEL_UP:
			ScrollRelative(-scrollSpeed);
			break;
		case NKCODE_EXT_MOUSEWHEEL_DOWN:
			ScrollRelative(scrollSpeed);
			break;
		}
	}
	return ViewGroup::Key(input);
}

const float friction = 0.92f;
const float stop_threshold = 0.1f;

bool ScrollView::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && scrollTouchId_ == -1) {
		scrollStart_ = scrollPos_;
		inertia_ = 0.0f;
		scrollTouchId_ = input.id;
	}

	Gesture gesture = orientation_ == ORIENT_VERTICAL ? GESTURE_DRAG_VERTICAL : GESTURE_DRAG_HORIZONTAL;

	if ((input.flags & TOUCH_UP) && input.id == scrollTouchId_) {
		float info[4];
		if (gesture_.GetGestureInfo(gesture, input.id, info)) {
			inertia_ = info[1];
		}
		scrollTouchId_ = -1;
	}

	TouchInput input2;
	if (CanScroll()) {
		input2 = gesture_.Update(input, bounds_);
		float info[4];
		if (input.id == scrollTouchId_ && gesture_.GetGestureInfo(gesture, input.id, info) && !(input.flags & TOUCH_DOWN)) {
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
		return ViewGroup::Touch(input2);
	} else {
		return false;
	}
}

void ScrollView::Draw(UIContext &dc) {
	if (!views_.size()) {
		ViewGroup::Draw(dc);
		return;
	}

	dc.PushScissor(bounds_);
	dc.FillRect(bg_, bounds_);

	// For debugging layout issues, this can be useful.
	// dc.FillRect(Drawable(0x60FF00FF), bounds_);
	views_[0]->Draw(dc);
	dc.PopScissor();

	float childHeight = views_[0]->GetBounds().h;
	float scrollMax = std::max(0.0f, childHeight - bounds_.h);

	float ratio = bounds_.h / std::max(0.01f, views_[0]->GetBounds().h);

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
	float visibleSize = orientation_ == ORIENT_VERTICAL ? bounds_.h : bounds_.w;
	float visibleEnd = scrollPos_ + visibleSize;

	float viewStart = 0.0f, viewEnd = 0.0f;
	switch (orientation_) {
	case ORIENT_HORIZONTAL:
		viewStart = layoutScrollPos_ + vBounds.x - bounds_.x;
		viewEnd = layoutScrollPos_ + vBounds.x2() - bounds_.x;
		break;
	case ORIENT_VERTICAL:
		viewStart = layoutScrollPos_ + vBounds.y - bounds_.y;
		viewEnd = layoutScrollPos_ + vBounds.y2() - bounds_.y;
		break;
	}

	if (viewEnd > visibleEnd) {
		ScrollTo(viewEnd - visibleSize + overscroll);
	} else if (viewStart < pos) {
		ScrollTo(viewStart - overscroll);
	}

	return true;
}

NeighborResult ScrollView::FindScrollNeighbor(View *view, const Point &target, FocusDirection direction, NeighborResult best) {
	if (ContainsSubview(view) && views_[0]->IsViewGroup()) {
		ViewGroup *vg = static_cast<ViewGroup *>(views_[0]);
		int found = -1;
		for (int i = 0, n = vg->GetNumSubviews(); i < n; ++i) {
			View *child = vg->GetViewByIndex(i);
			if (child == view || child->ContainsSubview(view)) {
				found = i;
				break;
			}
		}

		// Okay, the previously focused view is inside this.
		if (found != -1) {
			float mult = 0.0f;
			switch (direction) {
			case FOCUS_PREV_PAGE:
				mult = -1.0f;
				break;
			case FOCUS_NEXT_PAGE:
				mult = 1.0f;
				break;
			default:
				break;
			}

			// Okay, now where is our ideal target?
			Point targetPos = view->GetBounds().Center();
			if (orientation_ == ORIENT_VERTICAL)
				targetPos.y += mult * bounds_.h;
			else
				targetPos.x += mult * bounds_.x;

			// Okay, which subview is closest to that?
			best = vg->FindScrollNeighbor(view, targetPos, direction, best);
			// Avoid reselecting the same view.
			if (best.view == view)
				best.view = nullptr;
			return best;
		}
	}

	return ViewGroup::FindScrollNeighbor(view, target, direction, best);
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

	if (visibility == V_GONE && !rememberPos_) {
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
	if (!views_.size() || bounds_.h == 0.0f) {
		return 0.0f;
	}

	float childSize = orientation_ == ORIENT_VERTICAL ? views_[0]->GetBounds().h : views_[0]->GetBounds().w;
	float containerSize = (orientation_ == ORIENT_VERTICAL ? bounds_.h : bounds_.w);
	float scrollMax = std::max(0.0f, childSize - containerSize);

	Gesture gesture = orientation_ == ORIENT_VERTICAL ? GESTURE_DRAG_VERTICAL : GESTURE_DRAG_HORIZONTAL;

	if (scrollTouchId_ >= 0 && gesture_.IsGestureActive(gesture, scrollTouchId_) && bounds_.h > 0.0f) {
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
	if (childSize < containerSize && alignOpposite_) {
		pos = -(containerSize - childSize);
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

float ScrollView::lastScrollPosX = 0;
float ScrollView::lastScrollPosY = 0;

ScrollView::~ScrollView() {
	lastScrollPosX = 0;
	lastScrollPosY = 0;
}

void ScrollView::GetLastScrollPosition(float &x, float &y) {
	x = lastScrollPosX;
	y = lastScrollPosY;
}

void ScrollView::Update() {
	if (visibility_ != V_VISIBLE) {
		inertia_ = 0.0f;
	}
	ViewGroup::Update();
	float oldPos = scrollPos_;

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
	} else if (inertia_ != 0.0f && !gesture_.IsGestureActive(gesture, scrollTouchId_)) {
		scrollPos_ -= inertia_;
		inertia_ *= friction;
		if (fabsf(inertia_) < stop_threshold)
			inertia_ = 0.0f;
	}

	if (!gesture_.IsGestureActive(gesture, scrollTouchId_)) {
		scrollPos_ = ClampedScrollPos(scrollPos_);

		pull_ *= friction;
		if (fabsf(pull_) < 0.01f) {
			pull_ = 0.0f;
		}
	}

	if (oldPos != scrollPos_)
		orientation_ == ORIENT_HORIZONTAL ? lastScrollPosX = scrollPos_ : lastScrollPosY = scrollPos_;

	// We load some lists asynchronously, so don't update the position until it's loaded.
	if (rememberPos_ && ClampedScrollPos(scrollPos_) != ClampedScrollPos(*rememberPos_)) {
		*rememberPos_ = scrollPos_;
	}
}

void AnchorLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureBySpec(layoutParams_->width, 0.0f, horiz, &measuredWidth_);
	MeasureBySpec(layoutParams_->height, 0.0f, vert, &measuredHeight_);

	MeasureViews(dc, horiz, vert);

	const bool unspecifiedWidth = layoutParams_->width == WRAP_CONTENT && (overflow_ || horiz.type == UNSPECIFIED);
	const bool unspecifiedHeight = layoutParams_->height == WRAP_CONTENT && (overflow_ || vert.type == UNSPECIFIED);
	if (unspecifiedWidth || unspecifiedHeight) {
		// Give everything another chance to size, given the new measurements.
		MeasureSpec h = unspecifiedWidth ? MeasureSpec(AT_MOST, measuredWidth_) : horiz;
		MeasureSpec v = unspecifiedHeight ? MeasureSpec(AT_MOST, measuredHeight_) : vert;
		MeasureViews(dc, h, v);
	}
}

void AnchorLayout::MeasureViews(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
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
				if (params->left > NONE && params->right > NONE) {
					width = measuredWidth_ - params->left - params->right;
				}
				if (params->top > NONE && params->bottom > NONE) {
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

		if (layoutParams_->width == WRAP_CONTENT)
			measuredWidth_ = std::max(measuredWidth_, views_[i]->GetMeasuredWidth());
		if (layoutParams_->height == WRAP_CONTENT)
			measuredHeight_ = std::max(measuredHeight_, views_[i]->GetMeasuredHeight());
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

		float left = 0, top = 0, right = 0, bottom = 0;
		bool center = false;
		if (params) {
			left = params->left;
			top = params->top;
			right = params->right;
			bottom = params->bottom;
			center = params->center;
		}

		if (left > NONE) {
			vBounds.x = bounds_.x + left;
			if (center)
				vBounds.x -= vBounds.w * 0.5f;
		} else if (right > NONE) {
			vBounds.x = bounds_.x2() - right - vBounds.w;
			if (center) {
				vBounds.x += vBounds.w * 0.5f;
			}
		} else {
			// Both left and right are NONE. Center.
			vBounds.x = (bounds_.w - vBounds.w) / 2.0f + bounds_.x;
		}

		if (top > NONE) {
			vBounds.y = bounds_.y + top;
			if (center)
				vBounds.y -= vBounds.h * 0.5f;
		} else if (bottom > NONE) {
			vBounds.y = bounds_.y2() - bottom - vBounds.h;
			if (center)
				vBounds.y += vBounds.h * 0.5f;
		} else {
			// Both top and bottom are NONE. Center.
			vBounds.y = (bounds_.h - vBounds.h) / 2.0f + bounds_.y;
		}

		views_[i]->SetBounds(vBounds);
		views_[i]->Layout();
	}
}

GridLayout::GridLayout(GridLayoutSettings settings, LayoutParams *layoutParams)
	: ViewGroup(layoutParams), settings_(settings) {
	if (settings.orientation != ORIENT_HORIZONTAL)
		ERROR_LOG(SYSTEM, "GridLayout: Vertical layouts not yet supported");
}

void GridLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureSpecType measureType = settings_.fillCells ? EXACTLY : AT_MOST;

	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->Measure(dc, MeasureSpec(measureType, settings_.columnWidth), MeasureSpec(measureType, settings_.rowHeight));
	}

	// Use the max possible width so AT_MOST gives us the full size.
	float maxWidth = (settings_.columnWidth + settings_.spacing) * views_.size() + settings_.spacing;
	MeasureBySpec(layoutParams_->width, maxWidth, horiz, &measuredWidth_);

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
		const GridLayoutParams *lp = views_[i]->GetLayoutParams()->As<GridLayoutParams>();
		Bounds itemBounds, innerBounds;
		Gravity grav = lp ? lp->gravity : G_CENTER;

		itemBounds.x = bounds_.x + x;
		itemBounds.y = bounds_.y + y;
		itemBounds.w = settings_.columnWidth;
		itemBounds.h = settings_.rowHeight;

		ApplyGravity(itemBounds, Margins(0.0f),
			views_[i]->GetMeasuredWidth(), views_[i]->GetMeasuredHeight(),
			grav, innerBounds);

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

std::string GridLayoutList::DescribeText() const {
	auto u = GetI18NCategory("UI Elements");
	return DescribeListOrdered(u->T("List:"));
}

TabHolder::TabHolder(Orientation orientation, float stripSize, LayoutParams *layoutParams)
	: LinearLayout(Opposite(orientation), layoutParams), stripSize_(stripSize) {
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
	}
	tabStrip_->OnChoice.Handle(this, &TabHolder::OnTabClick);

	contents_ = new AnchorLayout(new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	Add(contents_)->SetClip(true);
}

void TabHolder::AddTabContents(const std::string &title, View *tabContents) {
	tabContents->ReplaceLayoutParams(new AnchorLayoutParams(FILL_PARENT, FILL_PARENT));
	tabs_.push_back(tabContents);
	tabStrip_->AddChoice(title);
	contents_->Add(tabContents);
	if (tabs_.size() > 1)
		tabContents->SetVisibility(V_GONE);

	// Will be filled in later.
	tabTweens_.push_back(nullptr);
}

void TabHolder::SetCurrentTab(int tab, bool skipTween) {
	if (tab >= (int)tabs_.size()) {
		// Ignore
		return;
	}

	auto setupTween = [&](View *view, AnchorTranslateTween *&tween) {
		if (tween)
			return;

		tween = new AnchorTranslateTween(0.15f, bezierEaseInOut);
		tween->Finish.Add([&](EventParams &e) {
			e.v->SetVisibility(tabs_[currentTab_] == e.v ? V_VISIBLE : V_GONE);
			return EVENT_DONE;
		});
		view->AddTween(tween)->Persist();
	};

	if (tab != currentTab_) {
		Orientation orient = Opposite(orientation_);
		// Direction from which the new tab will come.
		float dir = tab < currentTab_ ? -1.0f : 1.0f;

		// First, setup any missing tweens.
		setupTween(tabs_[currentTab_], tabTweens_[currentTab_]);
		setupTween(tabs_[tab], tabTweens_[tab]);

		// Currently displayed, so let's reset it.
		if (skipTween) {
			tabs_[currentTab_]->SetVisibility(V_GONE);
			tabTweens_[tab]->Reset(Point(0.0f, 0.0f));
			tabTweens_[tab]->Apply(tabs_[tab]);
		} else {
			tabTweens_[currentTab_]->Reset(Point(0.0f, 0.0f));

			if (orient == ORIENT_HORIZONTAL) {
				tabTweens_[tab]->Reset(Point(bounds_.w * dir, 0.0f));
				tabTweens_[currentTab_]->Divert(Point(bounds_.w * -dir, 0.0f));
			} else {
				tabTweens_[tab]->Reset(Point(0.0f, bounds_.h * dir));
				tabTweens_[currentTab_]->Divert(Point(0.0f, bounds_.h * -dir));
			}
			// Actually move it to the initial position now, just to avoid any flicker.
			tabTweens_[tab]->Apply(tabs_[tab]);
			tabTweens_[tab]->Divert(Point(0.0f, 0.0f));
		}
		tabs_[tab]->SetVisibility(V_VISIBLE);

		currentTab_ = tab;
	}
	tabStrip_->SetSelection(tab, false);
}

EventReturn TabHolder::OnTabClick(EventParams &e) {
	// We have e.b set when it was an explicit click action.
	// In that case, we make the view gone and then visible - this scrolls scrollviews to the top.
	if (e.b != 0) {
		SetCurrentTab((int)e.a);
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
			SetCurrentTab(buffer[0], true);
		}
		break;
	}
}

ChoiceStrip::ChoiceStrip(Orientation orientation, LayoutParams *layoutParams)
		: LinearLayout(orientation, layoutParams) {
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

	EventParams e2{};
	e2.v = views_[selected_];
	e2.a = selected_;
	// Set to 1 to indicate an explicit click.
	e2.b = 1;
	// Dispatch immediately (we're already on the UI thread as we're in an event handler).
	return OnChoice.Dispatch(e2);
}

void ChoiceStrip::SetSelection(int sel, bool triggerClick) {
	int prevSelected = selected_;
	StickyChoice *prevChoice = Choice(selected_);
	if (prevChoice)
		prevChoice->Release();
	selected_ = sel;
	StickyChoice *newChoice = Choice(selected_);
	if (newChoice) {
		newChoice->Press();

		if (topTabs_ && prevSelected != selected_) {
			EventParams e{};
			e.v = views_[selected_];
			e.a = selected_;
			// Set to 0 to indicate a selection change (not a click.)
			e.b = triggerClick ? 1 : 0;
			OnChoice.Trigger(e);
		}
	}
}

void ChoiceStrip::EnableChoice(int choice, bool enabled) {
	if (choice < (int)views_.size()) {
		Choice(choice)->SetEnabled(enabled);
	}
}

bool ChoiceStrip::Key(const KeyInput &input) {
	bool ret = false;
	if (topTabs_ && (input.flags & KEY_DOWN)) {
		if (IsTabLeftKey(input)) {
			if (selected_ > 0) {
				SetSelection(selected_ - 1, true);
				UI::PlayUISound(UI::UISound::TOGGLE_OFF);  // Maybe make specific sounds for this at some point?
			}
			ret = true;
		} else if (IsTabRightKey(input)) {
			if (selected_ < (int)views_.size() - 1) {
				SetSelection(selected_ + 1, true);
				UI::PlayUISound(UI::UISound::TOGGLE_ON);
			}
			ret = true;
		}
	}
	return ret || ViewGroup::Key(input);
}

void ChoiceStrip::Draw(UIContext &dc) {
	ViewGroup::Draw(dc);
	if (topTabs_) {
		if (orientation_ == ORIENT_HORIZONTAL)
			dc.Draw()->DrawImageCenterTexel(dc.theme->whiteImage, bounds_.x, bounds_.y2() - 4, bounds_.x2(), bounds_.y2(), dc.theme->itemDownStyle.background.color );
		else if (orientation_ == ORIENT_VERTICAL)
			dc.Draw()->DrawImageCenterTexel(dc.theme->whiteImage, bounds_.x2() - 4, bounds_.y, bounds_.x2(), bounds_.y2(), dc.theme->itemDownStyle.background.color );
	}
}

std::string ChoiceStrip::DescribeText() const {
	auto u = GetI18NCategory("UI Elements");
	return DescribeListUnordered(u->T("Choices:"));
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
			View *v = linLayout_->Add(adaptor_->CreateItemView(i));
			adaptor_->AddEventCallback(v, std::bind(&ListView::OnItemCallback, this, i, std::placeholders::_1));
		}
	}
}

void ListView::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	ScrollView::Measure(dc, horiz, vert);
	if (maxHeight_ > 0 && measuredHeight_ > maxHeight_) {
		measuredHeight_ = maxHeight_;
	}
}

std::string ListView::DescribeText() const {
	auto u = GetI18NCategory("UI Elements");
	return DescribeListOrdered(u->T("List:"));
}

EventReturn ListView::OnItemCallback(int num, EventParams &e) {
	EventParams ev{};
	ev.v = nullptr;
	ev.a = num;
	adaptor_->SetSelected(num);
	OnChoice.Trigger(ev);
	CreateAllItems();
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

}  // namespace UI
