#pragma once

#include "ui/screen.h"
#include "ui/viewgroup.h"

class UIScreen : public Screen {
public:
	UIScreen();
	~UIScreen() { delete root_; }

	virtual void update(InputState &input);
	virtual void render();
	virtual void touch(const TouchInput &touch);

	// Some useful default event handlers
	UI::EventReturn OnBack(UI::EventParams &e);

protected:
	virtual void CreateViews() = 0;
	virtual void DrawBackground(UIContext &dc) {}

	void RecreateViews() { recreateViews_ = true; }

	UI::ViewGroup *root_;

private:
	bool recreateViews_;
};
