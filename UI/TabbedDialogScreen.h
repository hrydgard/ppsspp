#pragma once

#include "ppsspp_config.h"
#include <functional>

#include "Common/UI/UIScreen.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/System/System.h"
#include "Core/ConfigValues.h"
#include "UI/BaseScreens.h"

namespace UI {
class TabHolder;
}

enum class TabDialogFlags {
	Default = 0,
	HorizontalOnlyIcons = 1,
	VerticalShowIcons = 2,
	AddAutoTitles = 4,
	ContextMenuInPortrait = 8,
};
ENUM_CLASS_BITOPS(TabDialogFlags);

enum class TabFlags {
	Default = 0,
	NonScrollable = 1,
};
ENUM_CLASS_BITOPS(TabFlags);

class UITabbedBaseDialogScreen : public UIBaseDialogScreen {
public:
	// currentTabSetting can be null if you don't want to persist the current tab.
	UITabbedBaseDialogScreen(const Path &gamePath, int *currentTabSetting = nullptr, TabDialogFlags flags = TabDialogFlags::Default);
	void AddTab(const char *tag, std::string_view title, ImageID imageId, std::function<void(UI::LinearLayout *)> createCallback, TabFlags flags = TabFlags::Default);
	void AddTab(const char *tag, std::string_view title, std::function<void(UI::LinearLayout *)> createCallback, TabFlags flags = TabFlags::Default) {
		AddTab(tag, title, ImageID::invalid(), createCallback, flags);
	}
	void CreateViews() override;

protected:
	// Load data and define your tabs here.
	virtual void PreCreateViews() {}
	virtual void CreateTabs() = 0;
	virtual void CreateExtraButtons(UI::ViewGroup *verticalLayout, int margins) {}
	virtual void EnsureTabs();
	virtual bool UsePopupMenuInPortrait() const { return false; }
	virtual bool ForceHorizontalTabs() const { return false; }

	// The tab to show first, after CreateTabs, if not the saved one (-1).
	virtual int InitialTab() { return -1; }
	virtual void TabChanged(int tab) {}

	int GetCurrentTab() const;
	void SetCurrentTab(int tab);

	UI::TabHolder *tabHolder_ = nullptr;
	int *currentTabSetting_ = nullptr;

private:
	TabDialogFlags flags_ = TabDialogFlags::Default;
};
