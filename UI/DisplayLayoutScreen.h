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

#pragma once

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "MiscScreens.h"

class DisplayLayoutScreen : public UIDialogScreenWithGameBackground {
public:
	DisplayLayoutScreen(const Path &filename);
	void CreateViews() override;
	bool touch(const TouchInput &touch) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void onFinish(DialogResult reason) override;

	void DrawBackground(UIContext &dc) override;

	void resized() override {
		RecreateViews();
	}

	const char *tag() const override { return "DisplayLayout"; }
	
protected:
	UI::EventReturn OnPostProcShaderChange(UI::EventParams &e);

	void sendMessage(const char *message, const char *value) override;

private:
	UI::ChoiceStrip *mode_ = nullptr;
	UI::Choice *postProcChoice_ = nullptr;
	std::string shaderNames_[256];

	bool dragging_ = false;
	bool bRotated_ = false;

	// Touch down state for drag to resize etc
	float startX_ = 0.0f;
	float startY_ = 0.0f;
	float startScale_ = -1.0f;
	float startDisplayOffsetX_ = -1.0f;
	float startDisplayOffsetY_ = -1.0f;
};
