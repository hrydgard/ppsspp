#pragma once

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "UI/BaseScreens.h"

// The simpler cousin of TabbedDialogScreen, without tabs or the other bling,
// but with a consistent portrait-compatible back button and title.
class UISimpleBaseDialogScreen : public UIBaseDialogScreen {
public:
	UISimpleBaseDialogScreen(const Path &gamePath = Path()) : UIBaseDialogScreen(gamePath) {
		// We need to check CanScroll before we know whether to ignore
		// bottom inset. Can't do that here, we do it in CreateViews
	}

	// Override this, don't override CreateViews. And don't touch root_ directly.
	virtual void CreateDialogViews(UI::ViewGroup *parent) = 0;
	virtual std::string_view GetTitle() const { return ""; }
protected:
	virtual bool CanScroll() const { return true; }

private:
	void CreateViews() override;
};
