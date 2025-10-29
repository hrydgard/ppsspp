#include "Common/UI/ScrollView.h"
#include "UI/SimpleDialogScreen.h"
#include "UI/MiscViews.h"

void UISimpleBaseDialogScreen::CreateViews() {
	using namespace UI;

	const bool canScroll = CanScroll();
	ignoreBottomInset_ = canScroll;

	const bool portrait = UsePortraitLayout();

	root_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(new TopBar(*screenManager()->getUIContext(), GetTitle()));

	if (canScroll) {
		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		LinearLayout *contents = new LinearLayoutList(ORIENT_VERTICAL);
		contents->SetSpacing(0);
		CreateDialogViews(contents);
		scroll->Add(contents);
		root_->Add(scroll);
	} else {
		CreateDialogViews(root_);
	}
}

