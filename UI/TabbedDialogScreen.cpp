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
#include "Common/UI/Root.h"
#include "UI/MiscViews.h"
#include "Common/UI/Context.h"
#include "UI/SettingsPage.h"
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

void UITabbedBaseDialogScreen::AddSettingsTab(const char *tag, std::string_view title, ImageID imageId, I18NCat cat, std::function<void(SettingsPage &)> build, std::function<void(UI::LinearLayout *)> createHeader) {
	int index = (int)settingsTabs_.size();
	settingsTabs_.push_back(SettingsTab{ (int)tabHolder_->GetTabContentViews().size(), std::string(title), cat, std::move(build) });
	AddTab(tag, title, imageId, [this, index, createHeader = std::move(createHeader)](UI::LinearLayout *parent) {
		if (createHeader) {
			createHeader(parent);
		}
		SettingsTab &tab = settingsTabs_[index];
		SettingsPage page(tab.cat);
		tab.build(page);
		tab.views.clear();
		CreateSettingViews(page, parent, screenManager(), &tab.views);
	});
}

void UITabbedBaseDialogScreen::CreateViews() {
	PreCreateViews();

	settingsTabs_.clear();
	pendingFocusView_ = nullptr;

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

	if (ShowSearchControls() && CanShowSearch() && !settingsTabs_.empty()) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		AddTab("GameSettingsSearch", ms->T("Search"), ImageID("I_SEARCH"), [this](UI::LinearLayout *parent) {
			CreateSearchTab(parent);
		});
	} else if (ShowSearchControls() && CanShowSearch()) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		AddTab("GameSettingsSearch", ms->T("Search"), ImageID("I_SEARCH"), [this](UI::LinearLayout *searchSettings) {
			auto se = GetI18NCategory(I18NCat::SEARCH);

			searchSettings->Add(new ItemHeader(se->T("Find settings")));
			searchSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &searchFilter_, se->T("Filter"), "", 64, screenManager()))->OnChange.Add([this](UI::EventParams &e) {
				System_PostUIMessage(UIMessage::GAMESETTINGS_SEARCH, StripSpaces(searchFilter_));
			});

			clearSearchChoice_ = searchSettings->Add(new Choice(se->T("Clear filter")));
			clearSearchChoice_->OnClick.Add([](UI::EventParams &e) {
				System_PostUIMessage(UIMessage::GAMESETTINGS_SEARCH, "");
			});
			clearSearchChoice_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);

			noSearchResults_ = searchSettings->Add(new TextView("", new LinearLayoutParams(Margins(20, 5))));
		});
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

bool UITabbedBaseDialogScreen::CanShowSearch() {
	if (!System_GetPropertyBool(SYSPROP_HAS_KEYBOARD) && !System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		return false;
	}
	// Hide search if screen is too small.
	int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
	return (g_display.dp_xres < g_display.dp_yres || g_display.dp_yres >= 500) && deviceType != DEVICE_TYPE_VR;
}

void UITabbedBaseDialogScreen::ApplySearchFilter() {
	using namespace UI;
	// GAMESETTINGS_SEARCH is broadcast to every screen, including ones whose views don't exist yet,
	// and ones that search some other way.
	if (!tabHolder_ || !ShowSearchControls()) {
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

		bool tabMatches = searchFilter_.empty();

		UI::View *lastHeading = nullptr;
		for (int i = 1; i < tabContents->GetNumSubviews(); ++i) {
			UI::View *v = tabContents->GetViewByIndex(i);
			if (!v->CanBeFocused()) {
				lastHeading = v;
			}

			std::string label = v->DescribeText();
			std::transform(label.begin(), label.end(), label.begin(), tolower);
			bool match = v->CanBeFocused() && label.find(searchFilter_) != label.npos;
			tabMatches = tabMatches || match;

			if (match && lastHeading)
				lastHeading->SetVisibility(UI::V_VISIBLE);
			v->SetVisibility((searchFilter_.empty() || match) ? UI::V_VISIBLE : UI::V_GONE);
		}
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

static bool SettingTextMatches(const SettingText &text, I18NCat pageCat, std::string_view query) {
	if (text.empty()) {
		return false;
	}
	return containsNoCase(text.Translate(pageCat), query) || containsNoCase(text.English(), query);
}

// Shows the matching settings themselves, so they can be changed right here.
void UITabbedBaseDialogScreen::CreateSearchTab(UI::LinearLayout *parent) {
	using namespace UI;
	auto se = GetI18NCategory(I18NCat::SEARCH);

	parent->Add(new ItemHeader(se->T("Find settings")));
	parent->Add(new PopupTextInputChoice(GetRequesterToken(), &searchText_, se->T("Filter"), "", 64, screenManager()))->OnChange.Add([this](UI::EventParams &e) {
		searchText_ = StripSpaces(searchText_);
		RecreateViews();
	});
	if (searchText_.empty()) {
		return;
	}
	parent->Add(new Choice(se->T("Clear filter")))->OnClick.Add([this](UI::EventParams &e) {
		searchText_.clear();
		RecreateViews();
	});

	bool found = false;
	for (int t = 0; t < (int)settingsTabs_.size(); t++) {
		const SettingsTab &tab = settingsTabs_[t];
		SettingsPage page(tab.cat);
		tab.build(page);
		const I18NCat cat = page.DefaultCat();
		const std::deque<SettingItem> &items = page.Items();

		// A matching header shows its whole section, and a matching hint the setting it belongs to.
		std::vector<bool> show(items.size());
		bool sectionMatches = false;
		int lastSetting = -1;
		for (int i = 0; i < (int)items.size(); i++) {
			const SettingItem &item = items[i];
			bool matches = SettingTextMatches(item.text, cat, searchText_);
			switch (item.type) {
			case SettingType::Header:
				sectionMatches = matches;
				break;
			case SettingType::Hint:
				if (matches && lastSetting >= 0) {
					show[lastSetting] = true;
				}
				break;
			default:
				lastSetting = i;
				show[i] = matches || sectionMatches;
				break;
			}
		}

		const SettingItem *header = nullptr;
		bool headerShown = false;
		View *prevView = nullptr;
		for (int i = 0; i < (int)items.size(); i++) {
			const SettingItem &item = items[i];
			if (item.type == SettingType::Header) {
				header = &item;
				headerShown = false;
				continue;
			}
			if (item.type == SettingType::Hint) {
				if (prevView) {
					CreateSettingView(item, cat, prevView, parent, screenManager());
				}
				continue;
			}
			prevView = nullptr;
			if (!show[i]) {
				continue;
			}

			LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			row->SetSpacing(0.0f);
			View *view = CreateSettingView(item, cat, nullptr, row, screenManager());
			if (!view) {
				delete row;
				continue;
			}
			// FILL_PARENT, since some views (like CheckBox) measure their width from their bounds.
			view->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f, Gravity::G_VCENTER));
			Choice *goTo = row->Add(new Choice(ImageID("I_ARROW_RIGHT"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT, 0.0f, Gravity::G_VCENTER)));
			goTo->OnClick.Add([this, t, i](UI::EventParams &e) {
				GoToSetting(t, i);
			});

			if (!headerShown) {
				std::string title = tab.title;
				if (header) {
					title += " > ";
					title += header->text.Translate(cat);
				}
				parent->Add(new ItemHeader(title));
				headerShown = true;
			}
			parent->Add(row);
			prevView = view;
			found = true;
		}
	}

	if (!found) {
		parent->Add(new TextView(ApplySafeSubstitutions(se->T("No settings matched '%1'"), searchText_), new LinearLayoutParams(Margins(20, 5))));
	}
}

// Switches to the setting's tab, and focuses it (in update) so it's highlighted and scrolled into view.
void UITabbedBaseDialogScreen::GoToSetting(int settingsTab, int itemIndex) {
	const SettingsTab &tab = settingsTabs_[settingsTab];
	SetCurrentTab(tab.tabIndex);
	// The views are filled in when the tab is created, which SetCurrentTab does if needed.
	if (itemIndex < (int)tab.views.size()) {
		pendingFocusView_ = tab.views[itemIndex];
	}
}

void UITabbedBaseDialogScreen::update() {
	UIBaseDialogScreen::update();

	// The tab may have just been created, or not been laid out while hidden. Scrolling needs the
	// view's position, so wait for a layout.
	if (pendingFocusView_ && pendingFocusView_->GetBounds().h > 0.0f) {
		UI::ScrollView *scrollView = UI::GetEnclosingScrollView(pendingFocusView_);
		if (scrollView) {
			scrollView->ScrollToCenter(pendingFocusView_);
		}
		// Highlights it. Done directly, since SetFocus does nothing when focus movement is off (mouse or touch).
		UI::SetFocusedView(pendingFocusView_, UI::FocusFlags::CAUSE_RESTORE);
		pendingFocusView_ = nullptr;
	}
}
