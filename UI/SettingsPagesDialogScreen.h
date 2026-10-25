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

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Common/Data/Text/I18n.h"
#include "Common/Render/TextureAtlas.h"
#include "UI/SettingsPage.h"
#include "UI/TabbedDialogScreen.h"

// Tabs described by SettingsPages, whose settings show up in search. Their pages can link to more pages,
// which open on top of them in the same tab, under a bar showing where you are (with search and help
// buttons). In portrait, instead of a strip of tabs, a page links to them, so all tabs must be settings tabs.
class SettingsPagesDialogScreen : public UITabbedBaseDialogScreen, public SettingsPageHost {
public:
	SettingsPagesDialogScreen(const Path &gamePath, int *currentTabSetting = nullptr, TabDialogFlags flags = TabDialogFlags::Default)
		: UITabbedBaseDialogScreen(gamePath, currentTabSetting, flags) {}

	// If helpTag is set, the bar links to the docs. build may be called at any time (also for search), so it
	// shouldn't create views.
	void AddSettingsTab(const char *tag, std::string_view title, ImageID imageId, I18NCat cat, std::function<void(SettingsPage *)> build, const char *helpTag = nullptr);
	// When the views are created, opens the page that the root page of tab tabTag links to with the text key linkKey.
	void OpenPage(const char *tabTag, const char *linkKey);
	void CreateViews() override;
	void update() override;
	bool key(const KeyInput &key) override;
	bool touch(const TouchInput &touch) override;

	ScreenManager *PageScreenManager() override { return screenManager(); }
	int PageRequesterToken() override { return GetRequesterToken(); }
	void RecreatePageViews() override { RecreateViews(); }
	const Path &PageGamePath() const override { return gamePath_; }

protected:
	int InitialTab() override;
	// While the views are recreated only to show another page or search results. Then what doesn't change
	// with the page, like lists read from disk, doesn't need to be read again.
	bool RecreatingForPageChange() const { return pageChange_; }
	void TabChanged(int tab) override;

	// Whether there's a way to type a search.
	static bool CanShowSearch();

private:
	struct SettingsTab {
		int tabIndex;
		std::string tag;
		std::string title;
		I18NCat cat;
		std::function<void(SettingsPage *)> build;
		std::string helpTag;
		ImageID icon;
		std::vector<UI::View *> views;  // One per item of the page shown, filled in when the tab is created.
	};

	// Builds the page at path, which lists the links to follow from the tab's root page. Cuts the path
	// where it stops leading to a page. titles gets the title of each page, starting with the tab's, and
	// helpTag the page's docs.
	std::unique_ptr<SettingsPage> BuildPage(const SettingsTab &tab, std::vector<int> *path, std::vector<std::string> *titles, std::string *helpTag);
	void CreateSettingsTabContents(int settingsTab, UI::LinearLayout *parent);
	void BuildStackRootPage(SettingsPage *page);
	void CreateNavBar(UI::ViewGroup *parent, int settingsTab, const std::vector<std::string> &titles, const std::string &helpTag);
	void CreateSearchResults(UI::ViewGroup *parent);
	bool SearchPage(UI::ViewGroup *parent, int settingsTab, const std::vector<int> &path, const SettingsPage &page, const std::string &title, int depth);
	// Returns the settings tab of the page, or -1 if none was asked for.
	int OpenRequestedPage();
	void AskForSearchText();
	void SetSearchText(std::string_view text);
	// Shows the page at path in the tab, and focuses itemIndex on it (if not -1). When stacked, that's under
	// the page linking to the tabs.
	void ShowPage(int settingsTab, const std::vector<int> &path, int itemIndex);
	// Closes the search, or else goes back a page. Returns false if there was nowhere to go back to.
	bool GoBack(int settingsTab);
	void GoToPage(int settingsTab, const std::vector<int> &path, int itemIndex);
	int CurrentSettingsTab() const;
	void ClearHighlight();

	std::vector<SettingsTab> settingsTabs_;
	// The page shown in each tab, by tab tag, and each page's scroll position. Kept when the views are recreated,
	// which is how a different page or the search results get shown.
	std::map<std::string, std::vector<int>> pagePaths_;
	std::map<std::string, float> scrollPositions_;
	std::string searchText_;
	std::string searchEditText_;  // For the text input popup.

	// In portrait there's no tab holder. Instead, the settings tab stackRootTab_ (added after the others) links
	// to the others, and is the only one shown. Its link to each tab has the tab's index.
	bool stacked_ = false;
	bool viewsCreated_ = false;  // So we know when rotating.
	bool pageChange_ = false;
	int stackRootTab_ = -1;

	// What to show when the views are next created.
	std::string openTabTag_;
	std::string openLinkKey_;
	int pendingTab_ = -1;  // In settingsTabs_.
	int pendingItem_ = -1;  // An item on that tab's page, to focus.
	// Highlighted and scrolled to once laid out.
	UI::View *pendingFocusView_ = nullptr;
	bool pendingHighlight_ = false;  // Show pendingFocusView_ even without keyboard navigation.
	// Highlighted instead of focused, without keyboard navigation. Until the next touch or key.
	UI::Clickable *highlightedView_ = nullptr;
};
