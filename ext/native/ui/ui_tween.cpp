#include "base/colorutil.h"
#include "ui/ui_tween.h"
#include "ui/view.h"

namespace UI {

uint32_t ColorTween::Current() {
	return colorBlend(to_, from_, Position());
}

void TextColorTween::Apply(View *view) {
	// TODO: No validation without RTTI?
	TextView *tv = (TextView *)view;
	tv->SetTextColor(Current());
}

void VisibilityTween::Apply(View *view) {
	view->SetVisibility(Current());
}

Visibility VisibilityTween::Current() {
	// Prefer V_VISIBLE over V_GONE/V_INVISIBLE.
	float p = Position();
	if (from_ == V_VISIBLE && p < 1.0f)
		return from_;
	if (to_ == V_VISIBLE && p > 0.0f)
		return to_;
	return p >= 1.0f ? to_ : from_;
}

}  // namespace
