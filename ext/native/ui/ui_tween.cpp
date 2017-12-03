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
