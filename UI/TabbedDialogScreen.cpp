#include <algorithm>

#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/System/Display.h"
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

	// Only created with the search tab, which depends on the orientation, so may not be there this time.
	clearSearchChoice_ = nullptr;
	noSearchResults_ = nullptr;

	auto se = GetI18NCategory(I18NCat::SEARCH);
	filterNotice_ = new TextView("(filter notice, you shouldn't see this text", new LinearLayoutParams(Margins(20, 5)));
	filterNotice_->SetVisibility(V_GONE);

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
		tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 200, tabHolderFlags, filterNotice_, contextMenu, new LinearLayoutParams(1.0f));
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
		tabHolder_ = new TabHolder(ORIENT_VERTICAL, 300, tabHolderFlags, filterNotice_, nullptr, new AnchorLayoutParams(10, 0, 0, 0));
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
	});

	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD) || System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		// Hide search if screen is too small.
		int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
		if ((g_display.dp_xres < g_display.dp_yres || g_display.dp_yres >= 500) && (deviceType != DEVICE_TYPE_VR) && ShowSearchControls()) {
			// Search
			auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
			AddTab("GameSettingsSearch", ms->T("Search"), ImageID("I_SEARCH"), [this](UI::LinearLayout *searchSettings) {
				auto se = GetI18NCategory(I18NCat::SEARCH);

				ChoiceGroup *searchGroup = searchSettings->Add(new ChoiceGroupList())->Add(new ChoiceGroup(se->T("Find settings")));
				searchGroup->Add(new PopupTextInputChoice(GetRequesterToken(), &searchFilter_, se->T("Filter"), "", 64, screenManager()))->OnChange.Add([this](UI::EventParams &e) {
					System_PostUIMessage(UIMessage::GAMESETTINGS_SEARCH, StripSpaces(searchFilter_));
				});

				clearSearchChoice_ = searchGroup->Add(new Choice(se->T("Clear filter")));
				clearSearchChoice_->OnClick.Add([](UI::EventParams &e) {
					System_PostUIMessage(UIMessage::GAMESETTINGS_SEARCH, "");
				});
				clearSearchChoice_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);

				noSearchResults_ = searchSettings->Add(new TextView("", new LinearLayoutParams(Margins(20, 5))));
			});
		}
	}

	// After the search tab, which the saved tab can be.
	if (currentTabSetting_) {
		tabHolder_->SetInitialTab(*currentTabSetting_);
	} else {
		tabHolder_->EnsureTab(tabHolder_->GetCurrentTab());
	}

	// Need to handle recreates, both important and accidental ones.
	if (!searchFilter_.empty()) {
		ApplySearchFilter();
	}
}

void UITabbedBaseDialogScreen::sendMessage(UIMessage message, const char *value) {
	UIBaseDialogScreen::sendMessage(message, value);
	if (message == UIMessage::GAMESETTINGS_SEARCH) {
		std::string filter = value ? value : "";
		searchFilter_.resize(filter.size());
		std::transform(filter.begin(), filter.end(), searchFilter_.begin(), tolower);

		ApplySearchFilter();
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

// Hides the views in the group that don't match the filter, and returns whether any did. Groups that can't
// be focused (like ChoiceGroups) are searched recursively and stay visible if anything in them matches.
// Other views that can't be focused (headings, notices) are shown only when something after them matches.
static bool ApplySearchFilterToGroup(UI::ViewGroup *group, int firstIndex, const std::string &filter) {
	using namespace UI;
	bool anyMatch = false;
	View *lastHeading = nullptr;
	for (int i = firstIndex; i < group->GetNumSubviews(); ++i) {
		View *v = group->GetViewByIndex(i);
		bool match;
		if (v->IsViewGroup() && !v->CanBeFocused()) {
			match = ApplySearchFilterToGroup((ViewGroup *)v, 0, filter);
		} else if (!v->CanBeFocused()) {
			lastHeading = v;
			v->SetVisibility(filter.empty() ? V_VISIBLE : V_GONE);
			continue;
		} else {
			std::string label = v->DescribeText();
			std::transform(label.begin(), label.end(), label.begin(), tolower);
			match = label.find(filter) != label.npos;
		}
		if (match && lastHeading) {
			lastHeading->SetVisibility(V_VISIBLE);
		}
		v->SetVisibility((filter.empty() || match) ? V_VISIBLE : V_GONE);
		anyMatch = anyMatch || match;
	}
	return anyMatch;
}

void UITabbedBaseDialogScreen::ApplySearchFilter() {
	using namespace UI;
	// GAMESETTINGS_SEARCH is broadcast to every screen, including ones whose views don't exist yet.
	if (!tabHolder_) {
		return;
	}
	auto se = GetI18NCategory(I18NCat::SEARCH);

	EnsureTabs();

	// Show an indicator that a filter is applied.
	filterNotice_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);
	filterNotice_->SetText(ApplySafeSubstitutions(se->T("Filtering settings by '%1'"), searchFilter_));

	bool matches = searchFilter_.empty();

	const std::vector<ViewGroup *> settingTabs = tabHolder_->GetTabContentViews();

	for (int t = 0; t < (int)settingTabs.size(); ++t) {
		const ViewGroup *tabContents = settingTabs[t];
		std::string_view tag = tabContents->Tag();

		// Dive down to the actual list of settings.
		// TODO: Do this recursively instead.
		while (tabContents->GetNumSubviews() == 1) {
			View *v = tabContents->GetViewByIndex(0);
			if (v->IsViewGroup()) {
				tabContents = (ViewGroup *)v;
			} else {
				break;
			}
		}

		if (tag == "GameSettingsSearch") {
			continue;
		}

		// The first view is the tab's title.
		const bool tabMatches = ApplySearchFilterToGroup((ViewGroup *)tabContents, 1, searchFilter_) || searchFilter_.empty();
		tabHolder_->EnableTab(t, tabMatches);
		matches = matches || tabMatches;
	}

	if (noSearchResults_) {
		noSearchResults_->SetText(ApplySafeSubstitutions(se->T("No settings matched '%1'"), searchFilter_));
		noSearchResults_->SetVisibility(matches ? UI::V_GONE : UI::V_VISIBLE);
	}
	if (clearSearchChoice_) {
		clearSearchChoice_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);
	}
}
