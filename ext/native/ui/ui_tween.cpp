#include "base/colorutil.h"
#include "ui/ui_tween.h"
#include "ui/view.h"

namespace UI {

void Tween::Apply(View *view) {
	if (time_now() >= start_ + duration_)
		finishApplied_ = true;

	float pos = Position();
	DoApply(view, pos);
}

template <typename Value>
void TweenBase<Value>::PersistData(PersistStatus status, std::string anonId, PersistMap &storage) {
	struct TweenData {
		float start;
		float duration;
		Value from;
		Value to;
	};

	PersistBuffer &buffer = storage["TweenBase::" + anonId];

	switch (status) {
	case UI::PERSIST_SAVE:
		buffer.resize(sizeof(TweenData) / sizeof(int));
		{
			TweenData &data = *(TweenData *)&buffer[0];
			data.start = start_;
			data.duration = duration_;
			data.from = from_;
			data.to = to_;
		}
		break;
	case UI::PERSIST_RESTORE:
		if (buffer.size() >= sizeof(TweenData) / sizeof(int)) {
			TweenData data = *(TweenData *)&buffer[0];
			start_ = data.start;
			duration_ = data.duration;
			from_ = data.from;
			to_ = data.to;
			// We skip finishApplied_ here so that the tween will reapply.
			// This does mean it's important to remember to update tweens even after finish.
		}
		break;
	}
}

template void TweenBase<uint32_t>::PersistData(PersistStatus status, std::string anonId, PersistMap &storage);
template void TweenBase<Visibility>::PersistData(PersistStatus status, std::string anonId, PersistMap &storage);

uint32_t ColorTween::Current(float pos) {
	return colorBlend(to_, from_, pos);
}

void TextColorTween::DoApply(View *view, float pos) {
	// TODO: No validation without RTTI?
	TextView *tv = (TextView *)view;
	tv->SetTextColor(Current(pos));
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

}  // namespace
