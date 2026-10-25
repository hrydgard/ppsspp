#include <algorithm>

#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Common/System/NativeApp.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"
#include "UI/MiscViews.h"
#include "Common/UI/Context.h"
#include "UI/TabbedDialogScreen.h"

UITabbedBaseDialogScreen::UITabbedBaseDialogScreen(const Path &gamePath, int *currentTabSetting, TabDialogFlags flags)
	: UIBaseDialogScreen(gamePath), currentTabSetting_(currentTabSetting), flags_(flags) {}

void UITabbedBaseDialogScreen::AddTab(const char *tag, std::string_view title, ImageID imageId, std::function<void(UI::LinearLayout *)> createCallback, TabFlags flags) {
	using namespace UI;

	TabDialogFlags dialogFlags = flags_;
	Path gamePath = gamePath_;
	std::string cachedTitle(title);
	tabHolder_->AddTabDeferred(title, imageId, [this, createCallback = std::move(createCallback), tag, flags, dialogFlags, gamePath, cachedTitle]() -> UI::ViewGroup * {
		using namespace UI;
		ViewGroup *scroll = nullptr;
		if (!(flags & TabFlags::NonScrollable)) {
			scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
			scroll->SetTag(tag);
		}
		LinearLayout *contents = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(Margins(0, 0, 8, 0)));
		contents->SetSpacing(0);

		if (dialogFlags & TabDialogFlags::AddAutoTitles) {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			contents->Add(new PaneTitleBar(gamePath, cachedTitle, ""));
		}

		// Tabs other than the first are created on demand, outside CreateViews. Their views still belong to the
		// current tree, so their requests must get the views token, to be forgotten when the tree is deleted.
		bool wasCreatingViews = IsCreatingViews();
		SetCreatingViews(true);
		createCallback(contents);
		SetCreatingViews(wasCreatingViews);
		if (scroll) {
			scroll->Add(contents);
			return scroll;
		} else {
			return contents;
		}
	});
}

void UITabbedBaseDialogScreen::CreateViews() {
	PreCreateViews();

	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait || ForceHorizontalTabs();

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	if (portrait) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		LinearLayout *verticalLayout = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));

		TabHolderFlags tabHolderFlags = TabHolderFlags::BackButton;
		if (flags_ & TabDialogFlags::HorizontalOnlyIcons) {
			tabHolderFlags |= TabHolderFlags::HorizontalOnlyIcons;
		}
		std::function<void()> contextMenu;
		if (flags_ & TabDialogFlags::ContextMenuInPortrait) {
			contextMenu = [this]() {
				this->screenManager()->push(new PopupCallbackScreen([this](UI::ViewGroup *parent) {
					CreateExtraButtons(parent, 0);
				}, nullptr));
			};
		}
		tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 200, tabHolderFlags, nullptr, contextMenu, new LinearLayoutParams(1.0f));
		verticalLayout->Add(tabHolder_);
		if (!(flags_ & TabDialogFlags::ContextMenuInPortrait)) {
			CreateExtraButtons(verticalLayout, 0);
		}
		root_ = verticalLayout;
	} else {
		TabHolderFlags tabHolderFlags = TabHolderFlags::Default;
		if (flags_ & TabDialogFlags::VerticalShowIcons) {
			tabHolderFlags |= TabHolderFlags::VerticalShowIcons;
		}
		tabHolder_ = new TabHolder(ORIENT_VERTICAL, 300, tabHolderFlags, nullptr, nullptr);
		CreateExtraButtons(tabHolder_->Container(), 10);
		tabHolder_->AddBack(this);
		root_ = tabHolder_;
	}

	tabHolder_->SetTag(tag());  // take the tag from the screen.
	root_->SetDefaultFocusView(tabHolder_);

	float leftSide = 40.0f;
	if (!portrait) {
		leftSide += 200.0f;
	}

	// Let the subclass create its tabs.
	CreateTabs();

	tabHolder_->OnChangeTab.Add([this](UI::EventParams &e) {
		if (currentTabSetting_) {
			*currentTabSetting_ = e.a;
		}
		TabChanged(e.a);
	});

	int initialTab = InitialTab();
	if (initialTab >= 0) {
		if (currentTabSetting_) {
			*currentTabSetting_ = initialTab;
		}
		tabHolder_->SetInitialTab(initialTab);
	} else if (currentTabSetting_) {
		tabHolder_->SetInitialTab(*currentTabSetting_);
	} else {
		tabHolder_->EnsureTab(tabHolder_->GetCurrentTab());
	}
}

void UITabbedBaseDialogScreen::EnsureTabs() {
	_dbg_assert_(tabHolder_);
	if (tabHolder_) {
		tabHolder_->EnsureAllCreated();
	}
}

int UITabbedBaseDialogScreen::GetCurrentTab() const {
	return tabHolder_->GetCurrentTab();
}

void UITabbedBaseDialogScreen::SetCurrentTab(int tab) {
	tabHolder_->SetCurrentTab(tab);
}
