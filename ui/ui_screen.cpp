
#include "ui/ui_screen.h"
#include "ui/ui_context.h"
#include "ui/screen.h"

void UIScreen::update(InputState &input) {
	if (!root_) {
		CreateViews();
	}

	if (orientationChanged_) {
		delete root_;
		root_ = 0;
		CreateViews();
	}

	UpdateViewHierarchy(input, root_);
}

void UIScreen::render() {
	UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_);

	screenManager()->getUIContext()->Begin();
	DrawBackground();
	root_->Draw(*screenManager()->getUIContext());
	screenManager()->getUIContext()->End();
	screenManager()->getUIContext()->Flush();
}

void UIScreen::touch(const TouchInput &touch) {
	root_->Touch(touch);
}

UI::EventReturn UIScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}
