#include "Common/UI/ScrollView.h"
#include "Common/Data/Text/I18n.h"
#include "UI/SimpleDialogScreen.h"
#include "UI/MiscViews.h"

void UISimpleBaseDialogScreen::CreateViews() {
	using namespace UI;

	const bool canScroll = CanScroll();
	ignoreBottomInset_ = canScroll;

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	root_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(new TopBar(*screenManager()->getUIContext(), portrait, GetTitle()));

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

void UITwoPaneBaseDialogScreen::CreateViews() {
	using namespace UI;

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	auto di = GetI18NCategory(I18NCat::DIALOG);

	if (portrait) {
		// Portrait layout is just a vertical stack.
		ignoreBottomInset_ = true;
		LinearLayout *root = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
		root->Add(new TopBar(*screenManager()->getUIContext(), portrait, GetTitle()));
		root->SetSpacing(0);
		CreateContentViews(root);

		ScrollView *settingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		LinearLayout *settingsPane = new LinearLayout(ORIENT_VERTICAL);
		settingsScroll->Add(settingsPane);
		CreateSettingsViews(settingsPane);
		root->Add(settingsScroll);

		root_ = root;
	} else {
		ignoreBottomInset_ = false;
		LinearLayout *root = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
		// root_->Add(new TopBar(*screenManager()->getUIContext(), portrait, GetTitle(), new LayoutParams(FILL_PARENT, FILL_PARENT)));
		LinearLayout *settingsPane = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(400.0f, FILL_PARENT));
		settingsPane->SetSpacing(0);
		CreateSettingsViews(settingsPane);
		settingsPane->Add(new BorderView(BORDER_BOTTOM, BorderStyle::HEADER_FG, 2.0f, new LayoutParams(FILL_PARENT, 40.0f)));
		settingsPane->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		root->Add(settingsPane);
		CreateContentViews(root);

		root_ = root;
	}
}

