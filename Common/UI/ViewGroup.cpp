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
#include "Common/UI/ScrollView.h"
#include "Common/UI/Tween.h"
#include "Common/UI/Root.h"
#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"

namespace UI {

static constexpr Size ITEM_HEIGHT = 64.f;

void ApplyGravity(const Bounds &outer, const Margins &margins, float w, float h, int gravity, Bounds &inner) {
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

void ViewGroup::RemoveSubview(View *subView) {
	// loop counter needed, so can't convert loop.
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == subView) {
			views_.erase(views_.begin() + i);
			delete subView;
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

int ViewGroup::IndexOfSubview(const View *view) const {
	int index = 0;
	for (const View *subview : views_) {
		if (subview == view)
			return index;
		index++;
	}
	return -1;
}

void ViewGroup::Clear() {
	for (View *view : views_) {
		delete view;
	}
	views_.clear();
}

void ViewGroup::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	std::string tag = Tag();
	if (tag.empty()) {
		tag = anonId;
	}

	for (size_t i = 0; i < views_.size(); i++) {
		views_[i]->PersistData(status, tag + "/" + StringFromInt((int)i), storage);
	}
}

bool ViewGroup::Touch(const TouchInput &input) {
	bool any = false;
	for (View *view : views_) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if (view->GetVisibility() == V_VISIBLE) {
			bool touch = view->Touch(input);
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
		for (View *view : views_) {
			view->Query(x, y, list);
		}
	}
}

bool ViewGroup::Key(const KeyInput &input) {
	bool ret = false;
	for (View *view : views_) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if (view->GetVisibility() == V_VISIBLE)
			ret = ret || view->Key(input);
	}
	return ret;
}

void ViewGroup::Axis(const AxisInput &input) {
	for (View *view : views_) {
		// TODO: If there is a transformation active, transform input coordinates accordingly.
		if (view->GetVisibility() == V_VISIBLE)
			view->Axis(input);
	}
}

void ViewGroup::DeviceLost() {
	for (View *view : views_) {
		view->DeviceLost();
	}
}

void ViewGroup::DeviceRestored(Draw::DrawContext *draw) {
	for (View *view : views_) {
		view->DeviceRestored(draw);
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

std::string ViewGroup::DescribeListUnordered(std::string_view heading) const {
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

std::string ViewGroup::DescribeListOrdered(std::string_view heading) const {
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
	if (!CanBeFocused() && !views_.empty()) {
		for (View *view : views_) {
			if (view->SetFocus())
				return true;
		}
	}
	return false;
}

bool ViewGroup::SubviewFocused(View *queryView) {
	for (View *view : views_) {
		if (view == queryView || view->SubviewFocused(queryView))
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

float GetTargetScore(const Point2D &originPos, int originIndex, const View *origin, const View *destination, FocusDirection direction) {
	// Skip labels and things like that.
	if (!destination->CanBeFocused())
		return 0.0f;
	if (destination->IsEnabled() == false)
		return 0.0f;
	if (destination->GetVisibility() != V_VISIBLE)
		return 0.0f;

	Point2D destPos = destination->GetFocusPosition(Opposite(direction));

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
			INFO_LOG(Log::System, "Contain overlap");
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
		ERROR_LOG(Log::System, "Invalid focus direction");
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
	Point2D originPos = origin->GetFocusPosition(direction);
	return GetTargetScore(originPos, originIndex, origin, destination, direction);
}

NeighborResult ViewGroup::FindNeighbor(View *view, FocusDirection direction, NeighborResult result) {
	if (!IsEnabled()) {
		INFO_LOG(Log::sceCtrl, "Not enabled");
		return result;
	}
	if (GetVisibility() != V_VISIBLE) {
		return result;
	}

	// First, find the position of the view in the list.
	int num = -1;
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i] == view) {
			num = (int)i;
			break;
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
		return FindScrollNeighbor(view, Point2D(INFINITY, INFINITY), direction, result);
	case FOCUS_PREV:
		// If view not found, no neighbor to find.
		if (num == -1)
			return NeighborResult(nullptr, 0.0f);
		return NeighborResult(views_[(num + views_.size() - 1) % views_.size()], 0.0f);
	case FOCUS_NEXT:
		// If view not found, no neighbor to find.
		if (num == -1)
			return NeighborResult(0, 0.0f);
		return NeighborResult(views_[(num + 1) % views_.size()], 0.0f);

	default:
		ERROR_LOG(Log::System, "Bad focus direction %d", (int)direction);
		return result;
	}
}

NeighborResult ViewGroup::FindScrollNeighbor(View *view, const Point2D &target, FocusDirection direction, NeighborResult best) {
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
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
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

static void ApplyAnchorLayoutParams(float measuredWidth, float measuredHeight, const Bounds &container, const AnchorLayoutParams *params, Bounds *vBounds) {
	vBounds->w = measuredWidth;
	vBounds->h = measuredHeight;

	// Clamp width/height to our own
	if (vBounds->w > container.w) vBounds->w = container.w;
	if (vBounds->h > container.h) vBounds->h = container.h;

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
		vBounds->x = container.x + left;
		if (center)
			vBounds->x -= vBounds->w * 0.5f;
	} else if (right > NONE) {
		vBounds->x = container.x2() - right - vBounds->w;
		if (center) {
			vBounds->x += vBounds->w * 0.5f;
		}
	} else {
		// Both left and right are NONE. Center.
		vBounds->x = (container.w - vBounds->w) / 2.0f + container.x;
	}

	if (top > NONE) {
		vBounds->y = container.y + top;
		if (center)
			vBounds->y -= vBounds->h * 0.5f;
	} else if (bottom > NONE) {
		vBounds->y = container.y2() - bottom - vBounds->h;
		if (center)
			vBounds->y += vBounds->h * 0.5f;
	} else {
		// Both top and bottom are NONE. Center.
		vBounds->y = (container.h - vBounds->h) / 2.0f + container.y;
	}
}

void AnchorLayout::Layout() {
	for (size_t i = 0; i < views_.size(); i++) {
		const AnchorLayoutParams *params = views_[i]->GetLayoutParams()->As<AnchorLayoutParams>();
		Bounds vBounds;
		ApplyAnchorLayoutParams(views_[i]->GetMeasuredWidth(), views_[i]->GetMeasuredHeight(), bounds_, params, &vBounds);
		views_[i]->SetBounds(vBounds);
		views_[i]->Layout();
	}
}

GridLayout::GridLayout(GridLayoutSettings settings, LayoutParams *layoutParams)
	: ViewGroup(layoutParams), settings_(settings) {
	if (settings.orientation != ORIENT_HORIZONTAL)
		ERROR_LOG(Log::System, "GridLayout: Vertical layouts not yet supported");
}

void GridLayout::Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) {
	MeasureSpecType measureType = settings_.fillCells ? EXACTLY : AT_MOST;

	int numItems = 0;
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i]->GetVisibility() != V_GONE) {
			views_[i]->Measure(dc, MeasureSpec(measureType, settings_.columnWidth), MeasureSpec(measureType, settings_.rowHeight));
			numItems++;
		}
	}

	// Use the max possible width so AT_MOST gives us the full size.
	float maxWidth = (settings_.columnWidth + settings_.spacing) * numItems + settings_.spacing;
	MeasureBySpec(layoutParams_->width, maxWidth, horiz, &measuredWidth_);

	// Okay, got the width we are supposed to adjust to. Now we can calculate the number of columns.
	numColumns_ = (measuredWidth_ - settings_.spacing) / (settings_.columnWidth + settings_.spacing);
	if (!numColumns_) numColumns_ = 1;
	int numRows = (numItems + (numColumns_ - 1)) / numColumns_;

	float estimatedHeight = (settings_.rowHeight + settings_.spacing) * numRows;

	MeasureBySpec(layoutParams_->height, estimatedHeight, vert, &measuredHeight_);
}

void GridLayout::Layout() {
	int y = 0;
	int x = 0;
	int count = 0;
	for (size_t i = 0; i < views_.size(); i++) {
		if (views_[i]->GetVisibility() == V_GONE)
			continue;

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
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
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
		tabContainer_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(stripSize, FILL_PARENT));
		tabStrip_ = new ChoiceStrip(orientation, new LayoutParams(FILL_PARENT, FILL_PARENT));
		tabStrip_->SetTopTabs(true);
		tabScroll_ = new ScrollView(orientation, new LinearLayoutParams(1.0f));
		tabScroll_->Add(tabStrip_);
		tabContainer_->Add(tabScroll_);
		Add(tabContainer_);
	}
	tabStrip_->OnChoice.Handle(this, &TabHolder::OnTabClick);

	Add(new Spacer(4.0f))->SetSeparator();

	contents_ = new AnchorLayout(new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	Add(contents_)->SetClip(true);
}

void TabHolder::AddBack(UIScreen *parent) {
	if (tabContainer_) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		tabContainer_->Add(new Choice(di->T("Back"), "", false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.0f, Margins(0, 0, 10, 10))))->OnClick.Handle<UIScreen>(parent, &UIScreen::OnBack);
	}
}

void TabHolder::AddTabContents(std::string_view title, View *tabContents) {
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
			tabTweens_[tab]->Reset(Point2D(0.0f, 0.0f));
			tabTweens_[tab]->Apply(tabs_[tab]);
		} else {
			tabTweens_[currentTab_]->Reset(Point2D(0.0f, 0.0f));

			if (orient == ORIENT_HORIZONTAL) {
				tabTweens_[tab]->Reset(Point2D(bounds_.w * dir, 0.0f));
				tabTweens_[currentTab_]->Divert(Point2D(bounds_.w * -dir, 0.0f));
			} else {
				tabTweens_[tab]->Reset(Point2D(0.0f, bounds_.h * dir));
				tabTweens_[currentTab_]->Divert(Point2D(0.0f, bounds_.h * -dir));
			}
			// Actually move it to the initial position now, just to avoid any flicker.
			tabTweens_[tab]->Apply(tabs_[tab]);
			tabTweens_[tab]->Divert(Point2D(0.0f, 0.0f));
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

void ChoiceStrip::AddChoice(std::string_view title) {
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

std::string ChoiceStrip::DescribeText() const {
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return DescribeListUnordered(u->T("Choices:"));
}

StickyChoice *ChoiceStrip::Choice(int index) {
	if ((size_t)index < views_.size())
		return static_cast<StickyChoice *>(views_[index]);
	return nullptr;
}

CollapsibleSection::CollapsibleSection(std::string_view title, LayoutParams *layoutParams) : LinearLayout(ORIENT_VERTICAL, layoutParams) {
	open_ = &localOpen_;
	SetSpacing(0.0f);

	header_ = new CollapsibleHeader(open_, title);
	views_.push_back(header_);
	header_->OnClick.Add([=](UI::EventParams &) {
		// Change the visibility of all children except the first one.
		// Later maybe try something more ambitious.
		UpdateVisibility();
		return UI::EVENT_DONE;
	});
}

void CollapsibleSection::Update() {
	ViewGroup::Update();
	header_->SetHasSubitems(views_.size() > 1);
}

void CollapsibleSection::UpdateVisibility() {
	const bool open = *open_;
	// Skipping over the header, we start from 1, not 0.
	for (size_t i = 1; i < views_.size(); i++) {
		views_[i]->SetVisibility(open ? V_VISIBLE : V_GONE);
	}
}

}  // namespace UI
