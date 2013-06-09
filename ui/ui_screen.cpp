
#include "ui/ui_screen.h"
#include "ui/ui_context.h"
#include "ui/screen.h"

UIScreen::UIScreen()
	: Screen(), root_(0), recreateViews_(true) {

}

void UIScreen::update(InputState &input) {
	if (recreateViews_) {
		delete root_;
		root_ = 0;
		CreateViews();
		recreateViews_ = false;
	}

	UpdateViewHierarchy(input, root_);
}

void UIScreen::render() {
	if (root_) {
		UI::LayoutViewHierarchy(*screenManager()->getUIContext(), root_);

		screenManager()->getUIContext()->Begin();
		DrawBackground(*screenManager()->getUIContext());
		root_->Draw(*screenManager()->getUIContext());
		screenManager()->getUIContext()->End();
		screenManager()->getUIContext()->Flush();
	} else {
		ELOG("Tried to render without a view root");
	}
}

void UIScreen::touch(const TouchInput &touch) {
	if (root_) {
		root_->Touch(touch);
	} else {
		ELOG("Tried to touch without a view root");
	}
}

UI::EventReturn UIScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}
