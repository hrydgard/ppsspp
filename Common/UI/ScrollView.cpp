#include <algorithm>
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Log.h"

namespace UI {

float ScrollView::lastScrollPosX = 0;
float ScrollView::lastScrollPosY = 0;

ScrollView::~ScrollView() {
	lastScrollPosX = 0;
	lastScrollPosY = 0;
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
	default:
		break;
	}

	if (input.flags & KEY_DOWN) {
		if ((input.keyCode == NKCODE_EXT_MOUSEWHEEL_UP || input.keyCode == NKCODE_EXT_MOUSEWHEEL_DOWN) &&
			(input.flags & KEY_HASWHEELDELTA)) {
			scrollSpeed = (float)(short)(input.flags >> 16) * 1.25f;  // Fudge factor. TODO: Should be moved to the backends.
		}

		switch (input.keyCode) {
		case NKCODE_EXT_MOUSEWHEEL_UP:
			ScrollRelative(-scrollSpeed);
			break;
		case NKCODE_EXT_MOUSEWHEEL_DOWN:
			ScrollRelative(scrollSpeed);
			break;
		default:
			break;
		}
	}
	return ViewGroup::Key(input);
}

const float friction = 0.92f;
const float stop_threshold = 0.1f;

bool ScrollView::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && scrollTouchId_ == -1 && bounds_.Contains(input.x, input.y)) {
		if (orientation_ == ORIENT_VERTICAL) {
			Bob bob = ComputeBob();
			float internalY = input.y - bounds_.y;
			float bobMargin = 3.0f;  // Add some extra margin for the touch.
			draggingBob_ = internalY >= bob.offset - bobMargin && internalY <= bob.offset + bob.size + bobMargin && input.x >= bounds_.x2() - 20.0f;
			barDragStart_ = bob.offset;
			barDragOffset_ = internalY - bob.offset;
		}

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
		draggingBob_ = false;
	}

	// We modify the input2 we send to children, so we can cancel drags if we start scrolling, and stuff like that.
	TouchInput input2;
	if (CanScroll()) {
		if (draggingBob_) {
			// Cancel any drags/holds on the children instantly to avoid accidental click-throughs.
			input2.flags = TOUCH_UP | TOUCH_CANCEL;
			// Skip the gesture manager, do calculations directly.
			// Might switch to the gesture later.
			Bob bob = ComputeBob();
			float internalY = input.y - bounds_.y;

			float bobPos = internalY - barDragOffset_;
			float bobDragMax = bounds_.h - bob.size;

			float newScrollPos = Clamp(bobPos / bobDragMax, 0.0f, 1.0f) * bob.scrollMax;

			scrollPos_ = newScrollPos;
			scrollTarget_ = newScrollPos;
			scrollToTarget_ = false;
		} else {
			input2 = gesture_.Update(input, bounds_);
			float info[4];
			if (input.id == scrollTouchId_ && gesture_.GetGestureInfo(gesture, input.id, info) && !(input.flags & TOUCH_DOWN)) {
				float pos = scrollStart_ - info[0];
				scrollPos_ = pos;
				scrollTarget_ = pos;
				scrollToTarget_ = false;
			}
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

ScrollView::Bob ScrollView::ComputeBob() const {
	Bob bob{};
	if (views_.empty()) {
		return bob;
	}
	float childHeight = std::max(0.01f, views_[0]->GetBounds().h);
	float scrollMax = std::max(0.0f, childHeight - bounds_.h);
	float ratio = bounds_.h / childHeight;

	if (ratio < 1.0f && scrollMax > 0.0f) {
		bob.show = true;
		bob.thickness = draggingBob_ ? 15.0f : 6.0f;
		bob.size = ratio * bounds_.h;
		bob.offset = (HardClampedScrollPos(scrollPos_) / scrollMax) * (bounds_.h - bob.size);
		bob.scrollMax = scrollMax;
	}
	return bob;
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

	// Vertical scroll bob. We don't support a horizontal yet.
	if (orientation_ != ORIENT_VERTICAL) {
		return;
	}

	Bob bob = ComputeBob();

	if (bob.show) {
		Bounds bobBounds(bounds_.x2() - bob.thickness, bounds_.y + bob.offset, bob.thickness, bob.size);
		dc.FillRect(Drawable(0x80FFFFFF), bobBounds);
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

NeighborResult ScrollView::FindScrollNeighbor(View *view, const Point2D &target, FocusDirection direction, NeighborResult best) {
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
			Point2D targetPos = view->GetBounds().Center();
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

float ScrollView::HardClampedScrollPos(float pos) const {
	if (!views_.size() || bounds_.h == 0.0f) {
		return 0.0f;
	}
	float childSize = orientation_ == ORIENT_VERTICAL ? views_[0]->GetBounds().h : views_[0]->GetBounds().w;
	float containerSize = (orientation_ == ORIENT_VERTICAL ? bounds_.h : bounds_.w);
	float scrollMax = std::max(0.0f, childSize - containerSize);
	return Clamp(pos, 0.0f, scrollMax);
}

float ScrollView::ClampedScrollPos(float pos) {
	if (!views_.size() || bounds_.h == 0.0f) {
		return 0.0f;
	}

	float childSize = orientation_ == ORIENT_VERTICAL ? views_[0]->GetBounds().h : views_[0]->GetBounds().w;
	float containerSize = (orientation_ == ORIENT_VERTICAL ? bounds_.h : bounds_.w);
	float scrollMax = std::max(0.0f, childSize - containerSize);

	Gesture gesture = orientation_ == ORIENT_VERTICAL ? GESTURE_DRAG_VERTICAL : GESTURE_DRAG_HORIZONTAL;

	// TODO: Not all of this is properly orientation independent.
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
	if (childSize < containerSize &&alignOpposite_) {
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

ListView::ListView(ListAdaptor *a, std::set<int> hidden, std::map<int, ImageID> icons, LayoutParams *layoutParams)
	: ScrollView(ORIENT_VERTICAL, layoutParams), adaptor_(a), maxHeight_(0), hidden_(hidden), icons_(icons) {
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
			ImageID *imageID = nullptr;
			auto iter = icons_.find(i);
			if (iter != icons_.end()) {
				imageID = &iter->second;
			}
			View *v = linLayout_->Add(adaptor_->CreateItemView(i, imageID));
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
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
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

View *ChoiceListAdaptor::CreateItemView(int index, ImageID *optionalImageID) {
	Choice *choice = new Choice(items_[index]);
	if (optionalImageID) {
		choice->SetIcon(*optionalImageID);
	}
	return choice;
}

bool ChoiceListAdaptor::AddEventCallback(View *view, std::function<EventReturn(EventParams &)> callback) {
	Choice *choice = (Choice *)view;
	choice->OnClick.Add(callback);
	return EVENT_DONE;
}


View *StringVectorListAdaptor::CreateItemView(int index, ImageID *optionalImageID) {
	Choice *choice = new Choice(items_[index], "", index == selected_);
	if (optionalImageID) {
		choice->SetIcon(*optionalImageID);
	}
	return choice;
}

bool StringVectorListAdaptor::AddEventCallback(View *view, std::function<EventReturn(EventParams &)> callback) {
	Choice *choice = (Choice *)view;
	choice->OnClick.Add(callback);
	return EVENT_DONE;
}

}  // namespace
