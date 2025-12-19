#include "Common/UI/ScrollView.h"
#include "Common/Data/Text/I18n.h"
#include "UI/SimpleDialogScreen.h"
#include "Common/UI/PopupScreens.h"
#include "UI/MiscViews.h"

void UISimpleBaseDialogScreen::CreateViews() {
	using namespace UI;

	const bool canScroll = flags_ & SimpleDialogFlags::ContentsCanScroll;
	ignoreBottomInset_ = canScroll;

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	root_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));

	TopBarFlags topBarFlags = portrait ? TopBarFlags::Portrait : TopBarFlags::Default;
	if (flags_ & SimpleDialogFlags::CustomContextMenu) {
		topBarFlags |= TopBarFlags::ContextMenuButton;
	}

	TopBar *topBar = root_->Add(new TopBar(*screenManager()->getUIContext(), topBarFlags, GetTitle()));
	if (flags_ & SimpleDialogFlags::CustomContextMenu) {
		View *menuButton = topBar->GetContextMenuButton();
		topBar->OnContextMenuClick.Add([this, menuButton](UI::EventParams &e) {
			this->screenManager()->push(new PopupCallbackScreen([this](UI::ViewGroup *parent) {
				CreateContextMenu(parent);
			}, menuButton));
		});
	}

	if (canScroll) {
		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		LinearLayout *contents = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(Margins(0, 0, 8, 0)));
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

	BeforeCreateViews();

	auto createContentViews = [this](UI::ViewGroup *parent) {
		if (flags_ & TwoPaneFlags::ContentsCanScroll) {
			Margins margins(8, 8, 8, 0);
			if (flags_ & TwoPaneFlags::SettingsToTheRight) {
				// If settings are in context menu, we want to avoid double margins on the sides.
				margins.left = 0;
			} else {
				margins.right = 0;
			}

			ScrollView *contentScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f, margins));
			parent->Add(contentScroll);
			CreateContentViews(contentScroll);
		} else {
			CreateContentViews(parent);
		}
	};

	if (portrait) {
		// Portrait layout is just a vertical stack.
		if (flags_ & TwoPaneFlags::SettingsCanScroll) {
			ignoreBottomInset_ = true;
		}
		LinearLayout *root = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));

		TopBarFlags topBarFlags = TopBarFlags::Portrait;
		if (flags_ & (TwoPaneFlags::SettingsInContextMenu | TwoPaneFlags::CustomContextMenu)) {
			topBarFlags |= TopBarFlags::ContextMenuButton;
		}
		TopBar *topBar = root->Add(new TopBar(*screenManager()->getUIContext(), topBarFlags, GetTitle()));
		root->SetSpacing(0);
		createContentViews(root);

		if (flags_ & (TwoPaneFlags::SettingsInContextMenu | TwoPaneFlags::CustomContextMenu)) {
			View *menuButton = topBar->GetContextMenuButton();
			topBar->OnContextMenuClick.Add([this, menuButton](UI::EventParams &e) {
				this->screenManager()->push(new PopupCallbackScreen([this](UI::ViewGroup *parent) {
					if (flags_ & TwoPaneFlags::CustomContextMenu) {
						CreateContextMenu(parent);
					} else {
						CreateSettingsViews(parent);
					}
				}, menuButton));
			});
		}
		if (!(flags_ & TwoPaneFlags::SettingsInContextMenu)) {
			LinearLayout *settingsPane;
			if (flags_ & TwoPaneFlags::SettingsCanScroll) {
				settingsPane = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
				settingsPane->SetSpacing(0.0f);
				ScrollView *settingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f, Margins(8)));
				settingsScroll->Add(settingsPane);
				root->Add(settingsScroll);
			} else {
				settingsPane = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.0f, Margins(8)));
				settingsPane->SetSpacing(0.0f);
				root->Add(settingsPane);
			}
			CreateSettingsViews(settingsPane);
		}
		root_ = root;
	} else {
		ignoreBottomInset_ = false;
		LinearLayout *root = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
		std::string title(GetTitle());
		TopBarFlags topBarFlags = portrait ? TopBarFlags::Portrait : TopBarFlags::Default;
		if (flags_ & TwoPaneFlags::CustomContextMenu) {
			topBarFlags |= TopBarFlags::ContextMenuButton;
		}
		TopBar *topBar = root->Add(new TopBar(*screenManager()->getUIContext(), topBarFlags, title));
		root->SetSpacing(0);

		if (flags_ & TwoPaneFlags::CustomContextMenu) {
			View *menuButton = topBar->GetContextMenuButton();
			topBar->OnContextMenuClick.Add([this, menuButton](UI::EventParams &e) {
				this->screenManager()->push(new PopupCallbackScreen([this](UI::ViewGroup *parent) {
					CreateContextMenu(parent);
				}, menuButton));
			});
		}
		LinearLayout *columns = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
		root->Add(columns);

		// root_->Add(new TopBar(*screenManager()->getUIContext(), portrait, GetTitle(), new LayoutParams(FILL_PARENT, FILL_PARENT)));
		ScrollView *settingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(SettingsWidth(), FILL_PARENT, 0.0f, Margins(0, 8, 0, 0)));
		LinearLayout *settingsPane = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, Margins(0, 8, 0, 0)));
		settingsPane->SetSpacing(0.0f);
		CreateSettingsViews(settingsPane);
		// settingsPane->Add(new BorderView(BORDER_BOTTOM, BorderStyle::HEADER_FG, 2.0f, new LayoutParams(FILL_PARENT, 40.0f)));
		// settingsPane->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		settingsScroll->Add(settingsPane);

		if (flags_ & TwoPaneFlags::SettingsToTheRight) {
			createContentViews(columns);
			columns->Add(settingsScroll);
		} else {
			columns->Add(settingsScroll);
			createContentViews(columns);
		}

		root_ = root;
	}
}

