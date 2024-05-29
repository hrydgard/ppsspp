#include "Common/Data/Color/RGBAUtil.h"
#include "Common/UI/Tween.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

namespace UI {

void Tween::Apply(View *view) {
	if (!valid_ || finishApplied_)
		return;

	if (DurationOffset() >= duration_)
		finishApplied_ = true;

	float pos = Position();
	DoApply(view, pos);

	if (finishApplied_) {
		UI::EventParams e{};
		e.v = view;
		e.f = DurationOffset() - duration_;
		Finish.Trigger(e);
	}
}

template <typename Value>
void TweenBase<Value>::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	struct TweenData {
		float start;
		float duration;
		float delay;
		Value from;
		Value to;
		bool valid;
	};

	PersistBuffer &buffer = storage["TweenBase::" + anonId];

	switch (status) {
	case UI::PERSIST_SAVE:
		buffer.resize(sizeof(TweenData) / sizeof(int));
		{
			TweenData &data = *(TweenData *)&buffer[0];
			data.start = start_;
			data.duration = duration_;
			data.delay = delay_;
			data.from = from_;
			data.to = to_;
			data.valid = valid_;
		}
		break;
	case UI::PERSIST_RESTORE:
		if (buffer.size() >= sizeof(TweenData) / sizeof(int)) {
			TweenData data = *(TweenData *)&buffer[0];
			start_ = data.start;
			duration_ = data.duration;
			delay_ = data.delay;
			from_ = data.from;
			to_ = data.to;
			valid_ = data.valid;
			// We skip finishApplied_ here so that the tween will reapply.
			// This does mean it's important to remember to update tweens even after finish.
		}
		break;
	}
}

template void TweenBase<uint32_t>::PersistData(PersistStatus status, std::string anonId, PersistMap &storage);
template void TweenBase<Visibility>::PersistData(PersistStatus status, std::string anonId, PersistMap &storage);
template void TweenBase<Point2D>::PersistData(PersistStatus status, std::string anonId, PersistMap &storage);

uint32_t ColorTween::Current(float pos) {
	return colorBlend(to_, from_, pos);
}

void TextColorTween::DoApply(View *view, float pos) {
	// TODO: No validation without RTTI?
	TextView *tv = (TextView *)view;
	tv->SetTextColor(Current(pos));
}

void CallbackColorTween::DoApply(View *view, float pos) {
	if (callback_) {
		callback_(view, Current(pos));
	}
}

void VisibilityTween::DoApply(View *view, float pos) {
	view->SetVisibility(Current(pos));
}

Visibility VisibilityTween::Current(float p) {
	// Prefer V_VISIBLE over V_GONE/V_INVISIBLE.
	if (from_ == V_VISIBLE && p < 1.0f)
		return from_;
	if (to_ == V_VISIBLE && p > 0.0f)
		return to_;
	return p >= 1.0f ? to_ : from_;
}

void AnchorTranslateTween::DoApply(View *view, float pos) {
	Point2D cur = Current(pos);

	auto prev = view->GetLayoutParams()->As<AnchorLayoutParams>();
	auto lp = new AnchorLayoutParams(prev ? *prev : AnchorLayoutParams(FILL_PARENT, FILL_PARENT));
	lp->left = cur.x;
	lp->top = cur.y;
	view->ReplaceLayoutParams(lp);
}

Point2D AnchorTranslateTween::Current(float p) {
	float inv = 1.0f - p;
	return Point2D(from_.x * inv + to_.x * p, from_.y * inv + to_.y * p);
}

}  // namespace
