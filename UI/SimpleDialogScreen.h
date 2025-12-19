#pragma once

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "UI/BaseScreens.h"

enum class SimpleDialogFlags {
	Default = 0,
	CustomContextMenu = 1,
	ContentsCanScroll = 8,
};
ENUM_CLASS_BITOPS(SimpleDialogFlags);

// The simpler cousin of TabbedDialogScreen, without tabs or the other bling,
// but with a consistent portrait-compatible back button and title.
class UISimpleBaseDialogScreen : public UIBaseDialogScreen {
public:
	UISimpleBaseDialogScreen(const Path &gamePath, SimpleDialogFlags flags) : UIBaseDialogScreen(gamePath), flags_(flags) {
		// We need to check CanScroll before we know whether to ignore
		// bottom inset. Can't do that here, we do it in CreateViews
	}

	// Override this, don't override CreateViews. And don't touch root_ directly.
	virtual void CreateDialogViews(UI::ViewGroup *parent) = 0;
	virtual void CreateContextMenu(UI::ViewGroup *parent) {}  // only called if CustomContextMenu is set in flags.
	virtual std::string_view GetTitle() const { return ""; }

private:
	void CreateViews() override;
	SimpleDialogFlags flags_;
};

enum class TwoPaneFlags {
	Default = 0,
	SettingsToTheRight = 1,
	SettingsInContextMenu = 2,
	SettingsCanScroll = 4,
	ContentsCanScroll = 8,
	CustomContextMenu = 16,
};
ENUM_CLASS_BITOPS(TwoPaneFlags);

// A two-pane version of the above, where settings are meant to go in the settings pane,
// and contents in the content pane. Will generate nice layouts for portrait and landscape.
// but with a consistent portrait-compatible back button and title.
// The settings pane is scrollable while the other is not.
class UITwoPaneBaseDialogScreen : public UIBaseDialogScreen {
public:
	UITwoPaneBaseDialogScreen(const Path &gamePath, TwoPaneFlags flags) : UIBaseDialogScreen(gamePath), flags_(flags) {
		// We need to check CanScroll before we know whether to ignore
		// bottom inset. Can't do that here, we do it in CreateViews
	}

	// Override this, don't override CreateViews. And don't touch root_ directly.
	virtual void BeforeCreateViews() {}  // If something needs to happen before both settings and contents, this is a good place.
	virtual void CreateSettingsViews(UI::ViewGroup *parent) = 0;
	virtual void CreateContentViews(UI::ViewGroup *parent) = 0;
	virtual void CreateContextMenu(UI::ViewGroup *parent) {}  // only called if CustomContextMenu is set in flags.
	virtual std::string_view GetTitle() const { return ""; }
	virtual float SettingsWidth() const { return 350.0f; }

private:
	void CreateViews() override;
	TwoPaneFlags flags_ = TwoPaneFlags::Default;
};
