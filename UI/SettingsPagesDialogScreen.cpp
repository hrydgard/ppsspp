// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/Data/Text/I18n.h"
#include "Common/Input/KeyCodes.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Root.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/ViewGroup.h"
#include "Core/Config.h"
#include "UI/MiscViews.h"
#include "UI/SettingsPage.h"
#include "UI/SettingsPagesDialogScreen.h"

// Pages can link to each other, so search doesn't follow links forever.
static const int MAX_PAGE_DEPTH = 8;

void SettingsPagesDialogScreen::AddSettingsTab(const char *tag, std::string_view title, ImageID imageId, I18NCat cat, std::function<void(SettingsPage *)> build, const char *helpTag) {
	int index = (int)settingsTabs_.size();
	if (stacked_) {
		settingsTabs_.push_back(SettingsTab{ index, tag, std::string(title), cat, std::move(build), helpTag ? helpTag : "", imageId });
		return;
	}
	settingsTabs_.push_back(SettingsTab{ (int)tabHolder_->GetTabContentViews().size(), tag, std::string(title), cat, std::move(build), helpTag ? helpTag : "", imageId });
	tabHolder_->AddTabDeferred(title, imageId, [this, index]() -> UI::ViewGroup * {
		using namespace UI;
		LinearLayout *contents = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		contents->SetSpacing(0);
		// Created on demand, outside CreateViews (see AddTab).
		bool wasCreatingViews = IsCreatingViews();
		SetCreatingViews(true);
		CreateSettingsTabContents(index, contents);
		SetCreatingViews(wasCreatingViews);
		return contents;
	});
}

void SettingsPagesDialogScreen::OpenPage(const char *tabTag, const char *linkKey) {
	openTabTag_ = tabTag;
	openLinkKey_ = linkKey;
}

std::unique_ptr<SettingsPage> SettingsPagesDialogScreen::BuildPage(const SettingsTab &tab, std::vector<int> *path, std::vector<std::string> *titles, std::string *helpTag) {
	std::unique_ptr<SettingsPage> page = std::make_unique<SettingsPage>(tab.cat, this);
	tab.build(page.get());
	titles->push_back(tab.title);
	*helpTag = tab.helpTag;
	for (size_t i = 0; i < path->size(); i++) {
		int index = (*path)[i];
		if (index < 0 || index >= (int)page->Items().size() || page->Items()[index].type != SettingType::Link || i >= MAX_PAGE_DEPTH) {
			path->resize(i);
			break;
		}
		const SettingItem &link = page->Items()[index];
		titles->push_back(std::string(link.text.Translate(page->DefaultCat())));
		if (link.helpTag) {
			*helpTag = link.helpTag;
		}
		std::unique_ptr<SettingsPage> next = std::make_unique<SettingsPage>(link.linkCat, page->Host());
		link.linkBuild(next.get());
		page = std::move(next);
	}
	return page;
}

void SettingsPagesDialogScreen::CreateSettingsTabContents(int settingsTab, UI::LinearLayout *parent) {
	using namespace UI;
	SettingsTab &tab = settingsTabs_[settingsTab];
	std::vector<int> &path = pagePaths_[tab.tag];
	std::vector<std::string> titles;
	std::string helpTag;
	std::unique_ptr<SettingsPage> page = BuildPage(tab, &path, &titles, &helpTag);
	if (settingsTab == stackRootTab_ && !path.empty()) {
		// What tabs show when rotating to them.
		if (currentTabSetting_) {
			*currentTabSetting_ = path[0];
		}
		pagePaths_[settingsTabs_[path[0]].tag].assign(path.begin() + 1, path.end());
	}

	if (!searchText_.empty()) {
		auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
		titles = { std::string(ms->T("Search")) + ": " + searchText_ };
	}
	CreateNavBar(parent, settingsTab, titles, helpTag);

	std::string pageKey = tab.tag;
	for (int index : path) {
		pageKey += "/" + std::to_string(index);
	}
	ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
	// Search results get their own tag, so their scroll position isn't restored onto the page when leaving them.
	scroll->SetTag(searchText_.empty() ? pageKey : pageKey + "/search");
	LinearLayout *contents = scroll->Add(new ChoiceGroupList());

	tab.views.clear();
	if (!searchText_.empty()) {
		CreateSearchResults(contents);
		return;
	}

	// Each page remembers where it was scrolled to, for coming back to it.
	scroll->RememberPosition(&scrollPositions_[pageKey]);
	CreateSettingViews(*page, contents, screenManager(), &tab.views);

	const std::deque<SettingItem> &items = page->Items();
	for (int i = 0; i < (int)items.size() && i < (int)tab.views.size(); i++) {
		if (items[i].type == SettingType::Link && tab.views[i]) {
			static_cast<Choice *>(tab.views[i])->OnClick.Add([this, settingsTab, i](UI::EventParams &) {
				std::vector<int> path = pagePaths_[settingsTabs_[settingsTab].tag];
				path.push_back(i);
				ShowPage(settingsTab, path, 0);
			});
		}
	}

	if (pendingTab_ == settingsTab && pendingItem_ >= 0) {
		for (int i = pendingItem_; i < (int)tab.views.size(); i++) {
			if (tab.views[i] && tab.views[i]->CanBeFocused()) {
				pendingFocusView_ = tab.views[i];
				break;
			}
		}
	}
	if (pendingTab_ == settingsTab) {
		pendingTab_ = -1;
		pendingItem_ = -1;
	}
}

// Where you are, with the earlier pages clickable to go back to them. Also has the search button.
void SettingsPagesDialogScreen::CreateNavBar(UI::ViewGroup *parent, int settingsTab, const std::vector<std::string> &titles, const std::string &helpTag) {
	using namespace UI;
	const SettingsTab &tab = settingsTabs_[settingsTab];
	LinearLayout *bar = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, ITEM_HEIGHT, Margins(0, 0, 8, 0))));
	bar->SetPadding(Margins(12, 8, 12, 8));
	bar->SetSpacing(0.0f);

	// When stacked in portrait, there's no other back button, so it's always there, and leaves the screen from the root page.
	if (stacked_ || !searchText_.empty() || !pagePaths_[tab.tag].empty()) {
		Choice *backButton = bar->Add(new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)));
		backButton->SetRoundCorners(RoundCorners::All);
		backButton->OnClick.Add([this, settingsTab](UI::EventParams &) {
			if (!GoBack(settingsTab)) {
				TriggerFinish(DR_BACK);
			}
		});
	}
	// Portrait has no room for the whole path, and the back button goes up it.
	int firstShownTitle = stacked_ ? (int)titles.size() - 1 : 0;
	for (int i = firstShownTitle; i < (int)titles.size(); i++) {
		if (i > firstShownTitle) {
			bar->Add(new TextView(">", new LinearLayoutParams(0.0f, Gravity::G_VCENTER, Margins(4, 0, 4, 0))));
		}
		if (i == (int)titles.size() - 1) {
			SimpleTextView *title = bar->Add(new SimpleTextView(titles[i], new LinearLayoutParams(0.0f, Gravity::G_VCENTER)));
			title->SetBig(true);
			continue;
		}
		Choice *crumb = bar->Add(new Choice(titles[i], new LinearLayoutParams(WRAP_CONTENT, ITEM_HEIGHT, 0.0f, Gravity::G_VCENTER)));
		crumb->SetRoundCorners(RoundCorners::All);
		crumb->OnClick.Add([this, settingsTab, i](UI::EventParams &) {
			std::vector<int> path = pagePaths_[settingsTabs_[settingsTab].tag];
			int focusItem = path[i];  // The link to the page we're leaving.
			path.resize(i);
			ShowPage(settingsTab, path, focusItem);
		});
	}

	bar->Add(new Spacer(10.0f, new LinearLayoutParams(1.0f)));

	if (!gamePath_.empty() && g_Config.IsGameSpecific()) {
		bar->Add(CreateGameIconView(gamePath_, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));
	}

	// While searching, the back button leaves the search, and this changes it.
	if (CanShowSearch()) {
		Choice *searchButton = bar->Add(new Choice(ImageID("I_SEARCH"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT, Margins(8, 0, 0, 0))));
		searchButton->SetRoundCorners(RoundCorners::All);
		searchButton->OnClick.Add([this](UI::EventParams &) {
			AskForSearchText();
		});
	}

	if (!helpTag.empty() && searchText_.empty()) {
		std::string settingsUrl = join("https://www.ppsspp.org/docs/settings/", helpTag);
		Choice *helpButton = bar->Add(new Choice(ImageID("I_LINK_OUT_QUESTION"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT, Margins(8, 0, 0, 0))));
		helpButton->SetRoundCorners(RoundCorners::All);
		helpButton->OnClick.Add([settingsUrl](UI::EventParams &) {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, settingsUrl);
		});
	}
}

void SettingsPagesDialogScreen::AskForSearchText() {
	auto se = GetI18NCategory(I18NCat::SEARCH);
	auto apply = [this](std::string_view text) {
		SetSearchText(StripSpaces(text));
	};
	if (System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		System_InputBoxGetString(GetRequesterToken(), se->T("Find settings"), searchText_, false, [apply](std::string_view value, int) {
			apply(value);
		});
		return;
	}
	// The popup writes the text here before calling OnChange.
	searchEditText_ = searchText_;
	UI::TextEditPopupScreen *popup = new UI::TextEditPopupScreen(&searchEditText_, "", se->T("Find settings"), 64);
	if (System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT)) {
		popup->SetAlignTop(true);
	}
	popup->OnChange.Add([this, apply](UI::EventParams &) {
		apply(searchEditText_);
	});
	screenManager()->push(popup);
}

// Every tab shows the search results while searching, so they all change.
void SettingsPagesDialogScreen::SetSearchText(std::string_view text) {
	if (text == searchText_) {
		return;
	}
	searchText_ = text;
	pageChange_ = true;
	RecreateViews();
}

// Recreating the views shows it. That waits for update, since this is usually called from a click on a view
// that will be deleted.
void SettingsPagesDialogScreen::ShowPage(int settingsTab, const std::vector<int> &path, int itemIndex) {
	if (stacked_ && settingsTab != stackRootTab_) {
		std::vector<int> rootPath = path;
		rootPath.insert(rootPath.begin(), settingsTab);
		ShowPage(stackRootTab_, rootPath, itemIndex);
		return;
	}
	pagePaths_[settingsTabs_[settingsTab].tag] = path;
	pageChange_ = true;
	RecreateViews();
	pendingTab_ = settingsTab;
	pendingItem_ = itemIndex;
	pendingHighlight_ = false;
}

bool SettingsPagesDialogScreen::GoBack(int settingsTab) {
	if (!searchText_.empty()) {
		SetSearchText("");
		return true;
	}
	if (settingsTab < 0) {
		return false;
	}
	std::vector<int> path = pagePaths_[settingsTabs_[settingsTab].tag];
	if (path.empty()) {
		return false;
	}
	int focusItem = path.back();  // The link to the page we're leaving.
	path.pop_back();
	ShowPage(settingsTab, path, focusItem);
	return true;
}

// Links to the tabs, like a tab strip would.
void SettingsPagesDialogScreen::BuildStackRootPage(SettingsPage *page) {
	for (int t = 0; t < stackRootTab_; t++) {
		const SettingsTab &tab = settingsTabs_[t];
		SettingItem &link = page->Link(SettingText::Raw(tab.title.c_str()), tab.cat, tab.build);
		link.image = tab.icon;
		if (!tab.helpTag.empty()) {
			link.helpTag = tab.helpTag.c_str();
		}
	}
}

// A page asked for with OpenPage.
int SettingsPagesDialogScreen::OpenRequestedPage() {
	if (openTabTag_.empty()) {
		return -1;
	}
	int opened = -1;
	for (int t = 0; t < (int)settingsTabs_.size(); t++) {
		const SettingsTab &tab = settingsTabs_[t];
		if (tab.tag != openTabTag_) {
			continue;
		}
		SettingsPage page(tab.cat, this);
		tab.build(&page);
		for (int i = 0; i < (int)page.Items().size(); i++) {
			const SettingItem &item = page.Items()[i];
			if (item.type == SettingType::Link && item.text.key && openLinkKey_ == item.text.key) {
				pagePaths_[tab.tag] = { i };
				pendingTab_ = t;
				opened = t;
				break;
			}
		}
	}
	openTabTag_.clear();
	openLinkKey_.clear();
	return opened;
}

int SettingsPagesDialogScreen::CurrentSettingsTab() const {
	if (stacked_) {
		return stackRootTab_;
	}
	if (!tabHolder_) {
		return -1;
	}
	int current = tabHolder_->GetCurrentTab();
	for (int t = 0; t < (int)settingsTabs_.size(); t++) {
		if (settingsTabs_[t].tabIndex == current) {
			return t;
		}
	}
	return -1;
}

bool SettingsPagesDialogScreen::CanShowSearch() {
	if (!System_GetPropertyBool(SYSPROP_HAS_KEYBOARD) && !System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		return false;
	}
	return System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR;
}

// Back closes the search, then goes back a page, before leaving the screen.
bool SettingsPagesDialogScreen::key(const KeyInput &key) {
	if (key.flags & KeyInputFlags::DOWN) {
		ClearHighlight();
	}
	if ((key.flags & KeyInputFlags::DOWN) && UI::IsEscapeKey(key)) {
		if (GoBack(CurrentSettingsTab())) {
			UI::PlayUISound(UI::UISound::BACK);
			return true;
		}
	}
	return UIBaseDialogScreen::key(key);
}

bool SettingsPagesDialogScreen::touch(const TouchInput &touch) {
	if (touch.flags & TouchInputFlags::DOWN) {
		ClearHighlight();
	}
	return UIBaseDialogScreen::touch(touch);
}

void SettingsPagesDialogScreen::ClearHighlight() {
	if (highlightedView_) {
		highlightedView_->SetHighlighted(false);
		highlightedView_ = nullptr;
	}
}

static bool SettingTextMatches(const SettingText &text, I18NCat pageCat, std::string_view query) {
	if (text.empty()) {
		return false;
	}
	return containsNoCase(text.Translate(pageCat), query) || containsNoCase(text.English(), query);
}

// Lists the matching settings from all tabs and the pages they link to, each leading to where it is.
void SettingsPagesDialogScreen::CreateSearchResults(UI::ViewGroup *parent) {
	using namespace UI;
	auto se = GetI18NCategory(I18NCat::SEARCH);

	bool found = false;
	for (int t = 0; t < (int)settingsTabs_.size(); t++) {
		// It only links to the others.
		if (t == stackRootTab_) {
			continue;
		}
		const SettingsTab &tab = settingsTabs_[t];
		SettingsPage page(tab.cat, this);
		tab.build(&page);
		if (SearchPage(parent, t, {}, page, tab.title, 0)) {
			found = true;
		}
	}

	if (!found) {
		parent->Add(new TextView(ApplySafeSubstitutions(se->T("No settings matched '%1'"), searchText_), new LinearLayoutParams(Margins(20, 5))));
	}
}

bool SettingsPagesDialogScreen::SearchPage(UI::ViewGroup *parent, int settingsTab, const std::vector<int> &path, const SettingsPage &page, const std::string &title, int depth) {
	using namespace UI;
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

	// The page title is the last part of title.
	size_t lastSeparator = title.rfind(" > ");
	std::string_view pageTitle = lastSeparator == std::string::npos ? std::string_view(title) : std::string_view(title).substr(lastSeparator + 3);

	bool found = false;
	const SettingItem *header = nullptr;
	// The section's results, created with its first.
	ChoiceGroup *group = nullptr;
	for (int i = 0; i < (int)items.size(); i++) {
		const SettingItem &item = items[i];
		if (item.type == SettingType::Header) {
			header = &item;
			group = nullptr;
			continue;
		}
		if (!show[i]) {
			continue;
		}

		if (!group) {
			std::string headerTitle = title;
			// Pages often start with a header repeating their title.
			if (header && !header->text.empty() && header->text.Translate(cat) != pageTitle) {
				headerTitle += " > ";
				headerTitle += header->text.Translate(cat);
			}
			group = parent->Add(new ChoiceGroup(headerTitle));
		}
		// Goes to the setting, highlighted, or opens the page that a link leads to.
		Choice *result = group->Add(new Choice(item.text.Translate(cat)));
		result->SetIconRight(ImageID("I_CHEVRON_RIGHT"));
		std::vector<int> pagePath = path;
		int focusItem = i;
		if (item.type == SettingType::Link) {
			pagePath.push_back(i);
			focusItem = -1;
		}
		result->OnClick.Add([this, settingsTab, pagePath, focusItem](UI::EventParams &) {
			GoToPage(settingsTab, pagePath, focusItem);
		});
		found = true;
	}

	// And the pages this one links to.
	if (depth < MAX_PAGE_DEPTH) {
		for (int i = 0; i < (int)items.size(); i++) {
			const SettingItem &item = items[i];
			if (item.type != SettingType::Link) {
				continue;
			}
			SettingsPage linked(item.linkCat, page.Host());
			item.linkBuild(&linked);
			std::vector<int> linkPath = path;
			linkPath.push_back(i);
			if (SearchPage(parent, settingsTab, linkPath, linked, title + " > " + std::string(item.text.Translate(cat)), depth + 1)) {
				found = true;
			}
		}
	}
	return found;
}

// Leaves the search, and shows the page at path in its tab, with the item focused (in update, so it's
// highlighted and scrolled into view).
void SettingsPagesDialogScreen::GoToPage(int settingsTab, const std::vector<int> &path, int itemIndex) {
	SetSearchText("");
	if (!stacked_) {
		// The tab strip keeps showing its tab when the views are recreated. This creates the tab if it wasn't
		// already, so it has to come before ShowPage, or that would take the item to focus.
		SetCurrentTab(settingsTabs_[settingsTab].tabIndex);
	}
	ShowPage(settingsTab, path, itemIndex);
	pendingHighlight_ = true;
}

void SettingsPagesDialogScreen::update() {
	UIBaseDialogScreen::update();

	// The tab may have just been created, or not been laid out while hidden. Scrolling needs the
	// view's position, so wait for a layout.
	if (pendingFocusView_ && pendingFocusView_->GetBounds().h > 0.0f) {
		// Moving between pages only focuses with the keyboard (or a controller), which needs a focused view.
		// A setting picked from search results is always shown, and with a mouse or touch, highlighted.
		if (pendingHighlight_ || UI::IsFocusMovementEnabled()) {
			UI::ScrollView *scrollView = UI::GetEnclosingScrollView(pendingFocusView_);
			if (scrollView) {
				scrollView->ScrollToCenter(pendingFocusView_);
			}
			if (UI::IsFocusMovementEnabled()) {
				UI::SetFocusedView(pendingFocusView_, UI::FocusFlags::CAUSE_RESTORE);
			} else if (UI::Clickable *clickable = dynamic_cast<UI::Clickable *>(pendingFocusView_)) {
				ClearHighlight();
				clickable->SetHighlighted(true);
				highlightedView_ = clickable;
			}
		}
		pendingFocusView_ = nullptr;
		pendingHighlight_ = false;
	}
}


void SettingsPagesDialogScreen::CreateViews() {
	settingsTabs_.clear();
	pendingFocusView_ = nullptr;
	highlightedView_ = nullptr;

	bool wasStacked = stacked_;
	stacked_ = GetDeviceOrientation() == DeviceOrientation::Portrait;
	using namespace UI;

	if (stacked_) {
		PreCreateViews();
		tabHolder_ = nullptr;
		CreateTabs();
		const int requestedTab = OpenRequestedPage();

		stackRootTab_ = (int)settingsTabs_.size();
		settingsTabs_.push_back(SettingsTab{ stackRootTab_, "SettingsRoot", std::string(T(I18NCat::DIALOG, "Settings")), I18NCat::DIALOG, [this](SettingsPage *page) {
			BuildStackRootPage(page);
		}, "", ImageID::invalid() });
		SettingsTab &rootTab = settingsTabs_[stackRootTab_];

		// Show the tab that was asked for, or that was shown before rotating, under the page linking to it.
		// (Pages shown from here are already on the root tab's path.)
		int showTab = -1;
		if (requestedTab >= 0) {
			showTab = requestedTab;
			pendingTab_ = stackRootTab_;
		} else if (viewsCreated_ && !wasStacked && currentTabSetting_ && *currentTabSetting_ >= 0 && *currentTabSetting_ < stackRootTab_) {
			showTab = *currentTabSetting_;
		}
		if (showTab >= 0) {
			std::vector<int> rootPath = pagePaths_[settingsTabs_[showTab].tag];
			rootPath.insert(rootPath.begin(), showTab);
			pagePaths_[rootTab.tag] = rootPath;
		}

		LinearLayout *contents = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
		contents->SetSpacing(0.0f);
		root_ = contents;
		CreateSettingsTabContents(stackRootTab_, contents);
		viewsCreated_ = true;
		pageChange_ = false;
		return;
	}
	stackRootTab_ = -1;

	UITabbedBaseDialogScreen::CreateViews();
	viewsCreated_ = true;
	pageChange_ = false;
}

// A page asked for with OpenPage is opened on its tab.
int SettingsPagesDialogScreen::InitialTab() {
	OpenRequestedPage();
	return pendingTab_ >= 0 ? settingsTabs_[pendingTab_].tabIndex : -1;
}

// Picking a tab means leaving the search for it.
void SettingsPagesDialogScreen::TabChanged(int tab) {
	SetSearchText("");
}
