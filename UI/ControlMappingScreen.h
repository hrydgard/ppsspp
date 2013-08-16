// Copyright (c) 2013- PPSSPP Project.

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

#include "ui/view.h"
#include "ui/ui_screen.h"

class KeyMappingScreen : public Screen {
public:
	KeyMappingScreen() : currentMap_(0) {}
	void update(InputState &input);
	void render();
private:
	int currentMap_;
};

// Dialog box, meant to be pushed
class KeyMappingNewKeyDialog : public PopupScreen {
public:
	KeyMappingNewKeyDialog(int btn, int currentMap) : PopupScreen("Map Key") {
		pspBtn_ = btn;
		last_kb_deviceid_ = 0;
		last_kb_key_ = 0;
		last_axis_deviceid_ = 0;
		last_axis_id_ = -1;
		currentMap_ = currentMap;
	}

	void key(const KeyInput &key);
	void axis(const AxisInput &axis);

protected:
	void CreatePopupContents(UI::ViewGroup *parent);

	virtual bool FillVertical() { return false; }
	virtual bool ShowButtons() { return false; }
	virtual void OnCompleted(DialogResult result) {}

private:
	int pspBtn_;
	int last_kb_deviceid_;
	int last_kb_key_;
	int last_axis_deviceid_;
	int last_axis_id_;
	int last_axis_direction_;
	int currentMap_;
};
