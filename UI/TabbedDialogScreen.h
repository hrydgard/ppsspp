#pragma once

#include "ppsspp_config.h"
#include <functional>

#include "Common/UI/UIScreen.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/System/System.h"
#include "Core/ConfigValues.h"
#include "UI/BaseScreens.h"

class SettingInfoMessage;

namespace UI {
class TabHolder;
}

enum class TabDialogFlags {
	Default = 0,
	HorizontalOnlyIcons = 1,
	VerticalShowIcons = 2,
	AddTitles = 4,
};
ENUM_CLASS_BITOPS(TabDialogFlags);

enum class TabFlags {
	Default = 0,
	NonScrollable = 1,
};
ENUM_CLASS_BITOPS(TabFlags);

class UITabbedBaseDialogScreen : public UIBaseDialogScreen {
public:
	UITabbedBaseDialogScreen(const Path &gamePath, TabDialogFlags flags = TabDialogFlags::Default) : UIBaseDialogScreen(gamePath), flags_(flags) {
		ignoreBottomInset_ = true;
	}

	void AddTab(const char *tag, std::string_view title, ImageID imageId, std::function<void(UI::LinearLayout *)> createCallback, TabFlags flags = TabFlags::Default);
	void AddTab(const char *tag, std::string_view title, std::function<void(UI::LinearLayout *)> createCallback, TabFlags flags = TabFlags::Default) {
		AddTab(tag, title, ImageID::invalid(), createCallback, flags);
	}
	void CreateViews() override;

protected:
	// Load data and define your tabs here.
	virtual void PreCreateViews() {}
	virtual void CreateTabs() = 0;
	virtual void CreateExtraButtons(UI::LinearLayout *verticalLayout, int margins) {}
	virtual bool ShowSearchControls() const { return true; }
	virtual void EnsureTabs();
	virtual bool ForceHorizontalTabs() const { return false; }

	int GetCurrentTab() const;
	void SetCurrentTab(int tab);

	void RecreateViews() override;
	void sendMessage(UIMessage message, const char *value) override;

	SettingInfoMessage *settingInfo_ = nullptr;

private:
	void ApplySearchFilter();

	UI::TabHolder *tabHolder_ = nullptr;
	UI::TextView *filterNotice_ = nullptr;
	UI::Choice *clearSearchChoice_ = nullptr;
	UI::TextView *noSearchResults_ = nullptr;

	// If we recreate the views while this is active we show it again
	std::string oldSettingInfo_;
	std::string searchFilter_;

	TabDialogFlags flags_ = TabDialogFlags::Default;
};
